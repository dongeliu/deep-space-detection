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
#include <linux/uaccess.h>      // Userspace memory access functions
#include <linux/of_device.h>    // Device tree device related functions
#include <linux/list.h>         // Linked list definitions and functions
#include <linux/mm.h>           // Memory types and remapping functions

#include "axitangxi_dev.h"
#include "axitangxi_ioctl.h"

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

// static void __iomem *LED_CTR_REG_MAP;
// static void __iomem *LED_FRE_REG_MAP;

/* 声明设备结构体 */
static struct axitangxi_device axitangxi_dev = {
    .cdev = {
        .owner = THIS_MODULE,
    },
};

// A structure that represents a DMA buffer allocation
struct axitangxi_allocation {
    size_t size;                // Size of the buffer
    void *user_addr;            // User virtual address of the buffer
    void *kern_addr;            // Kernel virtual address of the buffer
    dma_addr_t dma_addr;        // DMA bus address of the buffer
    struct list_head list;      // List node pointers for allocation list
};

extern struct axitangxi_irq_data axitangxi_irq_data;

void write_register(uint32_t data, void __iomem *addr) {
    iowrite32(data, addr);
}

uint32_t read_register(void __iomem *addr) {
    return ioread32(addr);
}

uint32_t get_irq() {
    uint32_t value;
    void __iomem *addr = ioremap(PL_IRQ_TASK_NUM, sizeof(uint32_t));
    if (!addr) {
        pr_err("Failed to map memory for IRQ task\n");
        return 0;
    }
    value = read_register(addr);
    iounmap(addr);
    return value;
}

void clean_irq() {
    void __iomem *addr = ioremap(PL_IRQ_TASK_NUM, sizeof(uint32_t));
    if (!addr) {
        pr_err("Failed to map memory for IRQ task\n");
        return;
    }
    write_register(0, addr);
    iounmap(addr);
}


/** 回掉 **/
/* 中断服务函数 */
static irqreturn_t key_handler(int irq, void *data)
{
    uint32_t irq_TX;
    irq_TX = get_irq();
	switch(irq_TX) {
		case MM2S_PS_DONE:
			//PL读PS DDR结束
            printk("irq: MM2S_PS_DONE\n");
			break;
		case S2MM_PS_DONE:
			//PL写PS DDR结束：代表整个接收数据流程结束
			if (axitangxi_irq_data.last_s2mm_ps_done) {
                printk("irq: last S2MM_PS_DONE\n");
			 	complete(axitangxi_irq_data.comp);
			}
            printk("irq: S2MM_PS_DONE\n");
			break;
		case MM2S_PL_DONE:
			//PL读PL DDR结束
            printk("irq: MM2S_PL_DONE\n");
			break;
		case S2MM_PL_DONE:
			//写 PL DDR结束
            if (axitangxi_irq_data.last_s2mm_pl_done) {
                printk("irq: last S2MM_PL_DONE\n");
			 	complete(axitangxi_irq_data.comp);
			}
            printk("irq: S2MM_PL_DONE\n");
			break;
		case REQ_DATA_DONE:
			//NI发包完成（req data），PL读PS DDR的数发送到芯片完成，代表整个发送数据完成
			// if(ps_conf_done) {
			// 	ps_transfer_done = 1;
			// }
            printk("irq: REQ_DATA_DONE\n");
			break;
		case READ_RESULT_DONE:
			//Read Result传输完成，tangxi芯片写 PL BRAM完成
            printk("irq: READ_RESULT_DONE\n");
			break;
	}
	clean_irq();
    /* 按键按下或抬起时会进入中断 */
    /* 开启50毫秒的定时器用作防抖动 */
    //mod_timer(&alinx_char.timer, jiffies + msecs_to_jiffies(50));
    return IRQ_RETVAL(IRQ_HANDLED);
}

