/*
 * TEMT6200FX01 light sample for JXV vitals.
 *
 * Collector is VDD, emitter is LIGHT_SENSE (P1.14 / AIN7). A 2.7 kΩ resistor
 * from LIGHT_SENSE goes to the RV2C010UNT2L drain; its gate is LHT_SNS_CTRL
 * (P2.06) and its source is GND. The gate is high only for the sample.
 *
 * lux = JUXTA_LIGHT_LUX_PER_MV * mV, with mV clamped at 0. Calibration
 * record: applications/tag-light/README.md.
 */

#include "juxta_light.h"

#include <errno.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(juxta_light, LOG_LEVEL_INF);

#define LIGHT_SETTLE_MS 10

static const struct adc_dt_spec light_adc = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);
static const struct gpio_dt_spec lht_sns_ctrl = GPIO_DT_SPEC_GET(DT_NODELABEL(lht_sns_ctrl), gpios);

static int16_t sample_buf;
static bool ready;
static bool calibrated;

int juxta_light_init(void)
{
	int err;

	if (ready) {
		return 0;
	}

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
		LOG_ERR("light ADC not ready");
		return -ENODEV;
	}

	err = adc_channel_setup_dt(&light_adc);
	if (err) {
		LOG_ERR("light adc_channel_setup_dt (%d)", err);
		return err;
	}

	ready = true;
	LOG_INF("light ADC ready (channel %u)", light_adc.channel_id);
	return 0;
}

int juxta_light_read_lux(uint32_t *lux)
{
	struct adc_sequence sequence = {
		.buffer = &sample_buf,
		.buffer_size = sizeof(sample_buf),
	};
	int err;
	int32_t mv;

	if (lux == NULL) {
		return -EINVAL;
	}
	if (!ready) {
		return -EAGAIN;
	}

	err = gpio_pin_set_dt(&lht_sns_ctrl, 1);
	if (err) {
		return err;
	}

	k_sleep(K_MSEC(LIGHT_SETTLE_MS));

	err = adc_sequence_init_dt(&light_adc, &sequence);
	if (err) {
		goto out;
	}

	sequence.calibrate = !calibrated;
	err = adc_read_dt(&light_adc, &sequence);
	if (err) {
		goto out;
	}
	calibrated = true;

	mv = sample_buf;
	err = adc_raw_to_millivolts_dt(&light_adc, &mv);
	if (err) {
		goto out;
	}

	if (mv < 0) {
		mv = 0;
	}
	*lux = (uint32_t)((JUXTA_LIGHT_LUX_PER_MV * (float)mv) + 0.5f);

out:
	(void)gpio_pin_set_dt(&lht_sns_ctrl, 0);
	return err;
}
