/* SPDX-License-Identifier: MIT */

#ifndef CANSUB_COMMON_H
#define CANSUB_COMMON_H

/***********************************************************************************************************************
* Includes.
***********************************************************************************************************************/
/* Standard includes. */
#include <stdbool.h>

/* Project includes. */
#include "cansub/cansub_ffi.h"

/***********************************************************************************************************************
* Public definitions.
***********************************************************************************************************************/

/** Bit of a state, for sets of states. The enum must be sequential from 0 and have fewer than 32 values. */
#define CANSUB_STATE_BIT(state) (UINT32_C(1) << (state))

/** True when the state is one of the states in the mask. */
#define CANSUB_STATE_IN(state, mask) ((CANSUB_STATE_BIT(state) & (mask)) != 0u)

/** Shift of a stream's transport flags in cansub_ready. Each stream has its flags in its own byte, see cansub_ready_code. */
#define CANSUB_READY_STREAM_SHIFT(stream) (8u * (uint32_t)(stream))

/** The transport flags of one stream, once shifted to the positions of the data stream flags. */
#define CANSUB_READY_STREAM_MASK (0xFFu)

/** Macro helper for returning the smaller element. */
#define MIN(a, b) ((a) < (b) ? (a) : (b))

/** Macro helper for returning the larger element. */
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/** Macro helper to clamp a value between two bounds. */
#define CLAMP(value, min, max) (MIN(MAX(value, min), max))

/** Macro helper for the absolute difference of two values, without an underflow for unsigned types. */
#define ABS_DIFF(a, b) ((a) < (b) ? (b) - (a) : (a) - (b))

/** Macro helper for getting the length of an array. */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/** Macro helper for getting a container from an element inside the container pointer. */
#define CONTAINER_OF(ptr, type, member) ((type *)((uint8_t *)(ptr) - offsetof(type, member)))
#define CONTAINER_OF_CONST(ptr, type, member) ((type const *)((uint8_t const *)(ptr) - offsetof(type, member)))

/***********************************************************************************************************************
* Public type declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public variable declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public function declarations.
***********************************************************************************************************************/

bool cansub_parse_u32(char const *text, uint32_t limit, uint32_t *value);

/**
 * Get the lower-case ASCII output for an input character.
 *
 * @param c Input.
 * @return Lower-case ASCII character output.
 */
char cansub_ascii_lower(char c);

/**
 * Check if a character is a decimal.
 *
 * @param c Input.
 * @return True if a decimal, false otherwise.
 */
bool cansub_ascii_isdigit(char c);

/**
 * Check if a character is hexadecimal.
 *
 * @param c Input.
 * @return True if a hexadecimal, false otherwise.
 */
bool cansub_ascii_isxdigit(char c);

/**
 * Check if a character is a control character or outside the ASCII range.
 *
 * @param c Input.
 * @return True if a control character or outside the ASCII range, false otherwise.
 */
bool cansub_ascii_iscntrl(char c);

/**
 * Check if a string is hexadecimal only.
 *
 * @param str NUL terminated input ASCII string.
 * @return True if hexadecimal only, false otherwise.
 */
bool cansub_ascii_is_hex_str(char const *str);

/**
 * Check if a string only holds characters of a DNS name or an IPv4 address: Letters, digits, '-', '.' and '_'.
 *
 * @param str NUL terminated input string.
 * @return True if only host characters, false otherwise.
 */
bool cansub_ascii_is_host_str(char const *str);

/**
 * Case-insensitive comparison of two NUL terminated ASCII strings.
 *
 * @param a NUL terminated ASCII string input a.
 * @param b NUL terminated ASCII string input b.
 * @return Result.
 */
int cansub_strcasecmp(char const *a, char const *b);

/**
 * ASCII case-insensitive comparison of a NUL terminated string with a string of known size.
 *
 * @param a NUL terminated ASCII string input.
 * @param b Second ASCII string input.
 * @param b_size Length of the second ASCII string input, need not include NUL.
 * @return Result.
 */
int cansub_strcasecmp_sized(char const *a, char const *b, size_t b_size);

/**
 * ASCII case-insensitive comparison of two string of known size.
 *
 * @param a First ASCII string input.
 * @param a_size Length of the first ASCII string input, need not include NUL.
 * @param b Second ASCII string input.
 * @param b_size Length of the second ASCII string input, need not include NUL.
 * @return Result.
 */
int cansub_memcasecmp(char const *a, size_t a_size, char const *b, size_t b_size);

#endif