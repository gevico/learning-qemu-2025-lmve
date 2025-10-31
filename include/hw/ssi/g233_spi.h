/*
 * SPI implementation for G233
*/

#ifndef __G233_SPI_H__
#define __G233_SPI_H__

#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"

/* 寄存器偏移 */
#define _SPI_CR1     (0x00)    /* Type: RW    Reset: 0x0000_0000 */
#define _SPI_CR2     (0x04)    /* Type: RW    Reset: 0x0000_0000 */
#define _SPI_SR      (0x08)    /* Type: RW    Reset: 0x0000_0002 */
#define _SPI_DR      (0x0C)    /* Type: RW    Reset: 0x0000_000C */
#define _SPI_CSCTRL  (0x10)    /* Type: RW    Reset: 0x0000_0000 */

#define TYPE_G233_SPI "g233-spi"
#define RISCV_G233_SPI(obj) \
    OBJECT_CHECK(G233SPIState, (obj), TYPE_G233_SPI)
OBJECT_DECLARE_SIMPLE_TYPE(G233SPIState, G233_SPI)

/* G233 SPI控制器状态结构体定义 */
typedef struct G233SPIState {

    /* 继承自SysBusDevice，表示这是一个系统总线设备 */
    SysBusDevice parent_obj;
    
    /* 内存映射I/O区域，用于SPI控制器寄存器访问 */
    MemoryRegion mmio;
    
    /* 连接的SSI总线，用于与其他SPI设备通信 */
    SSIBus *ssi;
    
    /* 片选信号线数组，包含两个片选输出和一个中断输出
     * cs_lines[0]: CS0片选信号
     * cs_lines[1]: CS1片选信号
     * cs_lines[2]: 中断信号输出 */
    qemu_irq cs_lines[3];

    /* SPI控制器寄存器 */
    uint32_t cr1;     /* 控制寄存器1 */
    uint32_t cr2;     /* 控制寄存器2 */
    uint32_t sr;      /* 状态寄存器 */
    uint32_t dr;      /* 数据寄存器 */
    uint32_t csctrl;  /* 片选控制寄存器 */

    /* 内部状态变量 */
    uint8_t  rx_data;     /* 最近接收到的字节数据 */
    bool spe;             /* SPI使能标志(Serial Peripheral Enable) */
    bool mstr;            /* 主机模式标志(Master) */
    bool cs0_en;          /* CS0片选使能标志 */
    bool cs0_act;         /* CS0片选激活标志 */
    bool cs1_en;          /* CS1片选使能标志 */
    bool cs1_act;         /* CS1片选激活标志 */
} G233SPIState;


/* 控制寄存器1 (CR1) 位定义 */
#define G233_SPI_CR1_SPE   (1u << 6)   /* SPI使能位 (Serial Peripheral Enable) */
#define G233_SPI_CR1_MSTR  (1u << 2)   /* 主机模式选择位 (Master Mode) */

/* 状态寄存器 (SR) 位定义 */
#define G233_SPI_SR_RXNE   (1u << 0)   /* 接收缓冲区非空 (Receive Buffer Not Empty) */
#define G233_SPI_SR_TXE    (1u << 1)   /* 发送缓冲区空 (Transmit Buffer Empty) */
#define G233_SPI_SR_UDR    (1u << 2)   /* 下溢错误标志 (Underrun Error) */
#define G233_SPI_SR_OVR    (1u << 3)   /* 溢出错误标志 (Overrun Error) */
#define G233_SPI_SR_BSY    (1u << 7)   /* 忙标志 (Busy) */

/* 片选控制寄存器 (CSCTRL) 位定义 */
#define G233_SPI_CS0_ENABLE  (1u << 0) /* CS0片选使能 */
#define G233_SPI_CS1_ENABLE  (1u << 1) /* CS1片选使能 */
#define G233_SPI_CS0_ACTIVE  (1u << 4) /* CS0片选激活 */
#define G233_SPI_CS1_ACTIVE  (1u << 5) /* CS1片选激活 */

#endif /* __G233_SPI_H__ */