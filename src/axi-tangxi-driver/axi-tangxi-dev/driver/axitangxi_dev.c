/**
 * @file axitangxi_dev.c
 * @date 2023/09/25
 * @author Wang Yang
 *
 * axi-tangxi内核驱动文件，初始化设备、注册设备、中断处理
 *
 * @bug
 **/

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h> // Linked list definitions and functions
#include <linux/mm.h>   // Memory types and remapping functions
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h> // Device tree device related functions
#include <linux/of_irq.h>
#include <linux/types.h>
#include <linux/uaccess.h> // Userspace memory access functions

#include "axitangxi_dev.h"
#include "axitangxi_ioctl.h"

/* 设备号个数 */
#define DEVID_COUNT 1
/* 驱动个数 */
#define DRIVE_COUNT 1
/* 主设备号 */
#define MAJOR_U
/* 次设备号 */
#define MINOR_U 0

// 中断同步信号
extern struct axitangxi_irq_data axitangxi_irq_data;

// 定义需要映射的寄存器虚拟地址
void __iomem *dma_mm2s_ps_addr;
void __iomem *dma_mm2s_ps_size;
void __iomem *noc_data_pkg_addr;
void __iomem *noc_data_pkg_ctl;
void __iomem *noc_ctl_pkg_ctl;
void __iomem *csram_write_ctl;
void __iomem *dma_s2mm_ps_addr;
void __iomem *dma_s2mm_ps_size;
void __iomem *result_read_addr;
void __iomem *result_read_ctl;
void __iomem *tx_riscv_reset_ctl;
void __iomem *dma_mm2s_pl_addr;
void __iomem *dma_mm2s_pl_size;
void __iomem *dma_s2mm_pl_addr;
void __iomem *dma_s2mm_pl_size;
void __iomem *pl_irq_task_num;
// 定义网络加速器需要映射的寄存器虚拟地址
void __iomem *network_acc_control;
void __iomem *network_acc_weight_addr;
void __iomem *network_acc_weight_size;
void __iomem *network_acc_quantify_addr;
void __iomem *network_acc_quantify_size;
void __iomem *network_acc_picture_addr;
void __iomem *network_acc_picture_size;
void __iomem *network_acc_trans_addr;
void __iomem *network_acc_trans_size;
void __iomem *network_acc_entropy_addr;
void __iomem *network_acc_entropy_size;

/* 声明设备结构体 */
static struct axitangxi_device axitangxi_dev = {
    .cdev =
        {
            .owner = THIS_MODULE,
        },
};

// 分配的地址参数
struct axitangxi_allocation {
  size_t size;           // 分配内存大小
  void *user_addr;       // 用户空间虚拟地址
  void *kern_addr;       // 内核空间虚拟地址
  uint32_t phys_addr;    // 物理地址
  struct list_head list; // 存储不同内存块的地址参数
};

/**
 * @brief Get the irq object
 *
 * @return uint32_t
 */
static uint32_t get_irq(void) {
  uint32_t value;
  value = readl(pl_irq_task_num);
  return value;
}

/**
 * @brief 清除中断
 *
 *
 */
static void clean_irq(void) { writel(0, pl_irq_task_num); }

/**
 * @brief 中断处理函数
 *
 * @param irq
 * @param data
 * @return irqreturn_t
 */
