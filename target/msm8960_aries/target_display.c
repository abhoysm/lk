/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 * Copyright (c) 2011-2014, Xiaomi Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <debug.h>
#include <msm_panel.h>
#include <dev/pm8921.h>
#include <board.h>
#include <mdp4.h>
#include <target/display.h>
#include <target/board.h>
#include <gsbi.h>
#include <i2c_qup.h>
#include <platform/iomap.h>

static struct msm_fb_panel_data panel;
static uint8_t display_enable;

extern int msm_display_init(struct msm_fb_panel_data *pdata);
extern int msm_display_on();
extern int msm_display_off();

static int apq8064_lvds_panel_power(int enable)
{
	if (enable) {
		/* Enable LVS7 */
		pm8921_low_voltage_switch_enable(lvs_7);
		/* Set and enabale LDO2 1.2V for  VDDA_LVDS_PLL*/
		pm8921_ldo_set_voltage(LDO_2, LDO_VOLTAGE_1_2V);

		/* Enable Ext 3.3V -  MSM GPIO 77*/
		/* TODO: IS this really needed? This wasn't even implemented correctly.
		 * GPIO enable was not happening.
		 */
		apq8064_ext_3p3V_enable();

		apq8064_display_gpio_init();

		/* Configure PMM MPP  3*/
		pm8921_mpp_set_digital_output(mpp_3);
	}

	return 0;
}

static int apq8064_lvds_clock(int enable)
{
	if (enable)
		mdp_clock_init();

	return 0;
}

#define PM8921_GPIO_LCD_DCDC_EN         11
#define PM8921_GPIO_PANEL_RESET         25
#define PM8921_GPIO_PANEL_ID            12

int panel_id_detection()
{
	unsigned int lcd_id_det = 2;
	lcd_id_det = pmic8921_gpio_get(PM8921_GPIO_PANEL_ID);
	return lcd_id_det;
}

static int fusion3_mtp_panel_power(int enable)
{
	unsigned int lcd_id_det = 2;
	if (enable) {
		/* Set and enabale LDO2 1.2V for  VDDA_MIPI_DSI0/1_PLL */
		pm8921_ldo_set_voltage(LDO_2, LDO_VOLTAGE_1_2V);

		mi_display_gpio_init();

		/* Initial condition */
		pmic8921_gpio_set(PM8921_GPIO_PANEL_RESET, 0);
		pmic8921_gpio_set(PM8921_GPIO_LCD_DCDC_EN, 0);
		pm8921_ldo_clear_voltage(LDO_23);
		mdelay(8);

		/* Enable LVS7 */
		pm8921_low_voltage_switch_enable(lvs_7);
		pm8921_ldo_set_voltage(LDO_23, LDO_VOLTAGE_1_8V);
		mdelay(10);

		/* Enable VSP VSN */
		pmic8921_gpio_set(PM8921_GPIO_LCD_DCDC_EN, 1);
		mdelay(8);

		/* Reset */
		pmic8921_gpio_set(PM8921_GPIO_PANEL_RESET, 1);
		mdelay(3);

		lcd_id_det = pmic8921_gpio_get(PM8921_GPIO_PANEL_ID);
	} else {
		if (!target_cont_splash_screen()) {
			/* Reset down */
			pmic8921_gpio_set(PM8921_GPIO_PANEL_RESET, 0);

			/* Disable VSP VSN */
			pmic8921_gpio_set(PM8921_GPIO_LCD_DCDC_EN, 0);
			mdelay(8);

			/* Disable 1V8 */
			pm8921_ldo_clear_voltage(LDO_23);
		}
	}

	return 0;
}

static int fusion3_mtp_clock(int enable)
{
	if (enable) {
		mdp_clock_init();
		mmss_clock_init();
	} else if(!target_cont_splash_screen()) {
		mmss_clock_disable();
	}

	return 0;
}

static void msm8960_backlight_on(void)
{
	struct pm8921_gpio backlight_pwm = {
		.direction = PM_GPIO_DIR_OUT,
		.output_buffer = 0,
		.output_value = 0,
		.pull = PM_GPIO_PULL_NO,
		.vin_sel = 2,
		.out_strength = PM_GPIO_STRENGTH_HIGH,
		.function = PM_GPIO_FUNC_1,
		.inv_int_pol = 0,
	};

	int rc = pm8921_gpio_config(PM_GPIO(24), &backlight_pwm);
	if (rc)
		dprintf(CRITICAL, "FAIL pm8921_gpio_config(): rc=%d.\n", rc);
}

