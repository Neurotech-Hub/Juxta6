#include "juxta_vdd.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(juxta_vdd, LOG_LEVEL_INF);

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "juxta_vdd requires /zephyr,user io-channels pointing at an NRF_SAADC_VDD ADC channel"
#endif

static const struct adc_dt_spec adc_vdd = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static int16_t sample_buf;
static bool ready;

int juxta_vdd_init(void)
{
	int err;

	if (ready) {
		return 0;
	}

	if (!adc_is_ready_dt(&adc_vdd)) {
		LOG_ERR("ADC device not ready");
		return -ENODEV;
	}

	err = adc_channel_setup_dt(&adc_vdd);
	if (err) {
		LOG_ERR("adc_channel_setup_dt (%d)", err);
		return err;
	}

	ready = true;
	LOG_INF("VDD ADC ready (channel %u)", adc_vdd.channel_id);
	return 0;
}

int32_t juxta_vdd_read_mv(void)
{
	int err;
	struct adc_sequence sequence = {
		.buffer = &sample_buf,
		.buffer_size = sizeof(sample_buf),
	};

	if (!ready) {
		return -EAGAIN;
	}

	err = adc_sequence_init_dt(&adc_vdd, &sequence);
	if (err) {
		return err;
	}

	err = adc_read_dt(&adc_vdd, &sequence);
	if (err) {
		return err;
	}

	int32_t val_mv = sample_buf;

	err = adc_raw_to_millivolts_dt(&adc_vdd, &val_mv);
	if (err) {
		return err;
	}

	return val_mv;
}

uint8_t juxta_vdd_mv_to_percent_cr2032(int32_t mv)
{
	/* Rough CR2032 curve: ~3000 mV full, ~2000 mV empty (estimate only). */
	const int32_t full_mv = 3000;
	const int32_t empty_mv = 2000;

	if (mv <= empty_mv) {
		return 0U;
	}
	if (mv >= full_mv) {
		return 100U;
	}

	return (uint8_t)(((mv - empty_mv) * 100) / (full_mv - empty_mv));
}
