/*
 * HIL: SAADC internal VDD measurement (CR2032 characterization).
 * Samples idle, then after a short non-connectable adv burst (TX droop check).
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "juxta_vdd.h"

LOG_MODULE_REGISTER(tag_vdd, LOG_LEVEL_INF);

#define IDLE_SAMPLES 5U
#define POST_ADV_SAMPLES 3U

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void log_sample(const char *tag)
{
	int32_t mv = juxta_vdd_read_mv();

	if (mv < 0) {
		LOG_WRN("%s vdd_read failed (%d)", tag, (int)mv);
		return;
	}

	LOG_INF("%s vdd_mv=%d batt_pct~%u (CR2032 estimate)", tag, (int)mv,
		juxta_vdd_mv_to_percent_cr2032(mv));
}

int main(void)
{
	int err;

	LOG_INF("tag-vdd started (SAADC NRF_SAADC_VDD)");

	err = juxta_vdd_init();
	if (err) {
		LOG_ERR("juxta_vdd_init (%d)", err);
		return err;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable (%d)", err);
		return err;
	}

	for (uint32_t i = 0; i < IDLE_SAMPLES; i++) {
		log_sample("idle");
		k_sleep(K_SECONDS(1));
	}

	err = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_WRN("adv start (%d) — continuing without TX droop check", err);
	} else {
		LOG_INF("Advertising 3 s for TX droop sample");
		k_sleep(K_SECONDS(3));
		for (uint32_t i = 0; i < POST_ADV_SAMPLES; i++) {
			log_sample("adv");
			k_sleep(K_MSEC(500));
		}
		(void)bt_le_adv_stop();
	}

	while (1) {
		log_sample("idle");
		k_sleep(K_SECONDS(2));
	}

	return 0;
}
