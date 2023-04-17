#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>             // Memset and memcpy functions
#include <fcntl.h>              // Flags for open()
#include <sys/stat.h>           // Open() system call
#include <sys/types.h>          // Types for open()
#include <sys/mman.h>           // Mmap system call
#include <sys/ioctl.h>          // IOCTL system call
#include <unistd.h>             // Close() system call
#include <errno.h>              // Error codes
#include <signal.h>             // Signal handling functions

#include "libaxitangxi.h"          // Local definitions
#include "axitangxi_ioctl.h"       // The IOCTL interface to AXI TANGXI

/**********************************Variable Definitions***************************************/

//static XScuGic INTC; // 创建XScuGic中断控制器实例
//XTime start_t, end_t;
//uint8_t ps_conf_done, pl_conf_done, ps_transfer_done, pl_transfer_done;

/**********************************Function Definitions***************************************/


//axitangxi结构，确认是否已经open
struct axitangxi_dev axitangxi_dev = {0};

/**
 * @brief 初始化AXI TANGXI设备
 *
 * @return struct axitangxi_dev*
 */
struct axitangxi_dev *axitangxi_dev_init()
{
    assert(!axitangxi_dev.initialized);

    // Open the AXI DMA device
    axitangxi_dev.fd = open("/dev/axi_tangxi", O_RDWR|O_EXCL);
    if (axitangxi_dev.fd < 0) {
        printf("Error opening AXI Tangxi device");
        fprintf(stderr, "Expected the AXI Tangxi device at the path `%s`\n",
                "/dev/axi_tangxi");
        return NULL;
    }

    // Return the AXI DMA device to the user
    axitangxi_dev.initialized = true;
    return &axitangxi_dev;
}

/**
 * @brief 关闭AXI TANGXI设备
 *
 * @param dev
 */
void axitangxi_destroy(axitangxi_dev_t dev)
{
    // Close the AXI TANGXI device
    if (close(dev->fd) < 0) {
        perror("Failed to close the AXI TANGXI device");
        assert(false);
    }

    // Free the device structure
    axitangxi_dev.initialized = false;
    return;
}

/**
 * @brief 初始化AXI TANGXI设备需要的参数
 *
 * @param dev
 * @param burst_size
 * @param tx_size
 * @param rx_size
 * @param tx_buf
 * @param rx_buf
 * @return int
 */
int axitangxi_data_init(axitangxi_dev_t dev, size_t burst_size, size_t tx_size, size_t rx_size, uint32_t *tx_buf, uint32_t *rx_buf)
{
	//int burst_count_temp = 0;
	// 禁用缓存，确保数据直接写入DDR
	//Xil_DCacheDisable();
	// ps_conf_done = 0;
	// pl_conf_done = 0;
	// ps_transfer_done = 0;
	// pl_transfer_done = 0;

	assert(burst_size <= 65536);
	//突发长度
	dev->burst_size = burst_size;
	//传输数据大小
	dev->tx_data_size = tx_size;
	//接收数据大小
	dev->rx_data_size = rx_size;

	//突发次数
	dev->burst_count = (dev->tx_data_size - 1) / (dev->burst_size * 16) + 1;
	//一次突发大小
	dev->burst_data = dev->burst_size * 16;

	//UINTPTR tx_data_ps_addr = (UINTPTR) tx_buf;
	//ps ddr传输首地址
	dev->tx_data_ps_ptr = tx_buf;

	//接收首地址默认加0x1000000(16MB数据空间)
	//UINTPTR rx_data_ps_addr = ADDR_ALIGNED_4096(tx_data_ps_addr + dev->tx_data_size + 0x1000000);
	//ps ddr接收首地址
	dev->rx_data_ps_ptr = rx_buf;

	//pl ddr传输/接收首地址(自定义，默认与ps ddr传输首地址相同)
	dev->tx_data_pl_ptr = dev->rx_data_pl_ptr = dev->tx_data_ps_ptr;

    // if((uint64_t) tx_data_ps_addr > 0xffffffff || (uint64_t) rx_data_ps_addr > 0xffffffff){
	// 	printf("Error: Failed to initialize ps addr, tx_data_addr: %ld; rx_data_addr: %ld\n", (long)(tx_data_ps_addr), (long)(rx_data_ps_addr));
	// 	return -1;
    // }
    printf("tx_data_ps_addr: 0x%x \nrx_data_ps_addr: 0x%x\ntx_data_pl_addr: 0x%x \nrx_data_pl_addr: 0x%x\n",
    		dev->tx_data_ps_ptr, dev->rx_data_ps_ptr, dev->tx_data_pl_ptr, dev->rx_data_pl_ptr);

    return 0;
}

/**
 * @brief 分配适合与AXI TANGXI驱动程序一起使用的内存区域。
 *        注意，这是一个相当昂贵的操作，应该在初始化时完成。
 *
 * @param dev
 * @param size
 * @return void*
 */
void *axitangxi_malloc(axitangxi_dev_t dev, size_t size)
{
    void *addr;

    //将文件映射到内存区域
    addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, dev->fd, 0);
    if (addr == MAP_FAILED) {
        return NULL;
    }

    return addr;
}

/**
 * @brief 释放通过调用axitangxi_malloc分配的内存区域。
 *        这里传入的大小必须与调用时使用的大小匹配，否则该函数将抛出异常。
 *
 * @param dev
 * @param addr
 * @param size
 */
void axitangxi_free(axitangxi_dev_t dev, void *addr, size_t size)
{
    // Silence the compiler
    (void)dev;

    if (munmap(addr, size) < 0) {
        perror("Failed to free the AXI TANGXI memory mapped region");
        assert(false);
    }

    return;
}

/**
 * @brief PS DDR <--->  PL DDR 回环
 *
 * @param dev
 * @return int
 */
int axitangxi_psddr_plddr_loopback(axitangxi_dev_t dev)
{
    int rc;
    struct axitangxi_transaction *trans;

    trans->tx_data_size = dev->tx_data_size;
	trans->rx_data_size = dev->rx_data_size;
	trans->burst_size = dev->burst_size;
	trans->burst_count = dev->burst_count;
	trans->burst_data = dev->burst_data;
	trans->tx_data_ps_ptr = dev->tx_data_ps_ptr;
	trans->rx_data_ps_ptr = dev->rx_data_ps_ptr;
	trans->tx_data_pl_ptr = dev->tx_data_pl_ptr;
	trans->rx_data_pl_ptr = dev->rx_data_pl_ptr;
    trans->node = dev->node;

    // 调用内核函数
    rc = ioctl(dev->fd, AXITANGXI_PSDDR_PLDDR_LOOPBACK, &trans);
    if (rc < 0) {
        perror("Failed to perform the AXITANGXI_PSDDR_PLDDR_LOOPBACK transfer");
    }

    return rc;
}
