#ifndef JUXTA_PROD_H_
#define JUXTA_PROD_H_

#include <stdint.h>

#define JUXTA_PRODUCT_NAME "Juxta6-0"
#define JUXTA_FIRMWARE_VERSION "6.3.0"
/* v7: JXS gains latitude,longitude (gateway last-known / time_set). v6 kept
 * JXB/JXV day-relative sec and dropped observer/JX_ peer prefix. Companion
 * parsers update in lockstep; deployed devices need a clearMemory/format when
 * upgrading (see release notes). */
#define JUXTA_LOG_SCHEMA "jxta-nor-csv-v7"
#define JUXTA_LOGGING_VERSION 7

#define JUXTA_DEVICE_ID_LEN 10
#define JUXTA_SUBJECT_ID_LEN 32
#define JUXTA_EXPERIMENT_LEN 32

#define JUXTA_DEFAULT_SCAN_INTERVAL_S 30U /* cadence between 1 s scan bursts */
#define JUXTA_DEFAULT_ADV_INTERVAL_S 5U   /* cadence between 1 s adv bursts */
#define JUXTA_DEFAULT_VITALS_INTERVAL_S 60U
#define JUXTA_MAX_BLE_INTERVAL_S 120U
#define JUXTA_DEFAULT_INACTIVITY_MULTIPLIER 1U
#define JUXTA_MAX_INACTIVITY_MULTIPLIER 10U

#define MAGNET_DEBOUNCE_MS 3000U
#define DFU_HOLD_THRESHOLD_MS 10000U
/* Soft floor for DFU entry on CR2032 (UVLO is lower; DFU needs headroom). */
#define BATT_DFU_MIN_MV 2700

/* CR2032 UVLO: below this, refuse consequential boot / leave production. */
#define BATT_UVLO_MV 2500
#define BATT_UVLO_GATE_SAMPLES 3U
#define BATT_UVLO_GATE_GAP_MS 10U
#define BATT_UVLO_LED_ON_MS 40U
#define BATT_UVLO_LED_OFF_MS 2000U

/* Filenames are "JXByyyymmdd.csv" = 15 chars + null. */
#define JUXTA_FILE_NAME_LEN 20
#define JUXTA_FILE_PATH_LEN 64
#define JUXTA_CACHE_NAME_LEN 20
/* 24 days x 3 file types (JXS/JXV/JXB): rotation cap for a 22-day deployment
 * with margin.  Knock-on sizes: files[] in juxta_log_context ~5.3 KB static
 * RAM; the NVS log-cache blob ~2.3 KB (see CONFIG_SETTINGS_NVS_SECTOR_COUNT
 * in prj.conf, raised for GC headroom). */
#define JUXTA_MAX_FILES 72
#define JUXTA_TRANSFER_CHUNK_SIZE 512

enum juxta_op_mode {
	JUXTA_OP_MODE_SHELF = 0,
	JUXTA_OP_MODE_SYNC = 1,
	JUXTA_OP_MODE_PROD = 2,
	JUXTA_OP_MODE_DFU = 3,
};

#endif /* JUXTA_PROD_H_ */
