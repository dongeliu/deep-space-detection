/**
 * @file axi_tangxi.c
 * @date 2023/09/25
 * @author Wang Yang
 *
 * axi-tangxi内核驱动文件，实现axi-tangxi基本寄存器配置
 *
 * @bug
 **/

// Kernel dependencies
#include <asm/io.h>
#include <linux/delay.h> // Milliseconds to jiffies conversation
#include <linux/wait.h>  // Completion related functions

#include "axitangxi_dev.h"
#include "axitangxi_ioctl.h" // IOCTL interface definition and types

// The default timeout for axi-tangxi is 10 seconds
#define AXI_TANGXI_TIMEOUT 10000

// 引入需要映射的寄存器虚拟地址定义
extern void __iomem *dma_mm2s_ps_addr;
extern void __iomem *dma_mm2s_ps_size;
extern void __iomem *noc_data_pkg_addr;
extern void __iomem *noc_data_pkg_ctl;
extern void __iomem *noc_ctl_pkg_ctl;
extern void __iomem *csram_write_ctl;
extern void __iomem *dma_s2mm_ps_addr;
extern void __iomem *dma_s2mm_ps_size;
extern void __iomem *result_read_addr;
extern void __iomem *result_read_ctl;
extern void __iomem *tx_riscv_reset_ctl;
extern void __iomem *dma_mm2s_pl_addr;
extern void __iomem *dma_mm2s_pl_size;
extern void __iomem *dma_s2mm_pl_addr;
extern void __iomem *dma_s2mm_pl_size;

// 定义网络加速器需要映射的寄存器虚拟地址
extern void __iomem *network_acc_control;
extern void __iomem *network_acc_weight_addr;
extern void __iomem *network_acc_weight_size;
extern void __iomem *network_acc_quantify_addr;
extern void __iomem *network_acc_quantify_size;
extern void __iomem *network_acc_picture_addr;
extern void __iomem *network_acc_picture_size;
extern void __iomem *network_acc_trans_addr;
extern void __iomem *network_acc_trans_size;
extern void __iomem *network_acc_entropy_addr;
extern void __iomem *network_acc_entropy_size;

// 内核参数定义
struct axitangxi_transfer {
  // struct completion comp;         // A completion to use for waiting

  // 必须配置
  size_t tx_data_size; // 发送数据大小
  size_t rx_data_size; // 接收数据大小

  size_t burst_size;  // 突发长度
  size_t burst_count; // 突发次数
  size_t burst_data;  // 一次突发数据大小

  uint32_t tx_data_ps_ptr; // 突发发送地址(PS DDR地址)
  uint32_t rx_data_ps_ptr; // 突发接收地址(PS DDR地址)

  uint32_t tx_data_pl_ptr; // 突发发送地址(PL DDR地址)
  uint32_t rx_data_pl_ptr; // 突发接收地址(PL DDR地址)

  uint8_t node; // 芯片node节点
};

// 网络加速器寄存器配置
struct network_acc_args {
  uint32_t control;       // Control Register（不需要配置）
  uint32_t weight_addr;   // PL端DRAM权重存储起始地址
  uint32_t weight_size;   // PL端DRAM权重大小
  uint32_t quantify_addr; // PL端DRAM量化因子存储起始地址
  uint32_t quantify_size; // PL端DRAM量化因子大小
  uint32_t picture_addr;  // PL端DRAM图像数据存储起始地址
  uint32_t picture_size;  // PL端DRAM图像数据大小
  uint32_t trans_addr;    // PL端DRAM变换系数存储起始地址
  uint32_t trans_size;    // PL端DRAM变换系数大小（初始化为0）
  uint32_t entropy_addr;  // PL端DRAM熵参数存储起始地址
  uint32_t entropy_size;  // PL端DRAM熵参数大小（初始化为0）
};

// 初始化中断参数
struct axitangxi_irq_data axitangxi_irq_data = {0};

/**
 * @brief PS DDR --> PL DDR
 *
 * @param axi_args axi传输相关参数
 */
