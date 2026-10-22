/*
 * tag-nor-dpd: isolate whether MX25L3233F deep power-down via Zephyr runtime
 * PM (juxta6-0-prod 6.5.2) can return non-erased junk when reading erased
 * flash, or corrupt reads/writes of valid data.
 *
 * Background: a prod JXV file reported a stored length ending exactly 0x20200,
 * ~100 KB past the real data, which means a boot-time find_eof_or_erased()
 * scan read ~400 consecutive 256-byte chunks of erased flash as non-0xFF.
 *
 * Scratch area: ONLY the last 64 KB (0x3F0000..0x3FFFFF), prod's checkpoint
 * ring. The CSV regions are never touched (reads beyond that are read-only).
 *
 * Flow (state in __noinit RAM across reboots):
 *   fresh boot : erase scratch, write ~29.7 KB of CSV-like rows (no 0xFF),
 *                stress (a) back-to-back scans, (b) scans spaced past the DPD
 *                dwell, (c) per-read jitter around the dwell, then append
 *                rows with random gaps, then time a full-chip read.
 *   boot loop  : 200 cold reboots mirroring prod boot order; odd boots reboot
 *                right after the last read (chip awake), even boots wait past
 *                the dwell first (chip in DPD at reset).
 * Result: one "[DPD] RESULT PASS|FAIL ..." line (repeated every 10 s at end).
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(tag_nor_dpd, LOG_LEVEL_INF);

#if !DT_NODE_EXISTS(DT_ALIAS(spi_mem))
#error "Enable MX25L3233F via boards/nrf54l15tag_nrf54l15_cpuapp.overlay (alias spi-mem)"
#endif

#define FLASH_NODE DT_ALIAS(spi_mem)

#define SCRATCH_START 0x3F0000U
#define SCRATCH_SIZE 0x010000U
#define CHUNK 256U /* same read size as prod find_eof_or_erased */
#define EXP_CAP 0x9000U
#define SETUP_TARGET 29700U
#define TAIL_CHECK 4096U /* erased bytes verified after the data end */

#define STRESS_A_SCANS 2000U
#define STRESS_B_SCANS 500U
#define STRESS_C_SCANS 500U
#define STRESS_C_CHUNKS 16U
#define WRITE_ROWS 200U
#define BOOT_TARGET 200U
#define FULL_READ_END 0x3F0000U /* read-only timing check: 0 .. scratch start */
#define FULL_READ_CHUNK 4096U

#define STATE_MAGIC 0x4450444EU /* 'DPDN' */
#define STAGE_BOOT_LOOP 1U

struct test_state {
	uint32_t magic;
	uint32_t stage;
	uint32_t rows; /* rows currently in flash */
	uint32_t boots_done;
	uint32_t scans;
	uint32_t scan_fail;
	uint32_t write_fail;
	uint32_t read_ms;
	uint32_t read_fail;
};

static __noinit struct test_state st;

static const struct device *const flash = DEVICE_DT_GET(FLASH_NODE);
static uint8_t exp_img[EXP_CAP];
static uint32_t exp_len;
static uint8_t buf[FULL_READ_CHUNK];
static uint32_t rng_state;

static const char *rpm_str(void)
{
	return IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME) ? "on" : "off";
}

