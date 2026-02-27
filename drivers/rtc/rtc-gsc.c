// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023-2025 Hewlett Packard Enterprise Development LP
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#define RTC_DRIVER_NAME            "rtc-gsc"
#define CMOS_READ(dev, reg)        ioread8((dev)->rtc_base + (reg))
#define CMOS_WRITE(dev, reg, val)  iowrite8((val), (dev)->rtc_base + (reg))

struct gsc_rtc {
    spinlock_t          lock;
    struct rtc_device   *rtc_dev;
    void __iomem        *rtc_base;
};


/************************************************
 * register summary
 ***********************************************/
#define RTC_SECONDS         0
#define RTC_SECONDS_ALARM   1
#define RTC_MINUTES         2
#define RTC_MINUTES_ALARM   3
#define RTC_HOURS           4
#define RTC_HOURS_ALARM     5
#define RTC_DAY_OF_WEEK     6
#define RTC_DAY_OF_MONTH    7
#define RTC_MONTH           8
#define RTC_YEAR            9
/* control registers - Moto names */
#define RTC_REG_A           10
#define RTC_REG_B           11
#define RTC_REG_C           12
#define RTC_REG_D           13

#define NVRAM_OFFSET        (RTC_REG_D + 1)
#define NVRAM_SIZE          50
/************************************************
 * register details
 ***********************************************/
#define RTC_FREQ_SELECT     RTC_REG_A
#define RTC_UIP             0x80
#define RTC_DIV_CTL         0x70
  /* divider control: refclock values 4.194 / 1.049 MHz / 32.768 kHz */
#define RTC_REF_CLCK_4MHZ   0x00
#define RTC_REF_CLCK_1MHZ   0x10
#define RTC_REF_CLCK_32KHZ  0x20
  /* 2 values for divider stage reset, others for "testing purposes only" */
#define RTC_DIV_RESET1      0x60
#define RTC_DIV_RESET2      0x70
 /* Periodic intr. / Square wave rate select. 0=none, 1=32.8kHz,... 15=2Hz */
#define RTC_RATE_SELECT     0x0F

#define RTC_CONTROL         RTC_REG_B
#define RTC_SET             0x80    /* disable updates for clock setting */
#define RTC_PIE             0x40    /* periodic interrupt enable */
#define RTC_AIE             0x20    /* alarm interrupt enable */
#define RTC_UIE             0x10    /* update-finished interrupt enable */
#define RTC_SQWE            0x08    /* enable square-wave output */
#define RTC_DM_BINARY       0x04    /* all time/date values are BCD if clear */
#define RTC_24H             0x02    /* 24 hour mode - else hours bit 7 means pm */
#define RTC_DST_EN          0x01    /* auto switch DST - works f. USA only */

#define RTC_INTR_FLAGS      RTC_REG_C
/* caution - cleared by read */
#define RTC_IRQF            0x80    /* any of the following 3 is active */
#define RTC_PF              0x40
#define RTC_AF              0x20
#define RTC_UF              0x10

#define RTC_VALID           RTC_REG_D
#define RTC_VRT             0x80    /* valid RAM and time */

//Returns true if a clock update is in progress
static inline unsigned char mc146818_is_updating(struct device *dev)
{
    unsigned char  uip;
    struct gsc_rtc *rtc = dev_get_drvdata(dev);

    uip = (CMOS_READ(rtc, RTC_FREQ_SELECT) & RTC_UIP);
    return uip;
}

static int gsc_rtc_read_time(struct device *dev, struct rtc_time *time)
{
    unsigned long  flags;
    struct gsc_rtc *rtc = dev_get_drvdata(dev);

    /* read RTC once any update in progress is done. The update
     * can take just over 2ms. We wait 20ms. There is no need to
     * to poll-wait (up to 1s - eeccch) for the falling edge of RTC_UIP. */
    if (mc146818_is_updating(dev))
        mdelay(20);

    spin_lock_irqsave(&rtc->lock, flags);
    time->tm_sec  = CMOS_READ(rtc, RTC_SECONDS);
    time->tm_min  = CMOS_READ(rtc, RTC_MINUTES);
    time->tm_hour = CMOS_READ(rtc, RTC_HOURS);
    time->tm_wday = CMOS_READ(rtc, RTC_DAY_OF_WEEK);
    time->tm_mday = CMOS_READ(rtc, RTC_DAY_OF_MONTH);
    time->tm_mon  = CMOS_READ(rtc, RTC_MONTH);
    time->tm_year = CMOS_READ(rtc, RTC_YEAR);
    spin_unlock_irqrestore(&rtc->lock, flags);

    /* Account for differences between how the RTC uses the values
     * and how they are defined in a struct rtc_time;  */
    if (time->tm_year <= 69)
        time->tm_year += 100;

    /* HW register start mon from one, but tm_mon start from zero. */
    time->tm_mon--;

    return 0;
}

