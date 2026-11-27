// SPDX-License-Identifier: GPL-2.0
/*
 * Nintendo Wii Nunchuk I2C driver.
 *
 * The Nunchuk is a simple I2C slave sitting at address 0x52. It exposes a
 * two-axis joystick, a three-axis accelerometer and two buttons (C and Z).
 * The device has no interrupt line and no end-of-conversion status bit, so
 * its registers have to be polled: every read triggers the next conversion.
 *
 * Copyright (C) 2026 Imel23
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/property.h>

/* Number of status registers read in one shot */
#define NUNCHUK_REGS_LEN		6

/* Offsets inside the register block */
#define NUNCHUK_REG_JOYSTICK_X		0
#define NUNCHUK_REG_JOYSTICK_Y		1
#define NUNCHUK_REG_BUTTONS		5

/* Buttons are active low in the last register */
#define NUNCHUK_BTN_Z_MASK		BIT(0)
#define NUNCHUK_BTN_C_MASK		BIT(1)

/* Initialization sequences, see https://bootlin.com/labs/doc/nunchuk.pdf */
#define NUNCHUK_INIT_REG		0xf0
#define NUNCHUK_INIT_VAL		0x55
#define NUNCHUK_ALT_INIT_REG		0x40
#define NUNCHUK_ALT_INIT_VAL		0x00
#define NUNCHUK_INIT2_REG		0xfb
#define NUNCHUK_INIT2_VAL		0x00

/* Delays required by the device between two I2C operations */
#define NUNCHUK_INIT_DELAY_US		1000
#define NUNCHUK_READ_DELAY_US		10000

/* Polling period of the input device */
#define NUNCHUK_POLL_INTERVAL_MS	50

/* Usable range of the joystick, as reported to the input core */
#define NUNCHUK_ABS_X_MIN		30
#define NUNCHUK_ABS_X_MAX		220
#define NUNCHUK_ABS_Y_MIN		40
#define NUNCHUK_ABS_Y_MAX		200
#define NUNCHUK_ABS_FUZZ		4
#define NUNCHUK_ABS_FLAT		8

/*
 * Per-device private data. It keeps the link between the logical device
 * (the input device handled by the input subsystem) and the physical one
 * (the I2C client used to talk to the hardware).
 */
struct nunchuk_dev {
	struct i2c_client *i2c_client;
};

