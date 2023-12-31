# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for NXP NCI based NFC I2C Slave Driver for SNxxx
#

ccflags-y += -I$(srctree)/techpack/securemsm/linux/
ccflags-y += -I$(srctree)/techpack/securemsm/smcinvoke/

nxp-nfc-objs := \
		ese_cold_reset.o \
		common.o \
		common_nxp.o \
		common_qcom.o \
		i2c_drv.o

obj-$(CONFIG_NXP_NFC_I2C) += nxp-nfc.o
