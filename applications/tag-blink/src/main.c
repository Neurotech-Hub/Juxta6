#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tag_blink, LOG_LEVEL_DBG);

#define COLOR_PERIOD_MS 500U

#if !DT_NODE_EXISTS(DT_NODELABEL(led1_red)) || !DT_NODE_EXISTS(DT_NODELABEL(led1_green)) ||       \
	!DT_NODE_EXISTS(DT_NODELABEL(led1_blue))
#error "Board must define led1_red, led1_green, led1_blue"
#endif

#if !DT_NODE_EXISTS(DT_ALIAS(sw0))
#error "Board must define alias sw0"
#endif

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_red), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_green), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_blue), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static struct gpio_callback button_cb_data;

static void leds_all_off(void)
{
	(void)gpio_pin_set_dt(&led_r, 0);
	(void)gpio_pin_set_dt(&led_g, 0);
	(void)gpio_pin_set_dt(&led_b, 0);
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	LOG_INF("sw0 pressed");
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

int main(void)
{
	int ret;
	unsigned int color = 0U;

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

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		LOG_ERR("Failed to configure sw0 interrupt (%d)", ret);
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	ret = gpio_add_callback(button.port, &button_cb_data);
	if (ret != 0) {
		LOG_ERR("Failed to add sw0 callback (%d)", ret);
		return ret;
	}

	LOG_INF("tag-blink started (LED1 R->G->B->W cycle %u ms, sw0 logs on press)", COLOR_PERIOD_MS);

	while (1) {
		leds_all_off();
		switch (color % 4U) {
		case 0U:
			(void)gpio_pin_set_dt(&led_r, 1);
			LOG_DBG("LED1 red");
			break;
		case 1U:
			(void)gpio_pin_set_dt(&led_g, 1);
			LOG_DBG("LED1 green");
			break;
		case 2U:
			(void)gpio_pin_set_dt(&led_b, 1);
			LOG_DBG("LED1 blue");
			break;
		default:
			(void)gpio_pin_set_dt(&led_r, 1);
			(void)gpio_pin_set_dt(&led_g, 1);
			(void)gpio_pin_set_dt(&led_b, 1);
			LOG_DBG("LED1 white");
			break;
		}
		color++;
		k_sleep(K_MSEC(COLOR_PERIOD_MS));
	}

	return 0;
}
