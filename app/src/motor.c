/*
 * A4988 stepper driver control on the J4 header (see DESIGN.md):
 *   STEP = GPIO11 = PWM2 channel 1  -- the step pulse train,
 *   DIR  = GPIO9  (plain output)    -- rotation direction,
 *   EN   = GPIO10 (plain output)    -- active-low driver enable.
 *
 * Registers a "motor" shell command group for bench debugging. The PWM, DIR and
 * EN references come from the zephyr,user node in app.overlay.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/shell/shell.h>
#include <errno.h>
#include <stdlib.h>

#define MOTOR_NODE DT_PATH(zephyr_user)

static const struct pwm_dt_spec step_pwm = PWM_DT_SPEC_GET(MOTOR_NODE);
static const struct gpio_dt_spec dir_line = GPIO_DT_SPEC_GET(MOTOR_NODE, dir_gpios);
static const struct gpio_dt_spec enable_line = GPIO_DT_SPEC_GET(MOTOR_NODE, en_gpios);

/* Last commanded state; the output pins can't be read back, so track it here so
 * "motor status" has something to report.
 */
static struct {
	bool enabled;
	int dir_level;
	uint32_t rate_hz; /* 0 when the step train is stopped */
} state;

/* Drive STEP as a 50% square wave at the given rate. The A4988 only needs a
 * ~1 us minimum high time, so half the period is always wide enough.
 */
static int start_train(uint32_t rate_hz)
{
	uint32_t period_ns = 1000000000U / rate_hz;

	return pwm_set_dt(&step_pwm, period_ns, period_ns / 2U);
}

/* Park STEP low: 0% duty produces no pulses. The period is irrelevant at zero
 * pulse width, so reuse the devicetree default.
 */
static int stop_train(void)
{
	return pwm_set_dt(&step_pwm, step_pwm.period, 0);
}

/* Finite-move timeout: parks STEP after the requested number of steps. Runs in
 * the system workqueue (thread context), so pwm_set_dt is safe here.
 */
static void stop_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	stop_train();
	state.rate_hz = 0;
}

K_WORK_DELAYABLE_DEFINE(stop_work, stop_work_handler);

static int cmd_enable(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int error = gpio_pin_set_dt(&enable_line, 1);

	if (error) {
		shell_error(sh, "EN set failed: %d", error);
		return error;
	}
	state.enabled = true;
	shell_print(sh, "motor enabled (EN active, holding torque on)");
	return 0;
}

static int cmd_disable(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int error = gpio_pin_set_dt(&enable_line, 0);

	if (error) {
		shell_error(sh, "EN set failed: %d", error);
		return error;
	}
	state.enabled = false;
	shell_print(sh, "motor disabled (EN inactive, coasting)");
	return 0;
}

static int cmd_dir(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	long level = strtol(argv[1], NULL, 10);

	if (level != 0 && level != 1) {
		shell_error(sh, "usage: motor dir <0|1>");
		return -EINVAL;
	}

	int error = gpio_pin_set_dt(&dir_line, (int)level);

	if (error) {
		shell_error(sh, "DIR set failed: %d", error);
		return error;
	}
	state.dir_level = (int)level;
	shell_print(sh, "DIR = %ld", level);
	return 0;
}

static int cmd_run(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	long rate_hz = strtol(argv[1], NULL, 10);

	if (rate_hz <= 0) {
		shell_error(sh, "usage: motor run <hz>  (hz > 0)");
		return -EINVAL;
	}

	k_work_cancel_delayable(&stop_work);

	int error = start_train((uint32_t)rate_hz);

	if (error) {
		shell_error(sh, "PWM set failed: %d", error);
		return error;
	}
	state.rate_hz = (uint32_t)rate_hz;
	shell_print(sh, "stepping at %ld Hz", rate_hz);
	if (!state.enabled) {
		shell_warn(sh, "driver disabled; run 'motor enable' to move");
	}
	return 0;
}

static int cmd_steps(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	long count = strtol(argv[1], NULL, 10);
	long rate_hz = strtol(argv[2], NULL, 10);

	if (count <= 0 || rate_hz <= 0) {
		shell_error(sh, "usage: motor steps <count> <hz>  (both > 0)");
		return -EINVAL;
	}

	k_work_cancel_delayable(&stop_work);

	int error = start_train((uint32_t)rate_hz);

	if (error) {
		shell_error(sh, "PWM set failed: %d", error);
		return error;
	}
	state.rate_hz = (uint32_t)rate_hz;

	/* Stop after count whole periods. Timer granularity makes this accurate to
	 * about +/-1 step, which is fine for bench debugging.
	 */
	uint64_t duration_us = (uint64_t)count * 1000000U / (uint64_t)rate_hz;

	k_work_reschedule(&stop_work, K_USEC(duration_us));

	shell_print(sh, "%ld steps at %ld Hz", count, rate_hz);
	if (!state.enabled) {
		shell_warn(sh, "driver disabled; run 'motor enable' to move");
	}
	return 0;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_work_cancel_delayable(&stop_work);

	int error = stop_train();

	if (error) {
		shell_error(sh, "PWM set failed: %d", error);
		return error;
	}
	state.rate_hz = 0;
	shell_print(sh, "stopped");
	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "enabled: %s", state.enabled ? "yes" : "no");
	shell_print(sh, "dir:     %d", state.dir_level);
	if (state.rate_hz) {
		shell_print(sh, "step:    running at %u Hz", state.rate_hz);
	} else {
		shell_print(sh, "step:    stopped");
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	motor_cmds,
	SHELL_CMD_ARG(enable, NULL, "Energize the driver (EN active).", cmd_enable, 1, 0),
	SHELL_CMD_ARG(disable, NULL, "De-energize the driver (EN inactive, motor coasts).",
		      cmd_disable, 1, 0),
	SHELL_CMD_ARG(dir, NULL, "Set the DIR line level.\nUsage: motor dir <0|1>", cmd_dir, 2, 0),
	SHELL_CMD_ARG(run, NULL, "Step continuously.\nUsage: motor run <hz>", cmd_run, 2, 0),
	SHELL_CMD_ARG(steps, NULL,
		      "Step a fixed count, then stop.\nUsage: motor steps <count> <hz>", cmd_steps,
		      3, 0),
	SHELL_CMD_ARG(stop, NULL, "Stop the step train.", cmd_stop, 1, 0),
	SHELL_CMD_ARG(status, NULL, "Show the last commanded motor state.", cmd_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(motor, &motor_cmds, "A4988 stepper debugging commands", NULL);

static int motor_init(void)
{
	if (!pwm_is_ready_dt(&step_pwm) || !gpio_is_ready_dt(&dir_line) ||
	    !gpio_is_ready_dt(&enable_line)) {
		return -ENODEV;
	}

	int error = gpio_pin_configure_dt(&dir_line, GPIO_OUTPUT_INACTIVE);

	if (error) {
		return error;
	}

	/* Active-low EN: INACTIVE parks the pin high, leaving the driver disabled,
	 * so the motor is free and cool until "motor enable".
	 */
	error = gpio_pin_configure_dt(&enable_line, GPIO_OUTPUT_INACTIVE);
	if (error) {
		return error;
	}

	return stop_train();
}

SYS_INIT(motor_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
