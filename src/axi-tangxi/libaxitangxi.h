#ifndef LIBAXITANGXI_H
#define LIBAXITANGXI_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "axitangxi_ioctl.h"

struct axitangxi_dev {
    bool initialized;           //指示该结构体的初始化
    int fd;                     //设备的文件描述符

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

typedef struct axitangxi_dev* axitangxi_dev_t;

/**
 * @brief
 *
 * @return struct axitangxi_dev*
 */
struct axitangxi_dev *axitangxi_dev_init();

/**
 * @brief
 *
 * @param dev
 */
void axitangxi_destroy(axitangxi_dev_t dev);

/**
 * @brief
 *
 * @param dev
 * @param burst_size
 * @param tx_size
 * @param rx_size
 * @param tx_buf
 * @param rx_buf
 * @return int
 */
int axitangxi_data_init(axitangxi_dev_t dev, size_t burst_size, size_t tx_size, size_t rx_size, uint32_t *tx_buf, uint32_t *rx_buf);

/**
 * @brief
 *
 * @param dev
 * @param size
 * @return void*
 */
void *axitangxi_malloc(axitangxi_dev_t dev, size_t size);

/**
 * @brief
 *
 * @param dev
 * @param addr
 * @param size
 */
void axitangxi_free(axitangxi_dev_t dev, void *addr, size_t size);

/**
 * @brief
 *
 * @param dev
 * @return int
 */
int axitangxi_psddr_plddr_loopback(axitangxi_dev_t dev);

#endif
