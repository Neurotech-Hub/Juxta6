cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(juxta6_0_prod LANGUAGES C)

target_sources(app PRIVATE
	src/main.c
	src/ble_service.c
	src/juxta_time.c
	src/juxta_settings.c
	src/juxta_rtt_log.c
	src/juxta_antenna.c
	src/juxta_log.c
	src/juxta_checkpoint.c
	${CMAKE_CURRENT_SOURCE_DIR}/../../lib/juxta_id/src/juxta_id.c
	${CMAKE_CURRENT_SOURCE_DIR}/../../lib/juxta_motion/src/juxta_motion.c
	${CMAKE_CURRENT_SOURCE_DIR}/../../lib/juxta_vdd/src/juxta_vdd.c
)
target_include_directories(app PRIVATE
	src
	${CMAKE_CURRENT_SOURCE_DIR}/../../lib/juxta_id/include
	${CMAKE_CURRENT_SOURCE_DIR}/../../lib/juxta_motion/include
	${CMAKE_CURRENT_SOURCE_DIR}/../../lib/juxta_vdd/include
)