static irqreturn_t irq_handler(int irq, void *data) {
  uint32_t irq_TX;
  // printk("irq get !\n");
  irq_TX = get_irq();
  switch (irq_TX) {
  case MM2S_PS_DONE:
    // PL读PS DDR结束
    printk("MM2S_PS_DONE, irq: %d\n", irq_TX);
    break;
  case S2MM_PS_DONE:
    // PL写PS DDR结束：代表整个接收数据流程结束
    if (axitangxi_irq_data.last_s2mm_ps_done) {
      printk("last S2MM_PS_DONE, irq: %d\n", irq_TX);
      complete(axitangxi_irq_data.comp);
    }
    printk("S2MM_PS_DONE irq: %d\n", irq_TX);
    break;
  case MM2S_PL_DONE:
    // PL读PL DDR结束
    printk("MM2S_PL_DONE, irq: %d\n", irq_TX);
    break;
  case S2MM_PL_DONE:
    // 写 PL DDR结束
    if (axitangxi_irq_data.last_s2mm_pl_done) {
      printk("last S2MM_PL_DONE irq: %d\n", irq_TX);
      complete(axitangxi_irq_data.comp);
    }
    printk("S2MM_PL_DONE irq: %d\n", irq_TX);
    break;
  case REQ_DATA_DONE:
    // NI发包完成（req data），PL读PS
    // DDR的数发送到芯片完成，代表整个发送数据完成
    if (axitangxi_irq_data.last_req_data_done) {
      printk("last REQ_DATA_DONE irq: %d\n", irq_TX);
    }
    printk("REQ_DATA_DONE, irq: %d\n", irq_TX);
    break;
  case READ_RESULT_DONE:
    // Read Result传输完成，tangxi芯片写 PL BRAM完成
    printk("READ_RESULT_DONE irq: %d\n", irq_TX);
    break;
  case NETWORK_ACC_DONE:
    // 加速器处理图片完成
    printk("NETWORK_ACC_DONE irq: %d\n", irq_TX);
    break;
  default:
    printk("not match! irq:  %d\n", irq_TX);
    break;
  }
  clean_irq();

  return IRQ_RETVAL(IRQ_HANDLED);
}

/**
 * @brief open函数实现, 对应到Linux系统调用函数的open函数
 *
 * @param inode_p
 * @param file_p
 * @return int
 */
static int axitangxi_open(struct inode *inode_p, struct file *file_p) {
  file_p->private_data =
      &axitangxi_dev; /* set our own structure as the private_data */

  printk("axi-tangxi module open\n");
  return 0;
}

/**
 * @brief  release函数实现, 对应到Linux系统调用函数的close函数
 *
 * @param inode_p
 * @param file_p
 * @return * int
 */
static int axitangxi_release(struct inode *inode_p, struct file *file_p) {
  printk("axi tangxi module release\n");
  return 0;
}

/**
 * @brief
 * ioctl函数实现，实现用户态与内核态的系统调用，对应到Linux系统调用函数的ioctl函数
 *
 * @param file
 * @param cmd ioctl定义的命令
 * @param arg ioctl传参
 * @return long
 */
static long axitangxi_ioctl(struct file *file, unsigned int cmd,
                            unsigned long arg) {
  long rc;
  void *__user arg_ptr;
  struct axitangxi_device *dev;
  struct axitangxi_transaction trans;
  struct network_acc_reg iargs;

  // Coerce the argument as a userspace pointer
  arg_ptr = (void __user *)arg;
  printk("axitangxi_ioctl start\n");

  // Verify that this IOCTL is intended for our device, and is in range
  if (_IOC_TYPE(cmd) != AXITANGXI_IOCTL_MAGIC) {
    axitangxi_err("IOCTL command magic number does not match.\n");
    return -ENOTTY;
  } else if (_IOC_NR(cmd) >= AXITANGXI_NUM_IOCTLS) {
    axitangxi_err("IOCTL command is out of range for this device.\n");
    return -ENOTTY;
  }

  // Get the axitangxi device from the file
  dev = file->private_data;

  // 各种功能命令，如果需要添加功能，添加对应的命令的实现函数
  switch (cmd) {
  case AXITANGXI_PSDDR_PLDDR_LOOPBACK:
    if (copy_from_user(&trans, arg_ptr, sizeof(trans)) != 0) {
      axitangxi_err("Unable to copy transfer info from userspace for "
                    "AXITANGXI_PSDDR_PLDDR_LOOPBACK.\n");
      return -EFAULT;
    }
    rc = axitangxi_psddr_plddr_loopback(dev, &trans);
    break;

  case AXITANGXI_PSDDR_TANGXI_LOOPBACK:
    if (copy_from_user(&trans, arg_ptr, sizeof(trans)) != 0) {
      axitangxi_err("Unable to copy channel buffer address from "
                    "userspace for AXIDMA_GET_DMA_CHANNELS.\n");
      return -EFAULT;
    }

    rc = 0;
    break;
  case NETWORK_ACC_CONFIG:
    if (copy_from_user(&iargs, arg_ptr, sizeof(iargs)) != 0) {
      axitangxi_err("Unable to copy network_acc_reg info from userspace for "
                    "NETWORK_ACC_CONFIG.\n");
      return -EFAULT;
    }
    rc = acc_config(dev, &iargs);
    break;
  case NETWORK_ACC_START:
    rc = acc_start(dev);
    break;
  case NETWORK_ACC_GET:
    if (copy_from_user(&iargs, arg_ptr, sizeof(iargs)) != 0) {
      axitangxi_err("Unable to copy network_acc_reg info from userspace for "
                    "NETWORK_ACC_GET.\n");
      return -EFAULT;
    }
    rc = acc_get(dev, &iargs);
    break;
  // Invalid command (already handled in preamble)
  default:
    return -ENOTTY;
  }

  return rc;
}

