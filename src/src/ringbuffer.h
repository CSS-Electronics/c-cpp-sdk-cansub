/* SPDX-License-Identifier: MIT */

#ifndef CANSUB_RINGBUFFER_H
#define CANSUB_RINGBUFFER_H

/***********************************************************************************************************************
* Includes.
***********************************************************************************************************************/
/* Standard includes. */
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/* Third-party includes. */

/* Project includes. */
#include "common.h"

/***********************************************************************************************************************
* Public definitions.
***********************************************************************************************************************/

/** True for a size cansub_ringbuffer_init() uses as is: A power of two. Other sizes are rounded up. */
#define CANSUB_RINGBUFFER_SIZE_EXACT(size) (((size) != 0) && (((size) & ((size) - 1)) == 0))

/***********************************************************************************************************************
* Public type declarations.
***********************************************************************************************************************/

typedef struct cansub_ringbuffer
{
    uint32_t read_index;
    uint32_t write_index;
    uint32_t size;
    uint8_t* buffer;
} cansub_ringbuffer;

typedef struct cansub_ringbuffer_spans
{
    void *span_1;
    size_t span_1_size;
    void *span_2;
    size_t span_2_size;
} cansub_ringbuffer_spans;

/***********************************************************************************************************************
* Public variable declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public function declarations.
***********************************************************************************************************************/

static inline cansub_result cansub_ringbuffer_init(cansub_ringbuffer *ringbuffer, uint32_t size)
{
    assert(ringbuffer);

    cansub_result result = CANSUB_RESULT_ERROR;

    do
    {
        memset(ringbuffer, 0x00, sizeof(*ringbuffer));

        if (size == 0 || size > 0x10000000)
        {
            result = CANSUB_RESULT_INVALID_ARG;
            break;
        }

        /* Find next power of two. */
        uint32_t next_pow = size - 1;
        next_pow |= next_pow >> 1;
        next_pow |= next_pow >> 2;
        next_pow |= next_pow >> 4;
        next_pow |= next_pow >> 8;
        next_pow |= next_pow >> 16;
        next_pow += 1;

        if (next_pow > INT32_MAX)
        {
            result = CANSUB_RESULT_INVALID_ARG;
            break;
        }

        ringbuffer->buffer = calloc(next_pow, sizeof(uint8_t));
        if (!ringbuffer->buffer)
        {
            result = CANSUB_RESULT_MEMORY;
            break;
        }

        ringbuffer->size = next_pow;
        ringbuffer->read_index = 0;
        ringbuffer->write_index = 0;

        result = CANSUB_RESULT_OK;
    } while (0);

    if (result != CANSUB_RESULT_OK && ringbuffer->buffer)
    {
        free(ringbuffer->buffer);
        ringbuffer->buffer = NULL;
    }

    return result;
}

static inline void cansub_ringbuffer_destroy(cansub_ringbuffer *ringbuffer)
{
    assert(ringbuffer);

    if (ringbuffer->buffer)
    {
        free(ringbuffer->buffer);
        ringbuffer->buffer = NULL;
        ringbuffer->size = 0;
        ringbuffer->read_index = 0;
        ringbuffer->write_index = 0;
    }
}

static inline uint32_t cansub_ringbuffer_size(cansub_ringbuffer const *ringbuffer)
{
    assert(ringbuffer);

    return ringbuffer->write_index - ringbuffer->read_index;
}

static inline bool cansub_ringbuffer_full(cansub_ringbuffer const *ringbuffer)
{
    assert(ringbuffer);

    return cansub_ringbuffer_size(ringbuffer) == ringbuffer->size;
}

static inline bool cansub_ringbuffer_empty(cansub_ringbuffer const *ringbuffer)
{
    assert(ringbuffer);

    return cansub_ringbuffer_size(ringbuffer) == 0;
}

static inline uint32_t cansub_ringbuffer_space(cansub_ringbuffer const *ringbuffer)
{
    assert(ringbuffer);

    return ringbuffer->size - cansub_ringbuffer_size(ringbuffer);
}

static inline uint32_t cansub_ringbuffer_mask(cansub_ringbuffer const *ringbuffer, uint32_t value)
{
    assert(ringbuffer);

    return value & (ringbuffer->size - 1);
}

static inline void cansub_ringbuffer_reset(cansub_ringbuffer *ringbuffer)
{
    assert(ringbuffer);

    ringbuffer->read_index = 0;
    ringbuffer->write_index = 0;
}

static inline cansub_ringbuffer_spans cansub_ringbuffer_spans_at(cansub_ringbuffer const *ringbuffer, uint32_t index, uint32_t length)
{
    assert(ringbuffer);

    uint32_t const offset = cansub_ringbuffer_mask(ringbuffer, index);
    cansub_ringbuffer_spans result;

    result.span_1 = &ringbuffer->buffer[offset];
    result.span_1_size = MIN(length, ringbuffer->size - offset);
    result.span_2 = ringbuffer->buffer;
    result.span_2_size = length - result.span_1_size;

    return result;
}

