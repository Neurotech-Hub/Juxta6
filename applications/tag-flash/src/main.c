/*
 * Destructive test on the LAST erase page of MX25L3233F only (4 MiB).
 * Never erases offset 0; never chip-erases.
 *
 * Requires boards/nrf54l15tag_nrf54l15_cpuapp.overlay (project U8 fitted).
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tag_flash, LOG_LEVEL_INF);

#if !DT_NODE_EXISTS(DT_ALIAS(spi_flash0))
#error "Enable MX25L3233F via boards/nrf54l15tag_nrf54l15_cpuapp.overlay (alias spi-flash0)"
#endif

#define FLASH_NODE DT_ALIAS(spi_flash0)
#define TEST_PATTERN_CAP 256U
#define EXPECTED_SIZE_BYTES (4ULL * 1024ULL * 1024ULL)

int main(void)
{
	const struct device *flash = DEVICE_DT_GET(FLASH_NODE);
	struct flash_pages_info page_info;
	const struct flash_parameters *params;
	uint8_t wbuf[TEST_PATTERN_CAP];
	uint8_t rbuf[TEST_PATTERN_CAP];
	uint64_t flash_size = 0;
	off_t test_offs;
	size_t test_len;
	size_t wbs;
	int rc;

	LOG_WRN("FLASH destroy test: LAST erase sector only (not offset 0)");

	if (!device_is_ready(flash)) {
		LOG_ERR("[PROBE] FAIL device not ready");
		return -ENODEV;
	}

	params = flash_get_parameters(flash);
	wbs = params->write_block_size;
	LOG_INF("[PROBE] ok write_block_size=%zu erase_value=0x%02x", (size_t)wbs,
		params->erase_value);

	rc = flash_get_size(flash, &flash_size);
	if (rc != 0) {
		LOG_ERR("[PROBE] FAIL flash_get_size rc=%d", rc);
		return rc;
	}

	LOG_INF("[PROBE] flash_size=%llu bytes (expect %llu)", (unsigned long long)flash_size,
		(unsigned long long)EXPECTED_SIZE_BYTES);
	if (flash_size != EXPECTED_SIZE_BYTES) {
		LOG_WRN("[PROBE] size mismatch vs 8 MiB — continuing with reported size");
	}

	if (flash_size == 0U) {
		LOG_ERR("[PROBE] FAIL flash_size=0");
		return -EINVAL;
	}

	/* Last byte offset → page containing it. */
	rc = flash_get_page_info_by_offs(flash, (off_t)(flash_size - 1U), &page_info);
	if (rc != 0) {
		LOG_ERR("[PROBE] FAIL flash_get_page_info_by_offs(last) rc=%d", rc);
		return rc;
	}

	test_offs = page_info.start_offset;
	LOG_INF("[PROBE] last_page offs=%zu size=%zu index=%u", (size_t)test_offs,
		(size_t)page_info.size, page_info.index);

	if (test_offs == 0) {
		LOG_ERR("[PROBE] FAIL refusing to destroy page at offset 0");
		return -EINVAL;
	}

	test_len = TEST_PATTERN_CAP;
	if (test_len > page_info.size) {
		test_len = (size_t)page_info.size;
	}
	test_len = (test_len / wbs) * wbs;
	if (test_len == 0U) {
		LOG_ERR("[PROBE] FAIL test_len=0");
		return -EINVAL;
	}

	LOG_INF("[ERASE] offs=%zu size=%zu", (size_t)test_offs, (size_t)page_info.size);
	rc = flash_erase(flash, test_offs, page_info.size);
	if (rc != 0) {
		LOG_ERR("[ERASE] FAIL rc=%d", rc);
		return rc;
	}
	LOG_INF("[ERASE] PASS");

	for (size_t i = 0; i < test_len; i++) {
		wbuf[i] = (uint8_t)(i & 0xFF);
	}

	LOG_INF("[WRITE] offs=%zu len=%zu", (size_t)test_offs, test_len);
	rc = flash_write(flash, test_offs, wbuf, test_len);
	if (rc != 0) {
		LOG_ERR("[WRITE] FAIL rc=%d", rc);
		return rc;
	}
	LOG_INF("[WRITE] PASS");

	memset(rbuf, 0, sizeof(rbuf));
	rc = flash_read(flash, test_offs, rbuf, test_len);
	if (rc != 0) {
		LOG_ERR("[READ] FAIL rc=%d", rc);
		return rc;
	}

	for (size_t i = 0; i < test_len; i++) {
		if (rbuf[i] != wbuf[i]) {
			LOG_ERR("[VERIFY] FAIL at +%zu exp=0x%02x got=0x%02x", i, wbuf[i], rbuf[i]);
			return -EIO;
		}
	}

	LOG_INF("[VERIFY] PASS len=%zu", test_len);
	LOG_INF("tag-flash done");
	return 0;
}