#define PM8921_GPIO_BL_LED_EN_MITWOA           22
#define PM8921_GPIO_BL_LED_EN_MITWO           13
#define LM3530_I2C_ADDR                 (0x38)

void panel_backlight_on_mitwoa(unsigned int on)
{
	int ret = 0;
	static struct qup_i2c_dev *lm3530_dev = NULL;

	if (on) {
		lm3530_dev = qup_i2c_init(GSBI_ID_10, 100000, 24000000);

		pmic8921_gpio_set(PM8921_GPIO_BL_LED_EN_MITWOA, 1);

		ret += i2c_write(lm3530_dev, LM3530_I2C_ADDR, 0x10, 0x17);
		ret += i2c_write(lm3530_dev, LM3530_I2C_ADDR, 0x20, 0x00);
		ret += i2c_write(lm3530_dev, LM3530_I2C_ADDR, 0x30, 0x00);
		ret += i2c_write(lm3530_dev, LM3530_I2C_ADDR, 0xA0, 0x1F);
	} else {
		ret += i2c_write(lm3530_dev, LM3530_I2C_ADDR, 0x10, 0x00);
		pmic8921_gpio_set(PM8921_GPIO_BL_LED_EN_MITWOA, 0);
		lm3530_dev = NULL;
	}
	if (ret) {
		dprintf(CRITICAL, "panel_backlight_on: ret %d\n", ret);
	}
}

void panel_backlight_on_mitwo(unsigned int on)
{
	int ret = 0;
	static struct qup_i2c_dev *lm3530_dev = NULL;

	if (on) {
		lm3530_dev = qup_i2c_init(GSBI_ID_1, 100000, 24000000);

		pmic8921_gpio_set(PM8921_GPIO_BL_LED_EN_MITWO, 1);

		ret += i2c_write(lm3530_dev, LM3530_I2C_ADDR, 0x10, 0x17);
		ret += i2c_write(lm3530_dev, LM3530_I2C_ADDR, 0x20, 0x00);
		ret += i2c_write(lm3530_dev, LM3530_I2C_ADDR, 0x30, 0x00);
		ret += i2c_write(lm3530_dev, LM3530_I2C_ADDR, 0xA0, 0x1F);
	} else {
		ret += i2c_write(lm3530_dev, LM3530_I2C_ADDR, 0x10, 0x00);
		pmic8921_gpio_set(PM8921_GPIO_BL_LED_EN_MITWO, 0);
		lm3530_dev = NULL;
	}
	if (ret) {
		dprintf(CRITICAL, "panel_backlight_on: ret %d\n", ret);
	}
}

/* Pull DISP_RST_N high to get panel out of reset */
static void msm8960_mipi_panel_reset(void)
{
	struct pm8921_gpio gpio43_param = {
		.direction = PM_GPIO_DIR_OUT,
		.output_buffer = 0,
		.output_value = 1,
		.pull = PM_GPIO_PULL_UP_30,
		.vin_sel = 2,
		.out_strength = PM_GPIO_STRENGTH_HIGH,
		.function = PM_GPIO_FUNC_PAIRED,
		.inv_int_pol = 0,
		.disable_pin = 0,
	};
	pm8921_gpio_config(PM_GPIO(43), &gpio43_param);
}

static int msm8960_mipi_panel_clock(int enable)
{
	if (enable) {
		mdp_clock_init();
		mmss_clock_init();
	} else if(!target_cont_splash_screen()) {
			mmss_clock_disable();
	}

	return 0;
}

static int mpq8064_hdmi_panel_clock(int enable)
{
	if (enable)
		mdp_clock_init();

	hdmi_app_clk_init(enable);

	return 0;
}

static int mpq8064_hdmi_panel_power(int enable)
{
	if (enable)
		hdmi_power_init();

	return 0;
}

static int msm8960_liquid_mipi_panel_clock(int enable)
{
	if (enable) {
		mdp_clock_init();
		liquid_mmss_clock_init(); /* 240 MHZ MIPI-DSI clk */
	} else if(!target_cont_splash_screen()) {
			mmss_clock_disable();
	}

	return 0;
}

