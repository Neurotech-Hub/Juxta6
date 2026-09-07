#include "juxta_id.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/util.h>

int juxta_id_fill_from_bt(char *out, size_t out_sz)
{
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(addrs);

	if (out == NULL || out_sz < JUXTA_ID_LEN) {
		return -EINVAL;
	}

	bt_id_get(addrs, &count);
	if (count == 0U) {
		(void)snprintf(out, out_sz, "JX_000000");
		return 0;
	}

	(void)snprintf(out, out_sz, "JX_%02X%02X%02X", addrs[0].a.val[2], addrs[0].a.val[1],
		       addrs[0].a.val[0]);
	return 0;
}

bool juxta_id_is_peer_name(const char *name, const char *self_name)
{
	if (name == NULL || self_name == NULL) {
		return false;
	}

	if (strlen(name) != (JUXTA_ID_LEN - 1U) || strncmp(name, "JX_", 3) != 0) {
		return false;
	}

	for (size_t i = 3U; i < (JUXTA_ID_LEN - 1U); i++) {
		if (!isxdigit((unsigned char)name[i])) {
			return false;
		}
	}

	return strcmp(name, self_name) != 0;
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
