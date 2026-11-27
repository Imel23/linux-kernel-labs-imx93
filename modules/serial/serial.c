// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>

/* LPUART register offsets and bitfields */
/* Version ID Register */
#define LPUART_VERID 0x00
/* Global Register */
#define LPUART_GLOBAL 0x08
#define LPUART_GLOBAL_RST BIT(1) /* Software Reset */
/* Baud Rate Register */
#define LPUART_BAUD 0x10
#define LPUART_BAUD_SBR_MASK 0x1FFF  /* Baud Rate Modulo Divisor */
#define LPUART_BAUD_BOTHEDGE BIT(17) /* Sample on both edges when receiving */
#define LPUART_BAUD_TDMAE BIT(23)    /* Transmit DMA Enable */
/* Status Register */
#define LPUART_STAT 0x14
#define LPUART_STAT_TDRE BIT(23) /* Transmit Data Register Empty */
/* Control Register */
#define LPUART_CTRL 0x18
#define LPUART_CTRL_TE BIT(19)  /* Transmitter Enable */
#define LPUART_CTRL_RE BIT(18)  /* Receiver Enable */
#define LPUART_CTRL_RIE BIT(21) /* Receiver Interrupt Enable */
/* Data Register */
#define LPUART_DATA 0x1C
/* FIFO Register */
#define LPUART_FIFO 0x28
#define LPUART_FIFO_TXFE BIT(7)     /* Transmit FIFO Enable */
#define LPUART_FIFO_RXFE BIT(3)     /* Receive FIFO Enable */
#define LPUART_FIFO_TXFLUSH BIT(15) /* Transmit FIFO Flush */
#define LPUART_FIFO_RXFLUSH BIT(14) /* Receive FIFO Flush */

/* Minimum duration for the global reset (microseconds) */
#define GLOBAL_RST_MIN_US 20

#define MY_MAGIC 'S'
#define SERIAL_RESET_COUNTER _IOW(MY_MAGIC, 0, int)
#define SERIAL_GET_COUNTER _IOR(MY_MAGIC, 1, int)

struct serial_dev
{
        void __iomem *regs;
        struct clk *clk;
        struct miscdevice serial_miscdev;
        int counter;
};

/*
 * Compute the LPUART baud rate register value (OSR + SBR) for a given clock
 * and baud rate, picking the closest match (error < 3%).
 *
 * Example: clk = 80 MHz, baud = 115200 -> OSR=16, SBR=43
 */
static int lpuart_build_baud_val(unsigned long clk_hz, unsigned int baud,
                                 u32 *baud_val_out)
{
        unsigned int best_osr = 0;
        unsigned int best_sbr = 0;
        unsigned long best_err = ~0UL;
        unsigned int osr;
        unsigned int sbr;
        unsigned long actual;
        unsigned long err;

        if (!baud_val_out || !baud || !clk_hz)
                return -EINVAL;

        for (osr = 4; osr <= 32; osr++)
        {
                sbr = DIV_ROUND_CLOSEST(clk_hz, baud * osr);

                if (!sbr || sbr > LPUART_BAUD_SBR_MASK)
                        continue;

                actual = clk_hz / (sbr * osr);
                err = abs((long)actual - (long)baud);

                if (err < best_err)
                {
                        best_err = err;
                        best_osr = osr;
                        best_sbr = sbr;
                        if (!err)
                                break;
                }
        }

        if (!best_osr || best_err * 100 > baud * 3)
                return -EINVAL;

        *baud_val_out = ((best_osr - 1) << 24) | (best_sbr & LPUART_BAUD_SBR_MASK);

        return 0;
}

static int serial_init_controller(struct serial_dev *serial)
{
        unsigned long clk_freq;
        u32 baud_val;
        int ret;

        /* Retrieve the actual clock frequency for baud rate calculations */
        clk_freq = clk_get_rate(serial->clk);

        /* Reset the controller, enable and flush the FIFOs, compute the prescaler */
        writel(LPUART_GLOBAL_RST, serial->regs + LPUART_GLOBAL);
        fsleep(GLOBAL_RST_MIN_US);
        writel(0x00000000, serial->regs + LPUART_GLOBAL);
        writel(0x0, serial->regs + LPUART_CTRL);
        writel(LPUART_FIFO_TXFLUSH | LPUART_FIFO_RXFLUSH, serial->regs + LPUART_FIFO);
        writel(LPUART_FIFO_TXFE | LPUART_FIFO_RXFE, serial->regs + LPUART_FIFO);

        /* Set the baudrate and enable the transmitter */
        ret = lpuart_build_baud_val(clk_freq, 115200, &baud_val);
        if (ret)
                return ret;

        writel(baud_val | LPUART_BAUD_BOTHEDGE, serial->regs + LPUART_BAUD);
        writel(LPUART_CTRL_TE, serial->regs + LPUART_CTRL);

        return 0;
}

