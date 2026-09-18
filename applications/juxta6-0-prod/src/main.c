/*
 * juxta6-0-prod M2: Juxta-style shelf / Hublink sync / dual-antenna RSSI /
 * MX25L3233 NOR CSV (JXS/JXV/JXB) with RTT mirrors. No MCUboot/CS.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include <cmsis_core.h>

#include "ble_service.h"
#include "juxta_antenna.h"
#include "juxta_checkpoint.h"
#include "juxta_id.h"
#include "juxta_log.h"
#include "juxta_motion.h"
#include "juxta_prod.h"
#include "juxta_rtt_log.h"
#include "juxta_settings.h"
#include "juxta_time.h"
#include "juxta_vdd.h"

LOG_MODULE_REGISTER(juxta6_0_prod, LOG_LEVEL_INF);

#define PEER_SLOT_COUNT 8U
#define ANT_DWELL_MS 500U   /* per-antenna dwell inside one scan burst */
#define SCAN_BURST_MS 1000U /* total passive scan wall time (2 × ANT_DWELL) */
#define ADV_BURST_MS 1000U  /* non-connectable adv burst wall time */

BUILD_ASSERT(ANT_DWELL_MS * 2U == SCAN_BURST_MS, "scan burst must be two antenna dwells");


#if !DT_NODE_EXISTS(DT_NODELABEL(led1_red)) || !DT_NODE_EXISTS(DT_NODELABEL(led1_green)) ||       \
	!DT_NODE_EXISTS(DT_NODELABEL(led1_blue))
#error "need led1 rgb"
#endif
#if !DT_NODE_EXISTS(DT_ALIAS(sw0))
#error "need sw0"
#endif

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_red), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_green), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_blue), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static char local_name[JUXTA_ID_LEN];
static atomic_t app_mode = ATOMIC_INIT(JUXTA_OP_MODE_SHELF);
static atomic_t enter_prod_req;
static atomic_t reset_to_shelf_req;
static atomic_t sync_restart_adv_req;
static atomic_t ble_connected;
static bool hardware_ready;
static struct juxta_log_context log_ctx;
static struct k_work clear_memory_work;
static bool pending_shelf_exit;
static bool pending_boot;
static bool pending_user_connected;

static struct bt_data ad_sync[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, JUXTA_HUBLINK_SERVICE_UUID),
};

static struct bt_data sd_sync[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, local_name, 0),
};

static struct bt_data ad_prod[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, local_name, 0),
};

struct peer_slot {
	char name[JUXTA_ID_LEN];
	int8_t best_rssi;
	bool active;
};

static struct peer_slot peers[PEER_SLOT_COUNT];
static struct k_mutex peers_lock;