static int gsc_rtc_set_time(struct device *dev, struct rtc_time *time)
{
    unsigned char  save_control, save_freq_select;
    unsigned long  flags;
    struct gsc_rtc *rtc = dev_get_drvdata(dev);

    if (time->tm_year > 255)  /* They are unsigned */
        return -EINVAL;

    /* The year register counts from the year 1900, assume we are in 1970...2069.
     * Also RTC sub-system validate function checks set tm_year < 70 then throwing invalid.
     */
    if (time->tm_year > 169)
        return -EINVAL;

    /* Year field maintains the last two digits of the calendar year in the RTC.
     * Valid programmed values are 00-99.
     */
    if (time->tm_year >= 100)
        time->tm_year -= 100;

    spin_lock_irqsave(&rtc->lock, flags);
    CMOS_WRITE(rtc, RTC_YEAR,         time->tm_year);
    CMOS_WRITE(rtc, RTC_MONTH,        time->tm_mon + 1);  /* tm_mon starts at zero */
    CMOS_WRITE(rtc, RTC_DAY_OF_MONTH, time->tm_mday);
    CMOS_WRITE(rtc, RTC_DAY_OF_WEEK,  time->tm_wday + 1); /* tm_wday starts at zero */
    CMOS_WRITE(rtc, RTC_HOURS,        time->tm_hour);
    CMOS_WRITE(rtc, RTC_MINUTES,      time->tm_min);
    CMOS_WRITE(rtc, RTC_SECONDS,      time->tm_sec);
    save_control = CMOS_READ(rtc, RTC_CONTROL);
    CMOS_WRITE(rtc, RTC_CONTROL,      (save_control|RTC_SET));
    save_freq_select = CMOS_READ(rtc, RTC_FREQ_SELECT);
    CMOS_WRITE(rtc, RTC_FREQ_SELECT,  (save_freq_select|RTC_DIV_RESET2));
    CMOS_WRITE(rtc, RTC_CONTROL,      save_control);
    CMOS_WRITE(rtc, RTC_FREQ_SELECT,  save_freq_select);
    spin_unlock_irqrestore(&rtc->lock, flags);

    return 0;
}

static const struct rtc_class_ops gsc_rtc_ops = {
    .read_time  = gsc_rtc_read_time,
    .set_time   = gsc_rtc_set_time,
};

static __init void gsc_rtc_of_init(struct platform_device *pdev, struct gsc_rtc *rtc)
{
    struct device_node *node = pdev->dev.of_node;
    const __be32 *val;

    if (!node)
        return;

    val = of_get_property(node, "freq-reg", NULL);
    if (val)
        CMOS_WRITE(rtc, RTC_FREQ_SELECT, be32_to_cpup(val));

    val = of_get_property(node, "ctrl-reg", NULL);
    if (val)
        CMOS_WRITE(rtc, RTC_CONTROL, be32_to_cpup(val));
}

static int gsc_cmos_nvmem_read(void *priv, unsigned int offset, void *val, size_t bytes)
{
    struct gsc_rtc *rtc = priv;
    unsigned char *buf = val;

    offset += NVRAM_OFFSET;
    for (; bytes; bytes--, offset++)
        *buf++ = CMOS_READ(rtc, offset);

    return 0;
}

static int gsc_cmos_nvmem_write(void *priv, unsigned int offset, void *val, size_t bytes)
{
    struct gsc_rtc *rtc = priv;
    unsigned char *buf = val;

    offset += NVRAM_OFFSET;
    for (; bytes; bytes--, offset++)
        CMOS_WRITE(rtc, offset, *buf++);

    return 0;
}

static int gsc_rtc_probe(struct platform_device *pdev)
{
    struct gsc_rtc *rtc;
    int ret = 0;

    struct nvmem_config nvmem_cfg = {
        .name = "gsc_cmos_nvram",
        .stride = 1,
        .size = NVRAM_SIZE,
        .word_size = 1,
        .reg_read = gsc_cmos_nvmem_read,
        .reg_write = gsc_cmos_nvmem_write,
        .type = NVMEM_TYPE_BATTERY_BACKED
    };

    rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
    if (!rtc) {
        dev_err(&pdev->dev, "failed to allocate memory\n");
        return -ENOMEM;
    }

    rtc->rtc_base = devm_platform_ioremap_resource(pdev, 0);
    if (IS_ERR(rtc->rtc_base)) {
        ret = PTR_ERR(rtc->rtc_base);
        dev_err(&pdev->dev, "failed to get memory resource\n");
        return -ENXIO;
    }

    rtc->rtc_dev = devm_rtc_allocate_device(&pdev->dev);
    if (IS_ERR(rtc->rtc_dev)) {
        ret = PTR_ERR(rtc->rtc_dev);
        dev_err(&pdev->dev, "failed to allocate RTC device\n");
        return ret;
    }

    gsc_rtc_of_init(pdev, rtc);
    platform_set_drvdata(pdev, rtc);

    rtc->rtc_dev->ops = &gsc_rtc_ops;
    rtc->rtc_dev->range_min = RTC_TIMESTAMP_BEGIN_2000;
    rtc->rtc_dev->range_max = RTC_TIMESTAMP_END_2099;

    ret = devm_rtc_register_device(rtc->rtc_dev);
    if (ret) {
        dev_err(&pdev->dev, "failed to register RTC device\n");
        return ret;
    }

    nvmem_cfg.priv = rtc;
    ret = devm_rtc_nvmem_register(rtc->rtc_dev, &nvmem_cfg);
    if (ret) {
        dev_err(&pdev->dev, "failed to register nvmem device for RTC\n");
        return ret;
    }

    spin_lock_init(&rtc->lock);

    return 0;
}

static const struct of_device_id gsc_rtc_table[] = {
    { .compatible = "hpe,gsc-rtc", },
    {}
};

MODULE_DEVICE_TABLE(of, gsc_rtc_table);

static struct platform_driver gsc_rtc_driver = {
    .probe  = gsc_rtc_probe,
    .driver = {
        .name = RTC_DRIVER_NAME,
        .of_match_table = gsc_rtc_table,
    },
};

module_platform_driver(gsc_rtc_driver);

MODULE_AUTHOR("Lakshman Garlapati <g-lakshmana-siva-kumar@hpe.com>");
MODULE_DESCRIPTION("Driver for HPE RTC device");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" RTC_DRIVER_NAME);
