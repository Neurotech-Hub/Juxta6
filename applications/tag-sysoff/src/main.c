/*
 * Shelf-mode analog: System OFF with BTN1 (sw0) as wake source.
 * On RESET_LOW_POWER_WAKE / OFF wake: green LED 1 s, then re-enter System OFF.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>

LOG_MODULE_REGISTER(tag_sysoff, LOG_LEVEL_INF);

#define WAKE_LED_MS 1000U

#if !DT_NODE_EXISTS(DT_NODELABEL(led1_green))
#error "Board must define led1_green"
#endif

#if !DT_NODE_EXISTS(DT_ALIAS(sw0))
#error "Board must define alias sw0 (BTN1)"
#endif

static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_green), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static void enter_sysoff(void)
{
	int ret;

	(void)gpio_pin_set_dt(&led_g, 0);

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_LEVEL_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Failed to arm BTN1 wake (%d)", ret);
		return;
	}

	LOG_INF("Entering System OFF (press BTN1 to wake)");
	k_sleep(K_MSEC(50));
	sys_poweroff();
}

int main(void)
{
	int ret;
	uint32_t cause = 0U;

	if (!gpio_is_ready_dt(&led_g)) {
		LOG_ERR("led1_green not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("sw0 not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("led1_green configure failed (%d)", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("sw0 configure failed (%d)", ret);
		return ret;
	}

	ret = hwinfo_get_reset_cause(&cause);
	if (ret != 0) {
		LOG_WRN("hwinfo_get_reset_cause failed (%d)", ret);
	} else {
		LOG_INF("reset_cause=0x%08x", cause);
		(void)hwinfo_clear_reset_cause();
	}

	if ((cause & RESET_LOW_POWER_WAKE) != 0U) {
		LOG_INF("Woke from System OFF via BTN1");
		(void)gpio_pin_set_dt(&led_g, 1);
		k_sleep(K_MSEC(WAKE_LED_MS));
		(void)gpio_pin_set_dt(&led_g, 0);
	} else {
		LOG_INF("tag-sysoff cold/other boot — preparing shelf");
	}

	enter_sysoff();

	LOG_ERR("sys_poweroff returned unexpectedly");
	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