/* J-Link / SWD: C_DEBUGEN is set while a debugger has the debug interface. */
static bool debugger_attached(void)
{
#if defined(DCB) && defined(DCB_DHCSR_C_DEBUGEN_Msk)
	return (DCB->DHCSR & DCB_DHCSR_C_DEBUGEN_Msk) != 0U;
#elif defined(CoreDebug) && defined(CoreDebug_DHCSR_C_DEBUGEN_Msk)
	return (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U;
#else
	return false;
#endif
}

static void leds_off(void)
{
	(void)gpio_pin_set_dt(&led_r, 0);
	(void)gpio_pin_set_dt(&led_g, 0);
	(void)gpio_pin_set_dt(&led_b, 0);
}

/* Brief white flash on every POR / soft reboot so cold-boot is visible without RTT. */
static void led_boot_chirp(void)
{
	(void)gpio_pin_set_dt(&led_r, 1);
	(void)gpio_pin_set_dt(&led_g, 1);
	(void)gpio_pin_set_dt(&led_b, 1);
	k_sleep(K_MSEC(20));
	leds_off();
}

static void led_slow_blink_step(bool on)
{
	leds_off();
	if (on) {
		(void)gpio_pin_set_dt(&led_g, 1);
	}
}

static void led_fast_blue_step(bool on)
{
	leds_off();
	if (on) {
		(void)gpio_pin_set_dt(&led_b, 1);
	}
}

/*
 * Init fault: 1 s on / 1 s off, color encodes which check failed (battery HIL).
 *   red   = SPI NOR (juxta_log_init)
 *   blue  = antenna switch (juxta_antenna_init)
 *   green = ADXL367 (juxta_motion_init)
 *   white = late hardware_ready guard
 * UVLO uses a different pattern: short red chirp + long off (see enter_uvlo_lockout).
 */
static void led_long_blink_fault(bool r, bool g, bool b)
{
	while (1) {
		leds_off();
		if (r) {
			(void)gpio_pin_set_dt(&led_r, 1);
		}
		if (g) {
			(void)gpio_pin_set_dt(&led_g, 1);
		}
		if (b) {
			(void)gpio_pin_set_dt(&led_b, 1);
		}
		k_sleep(K_SECONDS(1));
		leds_off();
		k_sleep(K_SECONDS(1));
	}
}

/* Sticky replace-battery mode: no radio / NOR; short red chirp, long sleep. */
static void enter_uvlo_lockout(const char *reason)
{
	LOG_ERR("UVLO lockout (%s) — replace CR2032 (VDD < %d mV)",
		reason != NULL ? reason : "battery", BATT_UVLO_MV);
	(void)bt_le_adv_stop();
	(void)bt_le_scan_stop();
	leds_off();

	while (1) {
		(void)gpio_pin_set_dt(&led_r, 1);
		k_sleep(K_MSEC(BATT_UVLO_LED_ON_MS));
		leds_off();
		k_sleep(K_MSEC(BATT_UVLO_LED_OFF_MS));
	}
}

/*
 * Multi-sample gate (Juxta5-8 style). Returns true if all samples are valid
 * and below UVLO. Skipped under debugger (bench supply / no cell).
 */
static bool battery_uvlo_tripped(void)
{
	uint8_t low = 0U;
	int32_t last_mv = 0;

	if (debugger_attached()) {
		return false;
	}

	for (uint8_t i = 0U; i < BATT_UVLO_GATE_SAMPLES; i++) {
		int32_t mv = juxta_vdd_read_mv();

		LOG_INF("UVLO sample %u/%u: %d mV", (unsigned int)(i + 1U),
			(unsigned int)BATT_UVLO_GATE_SAMPLES, (int)mv);
		if (mv > 0 && mv < BATT_UVLO_MV) {
			low++;
		}
		last_mv = mv;
		if (i + 1U < BATT_UVLO_GATE_SAMPLES) {
			k_sleep(K_MSEC(BATT_UVLO_GATE_GAP_MS));
		}
	}

	if (low == BATT_UVLO_GATE_SAMPLES) {
		LOG_WRN("UVLO: %u/%u samples < %d mV (last %d)", (unsigned int)low,
			(unsigned int)BATT_UVLO_GATE_SAMPLES, BATT_UVLO_MV, (int)last_mv);
		return true;
	}
	if (low > 0U) {
		LOG_WRN("UVLO: only %u/%u low — treating as transient, continuing",
			(unsigned int)low, (unsigned int)BATT_UVLO_GATE_SAMPLES);
	}
	return false;
}

/* CR2032 cold-boot: brief settle + retry before hard-fault LED. */
#define INIT_RETRY_ATTEMPTS 5
#define INIT_RETRY_DELAY_MS 100

static int init_with_retry(const char *name, int (*fn)(void))
{
	int err = -ENODEV;

	for (int attempt = 1; attempt <= INIT_RETRY_ATTEMPTS; attempt++) {
		err = fn();
		if (err == 0) {
			if (attempt > 1) {
				LOG_INF("%s ok on attempt %d", name, attempt);
			}
			return 0;
		}
		LOG_WRN("%s attempt %d/%d rc=%d", name, attempt, INIT_RETRY_ATTEMPTS, err);
		k_sleep(K_MSEC(INIT_RETRY_DELAY_MS));
	}

	return err;
}

/* device_init() for zephyr,deferred-init nodes (skipped at POST_KERNEL). */
static int deferred_device_init_once(const struct device *dev)
{
	int err;

	if (dev == NULL) {
		return -ENODEV;
	}

	err = device_init(dev);
	if (err == -EALREADY) {
		return 0;
	}
	return err;
}

static int deferred_adxl_init(void)
{
	return deferred_device_init_once(DEVICE_DT_GET(DT_NODELABEL(adxl367)));
}

static int deferred_nor_init(void)
{
	return deferred_device_init_once(DEVICE_DT_GET(DT_ALIAS(spi_mem)));
}

static void deferred_optional_sensor_init(const struct device *dev, const char *name)
{
	int err = deferred_device_init_once(dev);

	if (err != 0) {
		LOG_WRN("%s deferred device_init rc=%d (optional)", name, err);
	}
}

static int motion_init_once(void)
{
	return juxta_motion_init(true);
}

static int antenna_init_once(void)
{
	return juxta_antenna_init();
}

static int log_init_once(void)
{
	return juxta_log_init(&log_ctx, juxta_settings_get(), local_name);
}

static void blink_n(uint8_t n)
{
	for (uint8_t i = 0; i < n; i++) {
		leds_off();
		(void)gpio_pin_set_dt(&led_g, 1);
		k_sleep(K_MSEC(80));
		leds_off();
		k_sleep(K_MSEC(120));
	}
}

static void enter_shelf(const char *reason)
{
	uint32_t now = juxta_time_now();

	juxta_rtt_jxs("shelf_entry");
	if (now != 0U && log_ctx.initialized) {
		(void)juxta_log_append_event(&log_ctx, juxta_settings_get(), local_name, "shelf_entry",
					     now);
	}
	(void)juxta_checkpoint_disable();
	LOG_INF("Entering System OFF (%s)", reason != NULL ? reason : "");
	leds_off();
	(void)bt_le_adv_stop();
	(void)bt_le_scan_stop();
	k_sleep(K_MSEC(100)); /* flush RTT */

	/* Same alive cue for POR→shelf and prod/gateway→shelf (only chirp site). */
	led_boot_chirp();

	/*
	 * With a debugger attached, sys_poweroff() breaks SWD/RTT. Soft-reboot
	 * instead so main() re-enters the [DBG] simulated shelf loop (Juxta5-8).
	 */
	if (debugger_attached()) {
		LOG_INF("[DBG] Shelf → soft reboot (keep debugger/RTT)");
		k_sleep(K_MSEC(50));
		sys_reboot(SYS_REBOOT_COLD);
	}

	(void)gpio_pin_interrupt_configure_dt(&button, GPIO_INT_LEVEL_ACTIVE);
	k_sleep(K_MSEC(50));
	sys_poweroff();
}

static int32_t battery_mv_source(void)
{
	return juxta_vdd_read_mv();
}

static void clear_memory_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_INF("clearMemory: erasing NOR CSV regions");
	(void)juxta_log_format(&log_ctx);
	(void)juxta_checkpoint_reset();
	juxta_rtt_jxs("clear_memory");
	LOG_INF("clearMemory: done");
}

