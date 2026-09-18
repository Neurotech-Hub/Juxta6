#include "juxta_time.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

/*
 * base_unix and base_uptime_ms are a pair: pairing an old unix base with a
 * new uptime base (or vice versa) yields a wrong wall clock until the next
 * sync. juxta_time_set() runs on the BLE path while juxta_time_now() runs
 * on main/logging paths, so both fields are read/written under a spinlock.
 */
static struct k_spinlock time_lock;
static uint32_t base_unix;
static int64_t base_uptime_ms;

static bool is_leap(int year)
{
	return ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0));
}

void juxta_time_init(void)
{
	k_spinlock_key_t key = k_spin_lock(&time_lock);

	base_uptime_ms = k_uptime_get();
	base_unix = 0U;
	k_spin_unlock(&time_lock, key);
}

void juxta_time_set(uint32_t unix_time)
{
	k_spinlock_key_t key = k_spin_lock(&time_lock);

	base_uptime_ms = k_uptime_get();
	base_unix = unix_time;
	k_spin_unlock(&time_lock, key);
}

uint32_t juxta_time_now(void)
{
	uint32_t unix_time;
	int64_t uptime_base;
	k_spinlock_key_t key = k_spin_lock(&time_lock);

	unix_time = base_unix;
	uptime_base = base_uptime_ms;
	k_spin_unlock(&time_lock, key);

	if (unix_time == 0U) {
		return 0U;
	}

	return unix_time + (uint32_t)((k_uptime_get() - uptime_base) / 1000);
}

bool juxta_time_is_set(void)
{
	bool set;
	k_spinlock_key_t key = k_spin_lock(&time_lock);

	set = base_unix != 0U;
	k_spin_unlock(&time_lock, key);
	return set;
}

static void juxta_time_ymd(uint32_t unix_time, int *year, int *month, int *day)
{
	static const uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	uint32_t days = unix_time / 86400U;
	int y = 1970;

	while (true) {
		uint32_t diy = is_leap(y) ? 366U : 365U;

		if (days < diy) {
			break;
		}
		days -= diy;
		y++;
	}

	int m = 1;

	for (size_t i = 0; i < ARRAY_SIZE(month_days); i++) {
		uint32_t dim = month_days[i];

		if (i == 1U && is_leap(y)) {
			dim++;
		}
		if (days < dim) {
			m = (int)i + 1;
			break;
		}
		days -= dim;
	}

	*year = y;
	*month = m;
	*day = (int)days + 1;
}

void juxta_time_date_string(uint32_t unix_time, char out[9])
{
	int year;
	int month;
	int day;

	if (unix_time == 0U) {
		memcpy(out, "19700101", 9);
		return;
	}

	juxta_time_ymd(unix_time, &year, &month, &day);
	/* Clamp so YYYYMMDD always fits out[9]; silences -Wformat-truncation. */
	year = CLAMP(year, 1970, 9999);
	month = CLAMP(month, 1, 12);
	day = CLAMP(day, 1, 31);
	(void)snprintf(out, 9, "%04d%02d%02d", year, month, day);
}
