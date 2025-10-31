/*
 * SPI implementation for G233
*/

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/ssi/ssi.h"
#include "hw/ssi/g233_spi.h"
#include "migration/vmstate.h"

/**
 * 更新片选信号状态
 * 根据片选使能和激活状态设置GPIO输出电平
 * @param s G233 SPI控制器状态结构体指针
 */
static void g233_spi_update_cs(G233SPIState *s)
{
    /* 设置CS0信号: 当CS0使能且激活时输出低电平(0)，否则输出高电平(1) */
    qemu_set_irq(s->cs_lines[0], (s->cs0_en && s->cs0_act) ? 0 : 1);
    /* 设置CS1信号: 当CS1使能且激活时输出低电平(0)，否则输出高电平(1) */
    qemu_set_irq(s->cs_lines[1], (s->cs1_en && s->cs1_act) ? 0 : 1);
}

/**
 * 更新中断状态
 * 根据控制寄存器和状态寄存器的设置决定是否触发中断
 * @param s G233 SPI控制器状态结构体指针
 */
static void g233_spi_update_irq(G233SPIState *s)
{
    bool irq = false;
    /* TXE中断: 当TXE中断使能(CR2[7]=1)且发送缓冲区空(SR[TXE]=1)时触发 */
    if ((s->cr2 & (1u << 7)) && (s->sr & G233_SPI_SR_TXE)) {
        irq = true;
    }
    /* RXNE中断: 当RXNE中断使能(CR2[6]=1)且接收缓冲区非空(SR[RXNE]=1)时触发 */
    if ((s->cr2 & (1u << 6)) && (s->sr & G233_SPI_SR_RXNE)) {
        irq = true;
    }
    /* 错误中断: 当错误中断使能(CR2[5]=1)且有错误标志(SR[UDR]或SR[OVR])置位时触发 */
    if ((s->cr2 & (1u << 5)) && (s->sr & (G233_SPI_SR_UDR | G233_SPI_SR_OVR))) {
        irq = true;
    }
    /* 设置中断信号输出 */
    qemu_set_irq(s->cs_lines[2], irq);
}

/**
 * 重置G233 SPI控制器
 * 将所有寄存器和内部状态恢复到初始值
 * @param dev 设备状态结构体指针
 */
static void g233_spi_reset(DeviceState *dev)
{
    G233SPIState *s = G233_SPI(dev);

    /* 复位寄存器值 */
    s->cr1 = 0;                           /* 控制寄存器1清零 */
    s->cr2 = 0;                           /* 控制寄存器2清零 */
    s->sr  = G233_SPI_SR_TXE;             /* 状态寄存器设置为初始状态: TXE=1, RXNE=0, BSY=0 */

    /* 复位内部状态 */
    s->rx_data = 0;                       /* 清空接收数据 */
    s->spe = false;                       /* 禁用SPI */
    s->mstr = false;                      /* 设置为从机模式 */
    s->cs0_en = false;                    /* 禁用CS0 */
    s->cs0_act = false;                   /* 取消激活CS0 */
    s->cs1_en = false;                    /* 禁用CS1 */
    s->cs1_act = false;                   /* 取消激活CS1 */
    
    /* 更新片选和中断状态 */
    g233_spi_update_cs(s);
    g233_spi_update_irq(s);
}

/**
 * 执行SPI传输操作
 * 完成一次8位数据的SPI传输过程
 * @param s G233 SPI控制器状态结构体指针
 * @param tx 要发送的数据字节
 */
static void g233_spi_do_transfer(G233SPIState *s, uint8_t tx)
{
    /* 开始传输: 设置忙标志(BSY=1)，清除发送空标志(TXE=0) */
    s->sr &= ~G233_SPI_SR_TXE;
    s->sr |= G233_SPI_SR_BSY;

    /* 在SSI总线上执行一次8位数据传输 */
    uint32_t rx = ssi_transfer(s->ssi, tx);
    
    /* 检查是否有溢出错误: 如果之前接收的数据还未读取，则设置溢出标志 */
    if (s->sr & G233_SPI_SR_RXNE) {
        s->sr |= G233_SPI_SR_OVR;
    }
    /* 保存接收到的数据 */
    s->rx_data = (uint8_t)rx;

    /* 传输完成: 设置接收非空标志(RXNE=1)，设置发送空标志(TXE=1)，清除忙标志(BSY=0) */
    s->sr |= G233_SPI_SR_RXNE;
    s->sr |= G233_SPI_SR_TXE;
    s->sr &= ~G233_SPI_SR_BSY;

    /* 更新中断状态 */
    g233_spi_update_irq(s);
}

