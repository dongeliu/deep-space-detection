/**
 * @file axitangxi_test.c
 * @date 2023/09/25
 * @author Wang Yang
 *
 * axi-tangxi应用测试文件。
 *
 * @bug
 **/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // Strlen function

#include <errno.h>     // Error codes
#include <fcntl.h>     // Flags for open()
#include <getopt.h>    // Option parsing
#include <sys/stat.h>  // Open() system call
#include <sys/types.h> // Types for open()
#include <unistd.h>    // Close() system call

#include "libaxitangxi.h" // Interface to the AXI DMA

/**
 * @brief 校验数据
 *
 * @param dev
 */
static void verify_data(axitangxi_dev_t dev) {
  int len = dev->tx_data_size / sizeof(int);
  int count = 0;
  for (int i = 0; i < len; i++) {
    if (dev->rx_data_ps_ptr[i] != dev->tx_data_ps_ptr[i]) {
      count++;
      printf("different data! : tx address 0x%x: %u; rx address 0x%x: %u\n",
             dev->tx_data_ps_ptr + i, dev->tx_data_ps_ptr[i],
             dev->rx_data_ps_ptr + i, dev->rx_data_ps_ptr[i]);
    }
  }
  printf("all different data count %u\n", count);
}

/*----------------------------------------------------------------------------
 * Main Function
 *----------------------------------------------------------------------------*/

int main(int argc, char **argv) {
  int rc;

  /* 1. 初始化axitangxi设备 */
  axitangxi_dev_t axitangxi_dev;
  axitangxi_dev = axitangxi_dev_init();
  if (axitangxi_dev == NULL) {
    fprintf(stderr, "Failed to initialize the AXI DMA device.\n");
    rc = 1;
    goto ret;
  }

  /* 2. 分配内存 */
  // 传输数据大小
  size_t tx_size = 1024 * 1024;
  // 接收数据大小
  size_t rx_size = 1024 * 1024;

  // 传输空间
  uint32_t *tx_buffer = axitangxi_malloc(axitangxi_dev, tx_size);
  if (tx_buffer == NULL) {
    printf("Failed to allocate tx_buffer memory\n");
    goto free_tx_buf;
  }

  // 接收空间
  uint32_t *rx_buffer = axitangxi_malloc(axitangxi_dev, rx_size);
  if (rx_buffer == NULL) {
    printf("Failed to allocate rx_buffer memory\n");
    goto free_rx_buf;
  }

  // 将分配传输内存清0
  memset(tx_buffer, 0, tx_size);

  for (int i = 1; i <= (tx_size / sizeof(int)); i++) {
    tx_buffer[i - 1] = i;
  }

  // 打印前10个整数
  for (int i = 0; i < 10; i++) {
    printf("tx_buffer[%d]: %d\n", i, tx_buffer[i]);
  }

  /* 3. 初始化axitangxi数据 */
  rc = axitangxi_data_init(axitangxi_dev, tx_size, rx_size, tx_buffer,
                           rx_buffer);
  if (rc != 0) {
    printf("Failed to axitangxi_data_init\n");
    goto ret;
  }

  /* 4. 数据传输 */
  // PS DDR <--->  PL DDR 回环
  rc = axitangxi_psddr_plddr_loopback(axitangxi_dev);

  /* 5. 校验数据 */
  verify_data(axitangxi_dev);

  rc = acc_config_test(axitangxi_dev);

  rc = acc_start_test(axitangxi_dev);

  rc = acc_get_test(axitangxi_dev);

free_tx_buf:
  axitangxi_free(axitangxi_dev, tx_buffer, tx_size);
free_rx_buf:
  axitangxi_free(axitangxi_dev, rx_buffer, rx_size);
destroy_axitangxi:
  axitangxi_destroy(axitangxi_dev);
ret:
  return rc;
}