static int msm8960_mipi_panel_power(int enable)
{
	if (enable) {
		/* Turn on LDO8 for lcd1 mipi vdd */
		pm8921_ldo_set_voltage(LDO_8, LDO_VOLTAGE_3_0V);

		pm8921_ldo_set_voltage(LDO_29, LDO_VOLTAGE_1_8V);
		/* Turn on LDO23 for lcd1 mipi vddio */
		pm8921_ldo_set_voltage(LDO_23, LDO_VOLTAGE_1_8V);

		/* Turn on LDO2 for vdda_mipi_dsi */
		pm8921_ldo_set_voltage(LDO_2, LDO_VOLTAGE_1_2V);

		mdelay(3);
		msm8960_mipi_panel_reset();
		mdelay(5);
	}

	return 0;
}

#define	PM_GPIO_VIN_VPH			0 /* 3v ~ 4.4v */
#define	PM_GPIO_VIN_BB			1 /* ~3.3v */
#define	PM_GPIO_VIN_S4			2 /* 1.8v */
#define	PM_GPIO_VIN_L15			3

static int msm8960_liquid_mipi_panel_power(int enable)
{
	if (enable) {
		static int gpio17, gpio21, gpio43 ;
		int rc;

		struct pm8921_gpio gpio_config = {
			.direction = PM_GPIO_DIR_OUT,
			.output_buffer = 0,
			.output_value = 1,
			.pull = PM_GPIO_PULL_NO,
			.vin_sel = PM_GPIO_VIN_S4,
			.out_strength = PM_GPIO_STRENGTH_HIGH,
			.function = PM_GPIO_FUNC_NORMAL,
			.inv_int_pol = 0,
			.disable_pin = 0,
		};

		/* Note: PWM is controlled by PM-GPIO#24 */
		gpio17 = PM_GPIO(17); /* ext_3p3v */
		gpio21 = PM_GPIO(21); /* disp power enable_n , vin=VPH-PWR */
		gpio43 = PM_GPIO(43); /* Displays Enable (rst_n) */

		gpio_config.output_value = 1;
		rc = pm8921_gpio_config(gpio17, &gpio_config);
		mdelay(100);
		gpio_config.output_value = 0;
		/* disp disable (resx=0) */
		rc = pm8921_gpio_config(gpio43, &gpio_config);
		mdelay(100);
		gpio_config.output_value = 0;
		gpio_config.vin_sel = PM_GPIO_VIN_VPH; /* VPH_PWR */
		/* disp power enable_n */
		rc = pm8921_gpio_config(gpio21, &gpio_config);
		mdelay(100);
		gpio_config.output_value = 1;
		gpio_config.vin_sel = PM_GPIO_VIN_S4;
		/* disp enable */
		rc = pm8921_gpio_config(gpio43, &gpio_config);
		mdelay(100);

		pm8921_low_voltage_switch_enable(lvs_4); /* S4 1.8 V */

		/* Turn on LDO2 for vdda_mipi_dsi */
		pm8921_ldo_set_voltage(LDO_2, LDO_VOLTAGE_1_2V);

		msm8960_backlight_on();
	}

	return 0;
}

