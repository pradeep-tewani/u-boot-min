/*
 * board.c
 *
 * Board functions for TI AM335X based boards
 *
 * Copyright (C) 2011, Texas Instruments, Incorporated - http://www.ti.com/
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <errno.h>
#include <spl.h>
#include <asm/arch/cpu.h>
#include <asm/arch/hardware.h>
#include <asm/arch/omap.h>
#include <asm/arch/ddr_defs.h>
#include <asm/arch/clock.h>
#include <asm/arch/gpio.h>
#include <asm/arch/mmc_host_def.h>
#include <asm/arch/sys_proto.h>
#include <asm/arch/mem.h>
#include <asm/io.h>
#include <asm/emif.h>
#include <asm/gpio.h>
#include <i2c.h>
#include <miiphy.h>
#include <cpsw.h>
#include <power/tps65217.h>
#include <power/tps65910.h>
#include <environment.h>
#include <watchdog.h>
#include <environment.h>
#include "board.h"

DECLARE_GLOBAL_DATA_PTR;

/* GPIO that controls power to DDR on EVM-SK */
#define GPIO_DDR_VTT_EN		7

//static struct ctrl_dev *cdev = (struct ctrl_dev *)CTRL_DEVICE_BASE;

int do_userbutton (cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	int button = 0;
	gpio_direction_input(72);
	button = gpio_get_value(72);
	return button;
}

U_BOOT_CMD(
	userbutton, CONFIG_SYS_MAXARGS, 1, do_userbutton,
	"Return the status of the BeagleBoard USER button",
	""
);


#ifndef CONFIG_SKIP_LOWLEVEL_INIT
static const struct ddr_data ddr3_beagleblack_data = {
	.datardsratio0 = MT41K256M16HA125E_RD_DQS,
	.datawdsratio0 = MT41K256M16HA125E_WR_DQS,
	.datafwsratio0 = MT41K256M16HA125E_PHY_FIFO_WE,
	.datawrsratio0 = MT41K256M16HA125E_PHY_WR_DATA,
};

static const struct cmd_control ddr3_beagleblack_cmd_ctrl_data = {
	.cmd0csratio = MT41K256M16HA125E_RATIO,
	.cmd0iclkout = MT41K256M16HA125E_INVERT_CLKOUT,

	.cmd1csratio = MT41K256M16HA125E_RATIO,
	.cmd1iclkout = MT41K256M16HA125E_INVERT_CLKOUT,

	.cmd2csratio = MT41K256M16HA125E_RATIO,
	.cmd2iclkout = MT41K256M16HA125E_INVERT_CLKOUT,
};

static struct emif_regs ddr3_beagleblack_emif_reg_data = {
	.sdram_config = MT41K256M16HA125E_EMIF_SDCFG,
	.ref_ctrl = MT41K256M16HA125E_EMIF_SDREF,
	.sdram_tim1 = MT41K256M16HA125E_EMIF_TIM1,
	.sdram_tim2 = MT41K256M16HA125E_EMIF_TIM2,
	.sdram_tim3 = MT41K256M16HA125E_EMIF_TIM3,
	.zq_config = MT41K256M16HA125E_ZQ_CFG,
	.emif_ddr_phy_ctlr_1 = MT41K256M16HA125E_EMIF_READ_LATENCY,
	};


#define OSC	(V_OSCK/1000000)
const struct dpll_params dpll_ddr = {
		266, OSC-1, 1, -1, -1, -1, -1};
const struct dpll_params dpll_ddr_evm_sk = {
		303, OSC-1, 1, -1, -1, -1, -1};
const struct dpll_params dpll_ddr_bone_black = {
		400, OSC-1, 1, -1, -1, -1, -1};