/**
 * @brief 用户虚拟地址转化成物理地址
 *
 * @param dev
 * @param user_addr
 * @return uint32_t
 */
uint32_t axitangxi_uservirt_to_phys(struct axitangxi_device *dev,
                                    void *user_addr) {
  struct list_head *iter;
  struct axitangxi_allocation *addr_alloc;

  // 遍历链表，找到对应虚拟地址的物理地址
  list_for_each(iter, &dev->addr_list) {
    addr_alloc = container_of(iter, struct axitangxi_allocation, list);
    if (addr_alloc->user_addr == user_addr) {
      return addr_alloc->phys_addr;
    }
  }
}

/**
 * @brief
 * munmap函数实现，实现用户态解除内存映射，对应到Linux系统调用函数的munmap函数
 *
 * @param vma
 */
static void axitangxi_vma_close(struct vm_area_struct *vma) {
  kfree(vma->vm_private_data);
  return;
}

// The VMA operations for the AXI TANGXI device
static const struct vm_operations_struct axitangxi_vm_ops = {
    .close = axitangxi_vma_close,
};

/**
 * @brief
 * mmap函数实现，实现用户态分配内存映射，对应到Linux系统调用函数的mmap函数
 *
 * @param file
 * @param vma
 * @return int
 */
static int axitangxi_mmap(struct file *file, struct vm_area_struct *vma) {

  int rc = 0;
  size_t size;
  size = vma->vm_end - vma->vm_start;

  struct axitangxi_device *axi_dev;
  struct axitangxi_allocation *axitangxi_alloc;
  // Get the axitangxi device structure
  axi_dev = file->private_data;

  // 分配设备内存
  axitangxi_alloc = kmalloc(sizeof(*axitangxi_alloc), GFP_KERNEL);
  if (axitangxi_alloc == NULL) {
    axitangxi_err("Unable to allocate axitangxi_alloc data structure.");
    rc = -ENOMEM;
    goto ret;
  }

  // 分配数据内存
  void *kbuff;
  kbuff = kzalloc(size, GFP_KERNEL);
  if (!kbuff) {
    rc = -ENOMEM;
    goto ret;
  }

  size_t offset = vma->vm_pgoff << PAGE_SHIFT;
  size_t phys = virt_to_phys(kbuff);
  size_t pfn_start = (phys >> PAGE_SHIFT) + vma->vm_pgoff;
  size_t virt_start = (size_t)kbuff + offset;

  printk("kbuff: 0x%lx, vma->vm_start: 0x%lx, vma->vm_end: 0x%lx, phys: 0x%lx, "
         "pfn_start: 0x%lx\n",
         (size_t)kbuff, (size_t)vma->vm_start, (size_t)vma->vm_end, phys,
         pfn_start);

  printk("phy: 0x%lx, offset: 0x%lx, size: 0x%lx\n", pfn_start << PAGE_SHIFT,
         offset, size);

  // 映射物理地址与用户虚拟地址
  rc = remap_pfn_range(vma, vma->vm_start, pfn_start, size, vma->vm_page_prot);
  if (rc) {
    axitangxi_err("remap_pfn_range failed at [0x%lx  0x%lx]\n", vma->vm_start,
                  vma->vm_end);
    goto free_data;
  } else {
    axitangxi_info("map 0x%lx to 0x%lx, size: 0x%lx\n", virt_start,
                   (size_t)vma->vm_start, size);
  }

  vma->vm_ops = &axitangxi_vm_ops;

  axitangxi_alloc->size = size;
  axitangxi_alloc->user_addr = (void *)vma->vm_start;
  axitangxi_alloc->kern_addr = kbuff;
  axitangxi_alloc->phys_addr = (uint32_t)pfn_start << PAGE_SHIFT;

  vma->vm_private_data = axitangxi_alloc;

  list_add(&axitangxi_alloc->list, &axi_dev->addr_list);

free_data:
  kfree(kbuff);
ret:
  return rc;
}

