#ifndef AXITANGXI_DEV_H_
#define AXITANGXI_DEV_H_

#include <linux/list.h>         // Linked list definitions and functions
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/cdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
//#include <linux/platform_device.h>

#include <linux/dmaengine.h>        // Definitions for DMA structures and types
#include <linux/dma-buf.h>      // DMA shared buffers interface
#include <linux/of_dma.h>

#include "axitangxi_ioctl.h"           // IOCTL argument structures

/* 设备节点名称 */
#define DEVICE_NAME       "axi_tangxi"

// The standard path to the AXI DMA device
#define AXITX_DEV_PATH     ("/dev/" DEVICE_NAME)

/* 设备号个数 */
#define DEVID_COUNT       1
/* 驱动个数 */
#define DRIVE_COUNT       1
/* 主设备号 */
#define MAJOR_U
/* 次设备号 */
#define MINOR_U           0


#define  DMA_MM2S_PS_ADDR    0x80000000
#define  DMA_MM2S_PS_SIZE    0x80000004
#define  NOC_DATA_PKG_ADDR   0x80000008
#define  NOC_DATA_PKG_CTL    0x8000000C
#define  NOC_CTL_PKG_CTL     0x80000010
#define  CSRAM_WRITE_CTL     0x80000014
#define  DMA_S2MM_PS_ADDR    0x80000018
#define  DMA_S2MM_PS_SIZE    0x8000001C
#define  RESULT_READ_ADDR    0x80000020
#define  RESULT_READ_CTL     0x80000024
#define  TX_RISCV_RESET_CTL  0x80000028
#define  PL_IRQ_TASK_NUM     0x8000002C
#define  DMA_MM2S_PL_ADDR    0x80000030
#define  DMA_MM2S_PL_SIZE    0x80000034
#define  DMA_S2MM_PL_ADDR    0x80000038
#define  DMA_S2MM_PL_SIZE    0x8000003C
#define  NODE_OCCUPY_BEGIN   0x80001000 // 1KB 'h1000-'h13FC
#define  TASK_RECORD_BEGIN   0x80001400 // 1KB 'h1400-'h17FC

#define MM2S_PS_DONE 256  //MM2S_PS传输完成
#define S2MM_PS_DONE 257  //S2MM_PS传输完成
#define MM2S_PL_DONE 258  //MM2S_PL传输完成
#define S2MM_PL_DONE 259  //S2MM_PL传输完成
#define REQ_DATA_DONE 260  //NI发包完成（req data）
#define READ_RESULT_DONE 261  //Read Result传输完成
#define DDR_RW_DONE 262  //DDR_RW读写任务完成

//4K字节对齐
#define  ADDR_ALIGN_BASE_4096           0x1000
#define  ADDR_ALIGN_BASE_4096_MASK      0x0FFF
#define  ADDR_ALIGNED_4096(addr)  (UINTPTR) (((addr) + ADDR_ALIGN_BASE_4096_MASK) & (~ADDR_ALIGN_BASE_4096_MASK))

//16字节对齐
#define  ADDR_ALIGN_BASE_16           0x0010
#define  ADDR_ALIGN_BASE_16_MASK      0x000F
#define  ADDR_ALIGNED_16(addr)  (UINTPTR) (((addr) + ADDR_ALIGN_BASE_16_MASK) & (~ADDR_ALIGN_BASE_16_MASK))

// Truncates the full __FILE__ path, only displaying the basename
#define __FILENAME__ \
    (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

// Convenient macros for printing out messages to the kernel log buffer
#define axitangxi_err(fmt, ...) \
    printk(KERN_ERR DEVICE_NAME ": %s: %s: %d: " fmt, __FILENAME__, __func__, \
           __LINE__, ## __VA_ARGS__)
#define axitangxi_info(fmt, ...) \
    printk(KERN_INFO DEVICE_NAME ": %s: %s: %d: " fmt, __FILENAME__, __func__, \
            __LINE__, ## __VA_ARGS__)

// static void __iomem *LED_CTR_REG_MAP;
// static void __iomem *LED_FRE_REG_MAP;

/* 把驱动代码中会用到的数据打包进设备结构体 */
struct axitangxi_device {
/** 字符设备框架 **/
    dev_t              devid;             //设备号
    struct cdev        cdev;              //字符设备
    struct class       *class;            //类
    struct device      *device;           //设备
    struct device_node *nd;               //设备树的设备节点

/** 并发处理 **/
//    spinlock_t         lock;              //自旋锁变量

/** 中断 **/
    unsigned int       irq;               //中断号
    unsigned int       regdata[2];        //寄存器
/** 定时器 **/
    //struct timer_list  timer;             //定时器

    struct list_head dmabuf_list;   // List of allocated DMA buffers
    struct list_head external_dmabufs;  // Buffers allocated in other drivers
};

// The data to pass to the DMA transfer completion callback function
struct axitangxi_irq_data {
	bool last_mm2s_ps_done;         // 最后一次读PS DDR传输完成
	bool last_s2mm_ps_done;         // 最后一次写PS DDR传输完成
	bool last_mm2s_pl_done;         // 最后一次读PL DDR传输完成
	bool last_s2mm_pl_done;         // 最后一次写PL DDR传输完成
	bool last_req_data_done;        // 最后一次NI发包传输完成
	bool last_read_result_done;     // 最后一次read result传输完成

    struct completion *comp;        // For sync, the notification to kernel
};

int axitangxi_psddr_plddr_loopback(struct axitangxi_device *dev,
                          struct axitangxi_transaction *trans);

void write_register(uint32_t data, void __iomem *addr);
uint32_t read_register(void __iomem *addr);
uint32_t get_irq(void);
void clean_irq(void);


void mm2s_ps_data_s2mm_pl(uint32_t tx_data_ps_addr, uint32_t rx_data_pl_addr, int burst_size);
void s2mm_ps_data_mm2s_pl(uint32_t rx_data_ps_addr, uint32_t tx_data_pl_addr, int burst_size);
void s2mm_ps_data_tangxi(uint32_t rx_data_ps_addr, int burst_size);
//ps ddr --> pl ddr
//int axitangxi_psddr_plddr(axitangxi_transaction_t axitangxi_trans);
//int axitangxi_plddr_psddr(axitangxi_transaction_t axitangxi_trans);

#endif /* AXITANGXI_H_ */
