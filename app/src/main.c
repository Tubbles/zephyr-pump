#include "led.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

#include <app_version.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

int main(void)
{
	/*
	 * Both are static: the timer lives inside app_led and keeps firing after
	 * main returns, dereferencing app_led and led_spec from the ISR. Stack
	 * storage would be reclaimed on return and the timer would use freed memory.
	 */
	static struct led_dt_spec led_spec = LED_DT_SPEC_GET(DT_NODELABEL(yellow_led));
	static app_led_t app_led = {.config = {
					    .spec = &led_spec,
					    .blink_period_us = 1000000ull,
				    }};

	LOG_INF("zephyr-pump v%s starting", APP_VERSION_STRING);

	(void)led_start(&app_led);

	LOG_INF("started");

	return 0;
}