/* file_operations结构体声明, 是上面open、write实现函数与系统调用函数对应的关键
 */
static const struct file_operations axitangxi_fops = {
    .owner = THIS_MODULE,
    .open = axitangxi_open,
    .release = axitangxi_release,
    .unlocked_ioctl = axitangxi_ioctl,
    .mmap = axitangxi_mmap,
};

/**
 * @brief 映射寄存器地址，如果有增加寄存器地址，在此添加
 *
 */
void inline reg_map(void) {
  dma_mm2s_ps_addr = ioremap(DMA_MM2S_PS_ADDR, 4);
  dma_mm2s_ps_size = ioremap(DMA_MM2S_PS_SIZE, 4);

  noc_data_pkg_addr = ioremap(NOC_DATA_PKG_ADDR, 4);
  noc_data_pkg_ctl = ioremap(NOC_DATA_PKG_CTL, 4);

  noc_ctl_pkg_ctl = ioremap(NOC_CTL_PKG_CTL, 4);
  csram_write_ctl = ioremap(CSRAM_WRITE_CTL, 4);

  dma_s2mm_ps_addr = ioremap(DMA_S2MM_PS_ADDR, 4);
  dma_s2mm_ps_size = ioremap(DMA_S2MM_PS_SIZE, 4);

  result_read_addr = ioremap(RESULT_READ_ADDR, 4);
  result_read_ctl = ioremap(RESULT_READ_CTL, 4);
  tx_riscv_reset_ctl = ioremap(TX_RISCV_RESET_CTL, 4);

  dma_mm2s_pl_addr = ioremap(DMA_MM2S_PL_ADDR, 4);
  dma_mm2s_pl_size = ioremap(DMA_MM2S_PL_SIZE, 4);

  dma_s2mm_pl_addr = ioremap(DMA_S2MM_PL_ADDR, 4);
  dma_s2mm_pl_size = ioremap(DMA_S2MM_PL_SIZE, 4);

  pl_irq_task_num = ioremap(PL_IRQ_TASK_NUM, 4);

  // 网络加速器寄存器地址
  network_acc_control = ioremap(ENGINE_CR, 4);

  network_acc_weight_addr = ioremap(ENGINE_WA, 4);
  network_acc_weight_size = ioremap(ENGINE_WS, 4);

  network_acc_quantify_addr = ioremap(ENGINE_QA, 4);
  network_acc_quantify_size = ioremap(ENGINE_QS, 4);

  network_acc_picture_addr = ioremap(ENGINE_PA, 4);
  network_acc_picture_size = ioremap(ENGINE_PS, 4);

  network_acc_trans_addr = ioremap(ENGINE_TA, 4);
  network_acc_trans_size = ioremap(ENGINE_TS, 4);

  network_acc_entropy_addr = ioremap(ENGINE_EA, 4);
  network_acc_entropy_size = ioremap(ENGINE_ES, 4);

  printk("DMA_MM2S_PS_ADDR: 0x%lx\r\n", (size_t)dma_mm2s_ps_addr);
  printk("DMA_MM2S_PS_SIZE: 0x%lx\r\n", (size_t)dma_mm2s_ps_size);

  printk("NOC_DATA_PKG_ADDR: 0x%lx\r\n", (size_t)noc_data_pkg_addr);
  printk("NOC_DATA_PKG_CTL: 0x%lx\r\n", (size_t)noc_data_pkg_ctl);

  printk("NOC_CTL_PKG_CTL: 0x%lx\r\n", (size_t)noc_ctl_pkg_ctl);
  printk("CSRAM_WRITE_CTL: 0x%lx\r\n", (size_t)csram_write_ctl);

  printk("DMA_S2MM_PS_ADDR: 0x%lx\r\n", (size_t)dma_s2mm_ps_addr);
  printk("DMA_S2MM_PS_SIZE: 0x%lx\r\n", (size_t)dma_s2mm_ps_size);

  printk("RESULT_READ_ADDR: 0x%lx\r\n", (size_t)result_read_addr);
  printk("RESULT_READ_CTL: 0x%lx\r\n", (size_t)result_read_ctl);
  printk("TX_RISCV_RESET_CTL: 0x%lx\r\n", (size_t)tx_riscv_reset_ctl);

  printk("DMA_MM2S_PL_ADDR: 0x%lx\r\n", (size_t)dma_mm2s_pl_addr);
  printk("DMA_MM2S_PL_SIZE: 0x%lx\r\n", (size_t)dma_mm2s_pl_size);

  printk("DMA_S2MM_PL_ADDR: 0x%lx\r\n", (size_t)dma_s2mm_pl_addr);
  printk("DMA_S2MM_PL_SIZE: 0x%lx\r\n", (size_t)dma_s2mm_pl_size);

  printk("PL_IRQ_TASK_NUM: 0x%lx\r\n", (size_t)pl_irq_task_num);
  // 网络加速器
  printk("ENGINE_CR: 0x%lx\r\n", (size_t)network_acc_control);

  printk("ENGINE_WA: 0x%lx\r\n", (size_t)network_acc_weight_addr);
  printk("ENGINE_WS: 0x%lx\r\n", (size_t)network_acc_weight_size);

  printk("ENGINE_QA: 0x%lx\r\n", (size_t)network_acc_quantify_addr);
  printk("ENGINE_QS: 0x%lx\r\n", (size_t)network_acc_quantify_size);

  printk("ENGINE_PA: 0x%lx\r\n", (size_t)network_acc_picture_addr);
  printk("ENGINE_PS: 0x%lx\r\n", (size_t)network_acc_picture_size);

  printk("ENGINE_TA: 0x%lx\r\n", (size_t)network_acc_trans_addr);
  printk("ENGINE_TS: 0x%lx\r\n", (size_t)network_acc_trans_size);

  printk("ENGINE_EA: 0x%lx\r\n", (size_t)network_acc_entropy_addr);
  printk("ENGINE_ES: 0x%lx\r\n", (size_t)network_acc_entropy_size);
}