/** 系统调用实现 **/
/* open函数实现, 对应到Linux系统调用函数的open函数 */
static int axitangxi_open(struct inode *inode_p, struct file *file_p)
{
    file_p->private_data = &axitangxi_dev;	/* set our own structure as the private_data */

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
static int axitangxi_release(struct inode *inode_p, struct file *file_p)
{
    printk("axi tangxi module release\n");
    return 0;
}

static long axitangxi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    long rc;
    //size_t size;
    void *__user arg_ptr;
    struct axitangxi_device *dev;
    struct axitangxi_transaction trans;

    // Coerce the argument as a userspace pointer
    arg_ptr = (void __user *)arg;

    // Verify that this IOCTL is intended for our device, and is in range
    if (_IOC_TYPE(cmd) != AXITANGXI_IOCTL_MAGIC) {
        axitangxi_err("IOCTL command magic number does not match.\n");
        return -ENOTTY;
    } else if (_IOC_NR(cmd) >= AXITANGXI_NUM_IOCTLS) {
        axitangxi_err("IOCTL command is out of range for this device.\n");
        return -ENOTTY;
    }

    // Verify the input argument
    // if ((_IOC_DIR(cmd) & _IOC_READ)) {
    //     if (!axidma_access_ok(arg_ptr, _IOC_SIZE(cmd), false)) {
    //         return -EFAULT;
    //     }
    // } else if (_IOC_DIR(cmd) & _IOC_WRITE) {
    //     if (!axidma_access_ok(arg_ptr, _IOC_SIZE(cmd), true)) {
    //         return -EFAULT;
    //     }
    // }

    // Get the axidma device from the file
    dev = file->private_data;

    // Perform the specified command
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

            // Copy the channels array to userspace
            // axidma_get_num_channels(dev, &num_chans);
            // axidma_get_channel_info(dev, &kern_chans);
            // size = num_chans.num_channels * sizeof(kern_chans.channels[0]);
            // if (copy_to_user(usr_chans.channels, kern_chans.channels, size)) {
            //     axidma_err("Unable to copy channel ids to userspace for "
            //                "AXIDMA_GET_DMA_CHANNELS.\n");
            //     return -EFAULT;
            // }

            rc = 0;
            break;

        // Invalid command (already handled in preamble)
        default:
            return -ENOTTY;
    }

    return rc;
}

static void axitangxi_vma_close(struct vm_area_struct *vma)
{
    kfree(vma->vm_private_data);
    return;
}

// The VMA operations for the AXI DMA device
static const struct vm_operations_struct axitangxi_vm_ops = {
    .close = axitangxi_vma_close,
};

static int axitangxi_mmap(struct file *file, struct vm_area_struct *vma)
{

    int ret = 0;
    size_t size;
    size = vma->vm_end - vma->vm_start;

    void *kbuff = kzalloc(size, GFP_KERNEL);
    if (!kbuff) {
        ret = -ENOMEM;
        return ret;
    }

    size_t offset = vma->vm_pgoff << PAGE_SHIFT;
    size_t pfn_start = (virt_to_phys(kbuff) >> PAGE_SHIFT) + vma->vm_pgoff;
    size_t virt_start = (size_t)kbuff + offset;


    printk("phy: 0x%lx, offset: 0x%lx, size: 0x%lx\n", pfn_start << PAGE_SHIFT, offset, size);

    ret = remap_pfn_range(vma, vma->vm_start, pfn_start, size, vma->vm_page_prot);
    if (ret)
    printk("%s: remap_pfn_range failed at [0x%lx  0x%lx]\n",
        __func__, vma->vm_start, vma->vm_end);
    else
    printk("%s: map 0x%lx to 0x%lx, size: 0x%lx\n", __func__, virt_start,
        vma->vm_start, size);

    vma->vm_ops = &axitangxi_vm_ops;
    vma->vm_private_data = kbuff;
    return ret;
}

/* file_operations结构体声明, 是上面open、write实现函数与系统调用函数对应的关键 */
static const struct file_operations axitangxi_fops = {
    .owner = THIS_MODULE,
    .open = axitangxi_open,
    .release = axitangxi_release,
    .unlocked_ioctl = axitangxi_ioctl,
    .mmap = axitangxi_mmap,
};

// void inline reg_map (void){
//     LED_CTR_REG_MAP = ioremap(alinx_char.regdata[0], 4);
// 	LED_FRE_REG_MAP = ioremap(alinx_char.regdata[0]+4, 4);
// }

// void inline reg_unmap (void){
//     iounmap(LED_CTR_REG_MAP);
// 	iounmap(LED_FRE_REG_MAP);
// }

