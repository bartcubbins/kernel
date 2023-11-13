// SPDX-License-Identifier: GPL-2.0
/**
 * murray-camera-regulator.c - Murray camera regulator driver
 * Copyright (C) 2023 Pavel Dubrova <pashadubrova@gmail.com>
 */

#define DEBUG

#define pr_fmt(fmt)	"murray-camera-regulator: %s: " fmt, __func__

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#include "murray-camera-regulator.h"

static const struct regmap_range wl2868c_readable_ranges[] = {
	{ WL2868C_REG_CHIP_ID,  WL2868C_REG_RESERVED_0 },
	{ WL2868C_REG_INT_EN_SET, WL2868C_REG_RESERVED_1 },
};

static const struct regmap_access_table wl2868c_readable_tab = {
	.yes_ranges = wl2868c_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(wl2868c_readable_ranges),
};

static const struct regmap_range wl2868c_writable_ranges[] = {
	{ WL2868C_REG_DISCHARGE,    WL2868C_REG_SEQUENCING   },
	{ WL2868C_REG_LDO1_OCP_CTL, WL2868C_REG_LDO1_OCP_CTL },
	{ WL2868C_REG_LDO2_OCP_CTL, WL2868C_REG_LDO2_OCP_CTL },
	{ WL2868C_REG_LDO3_OCP_CTL, WL2868C_REG_LDO3_OCP_CTL },
	{ WL2868C_REG_LDO4_OCP_CTL, WL2868C_REG_LDO4_OCP_CTL },
	{ WL2868C_REG_LDO5_OCP_CTL, WL2868C_REG_LDO5_OCP_CTL },
	{ WL2868C_REG_LDO6_OCP_CTL, WL2868C_REG_LDO6_OCP_CTL },
	{ WL2868C_REG_LDO7_OCP_CTL, WL2868C_REG_INT_EN_SET   },
	{ WL2868C_REG_UVLO_CTL,     WL2868C_REG_RESERVED_1   },
};

static const struct regmap_access_table wl2868c_writable_tab = {
	.yes_ranges = wl2868c_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(wl2868c_writable_ranges),
};

static const struct regmap_range fan53870_readable_ranges[] = {
	{ FAN53870_REG_CHIP_ID, FAN53870_REG_MINT3 },
};

static const struct regmap_access_table fan53870_readable_tab = {
	.yes_ranges = fan53870_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(fan53870_readable_ranges),
};

static const struct regmap_range fan53870_writable_ranges[] = {
	{ FAN53870_REG_IOUT, FAN53870_REG_LDO_COMP1 },
	{ FAN53870_REG_MINT1, FAN53870_REG_MINT3 },
};

static const struct regmap_access_table fan53870_writable_tab = {
	.yes_ranges = fan53870_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(fan53870_writable_ranges),
};

static const struct regmap_config camera_regulator_regmap_config[] = {
	[REGULATOR_WL2868C] = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = WL2868C_REG_RESERVED_1,
		.rd_table = &wl2868c_readable_tab,
		.wr_table = &wl2868c_writable_tab,
	},
	[REGULATOR_ET5907] = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = ET5907_REG_TSD_UVLO_INTMA,
		//.rd_table = &fan53870_readable_tab,
		//.wr_table = &fan53870_writable_tab,
	},
	[REGULATOR_FAN53870] = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = FAN53870_REG_MINT3,
		.rd_table = &fan53870_readable_tab,
		.wr_table = &fan53870_writable_tab,
	},
};

static int camera_regulator_read(struct regmap *regmap, u16 reg, u8 *val, int count)
{
	int ret = 0;

	ret = regmap_bulk_read(regmap, reg, val, count);
	if (ret < 0)
		pr_err("failed to read 0x%02x\n", reg);

	pr_debug("read 0x%02x from 0x%02x\n", *val, reg);

	return ret;
}

