//#include <stdio.h>
//#include <stdlib.h>
//#include <unistd.h>

// Kernel dependencies
#include <linux/delay.h>            // Milliseconds to jiffies conversation
#include <linux/wait.h>             // Completion related functions
#include <linux/io.h>

#include "axitangxi_dev.h"
#include "axitangxi_ioctl.h"           // IOCTL interface definition and types

// The default timeout for DMA is 10 seconds
//#define AXI_TANGXI_TIMEOUT      10000

struct axitangxi_transfer {
    struct completion comp;         // A completion to use for waiting

	//必须配置
	size_t tx_data_size;  //发送数据大小
	size_t rx_data_size;  //接收数据大小

	size_t burst_size;  //突发长度
	size_t burst_count;  //突发次数
	size_t burst_data;  //一次突发数据大小

	uint32_t *tx_data_ps_ptr;  //突发发送地址(PS DDR地址)
	uint32_t *rx_data_ps_ptr;  //突发接收地址(PS DDR地址)

	uint32_t *tx_data_pl_ptr;  //突发发送地址(PL DDR地址)
	uint32_t *rx_data_pl_ptr;  //突发接收地址(PL DDR地址)

	uint8_t node;  //芯片node节点
};

struct axitangxi_irq_data axitangxi_irq_data = {0};

/**********************************Variable Definitions***************************************/

//static XScuGic INTC; // 创建XScuGic中断控制器实例
//XTime start_t, end_t;
//uint8_t ps_conf_done, pl_conf_done, ps_transfer_done, pl_transfer_done;

/**********************************Function Definitions***************************************/

/**
 * @brief PS DDR --> PL DDR
 *
 * @param axi_args axi传输相关参数
 */
void axitangxi_psddr_plddr(struct axitangxi_transfer *axitangxi_trans)
{
	size_t data_remaining = axitangxi_trans->tx_data_size;
	size_t burst_size = axitangxi_trans->burst_size;
    size_t burst_data = axitangxi_trans->burst_data;

    uint32_t tx_data_ps_next = (uintptr_t) axitangxi_trans->tx_data_ps_ptr;
    uint32_t tx_data_pl_next = (uintptr_t) axitangxi_trans->tx_data_pl_ptr;


	//XTime_GetTime(&start_t);
    while(data_remaining > 0)
    {
    	//最后一次突发
    	if (data_remaining < burst_data) {
    		if ((data_remaining & ADDR_ALIGN_BASE_16_MASK) != 0x00 ) {
    			//对齐16字节
    			data_remaining += ( ADDR_ALIGN_BASE_16 - ( data_remaining & ADDR_ALIGN_BASE_16_MASK ) );
    		}
    		burst_size = data_remaining >> 4;
    		burst_data = data_remaining;
    	}
		data_remaining -= burst_data;
		if(data_remaining == 0) {
			axitangxi_irq_data.last_s2mm_pl_done = true;
		}
    	//PS DDR data--> PL DDR
		mm2s_ps_data_s2mm_pl(tx_data_ps_next, tx_data_pl_next, burst_size);
        // 等待中断信号
    	tx_data_ps_next = tx_data_ps_next + burst_data;
    }
	axitangxi_info("PS DDR --> PL DDR 配置完成！\n");
}

/**
 * @brief PL DDR --> PS DDR
 *
 * @param axi_args axi传输相关参数
 */
void axitangxi_plddr_psddr(struct axitangxi_transfer *axitangxi_trans)
{

	size_t data_remaining = axitangxi_trans->tx_data_size;
	size_t burst_size = axitangxi_trans->burst_size;
    size_t burst_data = axitangxi_trans->burst_data;

    uint32_t rx_data_ps_next = (uintptr_t) axitangxi_trans->rx_data_ps_ptr;
    uint32_t tx_data_pl_next = (uintptr_t) axitangxi_trans->tx_data_pl_ptr;


	//XTime_GetTime(&start_t);
    while(data_remaining > 0)
    {
    	//最后一次突发
    	if (data_remaining < burst_data) {
    		if ((data_remaining & ADDR_ALIGN_BASE_16_MASK) != 0x00 ) {
    			//对齐16字节
    			data_remaining += ( ADDR_ALIGN_BASE_16 - ( data_remaining & ADDR_ALIGN_BASE_16_MASK ) );
    		}
    		burst_size = data_remaining >> 4;
    		burst_data = data_remaining;
    	}
		data_remaining -= burst_data;
		if(data_remaining == 0) {
			axitangxi_irq_data.last_s2mm_ps_done = true;
		}
		//PL DDR --> PS ddr
		s2mm_ps_data_mm2s_pl(rx_data_ps_next, tx_data_pl_next, burst_size);
        // 等待中断信号
    	rx_data_ps_next = rx_data_ps_next + burst_data;
    }
	axitangxi_info("PL DDR --> PS DDR 配置完成！\n");
	//printf("PL DDR --> PS DDR 配置完成！\n");
}