static inline cansub_ringbuffer_spans cansub_ringbuffer_spans_read(cansub_ringbuffer const *ringbuffer)
{
    assert(ringbuffer);

    return cansub_ringbuffer_spans_at(ringbuffer, ringbuffer->read_index, cansub_ringbuffer_size(ringbuffer));
}

static inline cansub_ringbuffer_spans cansub_ringbuffer_spans_write(cansub_ringbuffer *ringbuffer)
{
    assert(ringbuffer);

    return cansub_ringbuffer_spans_at(ringbuffer, ringbuffer->write_index, cansub_ringbuffer_space(ringbuffer));
}

static inline uint32_t cansub_ringbuffer_commit_read(cansub_ringbuffer *ringbuffer, size_t size)
{
    assert(ringbuffer);

    size = MIN(size, cansub_ringbuffer_size(ringbuffer));

    ringbuffer->read_index += (uint32_t)size;

    return (uint32_t)size;
}

static inline uint32_t cansub_ringbuffer_commit_write(cansub_ringbuffer *ringbuffer, size_t size)
{
    assert(ringbuffer);

    size = MIN(size, cansub_ringbuffer_space(ringbuffer));

    ringbuffer->write_index += (uint32_t)size;

    return (uint32_t)size;
}

/**
 * Copy bytes from the start of the ringbuffer without consuming them.
 *
 * @param ringbuffer Ringbuffer to peek into.
 * @param buffer Destination.
 * @param buffer_size Maximum number of bytes to copy.
 * @return Number of bytes copied.
 */
static inline uint32_t cansub_ringbuffer_peek(cansub_ringbuffer const *ringbuffer, void *buffer, size_t buffer_size)
{
    assert(ringbuffer);
    assert(buffer);

    cansub_ringbuffer_spans const spans = cansub_ringbuffer_spans_read(ringbuffer);
    uint8_t *ptr = buffer;

    size_t const span_1 = MIN(buffer_size, spans.span_1_size);
    size_t const span_2 = MIN(buffer_size - span_1, spans.span_2_size);

    memcpy(ptr, spans.span_1, span_1);
    memcpy(ptr + span_1, spans.span_2, span_2);

    return (uint32_t)(span_1 + span_2);
}

static inline uint32_t cansub_ringbuffer_read(cansub_ringbuffer *ringbuffer, void *buffer, size_t buffer_size)
{
    assert(ringbuffer);
    assert(buffer);

    uint32_t const result = cansub_ringbuffer_peek(ringbuffer, buffer, buffer_size);
    if (result > 0)
    {
        cansub_ringbuffer_commit_read(ringbuffer, result);
    }

    return result;
}

static inline uint32_t cansub_ringbuffer_write_no_commit(cansub_ringbuffer *ringbuffer, void const *buffer, size_t buffer_size)
{
    assert(ringbuffer);
    assert(buffer);

    cansub_ringbuffer_spans const spans = cansub_ringbuffer_spans_write(ringbuffer);
    uint8_t const *ptr = buffer;

    size_t const span_1 = MIN(buffer_size, spans.span_1_size);
    size_t const span_2 = MIN(buffer_size - span_1, spans.span_2_size);

    memcpy(spans.span_1, ptr, span_1);
    memcpy(spans.span_2, ptr + span_1, span_2);

    return (uint32_t)(span_1 + span_2);
}

static inline uint32_t cansub_ringbuffer_write(cansub_ringbuffer *ringbuffer, void const *buffer, size_t buffer_size)
{
    assert(ringbuffer);
    assert(buffer);

    uint32_t const result = cansub_ringbuffer_write_no_commit(ringbuffer, buffer, buffer_size);
    if (result > 0)
    {
        cansub_ringbuffer_commit_write(ringbuffer, result);
    }

    return result;
}

static inline void cansub_ringbuffer_reverse(uint8_t *data, uint32_t size)
{
    assert(data);

    for (uint32_t i = 0; i < size / 2; i++)
    {
        uint8_t const tmp = data[i];
        data[i] = data[size - 1 - i];
        data[size - 1 - i] = tmp;
    }
}

static inline void cansub_ringbuffer_linearize(cansub_ringbuffer *ringbuffer)
{
    assert(ringbuffer);

    uint32_t const used = cansub_ringbuffer_size(ringbuffer);
    uint32_t const offset = cansub_ringbuffer_mask(ringbuffer, ringbuffer->read_index);

    if (offset != 0)
    {
        /* Rotate left by offset: three reversals. */
        cansub_ringbuffer_reverse(ringbuffer->buffer, offset);
        cansub_ringbuffer_reverse(ringbuffer->buffer + offset, ringbuffer->size - offset);
        cansub_ringbuffer_reverse(ringbuffer->buffer, ringbuffer->size);
    }

    ringbuffer->read_index = 0;
    ringbuffer->write_index = used;
}

static inline void cansub_ringbuffer_truncate(cansub_ringbuffer *ringbuffer, uint32_t size)
{
    assert(ringbuffer);

    ringbuffer->write_index = ringbuffer->read_index + size;
}

#endif // CANSUB_RINGBUFFER_H
