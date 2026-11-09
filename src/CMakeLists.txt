# SPDX-License-Identifier: MIT

project(cansub C)

# Global config.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)

# Enforce C11 standard.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Third-party libraries.
set(ENABLE_TESTING OFF CACHE BOOL "Build Mbed TLS tests")
set(ENABLE_PROGRAMS OFF CACHE BOOL "Build Mbed TLS programs")
set(GEN_FILES OFF CACHE BOOL "Regenerate Mbed TLS generated files")
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "Treat Mbed TLS warnings as errors")
if(NOT WIN32)
    set(LINK_WITH_PTHREAD ON CACHE BOOL "" FORCE)
endif()

# Enforce a minimal mbedTLS configuration.
set(MBEDTLS_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/cansub_mbedtls_config.h" CACHE FILEPATH "mbedTLS config file" FORCE)
set(TF_PSA_CRYPTO_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/cansub_crypto_config.h" CACHE FILEPATH "TF-PSA-Crypto config file" FORCE)

if(WIN32)
    # MBEDTLS_THREADING_ALT: mbedtls/threading.h includes threading_alt.h, in the mbedTLS and in the library sources.
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/mbedtls_alt)
endif()
add_subdirectory(thirdparty/mbedtls-4.2.0)

add_subdirectory(thirdparty/zephyr-4.5.0)

add_library(picohttpparser STATIC ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/picohttpparser-1.5/picohttpparser.c)
target_include_directories(picohttpparser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/picohttpparser-1.5)

set(LIBRARY_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cansub.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/stream_control.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/stream_data.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/websocket.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cansub_protocol/cansub_protocol_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cansub_protocol/cansub_protocol_decode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cansub_protocol/cansub_protocol_encode.c
)

set(LIBRARY_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

set(LIBRARY_INCLUDE_DIRS_PRIVATE
)

set(LIBRARY_THIRDPARTY_LIBS
    mbedtls
    picohttpparser
    zephyr
)

set(LIBRARY_LINK_LIBS
)

# Handle certificate.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)

set(CANSUB_ROOT_CERT "${CMAKE_CURRENT_SOURCE_DIR}/src/cansub_root_cert.crt" CACHE FILEPATH "PEM root certificate(s) to embed")
file(READ "${CANSUB_ROOT_CERT}" ROOT_CERT_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," ROOT_CERT_BYTES "${ROOT_CERT_HEX}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/generated/root_cert.c" CONTENT
    "/* SPDX-License-Identifier: MIT */\n\n#include <stddef.h>\n\n/* Generated from ${CANSUB_ROOT_CERT} */\nunsigned char const cansub_root_cert[] = {${ROOT_CERT_BYTES} 0x00};\nsize_t const cansub_root_cert_size = sizeof(cansub_root_cert);\n")
list(APPEND LIBRARY_SOURCE_FILES ${CMAKE_BINARY_DIR}/generated/root_cert.c)

if(WIN32)
    list(APPEND LIBRARY_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/mbedtls_alt/threading_alt.c)
else()
    find_package(Threads REQUIRED)
    list(APPEND LIBRARY_LINK_LIBS Threads::Threads)
endif()

# Library version from CANSUB_VERSION in the public header (CALVER: 0xYYYYMMDD as 16.8.8 bits).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../include/cansub/cansub_ffi.h" _cansub_version_line
        REGEX "^#define CANSUB_VERSION 0x[0-9A-Fa-f]+$")
string(REGEX REPLACE "^#define CANSUB_VERSION (0x[0-9A-Fa-f]+)$" "\\1" _cansub_version_hex "${_cansub_version_line}")
math(EXPR CANSUB_VERSION_YEAR  "(${_cansub_version_hex} >> 16) & 0xFFFF")
math(EXPR CANSUB_VERSION_MONTH "(${_cansub_version_hex} >> 8) & 0xFF")
math(EXPR CANSUB_VERSION_DAY   "${_cansub_version_hex} & 0xFF")
set(CANSUB_VERSION_STRING "${CANSUB_VERSION_YEAR}.${CANSUB_VERSION_MONTH}.${CANSUB_VERSION_DAY}")

add_library(cansub STATIC ${LIBRARY_SOURCE_FILES})
add_library(cansub_shared SHARED ${LIBRARY_SOURCE_FILES})

# Public targets. Consumers link against these, all other targets are internal.
add_library(cansub::cansub ALIAS cansub)
add_library(cansub::cansub_shared ALIAS cansub_shared)

# Set common properties for both libraries.
set(CANSUB_LIBRARIES cansub cansub_shared)
foreach (lib ${CANSUB_LIBRARIES})
    target_include_directories(${lib} PUBLIC ${LIBRARY_INCLUDE_DIRS})
    target_include_directories(${lib} PRIVATE ${LIBRARY_INCLUDE_DIRS_PRIVATE})
    target_link_libraries(${lib} PRIVATE ${LIBRARY_LINK_LIBS})
    target_compile_options(${lib} PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic> $<$<C_COMPILER_ID:MSVC>:/W4>)
    target_link_options(${lib} PRIVATE $<$<PLATFORM_ID:Linux>:LINKER:--no-undefined>)
endforeach ()

# Properties for the static library.
target_compile_definitions(cansub PUBLIC CANSUB_STATIC)

# Bundle the third-party objects into the static library, so it's the only archive to link.
# The third-party targets are compile-only here, so they are not passed on to consumers as link dependencies.
foreach (lib ${LIBRARY_THIRDPARTY_LIBS})
    target_link_libraries(cansub PRIVATE $<COMPILE_ONLY:${lib}>)
endforeach ()

# TF-PSA-Crypto builds its drivers and utilities as object libraries, which only its SOURCES lists.
get_target_property(_tfpsacrypto_objlibs tfpsacrypto SOURCES)
list(FILTER _tfpsacrypto_objlibs INCLUDE REGEX "^\\$<TARGET_OBJECTS:")
target_sources(cansub PRIVATE
        $<TARGET_OBJECTS:mbedtls>
        $<TARGET_OBJECTS:mbedx509>
        $<TARGET_OBJECTS:tfpsacrypto>
        ${_tfpsacrypto_objlibs}
        $<TARGET_OBJECTS:zephyr>
        $<TARGET_OBJECTS:picohttpparser>
)

if(MSVC)
    # The import library of cansub.dll is also named cansub.lib, rename the static library.
    set_target_properties(cansub PROPERTIES OUTPUT_NAME cansub_static)
endif()

# Properties for the dynamic library.
set_target_properties(cansub_shared PROPERTIES OUTPUT_NAME cansub)
target_link_libraries(cansub_shared PRIVATE ${LIBRARY_THIRDPARTY_LIBS})
target_compile_definitions(cansub_shared PRIVATE CANSUB_BUILDING_SHARED)

set_target_properties(cansub_shared PROPERTIES
    VERSION   ${CANSUB_VERSION_STRING}
    SOVERSION ${CANSUB_VERSION_STRING}
)

if(WIN32)
    # cansub.dll for Windows.
    set_target_properties(cansub_shared PROPERTIES PREFIX "")
endif()

if(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$" AND NOT CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # Strip the symbol table from release builds of the shared library.
    target_link_options(cansub_shared PRIVATE $<$<CONFIG:Release,MinSizeRel>:-s>)
endif()
