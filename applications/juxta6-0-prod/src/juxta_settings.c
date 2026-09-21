#include "juxta_settings.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(juxta_settings, LOG_LEVEL_INF);

#define LOG_CACHE_MAGIC 0x4A584C43U /* JXLC */
#define LOG_CACHE_VERSION 1U

static struct juxta_settings current;
static struct juxta_log_cache current_log_cache;
static bool loaded_log_cache;

void juxta_settings_defaults(struct juxta_settings *settings, const char *device_id)
{
	memset(settings, 0, sizeof(*settings));
	if (device_id != NULL) {
		(void)snprintf(settings->subject_id, sizeof(settings->subject_id), "%s", device_id);
	}
	settings->adv_interval_s = JUXTA_DEFAULT_ADV_INTERVAL_S;
	settings->scan_interval_s = JUXTA_DEFAULT_SCAN_INTERVAL_S;
	settings->vitals_interval_s = JUXTA_DEFAULT_VITALS_INTERVAL_S;
	settings->inactivity_multiplier = JUXTA_DEFAULT_INACTIVITY_MULTIPLIER;
	settings->motion_logging = 1U;
	settings->location_valid = 0U;
	settings->latitude = 0.0f;
	settings->longitude = 0.0f;
	settings->is_basestation = 0U;
}

/*
 * Clamp/repair a settings struct. Applied to values loaded from NVS as well
 * as BLE updates: corrupt or legacy NVS content must never produce a
 * pathological runtime (vitals_interval_s below the 60 s floor would densify
 * NOR writes beyond the JXV budget) or non-terminated
 * strings (formatted with %s into Node JSON / JXS rows).
 */
static void settings_sanitize(struct juxta_settings *s)
{
	s->subject_id[sizeof(s->subject_id) - 1U] = '\0';
	s->experiment[sizeof(s->experiment) - 1U] = '\0';

	if (s->adv_interval_s > JUXTA_MAX_BLE_INTERVAL_S) {
		s->adv_interval_s = JUXTA_MAX_BLE_INTERVAL_S;
	}
	if (s->scan_interval_s > JUXTA_MAX_BLE_INTERVAL_S) {
		s->scan_interval_s = JUXTA_MAX_BLE_INTERVAL_S;
	}
	if (s->vitals_interval_s < JUXTA_MIN_VITALS_INTERVAL_S) {
		s->vitals_interval_s = JUXTA_MIN_VITALS_INTERVAL_S;
	}
	if (s->inactivity_multiplier < 1U) {
		s->inactivity_multiplier = 1U;
	}
	if (s->inactivity_multiplier > JUXTA_MAX_INACTIVITY_MULTIPLIER) {
		s->inactivity_multiplier = JUXTA_MAX_INACTIVITY_MULTIPLIER;
	}
	if (s->location_valid != 0U) {
		s->location_valid = 1U;
		if (s->latitude > 90.0f) {
			s->latitude = 90.0f;
		} else if (s->latitude < -90.0f) {
			s->latitude = -90.0f;
		}
		if (s->longitude > 180.0f) {
			s->longitude = 180.0f;
		} else if (s->longitude < -180.0f) {
			s->longitude = -180.0f;
		}
	} else {
		s->location_valid = 0U;
		s->latitude = 0.0f;
		s->longitude = 0.0f;
	}
	s->is_basestation = (s->is_basestation != 0U) ? 1U : 0U;
}

static int settings_set_handler(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (strcmp(name, "current") == 0) {
		/* Pre-v7 omit location_*; pre-v9 omit is_basestation. Accept shorter
		 * reads and keep defaults for trailing fields (from defaults()). */
		if (len > sizeof(current)) {
			return -EINVAL;
		}

		ssize_t n = read_cb(cb_arg, &current, len);

		return (n == (ssize_t)len) ? 0 : -EINVAL;
	}

	if (strcmp(name, "log_cache") == 0) {
		if (len != sizeof(current_log_cache)) {
			LOG_WRN("Ignoring log cache size %zu expected %zu", len,
				sizeof(current_log_cache));
			return -EINVAL;
		}

		ssize_t n = read_cb(cb_arg, &current_log_cache, sizeof(current_log_cache));

		if (n >= 0 && current_log_cache.magic == LOG_CACHE_MAGIC &&
		    current_log_cache.version == LOG_CACHE_VERSION &&
		    current_log_cache.file_count <= JUXTA_MAX_FILES) {
			loaded_log_cache = true;
		}
		return (n >= 0) ? 0 : (int)n;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(juxta, "juxta", NULL, settings_set_handler, NULL, NULL);

int juxta_settings_init(const char *device_id)
{
	int err = settings_subsys_init();

	if (err) {
		return err;
	}

	juxta_settings_defaults(&current, device_id);
	err = settings_load_subtree("juxta");
	if (err && err != -ENOENT) {
		LOG_WRN("settings_load (%d) — using defaults", err);
	}

	settings_sanitize(&current); /* NVS may hold corrupt/legacy values */

	if (current.subject_id[0] == '\0' && device_id != NULL) {
		(void)snprintf(current.subject_id, sizeof(current.subject_id), "%s", device_id);
	}

	LOG_INF("settings subject=%s scan=%us adv=%us vitals=%us motion=%u base=%u",
		current.subject_id, current.scan_interval_s, current.adv_interval_s,
		current.vitals_interval_s, current.motion_logging,
		(unsigned int)current.is_basestation);
	return 0;
}

const struct juxta_settings *juxta_settings_get(void)
{
	return &current;
}

int juxta_settings_update(const struct juxta_settings *settings)
{
	if (settings == NULL) {
		return -EINVAL;
	}

	current = *settings;
	settings_sanitize(&current);

	return settings_save_one("juxta/current", &current, sizeof(current));
}

int juxta_settings_load_log_cache(struct juxta_log_cache *cache)
{
	if (cache == NULL) {
		return -EINVAL;
	}
	if (!loaded_log_cache) {
		return -ENOENT;
	}

	*cache = current_log_cache;
	return 0;
}

int juxta_settings_save_log_cache(const struct juxta_log_cache *cache)
{
	static struct juxta_log_cache next;
	int rc;

	if (cache == NULL || cache->file_count > JUXTA_MAX_FILES) {
		return -EINVAL;
	}

	next = *cache;
	next.magic = LOG_CACHE_MAGIC;
	next.version = LOG_CACHE_VERSION;

	rc = settings_save_one("juxta/log_cache", &next, sizeof(next));
	if (rc != 0) {
		LOG_WRN("settings_save_one log cache failed: %d", rc);
		return rc;
	}

	current_log_cache = next;
	loaded_log_cache = true;
	return 0;
}

int juxta_settings_clear_log_cache(void)
{
	memset(&current_log_cache, 0, sizeof(current_log_cache));
	loaded_log_cache = false;
	return settings_delete("juxta/log_cache");
}