void juxta_ble_datetime_synchronized(void)
{
	uint32_t now = juxta_time_now();

	if (now == 0U || !log_ctx.initialized) {
		return;
	}

	if (pending_shelf_exit) {
		pending_shelf_exit = false;
		(void)juxta_log_append_event(&log_ctx, juxta_settings_get(), local_name, "shelf_exit",
					     now);
	}
	if (pending_boot) {
		pending_boot = false;
		(void)juxta_log_append_event(&log_ctx, juxta_settings_get(), local_name, "boot", now);
	}
	if (pending_user_connected) {
		pending_user_connected = false;
		(void)juxta_log_append_event(&log_ctx, juxta_settings_get(), local_name,
					     "user_connected", now);
	}
}

void juxta_ble_timing_update_trigger(void)
{
	/* M2: scan/adv intervals are read each production loop; nothing to poke. */
}

void juxta_ble_reset_requested(void)
{
	atomic_set(&reset_to_shelf_req, 1);
}

void juxta_ble_clear_memory_requested(void)
{
	(void)k_work_submit(&clear_memory_work);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connect failed 0x%02x", err);
		return;
	}
	atomic_set(&ble_connected, 1);
	LOG_INF("Connected (sync)");
	juxta_ble_connection_established(conn);
	juxta_rtt_jxs("user_connected");
	if (juxta_time_is_set() && log_ctx.initialized) {
		(void)juxta_log_append_event(&log_ctx, juxta_settings_get(), local_name,
					     "user_connected", juxta_time_now());
	} else {
		pending_user_connected = true;
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	LOG_INF("Disconnected 0x%02x", reason);
	atomic_set(&ble_connected, 0);
	juxta_ble_connection_terminated();

	if ((int)atomic_get(&app_mode) != JUXTA_OP_MODE_SYNC) {
		return;
	}

	if (juxta_ble_datetime_synced()) {
		juxta_rtt_jxs("user_disconnected");
		if (log_ctx.initialized && juxta_time_is_set()) {
			(void)juxta_log_append_event(&log_ctx, juxta_settings_get(), local_name,
						     "user_disconnected", juxta_time_now());
		}
		atomic_set(&enter_prod_req, 1);
	} else {
		atomic_set(&sync_restart_adv_req, 1);
		pending_user_connected = false;
	}
}

