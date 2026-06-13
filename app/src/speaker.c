/*
 * Software-driven speaker on the EDU Shield D2 line (FE310 GPIO18).
 *
 * GPIO18 has no PWM/serial mux (see DESIGN.md), so a tone is a square wave made
 * by toggling the pin from a repeating k_timer, whose expiry handler runs in
 * the system timer interrupt. A second one-shot timer ends the tone after the
 * requested duration and parks the pin low.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/shell/shell.h>
#include <errno.h>
#include <stdlib.h>

static const struct gpio_dt_spec speaker = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), speaker_gpios);

/* Flip the pin once per half-period; runs in the timer ISR. */
static void speaker_toggle(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	gpio_pin_toggle_dt(&speaker);
}

K_TIMER_DEFINE(toggle_timer, speaker_toggle, NULL);

/* End of tone: halt the toggling and leave the pin low. Runs in the timer ISR
 * (k_timer_stop is documented @isr_ok).
 */
static void speaker_silence(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	k_timer_stop(&toggle_timer);
	gpio_pin_set_dt(&speaker, 0);
}

K_TIMER_DEFINE(stop_timer, speaker_silence, NULL);

static int cmd_tone(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	if (!gpio_is_ready_dt(&speaker)) {
		shell_error(sh, "speaker GPIO not ready");
		return -ENODEV;
	}

	long centi_hz = strtol(argv[1], NULL, 10);
	long duration_ms = strtol(argv[2], NULL, 10);

	if (centi_hz <= 0 || duration_ms <= 0) {
		shell_error(sh, "usage: tone <centi-hz> <ms>  (both > 0)");
		return -EINVAL;
	}

	/* Half a square-wave period in ns. freq = centi_hz / 100 Hz, so the full
	 * period is 1e9 / freq ns and a half-period is 5e10 / centi_hz ns.
	 */
	uint64_t half_period_ns = 50000000000ULL / (uint64_t)centi_hz;

	k_timer_start(&toggle_timer, K_NSEC(half_period_ns), K_NSEC(half_period_ns));
	k_timer_start(&stop_timer, K_MSEC(duration_ms), K_NO_WAIT);

	shell_print(sh, "tone %ld.%02ld Hz for %ld ms", centi_hz / 100, centi_hz % 100,
		    duration_ms);
	return 0;
}

SHELL_CMD_ARG_REGISTER(tone, NULL,
		       "Play a software tone on the speaker (D2).\n"
		       "Usage: tone <centi-hz> <ms>",
		       cmd_tone, 3, 0);

static int speaker_init(void)
{
	if (!gpio_is_ready_dt(&speaker)) {
		return -ENODEV;
	}
	return gpio_pin_configure_dt(&speaker, GPIO_OUTPUT_INACTIVE);
}

SYS_INIT(speaker_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