/* 模块加载时会调用的函数 */
static int __init axitangxi_init(void)
{
    /* 用于接受返回值 */
    u32 ret = 0;

/** 并发处理 **/
    /* 初始化自旋锁 */
//    spin_lock_init(&alinx_char.lock);

/** gpio框架 **/
    /* 获取设备节点 */
    axitangxi_dev.nd = of_find_node_by_path("/amba_pl@0/axi_tangxi@80000000");
    if(axitangxi_dev.nd == NULL)
    {
        printk("axitangxi_dev node not find\r\n");
        return -EINVAL;
    }
    else
    {
        printk("axitangxi_dev node find\r\n");
    }

    /*获取寄存器*/
    ret = of_property_read_u32_array(axitangxi_dev.nd, "reg", axitangxi_dev.regdata, 2);
    if(ret < 0){
		printk("reg read failed!\r\n");
	} else {
		u8 i = 0;
		printk("reg data:\r\n");
		for(i = 0; i < 2; i++)
		    printk("%#X ", axitangxi_dev.regdata[i]);
		printk("\r\n");
	}
    //reg_map();

    /** 中断 **/
    /* 获取中断号 */
    //alinx_char.irq = gpio_to_irq(alinx_char.alinx_key_gpio);
    axitangxi_dev.irq = irq_of_parse_and_map(axitangxi_dev.nd,0);

    /* 申请中断 */
    ret = request_irq(axitangxi_dev.irq,
                      key_handler,
                      IRQF_TRIGGER_RISING,
                      "axitangxi_irq",
                      NULL);
    if(ret < 0)
    {
        printk("irq %d request failed\r\n", axitangxi_dev.irq);
        return -EFAULT;
    }
    printk("irq %d request done\r\n", axitangxi_dev.irq);
/** 定时器 **/
    //timer_setup(&alinx_char.timer, timer_function, NULL);

/** 字符设备框架 **/
    /* 注册设备号 */
    alloc_chrdev_region(&axitangxi_dev.devid, MINOR_U, DEVID_COUNT, DEVICE_NAME);

    /* 初始化字符设备结构体 */
    cdev_init(&axitangxi_dev.cdev, &axitangxi_fops);

    /* 注册字符设备 */
    cdev_add(&axitangxi_dev.cdev, axitangxi_dev.devid, DRIVE_COUNT);

    /* 创建类 */
    axitangxi_dev.class = class_create(THIS_MODULE, DEVICE_NAME);
    if(IS_ERR(axitangxi_dev.class))
    {
        return PTR_ERR(axitangxi_dev.class);
    }

    /* 创建设备节点 */
    axitangxi_dev.device = device_create(axitangxi_dev.class, NULL,
                                      axitangxi_dev.devid, NULL,
                                      DEVICE_NAME);
    if (IS_ERR(axitangxi_dev.device))
    {
        return PTR_ERR(axitangxi_dev.device);
    }

    printk("axi-tangxi init done!\r\n");

    return 0;
}

/* 卸载模块 */
static void __exit axitangxi_exit(void)
{
        /** 中断 **/
    /* 释放中断 */
    free_irq(axitangxi_dev.irq, NULL);

    /** 定时器 **/
    /* 删除定时器 */
    //del_timer_sync(&alinx_char.timer);

    /** 字符设备框架 **/
    /* 注销字符设备 */
    cdev_del(&axitangxi_dev.cdev);

    /* 注销设备号 */
    unregister_chrdev_region(axitangxi_dev.devid, DEVID_COUNT);

    /* 删除设备节点 */
    device_destroy(axitangxi_dev.class, axitangxi_dev.devid);

    /* 删除类 */
    class_destroy(axitangxi_dev.class);

    // /* 在出口函数中调用 platform_driver_register, 卸载 platform 驱动 */
    // platform_driver_unregister(&axi_tangxi_driver);
}

/* 标记加载、卸载函数 */
module_init(axitangxi_init);
module_exit(axitangxi_exit);

/* 驱动描述信息 */
MODULE_AUTHOR("Wyn");
MODULE_ALIAS("axitangxi device");
MODULE_DESCRIPTION("AXI TANGXI driver");
MODULE_VERSION("v1.0");
MODULE_LICENSE("GPL");