BT_CONN_CB_DEFINE(prod_conn_cb) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void extract_name(struct net_buf_simple *ad_buf, char *dev_name, size_t name_sz)
{
	struct net_buf_simple_state state;

	memset(dev_name, 0, name_sz);
	if (ad_buf == NULL || name_sz == 0U) {
		return;
	}

	net_buf_simple_save(ad_buf, &state);
	while (ad_buf->len > 1U) {
		uint8_t flen = net_buf_simple_pull_u8(ad_buf);

		if (flen == 0U || flen > ad_buf->len) {
			break;
		}
		uint8_t ftype = net_buf_simple_pull_u8(ad_buf);

		flen--;
		if (flen > ad_buf->len) {
			break;
		}
		if ((ftype == BT_DATA_NAME_COMPLETE || ftype == BT_DATA_NAME_SHORTENED) &&
		    flen < name_sz) {
			memcpy(dev_name, ad_buf->data, flen);
			dev_name[flen] = '\0';
		}
		net_buf_simple_pull(ad_buf, flen);
	}
	net_buf_simple_restore(ad_buf, &state);
}

static void peer_note_max(const char *name, int8_t rssi)
{
	int free_idx = -1;

	(void)k_mutex_lock(&peers_lock, K_FOREVER);
	for (int i = 0; i < (int)PEER_SLOT_COUNT; i++) {
		if (peers[i].active && strcmp(peers[i].name, name) == 0) {
			if (rssi > peers[i].best_rssi) {
				peers[i].best_rssi = rssi;
			}
			(void)k_mutex_unlock(&peers_lock);
			return;
		}
		if (!peers[i].active && free_idx < 0) {
			free_idx = i;
		}
	}
	if (free_idx >= 0) {
		peers[free_idx].active = true;
		(void)snprintf(peers[free_idx].name, sizeof(peers[free_idx].name), "%s", name);
		peers[free_idx].best_rssi = rssi;
	}
	(void)k_mutex_unlock(&peers_lock);
}

static void peers_clear(void)
{
	(void)k_mutex_lock(&peers_lock, K_FOREVER);
	memset(peers, 0, sizeof(peers));
	(void)k_mutex_unlock(&peers_lock);
}

static void peers_flush_jxb(void)
{
	(void)k_mutex_lock(&peers_lock, K_FOREVER);
	for (int i = 0; i < (int)PEER_SLOT_COUNT; i++) {
		if (peers[i].active) {
			juxta_rtt_jxb(local_name, peers[i].name, peers[i].best_rssi);
			if (log_ctx.initialized && juxta_time_is_set()) {
				(void)juxta_log_append_ble_observation(&log_ctx, juxta_time_now(),
								       local_name, peers[i].name,
								       peers[i].best_rssi);
			}
		}
	}
	(void)k_mutex_unlock(&peers_lock);
}