void target_display_init(const char *panel_name)
{
	int target_id = board_target_id();

	dprintf(INFO, "display_init(),target_id=%d.\n", target_id);

	switch (target_id) {
	case LINUX_MACHTYPE_8960_LIQUID:
		mipi_chimei_video_wxga_init(&(panel.panel_info));
		/*
		 * mipi_chimei_wxga panel not supported yet in LK.
		 * However, MIPI clocks and power should be set in LK.
		 */
		panel.clk_func = msm8960_liquid_mipi_panel_clock;
		panel.power_func = msm8960_liquid_mipi_panel_power;
		panel.fb.base = MIPI_FB_ADDR;
		panel.fb.width =  panel.panel_info.xres;
		panel.fb.height =  panel.panel_info.yres;
		panel.fb.stride =  panel.panel_info.xres;
		panel.fb.bpp =  panel.panel_info.bpp;
		panel.fb.format = FB_FORMAT_RGB888;
		panel.mdp_rev = MDP_REV_44;
		break;
	case LINUX_MACHTYPE_8064_CDP:
		lvds_chimei_wxga_init(&(panel.panel_info));
		panel.clk_func = apq8064_lvds_clock;
		panel.power_func = apq8064_lvds_panel_power;
		panel.fb.base = 0x80B00000;
		panel.fb.width =  panel.panel_info.xres;
		panel.fb.height =  panel.panel_info.yres;
		panel.fb.stride =  panel.panel_info.xres;
		panel.fb.bpp =  panel.panel_info.bpp;
		panel.fb.format = FB_FORMAT_RGB888;
		panel.mdp_rev = MDP_REV_44;
		break;
	case LINUX_MACHTYPE_8064_MTP:
	case LINUX_MACHTYPE_8064_MITWO:
		mipi_hitachi_cmd_hd720p_init(&(panel.panel_info));
		panel.clk_func = fusion3_mtp_clock;
		panel.power_func = fusion3_mtp_panel_power;
		panel.fb.base = 0x89000000;
		panel.fb.width =  panel.panel_info.xres;
		panel.fb.height =  panel.panel_info.yres;
		panel.fb.stride =  panel.panel_info.xres;
		panel.fb.bpp =  panel.panel_info.bpp;
		panel.fb.format = FB_FORMAT_RGB888;
		panel.mdp_rev = MDP_REV_44;
		break;
	case LINUX_MACHTYPE_8960_CDP:
	case LINUX_MACHTYPE_8960_MITWOA:
		mipi_lgd_cmd_hd720p_init(&(panel.panel_info));
		panel.clk_func = msm8960_mipi_panel_clock;
		panel.power_func = msm8960_mipi_panel_power;
		panel.fb.base = 0x89000000;
		panel.fb.width =  panel.panel_info.xres;
		panel.fb.height =  panel.panel_info.yres;
		panel.fb.stride =  panel.panel_info.xres;
		panel.fb.bpp =  panel.panel_info.bpp;
		panel.fb.format = FB_FORMAT_RGB888;
		panel.mdp_rev = MDP_REV_44;
		break;
	case LINUX_MACHTYPE_8960_MTP:
	case LINUX_MACHTYPE_8960_FLUID:
		mipi_toshiba_video_wsvga_init(&(panel.panel_info));
		panel.clk_func = msm8960_mipi_panel_clock;
		panel.power_func = msm8960_mipi_panel_power;
		panel.fb.base = 0x89000000;
		panel.fb.width =  panel.panel_info.xres;
		panel.fb.height =  panel.panel_info.yres;
		panel.fb.stride =  panel.panel_info.xres;
		panel.fb.bpp =  panel.panel_info.bpp;
		panel.fb.format = FB_FORMAT_RGB888;
		panel.mdp_rev = MDP_REV_42;
		break;
	case LINUX_MACHTYPE_8064_MPQ_CDP:
	case LINUX_MACHTYPE_8064_MPQ_HRD:
	case LINUX_MACHTYPE_8064_MPQ_DTV:
		hdmi_msm_panel_init(&panel.panel_info);

		panel.clk_func   = mpq8064_hdmi_panel_clock;
		panel.power_func = mpq8064_hdmi_panel_power;
		panel.fb.base    = 0x89000000;
		panel.fb.width   = panel.panel_info.xres;
		panel.fb.height  = panel.panel_info.yres;
		panel.fb.stride  = panel.panel_info.xres;
		panel.fb.bpp     = panel.panel_info.bpp;
		panel.fb.format  = FB_FORMAT_RGB565;
		panel.mdp_rev    = MDP_REV_44;

		hdmi_set_fb_addr(panel.fb.base);
		break;
	default:
		return;
	};

	if (msm_display_init(&panel)) {
		dprintf(CRITICAL, "Display init failed!\n");
		return;
	}

	mipi_dsi_cmd_trigger(&panel);

	mdelay(34);
	if (board_target_id() == LINUX_MACHTYPE_8064_MITWO || (board_target_id() == LINUX_MACHTYPE_8064_MTP))
		panel_backlight_on_mitwo(1);
	else
		panel_backlight_on_mitwoa(1);

	display_enable = 1;
}

void target_display_shutdown(void)
{
	if (display_enable) {
		msm_display_off();
	}
}