static int camera_regulator_write(struct regmap *regmap, u16 reg, u8*val, int count)
{
	int ret = 0;

	pr_debug("write 0x%02x to 0x%02x\n", *val, reg);

	ret = regmap_bulk_write(regmap, reg, val, count);
	if (ret < 0)
		pr_err("failed to write 0x%02x\n", reg);

	return ret;
}

static int camera_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct camera_regulator *chip = rdev_get_drvdata(rdev);
	int ret = 0, uv = 0;
	u8 vset;

	ret = camera_regulator_read(chip->regmap, rdev->desc->vsel_reg, &vset, 1);
	if (ret < 0) {
		pr_err("failed to read regulator voltage, ret = %d\n", ret);
		return ret;
	}

	switch (chip->chip_id) {
		case REGULATOR_WL2868C:
			if (rdev->desc->vsel_reg == WL2868C_REG_LDO1 ||
				rdev->desc->vsel_reg == WL2868C_REG_LDO2)
				uv = (WL2868C_VSET_BASE12_MV + vset * WL2868C_VSET_STEP_MV) * 1000;
			else
				uv = (WL2868C_VSET_BASE34567_MV + vset * WL2868C_VSET_STEP_MV) * 1000;
			break;
		case REGULATOR_ET5907:
			if (rdev->desc->vsel_reg == ET5907_REG_LDO1 ||
				rdev->desc->vsel_reg == ET5907_REG_LDO2)
				uv = (ET5907_VSET_BASE12_MV + vset * ET5907_VSET_STEP12_MV) * 1000;
			else
				uv = (ET5907_VSET_BASE34567_MV + vset * ET5907_VSET_STEP34567_MV) * 1000;
			break;
		case REGULATOR_FAN53870:
			if (rdev->desc->vsel_reg == FAN53870_REG_LDO1 ||
				rdev->desc->vsel_reg == FAN53870_REG_LDO2)
				uv = (FAN53870_VSET_BASE12_MV + (vset - 99) * FAN53870_VSET_STEP_MV) * 1000;
			else
				uv = (FAN53870_VSET_BASE34567_MV + (vset - 16) * FAN53870_VSET_STEP_MV) * 1000;
			break;
		default:
			break;
	}

	pr_debug("%s regulator voltage is: %duV\n", rdev->desc->name, uv);

	return uv;
}

static int camera_regulator_set_voltage(struct regulator_dev *rdev,
	int min_uv, int max_uv, unsigned int* selector)
{
	struct camera_regulator *chip = rdev_get_drvdata(rdev);
	int ret = 0, mv = 0;
	u8 vset = 0;

	mv = DIV_ROUND_UP(min_uv, 1000);
	if (mv * 1000 > max_uv) {
		pr_err("requested voltage above maximum limit\n");
		return -EINVAL;
	}

	switch (chip->chip_id) {
		case REGULATOR_WL2868C:
			if (rdev->desc->vsel_reg == WL2868C_REG_LDO1 ||
				rdev->desc->vsel_reg == WL2868C_REG_LDO2)
				vset = DIV_ROUND_UP(mv - WL2868C_VSET_BASE12_MV, WL2868C_VSET_STEP_MV);
			else
				vset = DIV_ROUND_UP(mv - WL2868C_VSET_BASE34567_MV, WL2868C_VSET_STEP_MV);
			break;
		case REGULATOR_ET5907:
			if (rdev->desc->vsel_reg == ET5907_REG_LDO1 ||
				rdev->desc->vsel_reg == ET5907_REG_LDO2)
				vset = DIV_ROUND_UP(mv - ET5907_VSET_BASE12_MV, ET5907_VSET_STEP12_MV);
			else
				vset = DIV_ROUND_UP(mv - ET5907_VSET_BASE34567_MV, ET5907_VSET_STEP34567_MV);
			break;
		case REGULATOR_FAN53870:
			if (rdev->desc->vsel_reg == FAN53870_REG_LDO1 ||
				rdev->desc->vsel_reg == FAN53870_REG_LDO2)
				vset = DIV_ROUND_UP(mv - FAN53870_VSET_BASE12_MV, FAN53870_VSET_STEP_MV) + 99;
			else
				vset = DIV_ROUND_UP(mv - FAN53870_VSET_BASE34567_MV, FAN53870_VSET_STEP_MV) + 16;
			break;
		default:
			break;
	}

	ret = camera_regulator_write(chip->regmap, rdev->desc->vsel_reg, &vset, 1);
	if (ret < 0) {
		pr_err("failed to write voltage ret = %d\n", ret);
		return ret;
	}

	pr_debug("regulator voltage set to %duV (0x%02x)\n", mv * 1000, vset);

	return ret;
}