static uint32_t rnd(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static void sleep_rand_ms(uint32_t lo, uint32_t hi)
{
	if (hi == 0U) {
		return;
	}
	k_sleep(K_MSEC(lo + (rnd() % (hi - lo + 1U))));
}

/* ---- deterministic reference image -------------------------------------- */

static int make_row(uint32_t i, char *out, size_t n)
{
	return snprintf(out, n, "%u,%u,%d.%02d,%d.%d,%d.%d\n", 4000U + i * 60U, (i * 37U) % 256U, 3,
			(int)(i % 100U), 20 + (int)(i % 9U), (int)(i % 10U), 30 + (int)(i % 20U),
			(int)((i * 7U) % 10U));
}

static bool append_exp_row(uint32_t i)
{
	char row[48];
	int n = make_row(i, row, sizeof(row));

	if (n <= 0 || exp_len + (uint32_t)n > EXP_CAP) {
		return false;
	}
	memcpy(&exp_img[exp_len], row, (size_t)n);
	exp_len += (uint32_t)n;
	return true;
}

static void build_exp(uint32_t rows)
{
	memset(exp_img, 0xFF, sizeof(exp_img));
	exp_len = 0U;
	for (uint32_t i = 0U; i < rows; i++) {
		(void)append_exp_row(i);
	}
}

static uint8_t ref_at(uint32_t rel)
{
	return rel < exp_len ? exp_img[rel] : 0xFFU;
}

/* ---- checked reads ------------------------------------------------------ */

static void log_mismatch(uint32_t rel, size_t len, size_t bad_i, const uint8_t *data)
{
	enum pm_device_state pm;
	int pm_rc = pm_device_state_get(flash, &pm);
	size_t nonff = 0U;
	for (size_t i = 0U; i < len; i++) {
		if (data[i] != 0xFFU) {
			nonff++;
		}
	}
	LOG_ERR("[DPD] MISMATCH off=0x%06x exp=0x%02x got=0x%02x chunk_rel=0x%05x len=%u "
		"nonFF_in_chunk=%u pm_state=%d",
		(unsigned int)(SCRATCH_START + rel + bad_i), ref_at(rel + (uint32_t)bad_i),
		data[bad_i], (unsigned int)rel, (unsigned int)len, (unsigned int)nonff,
		pm_rc == 0 ? (int)pm : -1);
	LOG_HEXDUMP_ERR(data, MIN(len, (size_t)16U), "[DPD] chunk head");
}

/* Read [rel, rel+len) (<= CHUNK) from scratch and compare to the reference. */
static bool read_check(uint32_t rel, size_t len)
{
	int rc = flash_read(flash, SCRATCH_START + rel, buf, len);

	if (rc != 0) {
		LOG_ERR("[DPD] flash_read rc=%d @0x%06x", rc, (unsigned int)(SCRATCH_START + rel));
		return false;
	}
	for (size_t i = 0U; i < len; i++) {
		if (buf[i] != ref_at(rel + (uint32_t)i)) {
			log_mismatch(rel, len, i, buf);
			return false;
		}
	}
	return true;
}

/*
 * Prod find_eof_or_erased equivalent: 256 B chunk reads until the first 0xFF,
 * with every byte before it also compared to the reference. Then the erased
 * tail (TAIL_CHECK bytes) is verified. Optional per-read jitter.
 */
static bool scan_once(uint32_t jit_lo, uint32_t jit_hi)
{
	uint32_t found_end = SCRATCH_SIZE;
	bool ok = true;
	uint32_t off = 0U;

	while (off < SCRATCH_SIZE) {
		size_t len = MIN(CHUNK, SCRATCH_SIZE - off);
		int rc = flash_read(flash, SCRATCH_START + off, buf, len);
		bool hit = false;

		if (rc != 0) {
			LOG_ERR("[DPD] scan read rc=%d @0x%06x", rc,
				(unsigned int)(SCRATCH_START + off));
			ok = false;
			break;
		}
		for (size_t i = 0U; i < len; i++) {
			if (buf[i] == 0xFFU) {
				found_end = off + (uint32_t)i;
				hit = true;
				break;
			}
			if (buf[i] != ref_at(off + (uint32_t)i)) {
				log_mismatch(off, len, i, buf);
				ok = false;
				hit = true;
				found_end = off + (uint32_t)i;
				break;
			}
		}
		if (hit) {
			break;
		}
		off += (uint32_t)len;
		sleep_rand_ms(jit_lo, jit_hi);
	}

	if (ok && found_end != exp_len) {
		LOG_ERR("[DPD] SCAN END MISMATCH found=%u expected=%u", (unsigned int)found_end,
			(unsigned int)exp_len);
		ok = false;
	}
	if (ok) {
		uint32_t t = exp_len;
		uint32_t t_end = MIN(exp_len + TAIL_CHECK, SCRATCH_SIZE);

		while (ok && t < t_end) {
			size_t len = MIN(CHUNK, t_end - t);

			ok = read_check(t, len);
			t += (uint32_t)len;
			sleep_rand_ms(jit_lo, jit_hi);
		}
	}

	st.scans++;
	if (!ok) {
		st.scan_fail++;
	}
	return ok;
}

/* Random 16-chunk window with jitter between reads (races put_async vs get). */
static bool window_check(uint32_t jit_lo, uint32_t jit_hi)
{
	uint32_t span_chunks = (MIN(exp_len + TAIL_CHECK, SCRATCH_SIZE)) / CHUNK;
	uint32_t first = rnd() % (span_chunks - STRESS_C_CHUNKS + 1U);
	bool ok = true;

	for (uint32_t c = 0U; c < STRESS_C_CHUNKS && ok; c++) {
		ok = read_check((first + c) * CHUNK, CHUNK);
		sleep_rand_ms(jit_lo, jit_hi);
	}
	st.scans++;
	if (!ok) {
		st.scan_fail++;
	}
	return ok;
}

/* ---- phases ------------------------------------------------------------- */

static int deferred_nor_init(void)
{
	int err = device_init(flash);

	if (err == -EALREADY) {
		return device_is_ready(flash) ? 0 : -ENODEV;
	}
	return err;
}

static void print_result(void)
{
	bool pass = st.scan_fail == 0U && st.write_fail == 0U && st.read_fail == 0U &&
		    st.boots_done >= BOOT_TARGET;

	LOG_INF("[DPD] RESULT %s rpm=%s boots=%u scans=%u scan_fail=%u write_fail=%u "
		"read_fail=%u read_3p94MB_ms=%u",
		pass ? "PASS" : "FAIL", rpm_str(), (unsigned int)st.boots_done,
		(unsigned int)st.scans, (unsigned int)st.scan_fail, (unsigned int)st.write_fail,
		(unsigned int)st.read_fail, (unsigned int)st.read_ms);
}

static void halt_with_result(void)
{
	for (;;) {
		print_result();
		k_sleep(K_SECONDS(10));
	}
}

static int phase_setup(void)
{
	int rc;

	memset(&st, 0, sizeof(st));
	st.magic = STATE_MAGIC;

	LOG_WRN("[DPD] fresh start rpm=%s: erasing last 64 KB only (0x%06x)", rpm_str(),
		SCRATCH_START);
	rc = flash_erase(flash, SCRATCH_START, SCRATCH_SIZE);
	if (rc != 0) {
		LOG_ERR("[DPD] erase rc=%d", rc);
		return rc;
	}

	exp_len = 0U;
	memset(exp_img, 0xFF, sizeof(exp_img));
	while (exp_len < SETUP_TARGET) {
		if (!append_exp_row(st.rows)) {
			return -ENOSPC;
		}
		st.rows++;
	}

	for (uint32_t off = 0U; off < exp_len; off += CHUNK) {
		size_t len = MIN(CHUNK, exp_len - off);

		rc = flash_write(flash, SCRATCH_START + off, &exp_img[off], len);
		if (rc != 0) {
			LOG_ERR("[DPD] setup write rc=%d @%u", rc, (unsigned int)off);
			return rc;
		}
	}
	LOG_INF("[DPD] setup: rows=%u bytes=%u", (unsigned int)st.rows, (unsigned int)exp_len);

	if (!scan_once(0U, 0U)) {
		LOG_ERR("[DPD] setup verify FAILED");
		return -EIO;
	}
	return 0;
}

static void phase_stress(void)
{
	LOG_INF("[DPD] stress (a): %u back-to-back scans", STRESS_A_SCANS);
	for (uint32_t i = 0U; i < STRESS_A_SCANS; i++) {
		(void)scan_once(0U, 0U);
	}
	LOG_INF("[DPD] (a) done scans=%u fail=%u", (unsigned int)st.scans,
		(unsigned int)st.scan_fail);

	LOG_INF("[DPD] stress (b): %u scans with 11-20 ms gaps (DPD resume each scan)",
		STRESS_B_SCANS);
	for (uint32_t i = 0U; i < STRESS_B_SCANS; i++) {
		sleep_rand_ms(11U, 20U);
		(void)scan_once(0U, 0U);
	}
	LOG_INF("[DPD] (b) done scans=%u fail=%u", (unsigned int)st.scans,
		(unsigned int)st.scan_fail);

	LOG_INF("[DPD] stress (c): %u windows x %u reads, 5-15 ms between reads", STRESS_C_SCANS,
		STRESS_C_CHUNKS);
	for (uint32_t i = 0U; i < STRESS_C_SCANS; i++) {
		(void)window_check(5U, 15U);
	}
	LOG_INF("[DPD] (c) done scans=%u fail=%u", (unsigned int)st.scans,
		(unsigned int)st.scan_fail);
}

static void phase_write(void)
{
	LOG_INF("[DPD] write path: appending %u rows with 0-20 ms gaps", WRITE_ROWS);
	for (uint32_t n = 0U; n < WRITE_ROWS; n++) {
		uint32_t at = exp_len;
		char row[48];
		int len;
		size_t verify_len;
		int rc;

		sleep_rand_ms(0U, 20U);
		len = make_row(st.rows, row, sizeof(row));
		if (len <= 0 || !append_exp_row(st.rows)) {
			LOG_ERR("[DPD] write path: reference overflow");
			st.write_fail++;
			return;
		}
		rc = flash_write(flash, SCRATCH_START + at, row, (size_t)len);
		if (rc != 0) {
			LOG_ERR("[DPD] row write rc=%d @%u", rc, (unsigned int)at);
			st.write_fail++;
			return;
		}
		st.rows++;
		/* Row must read back and the bytes after it must still be erased. */
		verify_len = MIN((size_t)len + 16U, CHUNK);
		if (!read_check(at, verify_len)) {
			st.write_fail++;
			return;
		}
	}
	LOG_INF("[DPD] write path done rows=%u bytes=%u", (unsigned int)st.rows,
		(unsigned int)exp_len);
}

static void phase_full_read(void)
{
	int64_t t0 = k_uptime_get();

	for (uint32_t off = 0U; off < FULL_READ_END; off += FULL_READ_CHUNK) {
		int rc = flash_read(flash, off, buf, FULL_READ_CHUNK);

		if (rc != 0) {
			LOG_ERR("[DPD] full read rc=%d @0x%06x", rc, (unsigned int)off);
			st.read_fail++;
			break;
		}
	}
	st.read_ms = (uint32_t)(k_uptime_get() - t0);
	LOG_INF("[DPD] full read 0..0x%06x in 4 KB reads: %u ms", FULL_READ_END,
		(unsigned int)st.read_ms);
}

int main(void)
{
	uint32_t cause = 0U;
	int err;

	rng_state = k_cycle_get_32() | 1U;
	(void)hwinfo_get_reset_cause(&cause);
	(void)hwinfo_clear_reset_cause();

	/* Same order as prod: contact-settle sleep, then deferred device_init. */
	k_sleep(K_MSEC(500));
	err = deferred_nor_init();
	if (err != 0) {
		LOG_ERR("[DPD] NOR init FAILED rc=%d", err);
		return err;
	}

	if (st.magic != STATE_MAGIC) {
		err = phase_setup();
		if (err != 0) {
			st.write_fail++;
			halt_with_result();
		}
		phase_stress();
		phase_write();
		if (st.write_fail != 0U) {
			halt_with_result();
		}
		phase_full_read();
		st.stage = STAGE_BOOT_LOOP;
		LOG_INF("[DPD] boot loop: %u cold reboots", BOOT_TARGET);
		k_sleep(K_MSEC(100));
		sys_reboot(SYS_REBOOT_COLD);
	}

	if (st.stage != STAGE_BOOT_LOOP) {
		halt_with_result();
	}

	build_exp(st.rows);

	/* Scan immediately after init, like juxta_log_init's boot re-measure. */
	(void)scan_once(0U, 0U);
	st.boots_done++;
	LOG_INF("[DPD] boot %u/%u reset_cause=0x%x scans=%u scan_fail=%u variant=%s",
		(unsigned int)st.boots_done, BOOT_TARGET, (unsigned int)cause,
		(unsigned int)st.scans, (unsigned int)st.scan_fail,
		(st.boots_done % 2U) ? "reset-awake" : "reset-in-dpd");

	if (st.boots_done >= BOOT_TARGET) {
		halt_with_result();
	}

	if ((st.boots_done % 2U) != 0U) {
		/* Reset right after the last read: chip still inside the dwell. */
		LOG_PANIC();
	} else {
		/* Wait past the dwell so the chip is in DPD at reset. */
		k_sleep(K_MSEC(100));
	}
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}
