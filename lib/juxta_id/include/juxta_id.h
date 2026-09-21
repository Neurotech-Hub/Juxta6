/*
 * Shared Juxta identity helpers (JX_/JB_ + last 3 public address bytes).
 */

#ifndef JUXTA_ID_H_
#define JUXTA_ID_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** "JX_" or "JB_" + 6 hex digits + NUL */
#define JUXTA_ID_LEN 10U

/** JXB storage: 'X' or 'B' + 6 hex + NUL */
#define JUXTA_JXB_PEER_LEN 8U

/**
 * Fill @p out with 6 hex digits (no prefix) from the first Bluetooth identity.
 * Requires bt_enable() first. On empty identity list uses "000000".
 *
 * @return 0 on success, negative errno on bad args.
 */
int juxta_id_hex_from_bt(char *out, size_t out_sz);

/**
 * Format ADV/device name into @p out: JX_XXXXXX (mobile) or JB_XXXXXX (base)
 * using the MAC hex from @ref juxta_id_hex_from_bt.
 *
 * @return 0 on success, negative errno on bad args.
 */
int juxta_id_format_from_bt(char *out, size_t out_sz, bool basestation);

/**
 * Fill @p out with JX_XXXXXX (mobile). Prefer @ref juxta_id_format_from_bt
 * when role is known.
 */
int juxta_id_fill_from_bt(char *out, size_t out_sz);

/**
 * @return true if @p name is JX_ or JB_ + six hex digits and not equal to
 *         @p self_name.
 */
bool juxta_id_is_peer_name(const char *name, const char *self_name);

/**
 * Map an ADV peer name (JX_/JB_ + 6 hex) to JXB storage form (X/B + 6 hex).
 *
 * @return 0 on success, -EINVAL if @p adv_name is not a valid Juxta peer name.
 */
int juxta_id_to_jxb_peer(const char *adv_name, char *out, size_t out_sz);

/**
 * Lexicographic compare of two Juxta names (or raw suffixes).
 * @return <0 if a < b, 0 if equal, >0 if a > b.
 */
int juxta_id_cmp(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* JUXTA_ID_H_ */
