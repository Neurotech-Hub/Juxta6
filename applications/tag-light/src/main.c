/*
 * HIL: TEMT6200FX01 light sense gated by an RV2C010UNT2L.
 *
 * Phototransistor collector is VDD, emitter is LIGHT_SENSE (P1.14 / AIN7).
 * A 2.7 kΩ resistor from LIGHT_SENSE goes to the MOSFET drain. The gate is
 * LHT_SNS_CTRL (P2.06) and the source is GND. Each pass prints one sample
 * with the gate off and one with it on.
 */

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tag_light, LOG_LEVEL_INF);

#define CTRL_SETTLE_MS 10

static const struct adc_dt_spec light_adc = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static const struct gpio_dt_spec lht_sns_ctrl = GPIO_DT_SPEC_GET(DT_NODELABEL(lht_sns_ctrl), gpios);

static int16_t sample_buf;
static bool calibrated;

static int light_read_mv(int32_t *mv)
{
	struct adc_sequence sequence = {
		.buffer = &sample_buf,
		.buffer_size = sizeof(sample_buf),
	};
	int err;
	int32_t val;

	err = adc_sequence_init_dt(&light_adc, &sequence);
	if (err) {
		return err;
	}

	sequence.calibrate = !calibrated;
	err = adc_read_dt(&light_adc, &sequence);
	if (err) {
		return err;
	}
	calibrated = true;

	val = sample_buf;
	err = adc_raw_to_millivolts_dt(&light_adc, &val);
	if (err) {
		return err;
	}

	*mv = val;
	return 0;
}

/* Drive the load FET, wait out the node, and print LIGHT_SENSE. */
static int sample_ctrl(int on)
{
	int err;
	int32_t mv;

	err = gpio_pin_set_dt(&lht_sns_ctrl, on);
	if (err) {
		LOG_ERR("ctrl=%d gpio (%d)", on, err);
		return err;
	}

	k_sleep(K_MSEC(CTRL_SETTLE_MS));

	err = light_read_mv(&mv);
	if (err) {
		LOG_ERR("ctrl=%d light_read (%d)", on, err);
		return err;
	}

	LOG_INF("ctrl=%d light_mv=%d", on, (int)mv);
	return 0;
}

int main(void)
{
	int err;

	LOG_INF("tag-light started (LIGHT_SENSE P1.14, LHT_SNS_CTRL P2.06)");

	if (!gpio_is_ready_dt(&lht_sns_ctrl)) {
		LOG_ERR("LHT_SNS_CTRL GPIO not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&lht_sns_ctrl, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("LHT_SNS_CTRL configure (%d)", err);
		return err;
	}

	if (!adc_is_ready_dt(&light_adc)) {
		LOG_ERR("ADC device not ready");
		return -ENODEV;
	}

	err = adc_channel_setup_dt(&light_adc);
	if (err) {
		LOG_ERR("adc_channel_setup_dt (%d)", err);
		return err;
	}

	while (1) {
		(void)sample_ctrl(0);
		(void)sample_ctrl(1);
		/* Leave the load off between passes. */
		(void)gpio_pin_set_dt(&lht_sns_ctrl, 0);
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