void am33xx_spl_board_init(void)
{
	int usb_cur_lim;
	int mpu_vdd;
	if (i2c_probe(TPS65217_CHIP_PM))
		return;
	/*
	 * Override what we have detected since we know if we have
	 * a Beaglebone Black it supports 1GHz.
	 */
	dpll_mpu_opp100.m = MPUPLL_M_1000;
	/*
	 * Increase USB current limit to 1300mA or 1800mA and set
	 * the MPU voltage controller as needed.
	 */
	usb_cur_lim = TPS65217_USB_INPUT_CUR_LIMIT_1800MA;
	mpu_vdd = TPS65217_DCDC_VOLT_SEL_1325MV;
	if (tps65217_reg_write(TPS65217_PROT_LEVEL_NONE,
				TPS65217_POWER_PATH,
				usb_cur_lim,
				TPS65217_USB_INPUT_CUR_LIMIT_MASK))
		puts("tps65217_reg_write failure\n");

	/* Set DCDC3 (CORE) voltage to 1.125V */
	if (tps65217_voltage_update(TPS65217_DEFDCDC3,
				TPS65217_DCDC_VOLT_SEL_1125MV)) {
		puts("tps65217_voltage_update failure\n");
		return;
	}

	/* Set CORE Frequencies to OPP100 */
	do_setup_dpll(&dpll_core_regs, &dpll_core_opp100);

	/* Set DCDC2 (MPU) voltage */
	if (tps65217_voltage_update(TPS65217_DEFDCDC2, mpu_vdd)) {
		puts("tps65217_voltage_update failure\n");
		return;
	}
	/*
	 * Set LDO3, LDO4 output voltage to 3.3V for Beaglebone.
	 * Set LDO3 to 1.8V and LDO4 to 3.3V for Beaglebone Black.
	 */
	if (tps65217_reg_write(TPS65217_PROT_LEVEL_2,
				TPS65217_DEFLS1,
				TPS65217_LDO_VOLTAGE_OUT_1_8,
				TPS65217_LDO_MASK))
		puts("tps65217_reg_write failure\n");
	if (tps65217_reg_write(TPS65217_PROT_LEVEL_2,
				TPS65217_DEFLS2,
				TPS65217_LDO_VOLTAGE_OUT_3_3,
				TPS65217_LDO_MASK))
		puts("tps65217_reg_write failure\n");
	/* Set MPU Frequency to what we detected now that voltages are set */
	do_setup_dpll(&dpll_mpu_regs, &dpll_mpu_opp100);
}

const struct dpll_params *get_dpll_ddr_params(void)
{
	return &dpll_ddr_bone_black;
}

void set_uart_mux_conf(void)
{
#if CONFIG_CONS_INDEX == 1
	enable_uart0_pin_mux();
#elif CONFIG_CONS_INDEX == 2
	enable_uart1_pin_mux();
#elif CONFIG_CONS_INDEX == 3
	enable_uart2_pin_mux();
#elif CONFIG_CONS_INDEX == 4
	enable_uart3_pin_mux();
#elif CONFIG_CONS_INDEX == 5
	enable_uart4_pin_mux();
#elif CONFIG_CONS_INDEX == 6
	enable_uart5_pin_mux();
#endif
}

void set_mux_conf_regs(void)
{
	enable_board_pin_mux();
}
const struct ctrl_ioregs ioregs_bonelt = {
	.cm0ioctl		= MT41K256M16HA125E_IOCTRL_VALUE,
	.cm1ioctl		= MT41K256M16HA125E_IOCTRL_VALUE,
	.cm2ioctl		= MT41K256M16HA125E_IOCTRL_VALUE,
	.dt0ioctl		= MT41K256M16HA125E_IOCTRL_VALUE,
	.dt1ioctl		= MT41K256M16HA125E_IOCTRL_VALUE,
};

const struct ctrl_ioregs ioregs_evm15 = {
	.cm0ioctl		= MT41J512M8RH125_IOCTRL_VALUE,
	.cm1ioctl		= MT41J512M8RH125_IOCTRL_VALUE,
	.cm2ioctl		= MT41J512M8RH125_IOCTRL_VALUE,
	.dt0ioctl		= MT41J512M8RH125_IOCTRL_VALUE,
	.dt1ioctl		= MT41J512M8RH125_IOCTRL_VALUE,
};

const struct ctrl_ioregs ioregs = {
	.cm0ioctl		= MT47H128M16RT25E_IOCTRL_VALUE,
	.cm1ioctl		= MT47H128M16RT25E_IOCTRL_VALUE,
	.cm2ioctl		= MT47H128M16RT25E_IOCTRL_VALUE,
	.dt0ioctl		= MT47H128M16RT25E_IOCTRL_VALUE,
	.dt1ioctl		= MT47H128M16RT25E_IOCTRL_VALUE,
};

void sdram_init(void)
{
	config_ddr(400, &ioregs_bonelt,
			&ddr3_beagleblack_data,
			&ddr3_beagleblack_cmd_ctrl_data,
			&ddr3_beagleblack_emif_reg_data, 0);
}
#endif

/*
 * Basic board specific setup.  Pinmux has been handled already.
 */
int board_init(void)
{
#if defined(CONFIG_HW_WATCHDOG)
	hw_watchdog_init();
#endif

	gd->bd->bi_boot_params = CONFIG_SYS_SDRAM_BASE + 0x100;
	return 0;
}