static struct regulator_ops camera_regulator_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.get_voltage	= camera_regulator_get_voltage,
	.set_voltage	= camera_regulator_set_voltage,
};

static struct regulator_ldo_data ldo_data[REGULATOR_MAX][CAMERA_LDO_MAX] = {
	[REGULATOR_WL2868C] = {
		/* name, supply_name, min_uV, max_uV, default_uV, en_time, mV_step, ldo_reg */
		{ "camera-ldo1", "vdd_l1_l2",  496000, 1512000, 1200000, 60, WL2868C_VSET_STEP_MV, WL2868C_REG_LDO1 },
		{ "camera-ldo2", "vdd_l1_l2",  496000, 1512000, 1200000, 60, WL2868C_VSET_STEP_MV, WL2868C_REG_LDO2 },
		{ "camera-ldo3", "vdd_l3_l4", 1504000, 3544000, 2800000, 80, WL2868C_VSET_STEP_MV, WL2868C_REG_LDO3 },
		{ "camera-ldo4", "vdd_l3_l4", 1504000, 3544000, 2800000, 80, WL2868C_VSET_STEP_MV, WL2868C_REG_LDO4 },
		{ "camera-ldo5", "vdd_l5",    1504000, 3544000, 2800000, 80, WL2868C_VSET_STEP_MV, WL2868C_REG_LDO5 },
		{ "camera-ldo6", "vdd_l6",    1504000, 3544000, 2800000, 80, WL2868C_VSET_STEP_MV, WL2868C_REG_LDO6 },
		{ "camera-ldo7", "vdd_l7",    1504000, 3544000, 2800000, 80, WL2868C_VSET_STEP_MV, WL2868C_REG_LDO7 },
	},
	[REGULATOR_ET5907] = {
		/* name, supply_name, min_uV, max_uV, default_uV, en_time, mV_step, ldo_reg */
		{ "camera-ldo1", "vdd_l1_l2",  600000, 1800000, 1200000, 300, ET5907_VSET_STEP12_MV,    ET5907_REG_LDO1 },
		{ "camera-ldo2", "vdd_l1_l2",  600000, 1800000, 1200000, 300, ET5907_VSET_STEP12_MV,    ET5907_REG_LDO2 },
		{ "camera-ldo3", "vdd_l3_l4", 1200000, 3750000, 2800000, 130, ET5907_VSET_STEP34567_MV, ET5907_REG_LDO3 },
		{ "camera-ldo4", "vdd_l3_l4", 1200000, 3750000, 2800000, 130, ET5907_VSET_STEP34567_MV, ET5907_REG_LDO4 },
		{ "camera-ldo5", "vdd_l5",    1200000, 3750000, 2800000, 130, ET5907_VSET_STEP34567_MV, ET5907_REG_LDO5 },
		{ "camera-ldo6", "vdd_l6",    1200000, 3750000, 2800000, 130, ET5907_VSET_STEP34567_MV, ET5907_REG_LDO6 },
		{ "camera-ldo7", "vdd_l7",    1200000, 3750000, 2800000, 130, ET5907_VSET_STEP34567_MV, ET5907_REG_LDO7 },
	},
	[REGULATOR_FAN53870] = {
		/* name, supply_name, min_uV, max_uV, default_uV, en_time, mV_step, ldo_reg */
		{ "camera-ldo1", "vdd_l1_l2",  800000, 1504000, 1050000, 400, FAN53870_VSET_STEP_MV, FAN53870_REG_LDO1 },
		{ "camera-ldo2", "vdd_l1_l2",  800000, 1504000, 1050000, 400, FAN53870_VSET_STEP_MV, FAN53870_REG_LDO2 },
		{ "camera-ldo3", "vdd_l3_l4", 1500000, 3412000, 2800000, 100, FAN53870_VSET_STEP_MV, FAN53870_REG_LDO3 },
		{ "camera-ldo4", "vdd_l3_l4", 1500000, 3412000, 2800000, 100, FAN53870_VSET_STEP_MV, FAN53870_REG_LDO4 },
		{ "camera-ldo5", "vdd_l5",    1500000, 3412000, 1800000, 100, FAN53870_VSET_STEP_MV, FAN53870_REG_LDO5 },
		{ "camera-ldo6", "vdd_l6",    1500000, 3412000, 2800000, 100, FAN53870_VSET_STEP_MV, FAN53870_REG_LDO6 },
		{ "camera-ldo7", "vdd_l7",    1500000, 3412000, 2800000, 100, FAN53870_VSET_STEP_MV, FAN53870_REG_LDO7 },
	},
};

