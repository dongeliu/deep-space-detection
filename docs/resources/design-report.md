<!-- markdownlint-disable MD024 -->

# 图像压缩软件设计报告大纲

## 项目背景与现状

### 软件名称

探月某型号搭载物理科普系统的子模块：图像压缩模块

### 软件用途

图像压缩

- 算法方面采用iWave无损/近无损可伸缩方案
- 硬件方面采用塘西河一号芯片配合FPGA (有arm、GPU) 实现

## MP SoC FPGA 硬件 Verilog 及驱动程序设计报告

### 开发环境

#### PS 驱动开发环境

Xilinx Vitis 2020.1

#### PL Verilog 代码开发环境

仿真工具：Cadence Xcelium (Ver. 1909)

FPGA 相关工具：Xilinx Vivado 2020.1

#### 硬件调试环境

Xilinx MPSoC FPGA 芯片，型号：XCZU7EV-2FFVF1517I

### 运行环境

Xilinx MPSoC FPGA 芯片，型号：XCZU7EV-2FFVF1517I

### 功能需求

#### PS 端 ARM 处理器控制数据流传输

数据写入 TangXi 芯片：将 PS 端 DRAM 内存的数据，通过 AXI 总线突发传输到 PL
端，再通过 TangXi 芯片的片上网络接口、DDR 接口和 CSRAM 接口，将数据流写入
TangXi 芯片的片内存储。

获取来自 TangXi 芯片的数据：PL 端接收来自 TangXi
芯片片上网络接口的数据包数据，缓存在 PL 端 SRAM，再通过 AXI 总线突发传输到 PS
端 DRAM 内存。

每次数据流传输完成，向 PS 端 ARM 处理器发起中断。

#### PS 端 ARM 处理器访存 TangXi 芯片的片内存储

通过 TangXi 芯片 DDR 接口访存芯片部分存储空间；

通过 TangXi 芯片 CSRAM 接口访存芯片部分存储空间；

#### PS 端 ARM 处理器控制芯片

对 TangXi 芯片进行复位；

配置 TangXi 芯片锁相环；

通过 TangXi 芯片片上网络接口发送控制类型数据包；

获取 TangXi 芯片片上节点的状态：PL 端接收来自 TangXi 芯片片上网络接口的数据包数据，解包获取节点状态，并将其提供给 PS 端 ARM 处理器；

### 性能需求

MPSoC FPGA 芯片 arm 核部分，

MPSoC FPGA 芯片可编程逻辑部分，工作频率 100MHz~200MHz；

TangXi 芯片工作频率 200MHz。

### 在轨重构

不支持。

### 可靠性措施

软件开发者错误地使用驱动程序时，PL 端硬件电路检测之并且向 PS 端 ARM 处理器发起中断，使 ARM 处理器在中断处理函数中纠正错误。

开发自检程序，通过 PS 端 ARM 处理器检查硬件电路是否存在故障。

### 继承性分析

无

## TangXi 1 软件设计报告

### 开发环境

- 版本管理工具：Git version 2.30.1
- 仿真工具：Cadence Xcelium (Ver. 1909)

### 运行环境

TangXi 1 CGRA 阵列。

### 功能需求

实现图像压缩算法中熵模型网络核变换网络的计算。

### 性能需求

编码器的计算速度不低于每秒 10 万像素，单张
4K（3840x2160）大小图像推理时间不超过 80 s。

### 在轨重构

支持。

### 可靠性措施

- 开发自检程序，通过 PS 端 ARM 处理器检查存算芯片是否存在故障；如存在故障，考虑重置存算芯片。
- 考虑到太空特殊环境，在存算芯片上对关键部分，设计冗余计算模式，增加计算结果可靠性。

### 继承性分析

无。

## MP SoC FPGA arm 软件设计报告

### 软件开发环境

Xilinx Vitis 2020.1

### 运行环境

- OS：Petalinux (PS)
- Xilinx MPSoC FPGA 芯片 arm 核，型号：XCZU7EV-2FFVF1517I

### 功能需求

1. YUV 数据经过 16 bit 变换网络得到量化前变换系数 (PL)
2. 量化前变换系数 (16 bit) 经过量化后得到量化后变换系数 (8 bit) (PS)
3. 量化后变换系数 (8 bit) 经过熵模型网络得到熵参数 (8 bit) (PL)
4. 量化后变换系数 (8 bit) 和熵参数 (8 bit) 经过算术编码得到熵编码码流 (PS)

### 性能需求

调用存算芯片和可编程逻辑（PL），完成变换网络、熵模型网络的计算，实现编码器的计算速度不低于每秒 10 万像素，单张
4K（3840x2160）大小图像推理时间不超过 80 s。

### 在轨重构

支持。

### 可靠性措施

软件保证不崩溃的措施：

- 软件代码包含完整的单元测试，并保证较高的测试覆盖率以确保每个模块按照设计正常工作，减少会导致软件崩溃的逻辑 bug （例如数组越界/溢出）
- 考虑到太空的特殊环境（宇宙射线可能导致寄存器位翻转，造成数据大面积出错，导致程序跑飞），
  在嵌入式环境中引入看门狗定时器，每隔一段时间检测是否程序跑飞的现象，如果有就及时复位重新开始运行该程序，从而阻止因为特殊环境导致的程序 bug

### 继承性分析

无。