static int serial_write_character(struct serial_dev *serial, char c)
{

        /* Wait for the transmit data register to be empty */
        while (!(readl(serial->regs + LPUART_STAT) & LPUART_STAT_TDRE))
                cpu_relax();

        /* Write the character to the data register */
        writel(c, serial->regs + LPUART_DATA);
        return 0;
}

static ssize_t serial_write(struct file *filp, const char __user *buf,
                            size_t count, loff_t *ppos)
{
        struct miscdevice *miscdev_ptr = filp->private_data;
        struct serial_dev *serial = container_of(miscdev_ptr, struct serial_dev, serial_miscdev);
        size_t i;
        char *kbuf = kmalloc(count, GFP_KERNEL);
        if (!kbuf)
                return -ENOMEM;
        i = copy_from_user(kbuf, buf, count);
        if (i)
        {
                kfree(kbuf);
                return -EFAULT;
        }
        for (size_t j = 0; j < count; j++)
        {
                if (kbuf[j] == '\n')
                {
                        serial_write_character(serial, '\n');
                        serial_write_character(serial, '\r');
                }
                else
                {
                        serial_write_character(serial, kbuf[j]);
                }
                serial->counter++;
        }
        kfree(kbuf);
        return count - i;
}
static ssize_t serial_read(struct file *filp, char __user *buf,
                           size_t count, loff_t *ppos)
{
        return -EINVAL;
}

static long serial_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
        struct miscdevice *miscdev_ptr = filp->private_data;
        struct serial_dev *serial = container_of(miscdev_ptr, struct serial_dev, serial_miscdev);
        switch (cmd)
        {
        case SERIAL_RESET_COUNTER:
                pr_info("Received ioctl command 0\n");
                serial->counter = 0;
                return 0;
        case SERIAL_GET_COUNTER:
                pr_info("Received ioctl command 1\n");
                if (copy_to_user((int __user *)arg, &serial->counter, sizeof(serial->counter)))
                        return -EFAULT;
                return 0;
        default:
                return -ENOTTY;
        }
        return -EINVAL;
}

static struct file_operations serial_fops = {
    .owner = THIS_MODULE,
    .write = serial_write,
    .read = serial_read,
    .unlocked_ioctl = serial_ioctl,
};

static int serial_probe(struct platform_device *pdev)
{
        pr_info("Called %s\n", __func__);
        struct serial_dev *serial;
        serial = devm_kzalloc(&pdev->dev, sizeof(*serial), GFP_KERNEL);
        if (!serial)
                return -ENOMEM;
        platform_set_drvdata(pdev, serial);
        serial->regs = devm_platform_ioremap_resource(pdev, 0);
        if (IS_ERR(serial->regs))
                return PTR_ERR(serial->regs);

        serial->clk = devm_clk_get_enabled(&pdev->dev, "ipg");
        if (IS_ERR(serial->clk))
        {
                dev_err(&pdev->dev, "Failed to get/enable IPG clock\n");
                return PTR_ERR(serial->clk);
        }
        u32 verid = readl(serial->regs);
        pr_info("Read version ID: 0x%x\n", verid);
        serial_init_controller(serial);

        serial->serial_miscdev.minor = MISC_DYNAMIC_MINOR;
        struct resource *res;
        res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        serial->serial_miscdev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "serial-%pa", &res->start);
        serial->serial_miscdev.fops = &serial_fops;
        serial->serial_miscdev.parent = &pdev->dev;
        int ret = misc_register(&serial->serial_miscdev);
        if (ret)
        {
                dev_err(&pdev->dev, "Failed to register misc device\n");
                return ret;
        }
        return 0;
}

static void serial_remove(struct platform_device *pdev)
{
        pr_info("Called %s\n", __func__);
        struct serial_dev *serial = platform_get_drvdata(pdev);
        misc_deregister(&serial->serial_miscdev);
}

static const struct of_device_id serial_match[] = {
    {
        .compatible = "bootlin,serial",
    },
    {},
};

static struct platform_driver serial_driver = {
    .driver = {
        .name = "serial",
        .owner = THIS_MODULE,
        .of_match_table = serial_match,
    },
    .probe = serial_probe,
    .remove = serial_remove,
};
module_platform_driver(serial_driver);

MODULE_LICENSE("GPL");