static int camera_regulator_register_ldo(struct camera_regulator *chip,
		const char *name, struct device_node *np)
{
	struct regulator_config reg_config = {};
	struct regulator_init_data *init_data = NULL;
	struct regulator_desc *rdesc = NULL;
	struct regulator_ldo_data *rdata = NULL;
	struct device *dev = chip->dev;
	int i = 0, ret = 0;

	/* get chip_id data */
	for (i = 0; i < CAMERA_LDO_MAX; i++) {
		if (strstr(name, ldo_data[chip->chip_id][i].name))
			break;
	}

	if (i == CAMERA_LDO_MAX) {
		pr_err("%s: invalid regulator name\n", name);
		return -EINVAL;
	}

	rdesc = &chip->rdesc[i];
	rdata = &ldo_data[chip->chip_id][i];

	init_data = of_get_regulator_init_data(dev, np, rdesc);
	if (init_data == NULL) {
		pr_err("%s: failed to get regulator data\n", name);
		return -ENODATA;
	}

	if (!init_data->constraints.name) {
		pr_err("%s: regulator name is missing\n", name);
		return -EINVAL;
	}

	init_data->constraints.valid_ops_mask |= REGULATOR_CHANGE_STATUS
			| REGULATOR_CHANGE_VOLTAGE;
	init_data->constraints.min_uV = rdata->min_uV;
	init_data->constraints.max_uV = rdata->max_uV;

	reg_config.dev = dev;
	reg_config.init_data = init_data;
	reg_config.driver_data = chip;
	reg_config.of_node = np;
	reg_config.regmap = chip->regmap;
	reg_config.ena_gpiod = NULL;

	rdesc->name				= rdata->name;
	rdesc->supply_name		= rdata->supply_name;
	rdesc->of_match			= rdata->name;
	rdesc->regulators_node	= "regulators";
	rdesc->id				= i;
	rdesc->n_voltages		= (rdata->max_uV - rdata->min_uV)
			/ rdesc->uV_step + 1;
	rdesc->ops				= &camera_regulator_ops;
	rdesc->type				= REGULATOR_VOLTAGE;
	rdesc->owner			= THIS_MODULE;
	rdesc->min_uV			= rdata->min_uV;
	rdesc->uV_step			= rdata->mV_step * 1000;
	rdesc->vsel_reg			= rdata->ldo_reg;
	rdesc->vsel_mask		= 0xff;
	/* ET5907 and FAN53870 share the same enable register (0x03) */
	rdesc->enable_reg		= (chip->chip_id == REGULATOR_WL2868C)
			? WL2868C_REG_LDO_ENABLE : ET5907_REG_LDO_ENABLE;
	rdesc->enable_mask		= BIT(i);
	rdesc->enable_time		= rdata->en_time;

	chip->rdev[i] = devm_regulator_register(dev,
			rdesc, &reg_config);
	if (IS_ERR(chip->rdev[i])) {
		ret = PTR_ERR(chip->rdev[i]);
		pr_err("%s: failed to register regulator ret = %d\n",
				name, ret);
		return ret;
	}

	pr_debug("%s regulator registered\n", name);

	return 0;
}

