cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tag_vdd LANGUAGES C)

target_sources(app PRIVATE
	src/main.c
	${CMAKE_CURRENT_SOURCE_DIR}/../../lib/juxta_vdd/src/juxta_vdd.c
)
target_include_directories(app PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../lib/juxta_vdd/include
)