/**
 * @brief 释放寄存器映射
 *
 */
void inline reg_unmap(void) {

  iounmap(dma_mm2s_ps_addr);
  iounmap(dma_mm2s_ps_size);
  iounmap(noc_data_pkg_addr);
  iounmap(noc_data_pkg_ctl);
  iounmap(noc_ctl_pkg_ctl);
  iounmap(csram_write_ctl);
  iounmap(dma_s2mm_ps_addr);
  iounmap(dma_s2mm_ps_size);
  iounmap(result_read_addr);
  iounmap(result_read_ctl);
  iounmap(tx_riscv_reset_ctl);
  iounmap(dma_mm2s_pl_addr);
  iounmap(dma_mm2s_pl_size);
  iounmap(dma_s2mm_pl_addr);
  iounmap(dma_s2mm_pl_size);
  iounmap(pl_irq_task_num);

  iounmap(network_acc_control);
  iounmap(network_acc_weight_addr);
  iounmap(network_acc_weight_size);
  iounmap(network_acc_quantify_addr);
  iounmap(network_acc_quantify_size);
  iounmap(network_acc_picture_addr);
  iounmap(network_acc_picture_size);
  iounmap(network_acc_trans_addr);
  iounmap(network_acc_trans_size);
  iounmap(network_acc_entropy_addr);
  iounmap(network_acc_entropy_size);
}