int axitangxi_psddr_plddr_loopback(struct axitangxi_device *dev,
                          struct axitangxi_transaction *trans)
{
	unsigned long timeout, time_remain;
    struct axitangxi_transfer axitx_trans;

	axitx_trans.tx_data_size = trans->tx_data_size;
	axitx_trans.rx_data_size = trans->rx_data_size;
	axitx_trans.burst_size = trans->burst_size;
	axitx_trans.burst_count = trans->burst_count;
	axitx_trans.burst_data = trans->burst_data;
	axitx_trans.tx_data_ps_ptr = trans->tx_data_ps_ptr;
	axitx_trans.rx_data_ps_ptr = trans->rx_data_ps_ptr;
	axitx_trans.tx_data_pl_ptr = trans->tx_data_pl_ptr;
	axitx_trans.rx_data_pl_ptr = trans->rx_data_pl_ptr;
	axitx_trans.node = trans->node;



	//PS DDR --> PL DDR
	init_completion(&axitx_trans.comp);
    axitangxi_psddr_plddr(&axitx_trans);
	//等待中断响应
	timeout = msecs_to_jiffies(AXI_TANGXI_TIMEOUT);
    time_remain = wait_for_completion_timeout(&axitx_trans.comp, timeout);
	if (time_remain == 0) {
		axitangxi_err("axitangxi_psddr_plddr transaction timed out.\n");
		return -ETIME;
	}

