/*
 * Juxta Channel Sounding ranging layer.
 *
 * Isolates Nordic RAS / CS APIs from application code.
 * Requires NCS with CONFIG_BT_CHANNEL_SOUNDING and RAS (Tag board: NCS >= 3.3.1).
 *
 * Do not copy nRF54L15 DK CS antenna overlays onto Tag (those GPIOs are TWI).
 */

#ifndef JUXTA_RANGE_H_
#define JUXTA_RANGE_H_

#include <stdint.h>

#include <zephyr/bluetooth/conn.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Reflector participated; no local distance estimate (Nordic CS DE is initiator-side). */
#define JUXTA_RANGE_QUALITY_REFLECTOR 0xFFFEu
/** Measurement failed / do not use. */
#define JUXTA_RANGE_QUALITY_INVALID 0U
/** Initiator DE quality OK (tone path usable). */
#define JUXTA_RANGE_QUALITY_OK 1U

struct juxta_range_result {
	/** Estimated distance in meters (NaN if not available). */
	float distance_m;
	/** SDK/quality metadata: JUXTA_RANGE_QUALITY_* or packed SDK fields. */
	uint16_t quality;
	/** 0 = success; negative errno on failure (distinguishable from success). */
	int status;
};

/**
 * Run Channel Sounding as initiator on an existing ACL connection.
 * Performs security/MTU/RAS discovery/CS procedure as needed, blocks until one
 * distance estimate or timeout.
 *
 * @param conn Connected peer (central role expected).
 * @param out  Result storage (required).
 * @return 0 and out->status==0 on success; negative errno on setup failure.
 *         Failed measurements set out->status != 0 and out->quality INVALID.
 */
int juxta_range_as_initiator(struct bt_conn *conn, struct juxta_range_result *out);

/**
 * Arm Channel Sounding as reflector on an existing ACL connection.
 * Blocks until CS procedures are observed / timeout. Distance is not estimated
 * locally; out->quality is JUXTA_RANGE_QUALITY_REFLECTOR on success.
 */
int juxta_range_as_reflector(struct bt_conn *conn, struct juxta_range_result *out);

#ifdef __cplusplus
}
#endif

#endif /* JUXTA_RANGE_H_ */