static int camera_regulator_parse_regulator(struct camera_regulator *chip)
{
	struct device_node *parent = NULL;
	struct device_node *child = NULL;
	const char *name;
	int ret = 0;

	parent = of_find_node_by_name(chip->dev->of_node, "regulators");
	if (!parent) {
		pr_err("no regulators node found\n");
		return -EINVAL;
	}

	/* Iterate through each child node of the "regulators" subnode */
	for_each_available_child_of_node(parent, child) {
		ret = of_property_read_string(child, "regulator-name", &name);
		if (ret)
			continue;

		ret = camera_regulator_register_ldo(chip, name, child);
		if (ret < 0) {
			pr_err("failed to register regulator %s ret = %d\n", name, ret);
			return ret;
		}
	}

	return 0;
}

static int get_chip_id(struct i2c_client *client) {
	int i = 0, chip = 0;
	struct i2c_device which_ldo_chip[REGULATOR_MAX] = {
		{ "WL2868C",  WL2868C_I2C_ADDR,  WL2868C_CHIP_ID,  WL2868C_REV_ID },
		{ "ET5907",   ET5907_I2C_ADDR,   ET5907_CHIP_ID,   ET5907_REV_ID },
		{ "FAN53870", FAN53870_I2C_ADDR, FAN53870_CHIP_ID, FAN53870_REV_ID },
	};

	for (i = 0; i < ARRAY_SIZE(which_ldo_chip); i++) {
		client->addr = which_ldo_chip[i].i2c_addr;

		pr_debug("Try camera regulator at: 0x%02x, chipid: 0x%02x, revid: 0x%02x\n",
				client->addr, which_ldo_chip[i].chip_id, which_ldo_chip[i].rev_id);

		chip = i2c_smbus_read_byte_data(client, which_ldo_chip[i].chip_id) & 0xff;
		if (chip == which_ldo_chip[i].rev_id) {
			pr_info("Camera regulator is %s\n", which_ldo_chip[i].name);
			return i;
		}
	}

	return -EINVAL;
}

static struct sequence init_sequence[REGULATOR_MAX][6] = {
	[REGULATOR_WL2868C] = {
		{ WL2868C_REG_DISCHARGE,  0x00 },
		{ WL2868C_REG_LDO12_SEQ,  0x00 },
		{ WL2868C_REG_LDO34_SEQ,  0x00 },
		{ WL2868C_REG_LDO56_SEQ,  0x00 },
		{ WL2868C_REG_LDO7_SEQ,   0x00 },
		{ WL2868C_REG_SEQUENCING, 0x00 },
	},
	[REGULATOR_ET5907] = {
		{ ET5907_REG_DISCHARGE,  0x7F },
		{ ET5907_REG_LDO12_SEQ,  0x00 },
		{ ET5907_REG_LDO34_SEQ,  0x00 },
		{ ET5907_REG_LDO56_SEQ,  0x00 },
		{ ET5907_REG_LDO7_SEQ,   0x00 },
		{ ET5907_REG_SEQUENCING, 0x00 },
	},
	[REGULATOR_FAN53870] = {
		{ FAN53870_REG_DISCHARGE,  0x7F },
		{ FAN53870_REG_LDO12_SEQ,  0x00 },
		{ FAN53870_REG_LDO34_SEQ,  0x00 },
		{ FAN53870_REG_LDO56_SEQ,  0x00 },
		{ FAN53870_REG_LDO7_SEQ,   0x00 },
		{ FAN53870_REG_SEQUENCING, 0x00 },
	},
};