	//PL DDR --> PS DDR
	init_completion(&axitx_trans.comp);
	axitangxi_plddr_psddr(&axitx_trans);
	//等待中断响应
	timeout = msecs_to_jiffies(AXI_TANGXI_TIMEOUT);
    time_remain = wait_for_completion_timeout(&axitx_trans.comp, timeout);
	if (time_remain == 0) {
		axitangxi_err("axitangxi_plddr_psddr transaction timed out.\n");
		return -ETIME;
	}

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
void mm2s_ps_data_tangxi(uint32_t tx_data_ps_addr, uint32_t rx_data_pl_addr, int burst_size, uint32_t chip_position, uint8_t node)
{
	/**
	 *
	 * NOC_DATA_PKG_ADDR
	 * 0xnnnaaaaa  [29:20][19:0] : node_addr
	 * n - node[29:20] → [9:8]表示chip_position(FPGA为2'b11,发送的包不能是2'b11), [7:0]表示此包的目的节点(0-255)
	 * a - addr[19:0] → 表示写地址
	 * 0x00300100:node 0x003, addr 0x00100 bram 256-4096,pl ddr 4096+
	 */
	uint32_t noc_data_size_addr = (chip_position << 28) | (node << 20) | rx_data_pl_addr;

	/**
	 * NOC_DATA_PKG_CTL
	 * 0bssssssssssssssssttcccccccc  [25:10][9:8][7:0] : size_type_cmd
	 * s - size[25:10] → 表示写大小（控制包为size不用管，数据包size表示数据长度）
	 * t - type[9:8] → 表示此包的类型, 00:data, 01:ctrl
	 * c - cmd[7:0] → 对应类型的具体命令(控制指令)
	     > cmd[7:6] → ctrl_cmd , 00:reset, 01:start, 10:end, 11:riscv change de_mode(=addr[15])
	     > cmd[5:3] → sync_cmd
	     > cmd[2:0] → ddr_cmd , 001:read, 010:write, 100:refresh
	 * 0b00 00000010:type 00, cmd 00000010
	 */
	uint32_t noc_data_node_type_cmd = ((burst_size - 1) << 10) | 0b0000000010;

	void __iomem *dma_mm2s_ps_addr = ioremap(DMA_MM2S_PS_ADDR, sizeof(uint32_t));
	void __iomem *dma_mm2s_ps_size = ioremap(DMA_MM2S_PS_SIZE, sizeof(uint32_t));
	void __iomem *noc_data_pkg_addr = ioremap(NOC_DATA_PKG_ADDR, sizeof(uint32_t));
	void __iomem *noc_data_pkg_ctl = ioremap(NOC_DATA_PKG_CTL, sizeof(uint32_t));
	//DMA_MM2S_PS_ADDR
	write_register(tx_data_ps_addr, dma_mm2s_ps_addr);
	//DMA_MM2S_PS_SIZE
	write_register(burst_size-1, dma_mm2s_ps_size);

	//NOC_DATA_PKG_ADDR
	write_register(noc_data_size_addr, noc_data_pkg_addr);
	//NOC_DATA_PKG_CTL
	write_register(noc_data_node_type_cmd, noc_data_pkg_ctl);
}

/**
 * @brief tangxi --> PL Read Result (默认存储BRAM，首地址256) --> PS ddr
 *
 * @param rx_data_ps_addr 写PS ddr数据首地址:0x00000000 - 0x7FFFFFFF
 * @param burst_size 突发长度：0-127
 */
void s2mm_ps_data_tangxi(uint32_t rx_data_ps_addr, int burst_size)
{
	//RESULT_READ_ADDR 与NOC_DATA_PKG_ADDR addr一致
	uint32_t noc_data_size_addr = ((burst_size - 1) << 20) | 0x00100;

	void __iomem *result_read_addr = ioremap(RESULT_READ_ADDR, sizeof(uint32_t));
	void __iomem *result_read_ctl = ioremap(RESULT_READ_CTL, sizeof(uint32_t));
	void __iomem *dma_s2mm_ps_addr = ioremap(DMA_S2MM_PS_ADDR, sizeof(uint32_t));
	void __iomem *dma_s2mm_ps_size = ioremap(DMA_S2MM_PS_SIZE, sizeof(uint32_t));

	write_register(noc_data_size_addr, result_read_addr);
	//RESULT_READ_CTL
	write_register(burst_size-1, result_read_ctl);

	//DMA_S2MM_PS_ADDR
	write_register(rx_data_ps_addr, dma_s2mm_ps_addr);

	//DMA_S2MM_PS_SIZE
	write_register(burst_size-1, dma_s2mm_ps_size);
}

/**
 * @brief PS DDR --> PL DDR
 *
 * @param tx_data_ps_addr 读取ps ddr数据首地址:0x00000000 - 0x7FFFFFFF
 * @param rx_data_pl_addr 写入pl ddr数据首地址:0x00000000 - 0x7FFFFFFF
 * @param burst_size 突发长度：0-127
 */
void mm2s_ps_data_s2mm_pl(uint32_t tx_data_ps_addr, uint32_t rx_data_pl_addr, int burst_size)
{
	void __iomem *dma_mm2s_ps_addr = ioremap(DMA_MM2S_PS_ADDR, sizeof(uint32_t));
	void __iomem *dma_mm2s_ps_size = ioremap(DMA_MM2S_PS_SIZE, sizeof(uint32_t));
	void __iomem *dma_s2mm_pl_addr = ioremap(DMA_S2MM_PL_ADDR, sizeof(uint32_t));
	void __iomem *dma_s2mm_pl_size = ioremap(DMA_S2MM_PL_SIZE, sizeof(uint32_t));

	//DMA_MM2S_PS_ADDR
	write_register(tx_data_ps_addr, dma_mm2s_ps_addr);
	//DMA_MM2S_PS_SIZE
	write_register(burst_size-1, dma_mm2s_ps_size);

	//DMA_S2MM_PL_ADDR
	write_register(rx_data_pl_addr, dma_s2mm_pl_addr);
	//DMA_S2MM_PL_SIZE
	write_register(burst_size-1, dma_s2mm_pl_size);
}

/**
 * @brief PL DDR --> PS DDR
 *
 * @param rx_data_ps_addr 写入ps ddr数据首地址:0x00000000 - 0x7FFFFFFF
 * @param tx_data_pl_addr 读取pl ddr数据首地址:0x00000000 - 0x7FFFFFFF
 * @param burst_size 突发长度：0-127
 */
void s2mm_ps_data_mm2s_pl(uint32_t rx_data_ps_addr, uint32_t tx_data_pl_addr, int burst_size)
{
	void __iomem *dma_mm2s_pl_addr = ioremap(DMA_MM2S_PL_ADDR, sizeof(uint32_t));
	void __iomem *dma_mm2s_pl_size = ioremap(DMA_MM2S_PL_SIZE, sizeof(uint32_t));
	void __iomem *dma_s2mm_ps_addr = ioremap(DMA_S2MM_PS_ADDR, sizeof(uint32_t));
	void __iomem *dma_s2mm_ps_size = ioremap(DMA_S2MM_PS_SIZE, sizeof(uint32_t));

	//DMA_MM2S_PL_ADDR
	write_register(tx_data_pl_addr, dma_mm2s_pl_addr);
	//DMA_MM2S_PL_SIZE
	write_register(burst_size-1, dma_mm2s_pl_size);

	//DMA_S2MM_PS_ADDR
	write_register(rx_data_ps_addr, dma_s2mm_ps_addr);
	//DMA_S2MM_PS_SIZE
	write_register(burst_size-1, dma_s2mm_ps_size);
}

//批量PS DDR --> tangxi --> PL DDR
void batch_mm2s_ps_data_tangxi(struct axitangxi_transfer *axitangxi_trans) {
    size_t data_remaining = axitangxi_trans->tx_data_size;

    uint32_t tx_data_ps_next = (uintptr_t) axitangxi_trans->tx_data_ps_ptr;
    uint32_t rx_data_pl_next = (uintptr_t) axitangxi_trans->rx_data_pl_ptr;

    size_t burst_size = axitangxi_trans->burst_size;
    size_t burst_data = axitangxi_trans->burst_data;

	//XTime_GetTime(&start_t);
    while(data_remaining > 0)
    {
    	//最后一次突发
    	if (data_remaining < burst_data) {
    		if ((data_remaining & ADDR_ALIGN_BASE_16_MASK) != 0x00 ) {
    			//对齐16字节
    			data_remaining += ( ADDR_ALIGN_BASE_16 - ( data_remaining & ADDR_ALIGN_BASE_16_MASK ) );
    		}
    		burst_size = data_remaining >> 4;
    		burst_data = data_remaining;
    	}
    	//PS DDR data--> PL --> tangxi, 0:第0个chip, 3:第三个node, 256:req data存储地址256
    	mm2s_ps_data_tangxi(tx_data_ps_next, rx_data_pl_next, burst_size, 0, 3);
        // 等待中断信号
    	tx_data_ps_next = tx_data_ps_next + burst_data;
    	data_remaining -= burst_data;
    }
}

//批量PL DDR --> PS DDR
void batch_s2mm_ps_data_mm2s_pl(struct axitangxi_transfer *axitangxi_trans) {
	size_t data_remaining = axitangxi_trans->rx_data_size;

	uint32_t rx_data_ps_next = (uintptr_t) axitangxi_trans->rx_data_ps_ptr;
	uint32_t tx_data_pl_next = (uintptr_t) axitangxi_trans->rx_data_pl_ptr;

	size_t burst_size = axitangxi_trans->burst_size;
	size_t burst_data = axitangxi_trans->burst_data;
	while (data_remaining > 0) {
		if(data_remaining < burst_data) {
			//如果剩余的数据小于一次性突发数据，修改突发长度
			if ((data_remaining & ADDR_ALIGN_BASE_16_MASK) != 0x00 ) {
				//对齐16字节
				data_remaining += ( ADDR_ALIGN_BASE_16 - ( data_remaining & ADDR_ALIGN_BASE_16_MASK ) );
			}

			burst_size = data_remaining >> 4;
			burst_data = data_remaining;
		}
		s2mm_ps_data_mm2s_pl(rx_data_ps_next, tx_data_pl_next, burst_size);
		rx_data_ps_next = rx_data_ps_next + burst_data;
		data_remaining -= burst_data;
	}
}

int axi_tangxi_loopback(struct axitangxi_transfer *axitangxi_trans) {
	//PS DDR --> tangxi --> PL DDR
	batch_mm2s_ps_data_tangxi(axitangxi_trans);

    udelay(2000);
	//ps最后一组数据发送完成，从PL DDR取数
	//if (ps_transfer_done) {
		batch_s2mm_ps_data_mm2s_pl(axitangxi_trans);
	//}
	udelay(2000);
	//if (pl_transfer_done){
		//XTime_GetTime(&end_t);
		//double elapsed_time = ((double)(end_t - start_t) * 1e6) / COUNTS_PER_SECOND;
		//printf("send done! elapsed_time: %.2f us.\n", elapsed_time);

		//刷新Data Cache
		//Xil_DCacheFlushRange((UINTPTR) axi_args.rx_data_ps_ptr, axi_args.tx_data_size);

	//verify_data(dev);
	//	for (int i = len - 25; i < len ; i++) {
	//		xil_printf("Data at address 0x%x: %u\n", axi_args.rx_data_ps_ptr + i, axi_args.rx_data_ps_ptr[i]);
	//	}
	//	for (int i = len - 25; i < len + 2; i++) {
	//		xil_printf("Data at address 0x%x: %u\n", axi_args.tx_data_ps_ptr + i, axi_args.tx_data_ps_ptr[i]);
	//	}

	    //disableInterruptSystem(&INTC);
	//}

    return 0;
}
