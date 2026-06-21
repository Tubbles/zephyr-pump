/*
 * Power the XIAO ESP32-C6 RF switch and select the onboard antenna.
 *
 * The board routes both the onboard ceramic antenna and the U.FL connector
 * through an RF switch: enable on GPIO3 (active-low), select on GPIO14
 * (active-high). With the switch unpowered the radio still works through a
 * leakage path, but the antenna is degraded (see docs/LOG.md [antenna]). The
 * upstream board only powers the switch via CONFIG_XIAO_ESP32C6_EXT_ANTENNA,
 * which also forces the external antenna, so drive the pins here to get a
 * powered switch with the onboard antenna. Reuses the board's rf_switch
 * devicetree node, so this stays board-agnostic.
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(antenna, CONFIG_LOG_DEFAULT_LEVEL);

#define RF_SWITCH_NODE DT_NODELABEL(rf_switch)

static const struct gpio_dt_spec rf_switch_enable =
	GPIO_DT_SPEC_GET(RF_SWITCH_NODE, enable_gpios);
static const struct gpio_dt_spec antenna_select = GPIO_DT_SPEC_GET(RF_SWITCH_NODE, select_gpios);

static int antenna_init(void)
{
	if (!gpio_is_ready_dt(&rf_switch_enable) || !gpio_is_ready_dt(&antenna_select)) {
		LOG_ERR("rf_switch GPIOs not ready");
		return -ENODEV;
	}

	/*
	 * enable_gpios is active-low, so OUTPUT_ACTIVE drives it low and powers the
	 * switch; select_gpios is active-high, so OUTPUT_INACTIVE drives it low and
	 * selects the onboard antenna.
	 */
	int ret = gpio_pin_configure_dt(&rf_switch_enable, GPIO_OUTPUT_ACTIVE);
	if (ret) {
		LOG_ERR("RF switch enable failed (%d)", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&antenna_select, GPIO_OUTPUT_INACTIVE);
	if (ret) {
		LOG_ERR("antenna select failed (%d)", ret);
		return ret;
	}

	LOG_INF("RF switch powered, onboard antenna selected");
	return 0;
}

SYS_INIT(antenna_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