static void scan_recv(const struct bt_le_scan_recv_info *info, struct net_buf_simple *buf)
{
	char name[JUXTA_ID_LEN];

	if ((int)atomic_get(&app_mode) != JUXTA_OP_MODE_PROD) {
		return;
	}

	extract_name(buf, name, sizeof(name));
	if (juxta_id_is_peer_name(name, local_name)) {
		peer_note_max(name, info->rssi);
	}
}

static struct bt_le_scan_cb scan_cb = {
	.recv = scan_recv,
};

static int magnet_wait_hold(void)
{
	int64_t press_ms;
	bool saw_3s = false;

	/* Wait for press */
	while (gpio_pin_get_dt(&button) <= 0) {
		k_sleep(K_MSEC(20));
	}

	press_ms = k_uptime_get();
	leds_off();
	(void)gpio_pin_set_dt(&led_r, 1);

	while (gpio_pin_get_dt(&button) > 0) {
		int64_t held = k_uptime_get() - press_ms;

		if (!saw_3s && held >= (int64_t)MAGNET_DEBOUNCE_MS) {
			saw_3s = true;
			leds_off(); /* commit cue */
		}
		if (held >= (int64_t)DFU_HOLD_THRESHOLD_MS) {
			return (int)DFU_HOLD_THRESHOLD_MS;
		}
		k_sleep(K_MSEC(20));
	}

	return (int)(k_uptime_get() - press_ms);
}

static void run_dfu_cue(void)
{
	atomic_set(&app_mode, JUXTA_OP_MODE_DFU);
	juxta_rtt_jxs("dfu_requested");
	if (log_ctx.initialized && juxta_time_is_set()) {
		(void)juxta_log_append_event(&log_ctx, juxta_settings_get(), local_name,
					     "dfu_requested", juxta_time_now());
	}
	blink_n(3);
	for (int i = 0; i < 50; i++) {
		led_fast_blue_step((i % 2) == 0);
		k_sleep(K_MSEC(50));
	}
	enter_shelf("dfu_exit");
}

static int start_sync_adv(void)
{
	/* Match juxta5-8-prod: Hublink UUID in ADV (iOS filters on it); name in scan rsp. */
	sd_sync[0].data_len = (uint8_t)strlen(local_name);
	(void)bt_le_adv_stop();
	return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad_sync, ARRAY_SIZE(ad_sync), sd_sync,
			       ARRAY_SIZE(sd_sync));
}

static void run_sync_phase(void)
{
	bool blink_on = false;

	atomic_set(&app_mode, JUXTA_OP_MODE_SYNC);
	juxta_ble_clear_datetime_synced();
	atomic_set(&sync_restart_adv_req, 0);
	atomic_set(&enter_prod_req, 0);
	pending_shelf_exit = true;
	pending_boot = true;
	pending_user_connected = false;
	juxta_rtt_jxs("shelf_exit");
	juxta_rtt_jxs("boot");

	if (start_sync_adv() != 0) {
		LOG_ERR("sync adv failed");
		enter_shelf("sync_adv_fail");
	}

	LOG_INF("Sync phase — connect companion, write timestamp, disconnect");

	while ((int)atomic_get(&app_mode) == JUXTA_OP_MODE_SYNC) {
		if (atomic_cas(&enter_prod_req, 1, 0)) {
			break;
		}
		if (atomic_cas(&reset_to_shelf_req, 1, 0)) {
			enter_shelf("gateway_reset");
		}
		if (atomic_cas(&sync_restart_adv_req, 1, 0)) {
			LOG_WRN("Sync disconnect without timestamp — restarting adv");
			k_sleep(K_MSEC(200));
			(void)bt_le_adv_stop();
			if (start_sync_adv() != 0) {
				LOG_ERR("sync adv restart failed");
				enter_shelf("sync_adv_fail");
			}
		}
		/* Solid green while connected; slow blink while waiting. */
		if (atomic_get(&ble_connected) != 0) {
			leds_off();
			(void)gpio_pin_set_dt(&led_g, 1);
			k_sleep(K_MSEC(100));
		} else {
			led_slow_blink_step(blink_on);
			blink_on = !blink_on;
			k_sleep(K_MSEC(blink_on ? 50 : 450));
		}
	}

	(void)bt_le_adv_stop();
	blink_n(5);
	leds_off();
}

