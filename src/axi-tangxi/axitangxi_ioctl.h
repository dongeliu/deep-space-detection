#ifndef AXITANGXI_IOCTL_H_
#define AXITANGXI_IOCTL_H_

#include <asm/ioctl.h>              // IOCTL macros

/*----------------------------------------------------------------------------
 * IOCTL Argument Definitions
 *----------------------------------------------------------------------------*/
// The default timeout for DMA is 10 seconds
#define AXI_TANGXI_TIMEOUT      10000

struct axitangxi_transaction {
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


/*----------------------------------------------------------------------------
 * IOCTL Interface
 *----------------------------------------------------------------------------*/

// The magic number used to distinguish IOCTL's for our device
#define AXITANGXI_IOCTL_MAGIC              'Y'

// The number of IOCTL's implemented, used for verification
#define AXITANGXI_NUM_IOCTLS               10

#define AXITANGXI_PSDDR_PLDDR_LOOPBACK          _IOR(AXITANGXI_IOCTL_MAGIC, 0, \
                                                        struct axitangxi_transaction)

#define AXITANGXI_PSDDR_TANGXI_LOOPBACK         _IOR(AXITANGXI_IOCTL_MAGIC, 1, \
                                                        struct axitangxi_transaction)

#endif /* AXIDMA_IOCTL_H_ */