void axitangxi_psddr_plddr(struct axitangxi_transfer *axitangxi_trans) {
  uint32_t data_remaining = axitangxi_trans->tx_data_size;
  uint32_t burst_size = axitangxi_trans->burst_size;
  uint32_t burst_data = axitangxi_trans->burst_data;

  uint32_t tx_data_ps_next = (uintptr_t)axitangxi_trans->tx_data_ps_ptr;
  uint32_t tx_data_pl_next = axitangxi_trans->tx_data_pl_ptr;

  // axitangxi_info("PS DDR --> PL DDR 开始配置！\n");

  int count = 0;
  while (data_remaining > 0) {
    // 最后一次突发
    if (data_remaining < burst_data) {
      if ((data_remaining & ADDR_ALIGN_BASE_16_MASK) != 0x00) {
        // 对齐16字节
        data_remaining +=
            (ADDR_ALIGN_BASE_16 - (data_remaining & ADDR_ALIGN_BASE_16_MASK));
      }
      burst_size = data_remaining >> 4;
      burst_data = data_remaining;
    }
    data_remaining -= burst_data;
    if (data_remaining == 0) {
      axitangxi_irq_data.last_s2mm_pl_done = true;
    }
    // PS DDR data--> PL DDR
    mm2s_ps_data_s2mm_pl(tx_data_ps_next, tx_data_pl_next, burst_size);
    // 等待中断信号
    tx_data_ps_next = tx_data_ps_next + burst_data;
    count++;
    axitangxi_info(
        "PS DDR --> PL DDR: 第 %d 次突发，突发长度：%d, 剩下数据：%d B\n",
        count, burst_size, data_remaining);
  }
  // axitangxi_info("PS DDR --> PL DDR 配置完成！\n");
}

/**
 * @brief PL DDR --> PS DDR
 *
 * @param axi_args axi传输相关参数
 */
void axitangxi_plddr_psddr(struct axitangxi_transfer *axitangxi_trans) {

  uint32_t data_remaining = axitangxi_trans->tx_data_size;
  uint32_t burst_size = axitangxi_trans->burst_size;
  uint32_t burst_data = axitangxi_trans->burst_data;

  uint32_t rx_data_ps_next = (uintptr_t)axitangxi_trans->rx_data_ps_ptr;
  uint32_t tx_data_pl_next = axitangxi_trans->tx_data_pl_ptr;

  int count = 0;
  while (data_remaining > 0) {
    // 最后一次突发
    if (data_remaining < burst_data) {
      if ((data_remaining & ADDR_ALIGN_BASE_16_MASK) != 0x00) {
        // 对齐16字节
        data_remaining +=
            (ADDR_ALIGN_BASE_16 - (data_remaining & ADDR_ALIGN_BASE_16_MASK));
      }
      burst_size = data_remaining >> 4;
      burst_data = data_remaining;
    }
    data_remaining -= burst_data;
    if (data_remaining == 0) {
      axitangxi_irq_data.last_s2mm_ps_done = true;
    }
    // PL DDR --> PS ddr
    s2mm_ps_data_mm2s_pl(rx_data_ps_next, tx_data_pl_next, burst_size);
    // 等待中断信号
    rx_data_ps_next = rx_data_ps_next + burst_data;
    count++;
    axitangxi_info(
        "PL DDR --> PS DDR: 第 %d 次突发，突发长度：%d, 剩下数据：%d B\n",
        count, burst_size, data_remaining);
  }

  // printf("PL DDR --> PS DDR 配置完成！\n");
}

/**
 * @brief PS DRAM --> PL DRAM --> PS DRAM回环
 *
 * @param dev
 * @param trans
 * @return int
 */
