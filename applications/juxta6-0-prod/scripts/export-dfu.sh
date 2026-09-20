#!/usr/bin/env bash
# Copy Sysbuild's dfu_application.zip to dist/juxta6-0-prod-<version>.zip
# for nRF Device Manager. Run after a successful nRF Connect / west build.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_DIR="$(cd "${APP_DIR}/../.." && pwd)"
HEADER="${APP_DIR}/src/juxta_prod.h"
DIST_DIR="${REPO_DIR}/dist"

if [[ ! -f "${HEADER}" ]]; then
	echo "error: missing ${HEADER}" >&2
	exit 1
fi

VERSION="$(sed -n 's/^#define JUXTA_FIRMWARE_VERSION "\([^"]*\)".*/\1/p' "${HEADER}" | head -n 1)"
if [[ -z "${VERSION}" ]]; then
	echo "error: could not parse JUXTA_FIRMWARE_VERSION from ${HEADER}" >&2
	exit 1
fi

SRC=""
for candidate in \
	"${APP_DIR}/build/dfu_application.zip" \
	"${APP_DIR}/build/juxta6-0-prod/zephyr/dfu_application.zip" \
	"${APP_DIR}/build/zephyr/dfu_application.zip"
do
	if [[ -f "${candidate}" ]]; then
		SRC="${candidate}"
		break
	fi
done

if [[ -z "${SRC}" ]]; then
	echo "error: dfu_application.zip not found under ${APP_DIR}/build" >&2
	echo "Build juxta6-0-prod with Sysbuild first (nRF Connect)." >&2
	exit 1
fi

mkdir -p "${DIST_DIR}"
DEST="${DIST_DIR}/juxta6-0-prod-${VERSION}.zip"
cp -f "${SRC}" "${DEST}"
echo "Copied ${SRC}"
echo "     -> ${DEST}"
