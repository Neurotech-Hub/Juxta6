#include "juxta_antenna.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(juxta_antenna, LOG_LEVEL_INF);

#if !DT_NODE_EXISTS(DT_NODELABEL(sky13348))
#error "juxta_antenna requires sky13348"
#endif

static const struct gpio_dt_spec ant_v1 = GPIO_DT_SPEC_GET(DT_NODELABEL(sky13348), v1_gpios);
static const struct gpio_dt_spec ant_v2 = GPIO_DT_SPEC_GET(DT_NODELABEL(sky13348), v2_gpios);
static atomic_t cur_ant = ATOMIC_INIT(1);

int juxta_antenna_select(uint8_t ant)
{
	int err;

	if (ant == 1U) {
		err = gpio_pin_set_dt(&ant_v1, 1);
		if (err) {
			return err;
		}
		err = gpio_pin_set_dt(&ant_v2, 0);
		if (err) {
			return err;
		}
		atomic_set(&cur_ant, 1);
	} else if (ant == 2U) {
		err = gpio_pin_set_dt(&ant_v2, 1);
		if (err) {
			return err;
		}
		err = gpio_pin_set_dt(&ant_v1, 0);
		if (err) {
			return err;
		}
		atomic_set(&cur_ant, 2);
	} else {
		return -EINVAL;
	}

	return 0;
}

int juxta_antenna_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&ant_v1) || !gpio_is_ready_dt(&ant_v2)) {
		LOG_ERR("antenna GPIO not ready (hog-delete overlay?)");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&ant_v1, GPIO_OUTPUT_ACTIVE);
	if (err) {
		return err;
	}
	err = gpio_pin_configure_dt(&ant_v2, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}

	return juxta_antenna_select(1);
}

uint8_t juxta_antenna_current(void)
{
	return (uint8_t)atomic_get(&cur_ant);
}