int axitangxi_psddr_plddr_loopback(struct axitangxi_device *dev,
                                   struct axitangxi_transaction *trans) {
  unsigned long timeout = msecs_to_jiffies(AXI_TANGXI_TIMEOUT), time_remain;
  struct axitangxi_transfer axitx_trans;
  struct completion comp;

  axitx_trans.tx_data_size = trans->tx_data_size;
  axitx_trans.rx_data_size = trans->rx_data_size;
  axitx_trans.burst_size = trans->burst_size;
  axitx_trans.burst_count = trans->burst_count;
  axitx_trans.burst_data = trans->burst_data;
  axitx_trans.rx_data_ps_ptr =
      axitangxi_uservirt_to_phys(dev, trans->rx_data_ps_ptr);
  axitx_trans.tx_data_pl_ptr = axitx_trans.rx_data_pl_ptr =
      axitx_trans.tx_data_ps_ptr =
          axitangxi_uservirt_to_phys(dev, trans->tx_data_ps_ptr);
  // axitx_trans.rx_data_pl_ptr = axitx_trans.rx_data_ps_ptr;
  axitx_trans.node = trans->node;
  axitangxi_irq_data.comp = &comp;

  printk("tx_data_ps_ptr: %#x, rx_data_ps_ptr: %#x, tx_data_pl_ptr: %#x, "
         "rx_data_pl_ptr: %#x, burst_size: %ld\n",
         axitx_trans.tx_data_ps_ptr, axitx_trans.rx_data_ps_ptr,
         axitx_trans.tx_data_pl_ptr, axitx_trans.rx_data_pl_ptr,
         axitx_trans.burst_size);

  int i;
  for (i = 0; i < 10; i++) {
    printk("trans->tx_data_ps_ptr[%d]: %d\n", i, trans->tx_data_ps_ptr[i]);
  }

  // 初始化 completion
  init_completion(axitangxi_irq_data.comp);
  // PS DDR --> PL DDR
  axitangxi_psddr_plddr(&axitx_trans);
  // 等待中断响应
  time_remain = wait_for_completion_timeout(axitangxi_irq_data.comp, timeout);
  if (time_remain == 0) {
    axitangxi_err("axitangxi_psddr_plddr transaction timed out.\n");
    return -ETIME;
  }
  printk("time_remain_1: %ld\n", time_remain);

  // udelay(10000);

  init_completion(axitangxi_irq_data.comp);
  // PL DDR --> PS DDR
  axitangxi_plddr_psddr(&axitx_trans);
  // 等待中断响应
  time_remain = wait_for_completion_timeout(axitangxi_irq_data.comp, timeout);
  if (time_remain == 0) {
    axitangxi_err("axitangxi_plddr_psddr transaction timed out.\n");
    return -ETIME;
  }
  printk("time_remain_2: %ld\n", time_remain);

  return 0;
}

/**
 * @brief PS DDR data--> PL --> tangxi
 *
 * @param tx_data_ps_addr 读ps ddr数据首地址:0x00000000 - 0x7FFFFFFF
 * @param rx_data_pl_addr 芯片写回pl ddr数据首地址:0x00000000 - 0x7FFFFFFF
 * @param burst_size 突发长度：0-127
 * @param chip_position chip号:00-01-10
 * @param node node节点号:0-255
 */
void mm2s_ps_data_tangxi(uint32_t tx_data_ps_addr, uint32_t rx_data_pl_addr,
                         uint32_t burst_size, uint32_t chip_position,
                         uint8_t node) {
  /**
   *
   * NOC_DATA_PKG_ADDR
   * 0xnnnaaaaa  [29:20][19:0] : node_addr
   * n - node[29:20] → [9:8]表示chip_position(FPGA为2'b11,发送的包不能是2'b11),
   * [7:0]表示此包的目的节点(0-255) a - addr[19:0] → 表示写地址 0x00300100:node
   * 0x003, addr 0x00100 bram 256-4096,pl ddr 4096+
   */
  uint32_t noc_data_size_addr =
      (chip_position << 28) | (node << 20) | rx_data_pl_addr;

  /**
   * NOC_DATA_PKG_CTL
   * 0bssssssssssssssssttcccccccc  [25:10][9:8][7:0] : size_type_cmd
   * s - size[25:10] → 表示写大小（控制包为size不用管，数据包size表示数据长度）
   * t - type[9:8] → 表示此包的类型, 00:data, 01:ctrl
   * c - cmd[7:0] → 对应类型的具体命令(控制指令)
       > cmd[7:6] → ctrl_cmd , 00:reset, 01:start, 10:end, 11:riscv change
   de_mode(=addr[15]) > cmd[5:3] → sync_cmd > cmd[2:0] → ddr_cmd , 001:read,
   010:write, 100:refresh
   * 0b00 00000010:type 00, cmd 00000010
   */
  uint32_t noc_data_node_type_cmd = ((burst_size - 1) << 10) | 0b0000000010;

  // DMA_MM2S_PS_ADDR
  writel(tx_data_ps_addr, dma_mm2s_ps_addr);
  // DMA_MM2S_PS_SIZE
  writel(burst_size - 1, dma_mm2s_ps_size);

  // NOC_DATA_PKG_ADDR
  writel(noc_data_size_addr, noc_data_pkg_addr);
  // NOC_DATA_PKG_CTL
  writel(noc_data_node_type_cmd, noc_data_pkg_ctl);
}