/**
 * 处理寄存器读取操作
 * 根据地址返回对应寄存器的值
 * @param opaque 设备状态结构体指针
 * @param addr 寄存器偏移地址
 * @param size 访问大小(字节数)
 * @return 对应寄存器的值
 */
static uint64_t g233_spi_read(void *opaque, hwaddr addr, unsigned size)
{
    G233SPIState *s = opaque;
    switch (addr) {
        case _SPI_CR1:
            return s->cr1;                /* 返回控制寄存器1的值 */
        case _SPI_CR2:
            return s->cr2;                /* 返回控制寄存器2的值 */
        case _SPI_SR:
            return s->sr;                 /* 返回状态寄存器的值 */
        case _SPI_DR: {
            /* 读取数据寄存器: 返回最后接收的字节数据 */
            uint32_t val = s->rx_data;
            /* 读取DR会清除接收非空标志(RXNE) */
            s->sr &= ~G233_SPI_SR_RXNE;
            /* 同时清除溢出错误标志(OVR)，然后重新评估中断 */
            s->sr &= ~G233_SPI_SR_OVR;
            g233_spi_update_irq(s);
            return val;
        }
        case _SPI_CSCTRL:
            return s->csctrl;             /* 返回片选控制寄存器的值 */
        default:
            /* 记录错误日志: 无效的寄存器读取地址 */
            qemu_log_mask(LOG_GUEST_ERROR,
                      "g233-spi: bad read offset 0x%" HWADDR_PRIx "\n", addr);
            return 0;
    }
}

/**
 * 处理寄存器写入操作
 * 根据地址更新对应寄存器的值
 * @param opaque 设备状态结构体指针
 * @param addr 寄存器偏移地址
 * @param val64 要写入的值
 * @param size 访问大小(字节数)
 */
static void g233_spi_write(void *opaque, hwaddr addr, uint64_t val64, unsigned size)
{
    G233SPIState *s = opaque;
    uint32_t val = val64;

    switch (addr) {
        case _SPI_CR1:
            s->cr1 = val;
            /* 更新SPI使能和主机模式标志 */
            s->spe  = (val & G233_SPI_CR1_SPE)  != 0;
            s->mstr = (val & G233_SPI_CR1_MSTR) != 0;
            return;
        case _SPI_CR2:
            /* 中断使能位在此寄存器中(TXEIE/RXNEIE/ERRIE) */
            s->cr2 = val;
            /* 更新中断状态 */
            g233_spi_update_irq(s);
            return;
        case _SPI_SR:
            /* 状态寄存器对此简化模型来说是只读的 */
            uint32_t tmp = val & (G233_SPI_SR_OVR | G233_SPI_SR_UDR);
            s->sr &= ~tmp;
            /* 更新中断状态 */
            g233_spi_update_irq(s);
            return;
        case _SPI_DR: {
            s->dr = val & 0xFF;
            /* 只有在SPI使能、主机模式且片选激活时才进行传输 */
            bool cs0_active = (s->cs0_en && s->cs0_act);
            bool cs1_active = (s->cs1_en && s->cs1_act);
            if (s->spe && s->mstr && (cs0_active ^ cs1_active)){
                /* 执行SPI传输 */
                g233_spi_do_transfer(s, (uint8_t)s->dr);
            }else {
                /* 如果未激活，则保持TXE=1，RXNE不变；BSY保持0 */
            }
            return;
        }
        case _SPI_CSCTRL:
            s->csctrl = val;
            /* 更新片选使能和激活状态 */
            s->cs0_en  = (val & G233_SPI_CS0_ENABLE) != 0;
            s->cs1_en  = (val & G233_SPI_CS1_ENABLE) != 0;
            s->cs0_act = (val & G233_SPI_CS0_ACTIVE) != 0;
            s->cs1_act = (val & G233_SPI_CS1_ACTIVE) != 0;
            /* 更新片选信号 */
            g233_spi_update_cs(s);
            return;
        default:
            /* 记录错误日志: 无效的寄存器写入地址 */
            qemu_log_mask(LOG_GUEST_ERROR,
                        "g233-spi: bad write offset 0x%" HWADDR_PRIx " val=0x%x\n",
                        addr, val);
            return;
    }    
}

