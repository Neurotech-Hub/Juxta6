/*
 * Shared Juxta identity helpers (JX_ + last 3 public address bytes).
 */

#ifndef JUXTA_ID_H_
#define JUXTA_ID_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** "JX_" + 6 hex digits + NUL */
#define JUXTA_ID_LEN 10U

/**
 * Fill @p out with JX_XXXXXX from the first Bluetooth identity address.
 * Requires bt_enable() first. On empty identity list uses JX_000000.
 *
 * @return 0 on success, negative errno on bad args.
 */
int juxta_id_fill_from_bt(char *out, size_t out_sz);

/**
 * @return true if @p name is JX_ + six hex digits and not equal to @p self_name.
 */
bool juxta_id_is_peer_name(const char *name, const char *self_name);

/**
 * Lexicographic compare of two JX_ names (or raw 6-hex suffixes).
 * @return <0 if a < b, 0 if equal, >0 if a > b.
 */
int juxta_id_cmp(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* JUXTA_ID_H_ */