/**
 * @brief tangxi --> PL Read Result (默认存储BRAM，首地址256) --> PS ddr
 *
 * @param rx_data_ps_addr 写PS ddr数据首地址:0x00000000 - 0x7FFFFFFF
 * @param burst_size 突发长度：0-127
 */
void s2mm_ps_data_tangxi(uint32_t rx_data_ps_addr, uint32_t burst_size) {
  // RESULT_READ_ADDR 与NOC_DATA_PKG_ADDR addr一致
  uint32_t noc_data_size_addr = ((burst_size - 1) << 20) | 0x00100;

  writel(noc_data_size_addr, result_read_addr);
  // RESULT_READ_CTL
  writel(burst_size - 1, result_read_ctl);

  // DMA_S2MM_PS_ADDR
  writel(rx_data_ps_addr, dma_s2mm_ps_addr);

  // DMA_S2MM_PS_SIZE
  writel(burst_size - 1, dma_s2mm_ps_size);
}

/**
 * @brief PS DDR --> PL DDR
 *
 * @param tx_data_ps_addr 读取ps ddr数据首地址:0x00000000 - 0x7FFFFFFF
 * @param rx_data_pl_addr 写入pl ddr数据首地址:0x00000000 - 0x7FFFFFFF
 * @param burst_size 突发长度：0-127
 */
void mm2s_ps_data_s2mm_pl(uint32_t tx_data_ps_addr, uint32_t rx_data_pl_addr,
                          uint32_t burst_size) {
  printk("DMA_MM2S_PS_ADDR: %#lx\n", (unsigned long)dma_mm2s_ps_addr);
  // DMA_MM2S_PS_ADDR
  writel(tx_data_ps_addr, dma_mm2s_ps_addr);

  printk("DMA_MM2S_PS_SIZE: %#lx\n", (unsigned long)dma_mm2s_ps_size);
  // DMA_MM2S_PS_SIZE
  writel(burst_size - 1, dma_mm2s_ps_size);

  printk("DMA_S2MM_PL_ADDR: %#lx\n", (unsigned long)dma_s2mm_pl_addr);
  // DMA_S2MM_PL_ADDR
  writel(rx_data_pl_addr, dma_s2mm_pl_addr);

  printk("DMA_S2MM_PL_SIZE: %#lx\n", (unsigned long)dma_s2mm_pl_size);
  // DMA_S2MM_PL_SIZE
  writel(burst_size - 1, dma_s2mm_pl_size);
}

/**
 * @brief PL DDR --> PS DDR
 *
 * @param rx_data_ps_addr 写入ps ddr数据首地址:0x00000000 - 0x7FFFFFFF
 * @param tx_data_pl_addr 读取pl ddr数据首地址:0x00000000 - 0x7FFFFFFF
 * @param burst_size 突发长度：0-127
 */