/* 定义内存区域操作结构体，用于处理寄存器的读写操作 */
static const MemoryRegionOps g233_spi_ops = {
    .read = g233_spi_read,                /* 读操作处理函数 */
    .write = g233_spi_write,              /* 写操作处理函数 */
    .endianness = DEVICE_NATIVE_ENDIAN,   /* 使用本地字节序 */
    .valid = { .min_access_size = 4, .max_access_size = 4 }, /* 限定访问大小为4字节 */
};

/* 定义VM状态描述结构体，用于虚拟机迁移时保存和恢复设备状态 */
static const VMStateDescription vmstate_g233_spi = {
    .name = TYPE_G233_SPI,                /* 设备类型名称 */
    .version_id = 1,                      /* 版本ID */
    .minimum_version_id = 1,              /* 最小兼容版本ID */
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cr1, G233SPIState),     /* 保存/恢复控制寄存器1 */
        VMSTATE_UINT32(cr2, G233SPIState),     /* 保存/恢复控制寄存器2 */
        VMSTATE_UINT32(sr,  G233SPIState),     /* 保存/恢复状态寄存器 */
        VMSTATE_UINT32(dr,  G233SPIState),     /* 保存/恢复数据寄存器 */
        VMSTATE_UINT32(csctrl, G233SPIState),  /* 保存/恢复片选控制寄存器 */
        VMSTATE_UINT8(rx_data, G233SPIState),  /* 保存/恢复接收数据 */
        VMSTATE_BOOL(spe, G233SPIState),       /* 保存/恢复SPI使能标志 */
        VMSTATE_BOOL(mstr, G233SPIState),      /* 保存/恢复主机模式标志 */
        VMSTATE_BOOL(cs0_en, G233SPIState),    /* 保存/恢复CS0使能标志 */
        VMSTATE_BOOL(cs0_act, G233SPIState),   /* 保存/恢复CS0激活标志 */
        VMSTATE_BOOL(cs1_en, G233SPIState),    /* 保存/恢复CS1使能标志 */
        VMSTATE_BOOL(cs1_act, G233SPIState),   /* 保存/恢复CS1激活标志 */
        VMSTATE_END_OF_LIST()                  /* 结束标记 */
    }
};

/**
 * 实现G233 SPI控制器的初始化
 * 初始化内存映射I/O区域、GPIO输出和SSI总线
 * @param dev 设备状态结构体指针
 * @param errp 错误信息指针
 */
static void g233_spi_realize(DeviceState *dev, Error **errp)
{
    G233SPIState *s = G233_SPI(dev);
    
    /* 初始化内存映射I/O区域 */
    memory_region_init_io(&s->mmio, OBJECT(dev), &g233_spi_ops, s, 
                        TYPE_G233_SPI, 0X1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);

    /* 初始化GPIO输出:
     * 0: CS0信号 (低电平有效)
     * 1: CS1信号 (低电平有效)
     * 2: IRQ中断信号线到PLIC
     */
    qdev_init_gpio_out(DEVICE(dev), s->cs_lines, 3);

    /* 创建SSI总线用于连接SPI设备 */
    s->ssi = ssi_create_bus(DEVICE(dev), "ssi");
}

/**
 * G233 SPI设备类初始化函数
 * 设置设备的操作函数和特性
 * @param klass 对象类指针
 * @param data 用户数据指针
 */
static void g233_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, g233_spi_reset);  /* 设置复位函数 */
    dc->realize = &g233_spi_realize;                    /* 设置初始化函数 */
    dc->vmsd = &vmstate_g233_spi;                       /* 设置VM状态描述符 */
}

/* 定义G233 SPI设备类型信息 */
static const TypeInfo g233_spi_info = {
    .name          = TYPE_G233_SPI,                     /* 类型名称 */
    .parent        = TYPE_SYS_BUS_DEVICE,               /* 父类型为系统总线设备 */
    .instance_size = sizeof(G233SPIState),              /* 实例大小 */
    .class_init    = g233_spi_class_init,               /* 类初始化函数 */
};

/**
 * 注册G233 SPI设备类型
 * 在QEMU启动时注册该设备类型
 */
static void g233_spi_register_types(void)
{
    type_register_static(&g233_spi_info);
}

/* 注册设备类型初始化函数 */
type_init(g233_spi_register_types)