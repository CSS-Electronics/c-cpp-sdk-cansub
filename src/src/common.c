/* SPDX-License-Identifier: MIT */

/***********************************************************************************************************************
 * Includes.
 **********************************************************************************************************************/
/* Standard includes. */
#include <string.h>

/* Third-party includes. */

/* Project includes. */
#include "common.h"

/***********************************************************************************************************************
 * External declarations.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public variable definition.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private definitions.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private type definitions.
 **********************************************************************************************************************/

/***********************************************************************************************************************
* Private variable definitions.
***********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function declarations.
 **********************************************************************************************************************/

/***********************************************************************************************************************
* Public function definitions.
***********************************************************************************************************************/

bool cansub_parse_u32(char const *text, uint32_t limit, uint32_t *value)
{
    uint64_t parsed = 0;

    if (text[0] == '\0')
    {
        return false;
    }

    for (char const *c = text; *c != '\0'; c++)
    {
        if ((*c < '0') || (*c > '9'))
        {
            return false;
        }

        parsed = (parsed * 10) + (uint64_t)(*c - '0');
        if (parsed > limit)
        {
            return false;
        }
    }

    *value = (uint32_t)parsed;
    return true;
}

char cansub_ascii_lower(char c)
{
    if ((c >= 'A') && (c <= 'Z'))
    {
        c = (char)(c - 'A' + 'a');
    }

    return c;
}

bool cansub_ascii_isdigit(char c)
{
    return (c >= '0') && (c <= '9');
}

bool cansub_ascii_isxdigit(char c)
{
    char const lower = cansub_ascii_lower(c);

    return cansub_ascii_isdigit(c) || ((lower >= 'a') && (lower <= 'f'));
}

bool cansub_ascii_iscntrl(char c)
{
    unsigned char const v = (unsigned char)c;

    return (v <= 0x1F) || (v >= 0x7F);
}

bool cansub_ascii_is_hex_str(char const *str)
{
    size_t const length = strlen(str);
    for (size_t i = 0; i < length; i++)
    {
        if (!cansub_ascii_isxdigit(str[i]))
        {
            return false;
        }
    }

    return true;
}

bool cansub_ascii_is_host_str(char const *str)
{
    size_t const length = strlen(str);
    for (size_t i = 0; i < length; i++)
    {
        char const lower = cansub_ascii_lower(str[i]);
        bool const letter = (lower >= 'a') && (lower <= 'z');
        bool const symbol = (str[i] == '-') || (str[i] == '.') || (str[i] == '_');

        if (!letter && !cansub_ascii_isdigit(str[i]) && !symbol)
        {
            return false;
        }
    }

    return true;
}

int cansub_memcasecmp(char const *a, size_t a_size, char const *b, size_t b_size)
{
    if (a_size != b_size)
    {
        return (a_size < b_size) ? -1 : 1;
    }

    for (size_t i = 0; i < a_size; i++)
    {
        if (cansub_ascii_lower(a[i]) != cansub_ascii_lower(b[i]))
        {
            return a[i] < b[i] ? -1 : 1;
        }
    }

    return 0;
}

int cansub_strcasecmp_sized(char const *a, char const *b, size_t b_size)
{
    size_t const a_size = strlen(a);

    return cansub_memcasecmp(a, a_size, b, b_size);
}

int cansub_strcasecmp(char const *a, char const *b)
{
    size_t const a_size = strlen(a);
    size_t const b_size = strlen(b);

    return cansub_memcasecmp(a, a_size, b, b_size);
}

/***********************************************************************************************************************
* Private function definitions.
***********************************************************************************************************************/