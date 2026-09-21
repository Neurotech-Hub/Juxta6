#include "juxta_id.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/util.h>

static bool hex6_ok(const char *p)
{
	for (size_t i = 0U; i < 6U; i++) {
		if (!isxdigit((unsigned char)p[i])) {
			return false;
		}
	}
	return true;
}

static bool juxta_adv_name_ok(const char *name)
{
	if (name == NULL || strlen(name) != (JUXTA_ID_LEN - 1U)) {
		return false;
	}
	if (strncmp(name, "JX_", 3) != 0 && strncmp(name, "JB_", 3) != 0) {
		return false;
	}
	return hex6_ok(name + 3);
}

int juxta_id_hex_from_bt(char *out, size_t out_sz)
{
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(addrs);

	if (out == NULL || out_sz < 7U) {
		return -EINVAL;
	}

	bt_id_get(addrs, &count);
	if (count == 0U) {
		(void)snprintf(out, out_sz, "000000");
		return 0;
	}

	(void)snprintf(out, out_sz, "%02X%02X%02X", addrs[0].a.val[2], addrs[0].a.val[1],
		       addrs[0].a.val[0]);
	return 0;
}

int juxta_id_format_from_bt(char *out, size_t out_sz, bool basestation)
{
	char hex[7];
	int rc;

	if (out == NULL || out_sz < JUXTA_ID_LEN) {
		return -EINVAL;
	}

	rc = juxta_id_hex_from_bt(hex, sizeof(hex));
	if (rc != 0) {
		return rc;
	}

	(void)snprintf(out, out_sz, "%s%s", basestation ? "JB_" : "JX_", hex);
	return 0;
}

int juxta_id_fill_from_bt(char *out, size_t out_sz)
{
	return juxta_id_format_from_bt(out, out_sz, false);
}

bool juxta_id_is_peer_name(const char *name, const char *self_name)
{
	if (name == NULL || self_name == NULL) {
		return false;
	}
	if (!juxta_adv_name_ok(name)) {
		return false;
	}
	return strcmp(name, self_name) != 0;
}

int juxta_id_to_jxb_peer(const char *adv_name, char *out, size_t out_sz)
{
	char role;

	if (out == NULL || out_sz < JUXTA_JXB_PEER_LEN || !juxta_adv_name_ok(adv_name)) {
		return -EINVAL;
	}

	role = (adv_name[1] == 'B') ? 'B' : 'X';
	(void)snprintf(out, out_sz, "%c%.6s", role, adv_name + 3);
	return 0;
}

int juxta_id_cmp(const char *a, const char *b)
{
	if (a == NULL && b == NULL) {
		return 0;
	}
	if (a == NULL) {
		return -1;
	}
	if (b == NULL) {
		return 1;
	}

	return strcmp(a, b);
}