void s2mm_ps_data_mm2s_pl(uint32_t rx_data_ps_addr, uint32_t tx_data_pl_addr,
                          uint32_t burst_size) {
  // DMA_MM2S_PL_ADDR
  writel(tx_data_pl_addr, dma_mm2s_pl_addr);
  // DMA_MM2S_PL_SIZE
  writel(burst_size - 1, dma_mm2s_pl_size);

  // DMA_S2MM_PS_ADDR
  writel(rx_data_ps_addr, dma_s2mm_ps_addr);
  // DMA_S2MM_PS_SIZE
  writel(burst_size - 1, dma_s2mm_ps_size);
}

/**
 * @brief 批量PS DDR --> tangxi --> PL DDR
 *
 * @param axitangxi_trans
 */
void batch_mm2s_ps_data_tangxi(struct axitangxi_transfer *axitangxi_trans) {
  uint32_t data_remaining = axitangxi_trans->tx_data_size;

  uint32_t tx_data_ps_next = (uintptr_t)axitangxi_trans->tx_data_ps_ptr;
  uint32_t rx_data_pl_next = axitangxi_trans->rx_data_pl_ptr;

  uint32_t burst_size = axitangxi_trans->burst_size;
  uint32_t burst_data = axitangxi_trans->burst_data;

  // XTime_GetTime(&start_t);
  while (data_remaining > 0) {
    // 最后一次突发
    if (data_remaining < burst_data) {
      if ((data_remaining & ADDR_ALIGN_BASE_16_MASK) != 0x00) {
        // 对齐16字节
        data_remaining +=
            (ADDR_ALIGN_BASE_16 - (data_remaining & ADDR_ALIGN_BASE_16_MASK));
      }
      burst_size = data_remaining >> 4;
      burst_data = data_remaining;
    }
    // PS DDR data--> PL --> tangxi, 0:第0个chip, 3:第三个node, 256:req
    // data存储地址256
    mm2s_ps_data_tangxi(tx_data_ps_next, rx_data_pl_next, burst_size, 0, 3);
    // 等待中断信号
    tx_data_ps_next = tx_data_ps_next + burst_data;
    data_remaining -= burst_data;
  }
}

/**
 * @brief 批量PL DDR --> PS DDR
 *
 * @param axitangxi_trans
 */
void batch_s2mm_ps_data_mm2s_pl(struct axitangxi_transfer *axitangxi_trans) {
  uint32_t data_remaining = axitangxi_trans->rx_data_size;

  uint32_t rx_data_ps_next = (uintptr_t)axitangxi_trans->rx_data_ps_ptr;
  uint32_t tx_data_pl_next = axitangxi_trans->rx_data_pl_ptr;

  uint32_t burst_size = axitangxi_trans->burst_size;
  uint32_t burst_data = axitangxi_trans->burst_data;
  while (data_remaining > 0) {
    if (data_remaining < burst_data) {
      // 如果剩余的数据小于一次性突发数据，修改突发长度
      if ((data_remaining & ADDR_ALIGN_BASE_16_MASK) != 0x00) {
        // 对齐16字节
        data_remaining +=
            (ADDR_ALIGN_BASE_16 - (data_remaining & ADDR_ALIGN_BASE_16_MASK));
      }

      burst_size = data_remaining >> 4;
      burst_data = data_remaining;
    }
    s2mm_ps_data_mm2s_pl(rx_data_ps_next, tx_data_pl_next, burst_size);
    rx_data_ps_next = rx_data_ps_next + burst_data;
    data_remaining -= burst_data;
  }
}

/**
 * @brief PS DRAM --> PL --> tangxi --> PL --> PS DRAM 回环
 *
 * @param axitangxi_trans
 * @return int
 */
int axi_tangxi_loopback(struct axitangxi_transfer *axitangxi_trans) {
  // PS DDR --> tangxi --> PL DDR
  batch_mm2s_ps_data_tangxi(axitangxi_trans);

  udelay(2000);

  batch_s2mm_ps_data_mm2s_pl(axitangxi_trans);

  udelay(2000);

  return 0;
}