/* 模块加载时会调用的函数 */
static int __init axitangxi_init(void) {
  /* 用于接受返回值 */
  u32 ret = 0;

  /** 并发处理 **/
  /* 初始化自旋锁 */
  //    spin_lock_init(&alinx_char.lock);

  /** 设备框架 **/
  /* 获取设备节点 */
  axitangxi_dev.nd = of_find_node_by_path("/amba_pl@0/axi_tangxi@80000000");
  if (axitangxi_dev.nd == NULL) {
    printk("axitangxi_dev node not find\r\n");
    return -EINVAL;
  } else {
    printk("axitangxi_dev node find\r\n");
  }

  /*获取寄存器*/
  ret = of_property_read_u32_array(axitangxi_dev.nd, "reg",
                                   axitangxi_dev.regdata, 2);
  if (ret < 0) {
    printk("reg read failed!\r\n");
  } else {
    u8 i = 0;
    printk("reg data:\r\n");
    for (i = 0; i < 2; i++)
      printk("%#X ", axitangxi_dev.regdata[i]);
    printk("\r\n");
  }

  /** 中断 **/
  /* 获取中断号 */
  axitangxi_dev.irq = irq_of_parse_and_map(axitangxi_dev.nd, 0);

  /* 申请中断 */
  ret = request_irq(axitangxi_dev.irq, irq_handler, IRQF_TRIGGER_RISING,
                    "axitangxi_irq", NULL);
  if (ret < 0) {
    printk("irq %d request failed\r\n", axitangxi_dev.irq);
    return -EFAULT;
  }
  printk("irq %d request done\r\n", axitangxi_dev.irq);

  /** 字符设备框架 **/
  /* 注册设备号 */
  alloc_chrdev_region(&axitangxi_dev.devid, MINOR_U, DEVID_COUNT, DEVICE_NAME);

  /* 初始化字符设备结构体 */
  cdev_init(&axitangxi_dev.cdev, &axitangxi_fops);

  /* 注册字符设备 */
  cdev_add(&axitangxi_dev.cdev, axitangxi_dev.devid, DRIVE_COUNT);

  /* 创建类 */
  axitangxi_dev.class = class_create(THIS_MODULE, DEVICE_NAME);
  if (IS_ERR(axitangxi_dev.class)) {
    return PTR_ERR(axitangxi_dev.class);
  }

  /* 创建设备节点 */
  axitangxi_dev.device = device_create(axitangxi_dev.class, NULL,
                                       axitangxi_dev.devid, NULL, DEVICE_NAME);
  if (IS_ERR(axitangxi_dev.device)) {
    return PTR_ERR(axitangxi_dev.device);
  }

  // 地址映射
  reg_map();

  INIT_LIST_HEAD(&axitangxi_dev.addr_list);

  printk("axi-tangxi init done!\r\n");

  return 0;
}

/* 卸载模块 */
static void __exit axitangxi_exit(void) {
  /** 中断 **/
  /* 释放中断 */
  free_irq(axitangxi_dev.irq, NULL);

  // 释放地址映射
  reg_unmap();

  /** 字符设备框架 **/
  /* 注销字符设备 */
  cdev_del(&axitangxi_dev.cdev);

  /* 注销设备号 */
  unregister_chrdev_region(axitangxi_dev.devid, DEVID_COUNT);

  /* 删除设备节点 */
  device_destroy(axitangxi_dev.class, axitangxi_dev.devid);

  /* 删除类 */
  class_destroy(axitangxi_dev.class);
}

/* 标记加载、卸载函数 */
module_init(axitangxi_init);
module_exit(axitangxi_exit);

/* 驱动描述信息 */
MODULE_AUTHOR("Wyn");
MODULE_ALIAS("axitangxi device");
MODULE_DESCRIPTION("AXI TANGXI driver");
MODULE_VERSION("v2.0");
MODULE_LICENSE("GPL");