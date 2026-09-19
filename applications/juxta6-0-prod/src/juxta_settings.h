#ifndef JUXTA_SETTINGS_H_
#define JUXTA_SETTINGS_H_

#include <stdint.h>

#include "juxta_prod.h"

struct juxta_settings {
	char subject_id[JUXTA_SUBJECT_ID_LEN];
	char experiment[JUXTA_EXPERIMENT_LEN];
	uint16_t adv_interval_s;  /* 0 = off; else seconds between 1 s non-conn adv bursts */
	uint16_t scan_interval_s; /* 0 = off; else seconds between 1 s passive scan bursts */
	uint16_t vitals_interval_s;
	uint8_t inactivity_multiplier;
	uint8_t motion_logging;
	/* Last gateway fix (WGS84). location_valid=0 → JXS day_start leaves coords blank. */
	uint8_t location_valid;
	float latitude;
	float longitude;
};

struct juxta_log_cache_file {
	char path[JUXTA_CACHE_NAME_LEN];
	uint32_t offset;
	uint32_t length;
	uint8_t log_type;
	uint8_t active;
};

struct juxta_log_cache {
	uint32_t magic;
	uint16_t version;
	uint16_t file_count;
	struct juxta_log_cache_file files[JUXTA_MAX_FILES];
};

int juxta_settings_init(const char *device_id);
const struct juxta_settings *juxta_settings_get(void);
int juxta_settings_update(const struct juxta_settings *settings);
void juxta_settings_defaults(struct juxta_settings *settings, const char *device_id);

int juxta_settings_load_log_cache(struct juxta_log_cache *cache);
int juxta_settings_save_log_cache(const struct juxta_log_cache *cache);
int juxta_settings_clear_log_cache(void);

#endif /* JUXTA_SETTINGS_H_ */