static void production_vitals(void)
{
	struct juxta_motion_sample m;
	int32_t mv = juxta_vdd_read_mv();
	int32_t batt = mv < 0 ? 0 : mv;
	int8_t temp_i8;

	juxta_motion_take(&m);
	if (juxta_settings_get()->motion_logging == 0U) {
		m.motion_count = 0U;
	}
	juxta_rtt_jxv(m.motion_count, batt, m.temp_c, m.temp_valid);

	if (battery_uvlo_tripped()) {
		if (log_ctx.initialized && juxta_time_is_set()) {
			(void)juxta_log_append_event(&log_ctx, juxta_settings_get(), local_name,
						     "low_battery", juxta_time_now());
			k_sleep(K_MSEC(50));
		}
		enter_uvlo_lockout("vitals");
	}

	if (!log_ctx.initialized || !juxta_time_is_set()) {
		return;
	}

	if (m.temp_valid) {
		if (m.temp_c > 127.0f) {
			temp_i8 = 127;
		} else if (m.temp_c < -128.0f) {
			temp_i8 = -128;
		} else {
			temp_i8 = (int8_t)m.temp_c;
		}
	} else {
		temp_i8 = 0;
	}

	(void)juxta_log_append_vitals(&log_ctx, juxta_time_now(), (uint16_t)m.motion_count, batt,
				      temp_i8);
}

static void run_production(void)
{
	const struct juxta_settings *s;
	int64_t next_vitals;
	uint32_t last_scan_s = 0U;
	uint32_t last_adv_s = 0U;
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	if (!hardware_ready) {
		led_long_blink_fault(true, true, true); /* white */
	}

	atomic_set(&app_mode, JUXTA_OP_MODE_PROD);
	juxta_ble_set_production_ready();
	(void)juxta_checkpoint_reset();
	leds_off();
	LOG_INF("Production started (NOR + RTT, dual-ant max RSSI)");

	s = juxta_settings_get();
	next_vitals = k_uptime_get() + (int64_t)s->vitals_interval_s * 1000;
	/* Fire first due bursts promptly (Juxta5-8: last_* starts at 0). */
	last_scan_s = 0U;
	last_adv_s = 0U;

	while ((int)atomic_get(&app_mode) == JUXTA_OP_MODE_PROD) {
		uint32_t now_s = (uint32_t)(k_uptime_get() / 1000);
		bool scan_due;
		bool adv_due;

		if (atomic_cas(&reset_to_shelf_req, 1, 0)) {
			enter_shelf("gateway_reset");
		}

		/* Magnet during prod → shelf (R+B cue, off at 3 s = commit) */
		if (gpio_pin_get_dt(&button) > 0) {
			int held = 0;
			bool committed = false;

			leds_off();
			(void)gpio_pin_set_dt(&led_r, 1);
			(void)gpio_pin_set_dt(&led_b, 1);

			while (gpio_pin_get_dt(&button) > 0) {
				k_sleep(K_MSEC(20));
				held += 20;
				if (held >= (int)MAGNET_DEBOUNCE_MS) {
					committed = true;
					leds_off(); /* commit cue */
					break;
				}
			}

			if (committed) {
				blink_n(5);
				/* Let the user release before System OFF wake-sense arms. */
				k_sleep(K_SECONDS(1));
				while (gpio_pin_get_dt(&button) > 0) {
					k_sleep(K_MSEC(20));
				}
				enter_shelf("magnet");
			}
			leds_off();
		}

		s = juxta_settings_get();
		/*
		 * Juxta5-8 semantics: scan_interval_s / adv_interval_s are cadence
		 * (seconds between bursts). Burst length is fixed ~1 s — never both
		 * at once; scan wins when both are due.
		 */
		scan_due = (s->scan_interval_s > 0U) &&
			   ((uint64_t)now_s >= (uint64_t)last_scan_s + (uint64_t)s->scan_interval_s);
		adv_due = (s->adv_interval_s > 0U) &&
			  ((uint64_t)now_s >= (uint64_t)last_adv_s + (uint64_t)s->adv_interval_s);

		if (scan_due) {
			last_scan_s = now_s;
			peers_clear();
			(void)juxta_antenna_select(1);
			(void)bt_le_scan_start(&scan_param, NULL);
			k_sleep(K_MSEC(ANT_DWELL_MS));
			(void)juxta_antenna_select(2);
			k_sleep(K_MSEC(ANT_DWELL_MS));
			(void)bt_le_scan_stop();
			peers_flush_jxb();
		} else if (adv_due) {
			last_adv_s = now_s;
			(void)juxta_antenna_select(1);
			ad_prod[1].data_len = (uint8_t)strlen(local_name);
			if (bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, ad_prod, ARRAY_SIZE(ad_prod),
					    NULL, 0) == 0) {
				k_sleep(K_MSEC(ADV_BURST_MS));
				(void)bt_le_adv_stop();
			}
		}

		if (k_uptime_get() >= next_vitals) {
			production_vitals();
			next_vitals = k_uptime_get() + (int64_t)s->vitals_interval_s * 1000;
		}

		k_sleep(K_MSEC(50));
	}
}

