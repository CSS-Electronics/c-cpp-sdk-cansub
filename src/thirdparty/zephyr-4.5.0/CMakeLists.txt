# SPDX-License-Identifier: MIT

# This is a minimal CMakeLists.txt file to use only the context of Zephyr required for the cansub library.

project(Zephyr)

set(ZEPHYR_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/net_buf/buf_simple.c
    ${CMAKE_CURRENT_SOURCE_DIR}/subsys/crc/crc32_sw.c
)

add_library(zephyr STATIC ${ZEPHYR_SOURCE_FILES})
target_include_directories(zephyr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Normally set by Kconfig.
if(CMAKE_C_BYTE_ORDER STREQUAL "BIG_ENDIAN")
    target_compile_definitions(zephyr PUBLIC CONFIG_BIG_ENDIAN)
else()
    target_compile_definitions(zephyr PUBLIC CONFIG_LITTLE_ENDIAN)
endif()
