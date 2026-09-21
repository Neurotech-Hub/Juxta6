#ifndef JUXTA_LOG_H_
#define JUXTA_LOG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "juxta_prod.h"
#include "juxta_settings.h"

enum juxta_log_type
{
	JUXTA_LOG_JXS = 'S',
	JUXTA_LOG_JXV = 'V',
	JUXTA_LOG_JXB = 'B',
};

struct juxta_file_entry
{
	char path[JUXTA_FILE_PATH_LEN];
	uint32_t offset;
	uint32_t length;
	enum juxta_log_type type;
	bool active;
};

struct juxta_log_context
{
	const struct device *flash;
	bool initialized;
	struct juxta_file_entry files[JUXTA_MAX_FILES];
	uint16_t file_count;
	uint32_t active_jxs;
	uint32_t active_jxv;
	uint32_t active_jxb;
	/* Per-region "full" latch indexed by region position in juxta_log.c's
	 * regions[] array: 0 = JXS, 1 = JXV, 2 = JXB.  Set by the bounds-checked
	 * append helper the first time a write would cross the region end, or
	 * by ensure_file when first_erased_offset has reached the region end.
	 * Once latched the region silently drops further writes (and rotations)
	 * so the NOR can never bleed into a neighbouring region.  Cleared by
	 * juxta_log_format() and zero-initialised by juxta_log_init(). */
	bool region_full[3];
};

int juxta_log_init(struct juxta_log_context *ctx, const struct juxta_settings *settings,
				   const char *device_id);
/* Update the cached device_id used for JXS day_start / ble_name after a role change. */
void juxta_log_set_device_id(const char *device_id);
int juxta_log_format(struct juxta_log_context *ctx);

/* Optional progress hook called inside long internal loops (currently the
 * per-sector flash_erase loop inside juxta_log_format).  main.c registers
 * prod_wdt_feed() here so the hardware watchdog stays fed during multi-second
 * clearMemory erases: the erase holds the log mutex, which can block the
 * main-loop feeds for the whole erase.  Implementations must be safe to call
 * from any workqueue thread and must not block. */
void juxta_log_set_long_op_tick(void (*tick)(void));
/* Append a JXS event with empty latitude/longitude columns. */
int juxta_log_append_event(struct juxta_log_context *ctx, const struct juxta_settings *settings,
						   const char *device_id, const char *event, uint32_t unix_time);
/* Same as append_event, but write this-event coords when loc_valid (time_set). */
int juxta_log_append_event_loc(struct juxta_log_context *ctx, const struct juxta_settings *settings,
			       const char *device_id, const char *event, uint32_t unix_time,
			       bool loc_valid, float latitude, float longitude);
/* temp_c / humidity: one-decimal BME688 fields; empty CSV cells when !*ok. */
int juxta_log_append_vitals(struct juxta_log_context *ctx, uint32_t unix_time, uint16_t motion,
			    int32_t batt_mv, float temp_c, bool temp_ok, float humidity,
			    bool humidity_ok);
/* No observer column (constant per device, in the package folder). ADV names
 * JX_/JB_XXXXXX are stored as 7-char peer_id: X|B + 6 hex. */
int juxta_log_append_ble_observation(struct juxta_log_context *ctx, uint32_t unix_time,
									 const char *peer_id, int8_t rssi);
int juxta_log_list_files(struct juxta_log_context *ctx, char *buffer, size_t buffer_size);
int juxta_log_find_file(struct juxta_log_context *ctx, const char *path,
						struct juxta_file_entry *entry);
int juxta_log_read_file(struct juxta_log_context *ctx, const struct juxta_file_entry *entry,
						uint32_t file_offset, uint8_t *buffer, size_t buffer_size,
						size_t *bytes_read);
/* BLE file transfer: CSV body only (no stored filename line, no #EOF trailer). */
uint32_t juxta_log_transfer_payload_bytes(const struct juxta_file_entry *entry);
int juxta_log_read_file_for_transfer(struct juxta_log_context *ctx,
									 const struct juxta_file_entry *entry, uint32_t file_offset,
									 uint8_t *buffer, size_t buffer_size, size_t *bytes_read);
int juxta_log_recover_files(struct juxta_log_context *ctx);
uint8_t juxta_log_memory_level_percent(const struct juxta_log_context *ctx);

/* Drain one pending "region latched full" notification. Returns 1 and copies
 * the region prefix ("JXS"/"JXV"/"JXB") into prefix (needs >= 4 bytes) if a
 * region newly latched since the last call, 0 if none, negative on bad args.
 * main.c polls this at vitals cadence and turns each into a JXS event + RTT
 * line so multi-week deployments never lose data silently. */
int juxta_log_take_region_full_event(char *prefix, size_t prefix_len);

#endif /* JUXTA_LOG_H_ */