int main(void)
{
	uint32_t cause = 0U;
	int err;

	k_mutex_init(&peers_lock);
	k_work_init(&clear_memory_work, clear_memory_work_handler);

	/*
	 * DO NOT REMOVE — physical CR2032 reseat contact settle.
	 *
	 * Battery holder bounce lasts tens of ms after insertion. A clean PPK /
	 * GUI power toggle never fails; reseat was intermittent until settle +
	 * deferred device_init. ADXL / BME / BMI / SPI NOR are zephyr,deferred-init
	 * in the app overlay so POST_KERNEL does not probe them during bounce —
	 * device_init() runs only after this sleep. Do not shorten/delete without
	 * reseat HIL. Currently 500 ms to stress-test the contact-settle hypothesis.
	 */
	k_sleep(K_MSEC(500));

	(void)gpio_pin_configure_dt(&led_r, GPIO_OUTPUT_INACTIVE);
	(void)gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
	(void)gpio_pin_configure_dt(&led_b, GPIO_OUTPUT_INACTIVE);
	(void)gpio_pin_configure_dt(&button, GPIO_INPUT);

	(void)hwinfo_get_reset_cause(&cause);
	(void)hwinfo_clear_reset_cause();

	juxta_time_init();

	/*
	 * UVLO before consequential I/O: VDD ADC only, then multi-sample gate.
	 * Fail → sticky red chirp (replace battery); no sensors/NOR/BT.
	 */
	err = juxta_vdd_init();
	if (err) {
		LOG_WRN("vdd_init (%d) — UVLO gate skipped", err);
	} else if (battery_uvlo_tripped()) {
		enter_uvlo_lockout("boot");
	} else {
		int32_t mv = juxta_vdd_read_mv();

		LOG_INF("VDD ok batt_mv=%d pct~%u", (int)mv,
			mv < 0 ? 0U : juxta_vdd_mv_to_percent_cr2032(mv));
	}

	/*
	 * Bring up deferred peripherals now that VDD has had settle time.
	 * ADXL + NOR are required; BME/BMI are optional (suspend best-effort).
	 */
	err = init_with_retry("adxl367", deferred_adxl_init);
	if (err) {
		LOG_ERR("adxl367 device_init (%d) — hardware fault", err);
		led_long_blink_fault(false, true, false); /* green */
	}
#if DT_NODE_EXISTS(DT_NODELABEL(bme688))
	deferred_optional_sensor_init(DEVICE_DT_GET(DT_NODELABEL(bme688)), "bme688");
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(bmi270))
	deferred_optional_sensor_init(DEVICE_DT_GET(DT_NODELABEL(bmi270)), "bmi270");