static const struct of_device_id nunchuk_of_match[] = {
	{ .compatible = "nintendo,nunchuk" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nunchuk_of_match);

/*
 * Send the two-step initialization sequence which switches the device to
 * unencrypted mode. White ("alternate") Nunchuks expect a different first
 * register, selected through the nintendo,alternate-init DT property.
 */
static int nunchuk_init_device(struct i2c_client *client)
{
	char buf[2];
	int ret;

	if (device_property_read_bool(&client->dev, "nintendo,alternate-init")) {
		buf[0] = NUNCHUK_ALT_INIT_REG;
		buf[1] = NUNCHUK_ALT_INIT_VAL;
	} else {
		buf[0] = NUNCHUK_INIT_REG;
		buf[1] = NUNCHUK_INIT_VAL;
	}

	ret = i2c_master_send(client, buf, sizeof(buf));
	if (ret < 0)
		return ret;
	if (ret != sizeof(buf))
		return -EIO;

	fsleep(NUNCHUK_INIT_DELAY_US);

	buf[0] = NUNCHUK_INIT2_REG;
	buf[1] = NUNCHUK_INIT2_VAL;

	ret = i2c_master_send(client, buf, sizeof(buf));
	if (ret < 0)
		return ret;
	if (ret != sizeof(buf))
		return -EIO;

	return 0;
}

/*
 * Read the whole register block once. Writing 0x00 selects the first
 * register, and the device needs some time before and after that write to
 * complete the conversion started by the previous read.
 */
static int nunchuk_read_registers(struct i2c_client *client, char *buf,
				  size_t len)
{
	int ret;

	fsleep(NUNCHUK_READ_DELAY_US);

	ret = i2c_master_send(client, "\x00", 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;

	fsleep(NUNCHUK_READ_DELAY_US);

	ret = i2c_master_recv(client, buf, len);
	if (ret < 0)
		return ret;
	if (ret != len)
		return -EIO;

	return 0;
}

/*
 * Polling routine called by the input core every
 * NUNCHUK_POLL_INTERVAL_MS milliseconds. It samples the device state and
 * reports it to user space.
 */
static void nunchuk_poll(struct input_dev *input)
{
	struct nunchuk_dev *nunchuk = input_get_drvdata(input);
	char regs[NUNCHUK_REGS_LEN];
	bool zpressed, cpressed;
	int ret;

	ret = nunchuk_read_registers(nunchuk->i2c_client, regs, sizeof(regs));
	if (ret)
		return;

	/* Both buttons are active low */
	zpressed = !(regs[NUNCHUK_REG_BUTTONS] & NUNCHUK_BTN_Z_MASK);
	cpressed = !(regs[NUNCHUK_REG_BUTTONS] & NUNCHUK_BTN_C_MASK);

	input_report_abs(input, ABS_X, regs[NUNCHUK_REG_JOYSTICK_X]);
	input_report_abs(input, ABS_Y, regs[NUNCHUK_REG_JOYSTICK_Y]);
	input_report_key(input, BTN_Z, zpressed);
	input_report_key(input, BTN_C, cpressed);
	input_sync(input);
}

static int nunchuk_probe(struct i2c_client *client)
{
	struct nunchuk_dev *nunchuk;
	struct input_dev *input;
	int ret;

	ret = nunchuk_init_device(client);
	if (ret) {
		dev_err(&client->dev, "failed to initialize the device (%d)\n",
			ret);
		return ret;
	}

	nunchuk = devm_kzalloc(&client->dev, sizeof(*nunchuk), GFP_KERNEL);
	if (!nunchuk)
		return -ENOMEM;

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;

	/* Keep the link between the logical and the physical device */
	nunchuk->i2c_client = client;
	input_set_drvdata(input, nunchuk);

	input->name = "Wii Nunchuk";
	input->id.bustype = BUS_I2C;

	/* Nunchuk buttons */
	set_bit(EV_KEY, input->evbit);
	set_bit(BTN_C, input->keybit);
	set_bit(BTN_Z, input->keybit);

	/* Joystick axes */
	set_bit(ABS_X, input->absbit);
	set_bit(ABS_Y, input->absbit);
	input_set_abs_params(input, ABS_X, NUNCHUK_ABS_X_MIN,
			     NUNCHUK_ABS_X_MAX, NUNCHUK_ABS_FUZZ,
			     NUNCHUK_ABS_FLAT);
	input_set_abs_params(input, ABS_Y, NUNCHUK_ABS_Y_MIN,
			     NUNCHUK_ABS_Y_MAX, NUNCHUK_ABS_FUZZ,
			     NUNCHUK_ABS_FLAT);

	/*
	 * Classic controller buttons. They are not wired on the Nunchuk but
	 * user space joystick applications expect them to be advertised.
	 */
	set_bit(BTN_TL, input->keybit);
	set_bit(BTN_SELECT, input->keybit);
	set_bit(BTN_MODE, input->keybit);
	set_bit(BTN_START, input->keybit);
	set_bit(BTN_TR, input->keybit);
	set_bit(BTN_TL2, input->keybit);
	set_bit(BTN_B, input->keybit);
	set_bit(BTN_Y, input->keybit);
	set_bit(BTN_A, input->keybit);
	set_bit(BTN_X, input->keybit);
	set_bit(BTN_TR2, input->keybit);

	ret = input_setup_polling(input, nunchuk_poll);
	if (ret) {
		dev_err(&client->dev, "failed to set up polling (%d)\n", ret);
		return ret;
	}

	input_set_poll_interval(input, NUNCHUK_POLL_INTERVAL_MS);

	/* Register last, once the device is fully described */
	ret = input_register_device(input);
	if (ret) {
		dev_err(&client->dev, "failed to register input device (%d)\n",
			ret);
		return ret;
	}

	return 0;
}

static void nunchuk_remove(struct i2c_client *client)
{
	/*
	 * Nothing to do here: every resource was allocated with a devm_
	 * helper, so the driver core releases them automatically, including
	 * the unregistration of the input device.
	 */
}

static struct i2c_driver nunchuk_driver = {
	.driver = {
		.name = "nunchuk",
		.of_match_table = nunchuk_of_match,
	},
	.probe = nunchuk_probe,
	.remove = nunchuk_remove,
};
module_i2c_driver(nunchuk_driver);

MODULE_AUTHOR("Imel23");
MODULE_DESCRIPTION("Nintendo Wii Nunchuk I2C input driver");
MODULE_LICENSE("GPL");
