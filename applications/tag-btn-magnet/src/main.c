/*
 * BTN1 mimics Juxta5-8 MAG_INT hold semantics (no BLE / System OFF).
 *
 * Hold timings from juxta5-8-prod:
 *   MAGNET_DEBOUNCE_MS = 3000, DFU_HOLD_THRESHOLD_MS = 10000
 *
 * LED1 cues:
 *   held           -> solid red
 *   at 3 s         -> off (commit cue)
 *   release 3-10 s -> green slow blink (wake/sync analog)
 *   hold >= 10 s   -> 3x blink then blue fast blink (DFU analog)
 *   release < 3 s  -> reject, LED off
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tag_btn_magnet, LOG_LEVEL_INF);

#define MAGNET_DEBOUNCE_MS 3000U
#define DFU_HOLD_THRESHOLD_MS 10000U

#define SLOW_ON_MS 50U
#define SLOW_OFF_MS 450U
#define FAST_ON_MS 50U
#define FAST_OFF_MS 50U

#if !DT_NODE_EXISTS(DT_NODELABEL(led1_red)) || !DT_NODE_EXISTS(DT_NODELABEL(led1_green)) ||       \
	!DT_NODE_EXISTS(DT_NODELABEL(led1_blue))
#error "Board must define led1_red, led1_green, led1_blue"
#endif

#if !DT_NODE_EXISTS(DT_ALIAS(sw0))
#error "Board must define alias sw0 (BTN1)"
#endif

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_red), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_green), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_blue), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

enum outcome {
	OUTCOME_IDLE = 0,
	OUTCOME_REJECT,
	OUTCOME_WAKE,
	OUTCOME_DFU,
};

static void leds_all_off(void)
{
	(void)gpio_pin_set_dt(&led_r, 0);
	(void)gpio_pin_set_dt(&led_g, 0);
	(void)gpio_pin_set_dt(&led_b, 0);
}

static void led_solid(const struct gpio_dt_spec *led)
{
	leds_all_off();
	(void)gpio_pin_set_dt(led, 1);
}

static int led_configure(const struct gpio_dt_spec *led, const char *name)
{
	int ret;

	if (!gpio_is_ready_dt(led)) {
		LOG_ERR("%s GPIO not ready", name);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(led, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("Failed to configure %s (%d)", name, ret);
	}

	return ret;
}

static bool button_active(void)
{
	int val = gpio_pin_get_dt(&button);

	return val > 0;
}

static void blink_n(const struct gpio_dt_spec *led, unsigned int times, uint32_t on_ms,
		    uint32_t off_ms)
{
	for (unsigned int i = 0U; i < times; i++) {
		leds_all_off();
		(void)gpio_pin_set_dt(led, 1);
		k_sleep(K_MSEC(on_ms));
		leds_all_off();
		k_sleep(K_MSEC(off_ms));
	}
}

static void pattern_slow_green(void)
{
	leds_all_off();
	(void)gpio_pin_set_dt(&led_g, 1);
	k_sleep(K_MSEC(SLOW_ON_MS));
	leds_all_off();
	k_sleep(K_MSEC(SLOW_OFF_MS));
}

static void pattern_fast_blue(void)
{
	leds_all_off();
	(void)gpio_pin_set_dt(&led_b, 1);
	k_sleep(K_MSEC(FAST_ON_MS));
	leds_all_off();
	k_sleep(K_MSEC(FAST_OFF_MS));
}

static enum outcome wait_hold(int64_t *hold_ms_out)
{
	int64_t start;
	bool commit_cued = false;

	while (!button_active()) {
		k_sleep(K_MSEC(10));
	}

	start = k_uptime_get();
	led_solid(&led_r);
	LOG_INF("BTN1 pressed — solid red (measuring hold)");

	while (button_active()) {
		int64_t elapsed = k_uptime_get() - start;

		if (!commit_cued && elapsed >= (int64_t)MAGNET_DEBOUNCE_MS) {
			leds_all_off();
			commit_cued = true;
			LOG_INF("commit cue at %lld ms — LED off (release for wake, or keep holding for DFU)",
				(long long)elapsed);
		}

		if (elapsed >= (int64_t)DFU_HOLD_THRESHOLD_MS) {
			*hold_ms_out = elapsed;
			return OUTCOME_DFU;
		}

		k_sleep(K_MSEC(10));
	}

	*hold_ms_out = k_uptime_get() - start;
	if (*hold_ms_out < (int64_t)MAGNET_DEBOUNCE_MS) {
		return OUTCOME_REJECT;
	}

	return OUTCOME_WAKE;
}

int main(void)
{
	int ret;
	enum outcome mode = OUTCOME_IDLE;

	ret = led_configure(&led_r, "led1_red");
	if (ret != 0) {
		return ret;
	}
	ret = led_configure(&led_g, "led1_green");
	if (ret != 0) {
		return ret;
	}
	ret = led_configure(&led_b, "led1_blue");
	if (ret != 0) {
		return ret;
	}

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("sw0 GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Failed to configure sw0 (%d)", ret);
		return ret;
	}

	leds_all_off();
	LOG_INF("tag-btn-magnet started");
	LOG_INF("Hold BTN1: <3 s reject | 3-10 s wake (green slow) | >=10 s DFU (blue fast)");

	while (1) {
		if (mode == OUTCOME_IDLE) {
			int64_t hold_ms = 0;
			enum outcome result = wait_hold(&hold_ms);

			LOG_INF("hold_ms=%lld outcome=%s", (long long)hold_ms,
				result == OUTCOME_REJECT ? "REJECT"
				: result == OUTCOME_WAKE ? "WAKE"
							 : "DFU");

			if (result == OUTCOME_REJECT) {
				leds_all_off();
				LOG_INF("rejected false-positive (< %u ms)", MAGNET_DEBOUNCE_MS);
				mode = OUTCOME_IDLE;
			} else if (result == OUTCOME_WAKE) {
				mode = OUTCOME_WAKE;
				LOG_INF("wake/sync analog — green slow blink (press BTN1 to reset)");
			} else {
				blink_n(&led_b, 3U, FAST_ON_MS, FAST_OFF_MS);
				mode = OUTCOME_DFU;
				LOG_INF("DFU analog — blue fast blink (press BTN1 to reset)");
			}
			continue;
		}

		/* Active pattern mode until next BTN1 press resets to idle measure. */
		if (button_active()) {
			/* Wait for release then re-enter measure path. */
			while (button_active()) {
				k_sleep(K_MSEC(10));
			}
			leds_all_off();
			mode = OUTCOME_IDLE;
			LOG_INF("BTN1 pressed in pattern mode — back to hold measure");
			continue;
		}

		if (mode == OUTCOME_WAKE) {
			pattern_slow_green();
		} else {
			pattern_fast_blue();
		}
	}

	return 0;
}