void network_acc_config_dev(uint32_t weight_addr, uint32_t weight_size,
                            uint32_t quantify_addr, uint32_t quantify_size,
                            uint32_t picture_addr, uint32_t picture_size) {
  writel(weight_addr, network_acc_weight_addr);
  writel(weight_size, network_acc_weight_size);
  writel(quantify_addr, network_acc_quantify_addr);
  writel(quantify_size, network_acc_quantify_size);
  writel(picture_addr, network_acc_picture_addr);
  writel(picture_size, network_acc_picture_size);
}

/**
 * @brief 启动网络加速器
 *
 * @param *args
 */
void network_acc_start_dev(uint32_t args_control) {
  args_control = 1;
  writel(args_control, network_acc_control);
  udelay(2000);
  args_control = 0;
  writel(args_control, network_acc_control);
}

/**
 * @brief 获取网络加速器的变换系数和熵参数大小
 *
 * @param *args
 */
void get_network_acc_args_dev(uint32_t trans_addr, uint32_t trans_size,
                              uint32_t entropy_addr, uint32_t entropy_size) {
  trans_addr = readl(network_acc_trans_addr);
  trans_size = readl(network_acc_trans_size);
  entropy_addr = readl(network_acc_entropy_addr);
  entropy_size = readl(network_acc_entropy_size);
}

/**
 * @brief 配置网络加速器的寄存器
 *
 * @param *args
 */
void network_acc_config(struct network_acc_args *args) {
  uint32_t weight_addr = args->weight_addr;
  uint32_t weight_size = args->weight_size;
  uint32_t quantify_addr = args->quantify_addr;
  uint32_t quantify_size = args->quantify_size;
  uint32_t picture_addr = args->picture_addr;
  uint32_t picture_size = args->picture_size;
  network_acc_config_dev(weight_addr, weight_size, quantify_addr, quantify_size,
                         picture_addr, picture_size);
}

/**
 * @brief 启动网络加速器
 *
 * @param *args
 */
void network_acc_start(struct network_acc_args *args) {
  uint32_t args_control = args->control;
  network_acc_start_dev(args_control);
}

/**
 * @brief 获取网络加速器的变换系数和熵参数大小
 *
 * @param *args
 */
void get_network_acc_args(struct network_acc_args *args) {
  uint32_t trans_addr = args->trans_addr;
  uint32_t trans_size = args->trans_size;
  uint32_t entropy_addr = args->entropy_addr;
  uint32_t entropy_size = args->entropy_size;
  get_network_acc_args_dev(trans_addr, trans_size, entropy_addr, entropy_size);
}

///////////////////
/**
 * @brief PS DRAM --> PL DRAM --> PS DRAM回环
 *
 * @param dev
 * @param trans
 * @return int
 */
int acc_config(struct axitangxi_device *dev, struct network_acc_reg *args) {
  struct network_acc_args iargs;

  iargs.weight_addr = args->weight_addr;
  iargs.weight_size = args->weight_size;
  iargs.quantify_addr = args->quantify_addr;
  iargs.quantify_size = args->quantify_size;
  iargs.picture_addr = args->picture_addr;
  iargs.picture_size = args->picture_size;

  printk("network_acc_config_successful");

  return 0;
};

/**
 * @brief PS DRAM --> PL DRAM --> PS DRAM回环
 *
 * @param dev
 * @param trans
 * @return int
 */
int acc_start(struct axitangxi_device *dev) {
  struct network_acc_args iargs;

  iargs.control = 1;
  udelay(2000);
  iargs.control = 0;
  printk("network_acc_start_successful");

  return 0;
};

/**
 * @brief PS DRAM --> PL DRAM --> PS DRAM回环
 *
 * @param dev
 * @param trans
 * @return int
 */
int acc_get(struct axitangxi_device *dev, struct network_acc_reg *args) {
  // struct network_acc_args iargs;

  // args->trans_addr = iargs.trans_addr;
  // args->trans_size = iargs.trans_size;
  // args->entropy_addr = iargs.entropy_addr;
  // args->entropy_size = iargs.entropy_size;

  printk("network_acc_get_successful");

  return 0;
};