static int camera_regulator_init_sequence(struct camera_regulator *chip)
{
	int i = 0, ret = 0;
	struct sequence *seq;

	for (i = 0; i < ARRAY_SIZE(init_sequence[chip->chip_id]); i++) {
		seq = &init_sequence[chip->chip_id][i];

		pr_debug("Send init sequence %d, reg = 0x%02x, val = 0x%02x\n",
			i, seq->reg, seq->val);
		ret = regmap_write(chip->regmap, seq->reg, seq->val);
		if (ret < 0) {
			pr_err("Failed to write 0x%02x register\n", seq->reg);
			return -EINVAL;
		}
	}

	return ret;
}

static int camera_regulator_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct camera_regulator *chip = NULL;
	int ret = 0;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->rstn_gpio = of_get_named_gpio(dev->of_node, "rstn-gpio", 0);
	if (gpio_is_valid(chip->rstn_gpio)) {
		ret = devm_gpio_request_one(dev, chip->rstn_gpio,
			GPIOF_OUT_INIT_HIGH, "cam_reg_rstn");
		if (ret < 0) {
			dev_err(dev, "failed to request reset_n gpio %d: %d",
				chip->rstn_gpio, ret);
			return ret;
		}
	}

	chip->chip_id = get_chip_id(client);
	if (chip->chip_id < 0) {
		pr_err("unknown regulator\n");
		return -ENODEV;
	}

	i2c_set_clientdata(client, chip);
	chip->dev = dev;

	chip->regmap = devm_regmap_init_i2c(client, &camera_regulator_regmap_config[chip->chip_id]);
	if (IS_ERR(chip->regmap)) {
		pr_err("failed to allocate regmap\n");
		return PTR_ERR(chip->regmap);
	}

	ret = camera_regulator_init_sequence(chip);
	if (ret < 0) {
		pr_err("failed to parse device tree ret = %d\n", ret);
		return ret;
	}

	ret = camera_regulator_parse_regulator(chip);
	if (ret < 0) {
		pr_err("failed to parse device tree ret = %d\n", ret);
		return ret;
	}

	return 0;
}

static int camera_regulator_remove(struct i2c_client *client)
{
	struct camera_regulator *chip = i2c_get_clientdata(client);

	if (gpio_is_valid(chip->rstn_gpio)) {
		gpio_direction_output(chip->rstn_gpio, GPIOF_INIT_LOW);
	}

	return 0;
}

static void camera_regulator_shutdown(struct i2c_client *client)
{
	struct camera_regulator *chip = i2c_get_clientdata(client);
	u16 reg;
	u8 vset = 0x00;

	/* ET5907 and FAN53870 share the same enable register (0x03) */
	reg = (chip->chip_id == REGULATOR_WL2868C)
			? WL2868C_REG_LDO_ENABLE : ET5907_REG_LDO_ENABLE;

	/* Disable regulator to avoid current leak */
	camera_regulator_write(chip->regmap, reg, &vset, 1);
}

static const struct of_device_id camera_regulator_dt_ids[] = {
	{ .compatible = "murray,camera-regulator", },
	{}
};
MODULE_DEVICE_TABLE(of, camera_regulator_dt_ids);

static const struct i2c_device_id camera_regulator_i2c_id[] = {
	{ "camera-regulator", },
	{},
};
MODULE_DEVICE_TABLE(i2c, camera_regulator_i2c_id);

static struct i2c_driver camera_regulator_driver = {
	.driver = {
		.name = "murray-camera-regulator",
		.of_match_table = of_match_ptr(camera_regulator_dt_ids),
	},
	.probe = camera_regulator_probe,
	.remove = camera_regulator_remove,
	.shutdown = camera_regulator_shutdown,
	.id_table = camera_regulator_i2c_id,
};
module_i2c_driver(camera_regulator_driver);

MODULE_AUTHOR("Pavel Dubrova <pashadubrova@gmail.com>");
MODULE_DESCRIPTION("Murray camera regulator intermediator driver");
MODULE_LICENSE("GPL"); 