#endif
	err = init_with_retry("spi_nor", deferred_nor_init);
	if (err) {
		LOG_ERR("spi_nor device_init (%d) — NOR required for M2", err);
		led_long_blink_fault(true, false, false); /* red */
	}

	/*
	 * Sensors / antenna before bt_enable: avoid radio bring-up before
	 * motion poll / antenna GPIO are ready.
	 */
	err = init_with_retry("motion_init", motion_init_once);
	if (err) {
		LOG_ERR("motion_init (%d) — hardware fault", err);
		led_long_blink_fault(false, true, false); /* green */
	}

	err = init_with_retry("antenna_init", antenna_init_once);
	if (err) {
		LOG_ERR("antenna_init (%d)", err);
		led_long_blink_fault(false, false, true); /* blue */
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable (%d)", err);
		return err;
	}

	err = juxta_id_fill_from_bt(local_name, sizeof(local_name));
	if (err) {
		return err;
	}

	/* GAP Device Name must be JX_XXXXXX — iOS re-reads this and replaces scan name. */
	err = bt_set_name(local_name);
	if (err) {
		LOG_WRN("bt_set_name(%s) (%d)", local_name, err);
	} else {
		LOG_INF("bt_set_name → %s", local_name);
	}

	err = juxta_settings_init(local_name);
	if (err) {
		LOG_WRN("settings_init (%d)", err);
	}

	err = init_with_retry("juxta_log_init", log_init_once);
	if (err) {
		LOG_ERR("juxta_log_init (%d) — NOR required for M2", err);
		led_long_blink_fault(true, false, false); /* red */
	}
	(void)juxta_checkpoint_init();

	(void)juxta_ble_service_init(&log_ctx);
	juxta_ble_set_battery_mv_source(battery_mv_source);
	bt_le_scan_cb_register(&scan_cb);

	hardware_ready = true;

	LOG_INF("%s %s local=%s resetreas=0x%x debugger=%d", JUXTA_PRODUCT_NAME,
		JUXTA_FIRMWARE_VERSION, local_name, cause, (int)debugger_attached());

	int held_ms;

	if (debugger_attached()) {
		/*
		 * Debug simulation (same idea as juxta5-8-prod): stay awake, LED off
		 * = shelf, wait for BTN1 hold. False positives loop; DFU soft-reboots
		 * back here via enter_shelf().
		 */
		LOG_INF("[DBG] Simulated shelf — hold BTN1 3–10 s for sync, ≥10 s DFU cue");
		leds_off();
		for (;;) {
			held_ms = magnet_wait_hold();
			leds_off();
			if (held_ms < (int)MAGNET_DEBOUNCE_MS) {
				LOG_INF("[DBG] False positive hold_ms=%d — stay in shelf", held_ms);
				continue;
			}
			break;
		}
	} else {
		/* Fresh boot / non-OFF wake → real System OFF */
		if ((cause & RESET_LOW_POWER_WAKE) == 0U) {
			enter_shelf("fresh_boot");
		}

		LOG_INF("System OFF wake — hold BTN1 3–10 s for sync, ≥10 s DFU cue");
		held_ms = magnet_wait_hold();
		leds_off();
		if (held_ms < (int)MAGNET_DEBOUNCE_MS) {
			LOG_INF("False positive hold_ms=%d", held_ms);
			enter_shelf("false_positive");
		}
	}

	if (held_ms >= (int)DFU_HOLD_THRESHOLD_MS) {
		run_dfu_cue();
	}

	run_sync_phase();
	run_production();

	enter_shelf("prod_exit");
	return 0;
}
