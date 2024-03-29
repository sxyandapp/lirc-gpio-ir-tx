/*
 * Copyright (C) 2017 Sean Young <sean@mess.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <media/rc-core.h>

#define DRIVER_NAME	"gpio-ir-tx"
#define DEVICE_NAME	"GPIO IR Bit Banging Transmitter"

#define DEBUG   1
#if DEBUG
	#define dprintk(fmt, args...)					\
		do {							\
				printk(KERN_INFO DRIVER_NAME ": "	\
					   fmt, ## args);			\
		} while (0)
#else
	#define dprintk(fmt, args...)
#endif
		
struct gpio_ir {
	struct gpio_desc *gpio;
	unsigned int carrier;
	unsigned int duty_cycle;
	/* we need a spinlock to hold the cpu while transmitting */
	spinlock_t lock;
};

static const struct of_device_id gpio_ir_tx_of_match[] = {
	{ .compatible = "gpio-ir-tx", },
	{ },
};

MODULE_DEVICE_TABLE(of, gpio_ir_tx_of_match);

static int gpio_ir_tx_set_duty_cycle(struct rc_dev *dev, u32 duty_cycle)
{
	struct gpio_ir *gpio_ir = dev->priv;

	gpio_ir->duty_cycle = duty_cycle;

	return 0;
}

static int gpio_ir_tx_set_carrier(struct rc_dev *dev, u32 carrier)
{
	struct gpio_ir *gpio_ir = dev->priv;

	//if (!carrier)
	//	return -EINVAL;

	gpio_ir->carrier = carrier;

	return 0;
}

static int gpio_ir_tx(struct rc_dev *dev, unsigned int *txbuf,
		      unsigned int count)
{
	struct gpio_ir *gpio_ir = dev->priv;
	unsigned long flags;
	ktime_t edge;
	/*
	 * delta should never exceed 0.5 seconds (IR_MAX_DURATION) and on
	 * m68k ndelay(s64) does not compile; so use s32 rather than s64.
	 */
	s32 delta;
	int i;
	unsigned int pulse, space;
	bool useCarrier = (0 < gpio_ir->carrier);
	if(useCarrier){
		/* Ensure the dividend fits into 32 bit */
		pulse = DIV_ROUND_CLOSEST(gpio_ir->duty_cycle * (NSEC_PER_SEC / 100),
					  gpio_ir->carrier);
		space = DIV_ROUND_CLOSEST((100 - gpio_ir->duty_cycle) *
					  (NSEC_PER_SEC / 100), gpio_ir->carrier);
	}
	dprintk("carrier is %d \n",gpio_ir->carrier);
	spin_lock_irqsave(&gpio_ir->lock, flags);

	edge = ktime_get();
	for (i = 0; i < count; i++) {
		if (i % 2) {
			// space
			edge = ktime_add_us(edge, txbuf[i]);
			delta = ktime_us_delta(edge, ktime_get());
			if (delta > 10) {
				spin_unlock_irqrestore(&gpio_ir->lock, flags);
				usleep_range(delta, delta + 10);
				spin_lock_irqsave(&gpio_ir->lock, flags);
			} else if (delta > 0) {
				udelay(delta);
			}
		} else {
			// pulse
			ktime_t last = ktime_add_us(edge, txbuf[i]);
			if(useCarrier){
				while (ktime_before(ktime_get(), last)) {
					gpiod_set_value(gpio_ir->gpio, 1);
					edge = ktime_add_ns(edge, pulse);
					delta = ktime_to_ns(ktime_sub(edge,
								      ktime_get()));
					if (delta > 0)
						ndelay(delta);
					gpiod_set_value(gpio_ir->gpio, 0);
					edge = ktime_add_ns(edge, space);
					delta = ktime_to_ns(ktime_sub(edge,
								      ktime_get()));
					if (delta > 0)
						ndelay(delta);
				}
				edge = last;
			}else{
				gpiod_set_value(gpio_ir->gpio, 1);
				edge = last;
				delta = ktime_us_delta(edge, ktime_get());
				if (delta > 10) {
					spin_unlock_irqrestore(&gpio_ir->lock, flags);
					usleep_range(delta, delta + 10);
					spin_lock_irqsave(&gpio_ir->lock, flags);
				} else if (delta > 0) {
					udelay(delta);
				}
				gpiod_set_value(gpio_ir->gpio, 0);
			}
		}
	}

	spin_unlock_irqrestore(&gpio_ir->lock, flags);

	return count;
}

static int gpio_ir_tx_probe(struct platform_device *pdev)
{
	struct gpio_ir *gpio_ir;
	struct rc_dev *rcdev;
	int rc;
	int carrier = 38000;
	
	gpio_ir = devm_kmalloc(&pdev->dev, sizeof(*gpio_ir), GFP_KERNEL);
	if (!gpio_ir)
		return -ENOMEM;

	rcdev = devm_rc_allocate_device(&pdev->dev, RC_DRIVER_IR_RAW_TX);
	if (!rcdev)
		return -ENOMEM;

	gpio_ir->gpio = devm_gpiod_get(&pdev->dev, NULL, GPIOD_OUT_LOW);
	if (IS_ERR(gpio_ir->gpio)) {
		if (PTR_ERR(gpio_ir->gpio) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get gpio (%ld)\n",
				PTR_ERR(gpio_ir->gpio));
		return PTR_ERR(gpio_ir->gpio);
	}
	
	//of_property_read_string(pdev->dev.of_node, "rc,devicename", &devicename);
	of_property_read_u32(pdev->dev.of_node, "rc,softcarrier", &carrier);
	
	rcdev->priv = gpio_ir;
	rcdev->driver_name = DRIVER_NAME;
	rcdev->device_name = of_get_property(pdev->dev.of_node, "rc,devicename", NULL);
	if (!rcdev->device_name)
		rcdev->device_name = DEVICE_NAME;
	
	rcdev->tx_ir = gpio_ir_tx;
	rcdev->s_tx_duty_cycle = gpio_ir_tx_set_duty_cycle;
	rcdev->s_tx_carrier = gpio_ir_tx_set_carrier;

	gpio_ir->carrier = carrier;
	gpio_ir->duty_cycle = 50;
	spin_lock_init(&gpio_ir->lock);
	
	dprintk("device_name is %s !\n",rcdev->device_name);
	dprintk("carrier is %d \n",carrier);

	rc = devm_rc_register_device(&pdev->dev, rcdev);
	if (rc < 0)
		dev_err(&pdev->dev, "failed to register rc device\n");

	return rc;
}

static struct platform_driver gpio_ir_tx_driver = {
	.probe	= gpio_ir_tx_probe,
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(gpio_ir_tx_of_match),
	},
};
module_platform_driver(gpio_ir_tx_driver);

MODULE_DESCRIPTION(DEVICE_NAME);
MODULE_AUTHOR("Simon");
MODULE_LICENSE("GPL");
