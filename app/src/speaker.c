/*
 * Speaker tone on the XIAO ESP32-C6 header (D3 / GPIO21), driven by a hardware
 * LEDC PWM channel (channel 1, set in app.overlay).
 *
 * A tone is a 50% square wave at the requested frequency; the LEDC peripheral
 * generates it with no CPU work. A one-shot work item silences the tone after
 * the requested duration. The work runs in the system workqueue (thread
 * context), which is where pwm_set_dt is safe to call.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/shell/shell.h>
#include <errno.h>
#include <stdlib.h>

static const struct pwm_dt_spec speaker = PWM_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), speaker);

/* Silence the tone: 0% duty produces no output. The period is irrelevant at zero
 * pulse width, so reuse the devicetree default.
 */
static void speaker_silence(struct k_work *work)
{
	ARG_UNUSED(work);
	pwm_set_dt(&speaker, speaker.period, 0);
}

K_WORK_DELAYABLE_DEFINE(silence_work, speaker_silence);

static int cmd_tone(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	if (!pwm_is_ready_dt(&speaker)) {
		shell_error(sh, "speaker PWM not ready");
		return -ENODEV;
	}

	long centi_hz = strtol(argv[1], NULL, 10);
	long duration_ms = strtol(argv[2], NULL, 10);

	if (centi_hz <= 0 || duration_ms <= 0) {
		shell_error(sh, "usage: tone <centi-hz> <ms>  (both > 0)");
		return -EINVAL;
	}

	/* freq = centi_hz / 100 Hz, so the period is 1e9 / freq ns = 1e11 / centi_hz
	 * ns. Drive a 50% square wave (pulse = half the period).
	 */
	uint32_t period_ns = (uint32_t)(100000000000ULL / (uint64_t)centi_hz);

	int error = pwm_set_dt(&speaker, period_ns, period_ns / 2U);

	if (error) {
		shell_error(sh, "PWM set failed: %d", error);
		return error;
	}

	k_work_reschedule(&silence_work, K_MSEC(duration_ms));

	shell_print(sh, "tone %ld.%02ld Hz for %ld ms", centi_hz / 100, centi_hz % 100,
		    duration_ms);
	return 0;
}

SHELL_CMD_ARG_REGISTER(tone, NULL,
		       "Play a tone on the speaker (D3, LEDC PWM).\n"
		       "Usage: tone <centi-hz> <ms>",
		       cmd_tone, 3, 0);

static int speaker_init(void)
{
	if (!pwm_is_ready_dt(&speaker)) {
		return -ENODEV;
	}
	return pwm_set_dt(&speaker, speaker.period, 0);
}

SYS_INIT(speaker_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
