/* SPDX-License-Identifier: MIT */

/***********************************************************************************************************************
 * Includes.
 **********************************************************************************************************************/
/* Standard includes. */
#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>

/* Third-party includes. */
#include <picohttpparser.h>

/* Project includes. */
#include "common.h"
#include "http.h"

/***********************************************************************************************************************
 * External declarations.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public variable definition.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private definitions.
 **********************************************************************************************************************/

_Static_assert(sizeof(struct phr_chunked_decoder) <= sizeof(((cansub_http_chunked *)0)->storage),
               "cansub_http_chunked storage is too small for phr_chunked_decoder");

/***********************************************************************************************************************
 * Private type definitions.
 **********************************************************************************************************************/

/***********************************************************************************************************************
* Private variable definitions.
***********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function declarations.
 **********************************************************************************************************************/

/**
 * Helper ensuring that the string contains no control characters or whitespace.
 *
 * @param str String to check.
 * @return True if the string contains no control characters or whitespace.
 */
static bool cansub_http_contains_no_ctrl_whitespace(char const *str);

/**
 * Check a header of a request: Its name and value must be valid, and it must not be one the library writes itself.
 *
 * @param header Header to check.
 * @return True if the header may be sent.
 */
static bool cansub_http_header_allowed(cansub_http_header const *header);

/**
 * Number of header fields with the given name, matched ASCII case-insensitively.
 *
 * @param headers Headers array.
 * @param num_headers Number of headers in the array.
 * @param name Name of the header field to count.
 * @return Number of header fields with the given name.
 */
static size_t cansub_http_header_count(struct phr_header const *headers, size_t num_headers, char const *name);

/**
 * Parse the status line and the headers of a response, and decide how its body is framed.
 *
 * @param response Response to fill in.
 * @param ringbuffer Buffer holding the response, only checked for being full.
 * @param data Start of the response, linear.
 * @param size Number of bytes received so far.
 * @return CANSUB_RESULT_OK once the head is complete, CANSUB_RESULT_AGAIN while more data is needed, or
 *         CANSUB_RESULT_CONNECTION_PROTOCOL.
 */
static cansub_result cansub_http_parse_head(cansub_http_response *response, cansub_ringbuffer const *ringbuffer, char const *data, size_t size);

/**
 * Check a body framed by Content-Length, or a response without a body.
 *
 * @param received Number of bytes received after the head.
 * @return CANSUB_RESULT_OK once the body is complete, CANSUB_RESULT_AGAIN or CANSUB_RESULT_CONNECTION_PROTOCOL.
 */
static cansub_result cansub_http_parse_body_length(cansub_http_response *response, cansub_ringbuffer const *ringbuffer, uint32_t received);

/**
 * Decode a chunked body in place, continuing where the last call stopped.
 *
 * @param body Start of the body, directly after the head.
 * @param received Number of bytes received after the head.
 * @return CANSUB_RESULT_OK once the last chunk is decoded, CANSUB_RESULT_AGAIN or CANSUB_RESULT_CONNECTION_PROTOCOL.
 */
static cansub_result cansub_http_parse_body_chunked(cansub_http_response *response, cansub_ringbuffer *ringbuffer, char *body, uint32_t received);

/**
 * Track a body that ends when the remote closes the transport.
 *
 * @param received Number of bytes received after the head.
 * @return CANSUB_RESULT_AGAIN, as only the end of the transport completes the body, or
 *         CANSUB_RESULT_CONNECTION_PROTOCOL if the buffer is full.
 */
static cansub_result cansub_http_parse_body_until_close(cansub_http_response *response, cansub_ringbuffer const *ringbuffer, uint32_t received);

/***********************************************************************************************************************
* Public function definitions.
***********************************************************************************************************************/

bool cansub_http_field_valid(char const *field)
{
    assert(field);

    size_t const length = strlen(field);
    for (size_t i = 0; i < length; ++i)
    {
        if (cansub_ascii_iscntrl(field[i]))
        {
            return false;
        }
    }

    return true;
}

bool cansub_http_valid_request(cansub_http_request const *request)
{
    assert(request);

    bool result = false;

    do
    {
        if (!request->method || request->method[0] == '\0' || !cansub_http_contains_no_ctrl_whitespace(request->method) ||
            !request->path || request->path[0] != '/' || !cansub_http_contains_no_ctrl_whitespace(request->path))
        {
            break;
        }

        if (!request->body && request->body_size)
        {
            /* A body size is specified but no body. */
            break;
        }

        if (request->content_type && !cansub_http_field_valid(request->content_type))
        {
            /* Content-type contains invalid data. */
            break;
        }

        if (request->body && !request->content_type)
        {
            /* Body set but no content-type. */
            break;
        }

        bool headers_allowed = true;
        for (size_t i = 0; (i < request->header_count) && headers_allowed; ++i)
        {
            headers_allowed = cansub_http_header_allowed(&request->headers[i]);
        }
        if (!headers_allowed)
        {
            /* A header is invalid, or one the library writes itself. */
            break;
        }

        result = true;
    } while (0);

    return result;
}

cansub_result cansub_http_write_request(cansub_http_request const *request, cansub_ringbuffer *ringbuffer)
{
    assert(request);
    assert(ringbuffer);

    cansub_result result = CANSUB_RESULT_ERROR;

    do
    {
        cansub_result bytes_written = 0;

        /* HTTP line. */
        bytes_written += cansub_ringbuffer_write(ringbuffer, request->method, strlen(request->method));
        bytes_written += cansub_ringbuffer_write(ringbuffer, " ", 1);
        bytes_written += cansub_ringbuffer_write(ringbuffer, request->path, strlen(request->path));
        bytes_written += cansub_ringbuffer_write(ringbuffer, " HTTP/1.1\r\n", sizeof(" HTTP/1.1\r\n") - 1);

        /* Headers. */
        if (request->host)
        {
            bytes_written += cansub_ringbuffer_write(ringbuffer, "Host: ", sizeof("Host: ") - 1);
            bytes_written += cansub_ringbuffer_write(ringbuffer, request->host, strlen(request->host));
            bytes_written += cansub_ringbuffer_write(ringbuffer, "\r\n", sizeof("\r\n") - 1);
        }

        {
            bytes_written += cansub_ringbuffer_write(ringbuffer, "User-Agent: cansub/" CANSUB_VERSION_HEX_STR "\r\n", sizeof("User-Agent: cansub/" CANSUB_VERSION_HEX_STR "\r\n") - 1);
        }

        for (size_t i = 0; i < request->header_count; i++)
        {
            bytes_written += cansub_ringbuffer_write(ringbuffer, request->headers[i].name, strlen(request->headers[i].name));
            bytes_written += cansub_ringbuffer_write(ringbuffer, ": ", sizeof(": ") - 1);
            bytes_written += cansub_ringbuffer_write(ringbuffer, request->headers[i].value, strlen(request->headers[i].value));
            bytes_written += cansub_ringbuffer_write(ringbuffer, "\r\n", sizeof("\r\n") - 1);
        }

        if (request->body_size)
        {
            {
                bytes_written += cansub_ringbuffer_write(ringbuffer, "Content-Type: ", sizeof("Content-Type: ") - 1);
                bytes_written += cansub_ringbuffer_write(ringbuffer, request->content_type, strlen(request->content_type));
                bytes_written += cansub_ringbuffer_write(ringbuffer, "\r\n", sizeof("\r\n") - 1);
            }

            {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%" PRIu32, (uint32_t)request->body_size);
                bytes_written += cansub_ringbuffer_write(ringbuffer, "Content-Length: ", sizeof("Content-Length: ") - 1);
                bytes_written += cansub_ringbuffer_write(ringbuffer, tmp, strlen(tmp));
                bytes_written += cansub_ringbuffer_write(ringbuffer, "\r\n", sizeof("\r\n") - 1);
            }
        }
        else if (strcmp(request->method, "PATCH") == 0 || strcmp(request->method, "PUT") == 0 || strcmp(request->method, "POST") == 0)
        {
            bytes_written += cansub_ringbuffer_write(ringbuffer, "Content-Length: 0\r\n", sizeof("Content-Length: 0\r\n") - 1);
        }

        bytes_written += cansub_ringbuffer_write(ringbuffer, "\r\n", 2);

        /* Body. */
        if (request->body && request->body_size)
        {
            bytes_written += cansub_ringbuffer_write(ringbuffer, request->body, request->body_size);
        }

        if (cansub_ringbuffer_full(ringbuffer))
        {
            result = CANSUB_RESULT_INVALID_ARG;
            break;
        }

        result = bytes_written;
    } while (0);

    return result;
}

cansub_result cansub_http_parse_response(cansub_http_response *response, cansub_ringbuffer *ringbuffer)
{
    assert(response);
    assert(ringbuffer);

    cansub_result result = CANSUB_RESULT_ERROR;

    /* picohttpparser can only work on linear buffers, ensure the ringbuffer is prepared. */
    cansub_ringbuffer_linearize(ringbuffer);
    cansub_ringbuffer_spans const spans = cansub_ringbuffer_spans_read(ringbuffer);
    char *const data = spans.span_1;

    /* The head is parsed once, later calls only continue with the body. */
    if (!response->header_size)
    {
        result = cansub_http_parse_head(response, ringbuffer, data, spans.span_1_size);
        if (result != CANSUB_RESULT_OK)
        {
            return result;
        }
    }

    uint32_t const received = (uint32_t)spans.span_1_size - response->header_size;

    switch (response->framing)
    {
        case CANSUB_HTTP_BODY_CHUNKED:
        {
            result = cansub_http_parse_body_chunked(response, ringbuffer, data + response->header_size, received);
            break;
        }
        case CANSUB_HTTP_BODY_UNTIL_CLOSE:
        {
            result = cansub_http_parse_body_until_close(response, ringbuffer, received);
            break;
        }
        case CANSUB_HTTP_BODY_LENGTH:
        default:
        {
            result = cansub_http_parse_body_length(response, ringbuffer, received);
            break;
        }
    }

    return result;
}

cansub_result cansub_http_response_header_first(cansub_http_response const *response, void const *data, char const *name, char *buffer, size_t buffer_size)
{
    assert(response);
    assert(data);
    assert(name);
    assert(buffer || !buffer_size);

    char const *const base = data;

    for (size_t i = 0; i < response->field_count; ++i)
    {
        cansub_http_field const *const field = &response->fields[i];

        if (cansub_strcasecmp_sized(name, base + field->name_offset, field->name_size) == 0)
        {
            if ((field->value_size + 1) > buffer_size)
            {
                return CANSUB_RESULT_INVALID_ARG;
            }

            memcpy(buffer, base + field->value_offset, field->value_size);
            buffer[field->value_size] = '\0';
            return CANSUB_RESULT_OK;
        }
    }

    return CANSUB_RESULT_NOT_FOUND;
}

cansub_result cansub_http_response_header(cansub_http_response const *response, void const *data, char const *name, char *buffer, size_t buffer_size)
{
    char const *const base = data;
    size_t total = 0;
    size_t matches = 0;

    /* First pass: size of all matching values, joined with ", ". */
    for (size_t i = 0; i < response->field_count; ++i)
    {
        cansub_http_field const *const field = &response->fields[i];

        if (cansub_strcasecmp_sized(name, base + field->name_offset, field->name_size) == 0)
        {
            total += field->value_size + ((matches > 0) ? 2 : 0);
            matches++;
        }
    }

    if (matches == 0)
    {
        return CANSUB_RESULT_NOT_FOUND;
    }

    /* NUL terminator. */
    total += 1;

    /* Second pass: copy, only if everything fits. */
    if (buffer && (total <= buffer_size))
    {
        char *out = buffer;
        bool first = true;

        for (size_t i = 0; i < response->field_count; ++i)
        {
            cansub_http_field const *const field = &response->fields[i];

            if (cansub_strcasecmp_sized(name, base + field->name_offset, field->name_size) != 0)
            {
                continue;
            }

            if (!first)
            {
                memcpy(out, ", ", 2);
                out += 2;
            }
            memcpy(out, base + field->value_offset, field->value_size);
            out += field->value_size;
            first = false;
        }

        *out = '\0';
    }

    /* Bounded by the receive buffer size, so it fits a cansub_result. */
    return (cansub_result)total;
}

/***********************************************************************************************************************
* Private function definitions.
***********************************************************************************************************************/

static bool cansub_http_contains_no_ctrl_whitespace(char const *str)
{
    size_t const length = strlen(str);
    for (size_t i = 0; i < length; i++)
    {
        if (cansub_ascii_iscntrl(str[i]) || (str[i] == ' '))
        {
            return false;
        }
    }

    return true;
}

static bool cansub_http_header_allowed(cansub_http_header const *header)
{
    /* Written by cansub_http_write_request() itself. */
    static char const *const reserved_headers[] = { "Host", "User-Agent", "Content-Type", "Content-Length" };

    if (!cansub_http_field_valid(header->name) || !cansub_http_field_valid(header->value))
    {
        return false;
    }

    for (size_t i = 0; i < ARRAY_SIZE(reserved_headers); ++i)
    {
        if (cansub_strcasecmp(header->name, reserved_headers[i]) == 0)
        {
            return false;
        }
    }

    return true;
}

static size_t cansub_http_header_count(struct phr_header const *headers, size_t num_headers, char const *name)
{
    size_t count = 0;

    for (size_t i = 0; i < num_headers; i++)
    {
        if (cansub_strcasecmp_sized(name, headers[i].name, headers[i].name_len) == 0)
        {
            count++;
        }
    }

    return count;
}

static cansub_result cansub_http_parse_head(cansub_http_response *response, cansub_ringbuffer const *ringbuffer, char const *data, size_t size)
{
    /* Stack allocate storage for the headers. */
    struct phr_header headers[CANSUB_HTTP_MAX_HEADERS];
    size_t num_headers = CANSUB_HTTP_MAX_HEADERS;
    int minor_version = 0;
    int status = 0;
    char const *message = NULL;
    size_t message_size = 0;

    int const header_length = phr_parse_response(data, size, &minor_version, &status, &message, &message_size, headers, &num_headers, 0);
    if (header_length == -2)
    {
        /* Not yet enough data. */
        if (cansub_ringbuffer_full(ringbuffer))
        {
            /* Unable to parse the header as the buffer is too small. */
            return CANSUB_RESULT_CONNECTION_PROTOCOL;
        }
        return CANSUB_RESULT_AGAIN;
    }
    if (header_length == -1)
    {
        /* Unable to parse the header. */
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }

    for (size_t i = 0; i < num_headers; i++)
    {
        if (!headers[i].name)
        {
            /* Obsolete line folding (continuation line). */
            return CANSUB_RESULT_CONNECTION_PROTOCOL;
        }
        response->fields[i].name_offset = (uint32_t)(headers[i].name - data);
        response->fields[i].name_size = (uint32_t)headers[i].name_len;
        response->fields[i].value_offset = (uint32_t)(headers[i].value - data);
        response->fields[i].value_size = (uint32_t)headers[i].value_len;
    }
    response->field_count = num_headers;
    response->status = (uint32_t)status;
    response->header_size = (uint32_t)header_length;

    /* A repeated framing header makes the length of the body ambiguous.
     * Other repeated headers are valid and are joined by cansub_http_response_header(). */
    if ((cansub_http_header_count(headers, num_headers, "Transfer-Encoding") > 1) ||
        (cansub_http_header_count(headers, num_headers, "Content-Length") > 1))
    {
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }

    char tmp[32];
    cansub_result result;

    /* The remote closes the transport after this response. */
    result = cansub_http_response_header_first(response, data, "Connection", tmp, sizeof(tmp));
    response->close = (result == CANSUB_RESULT_OK) && (cansub_strcasecmp(tmp, "close") == 0);

    /* Determine the framing of the body. Transfer-Encoding takes precedence over Content-Length. */
    result = cansub_http_response_header_first(response, data, "Transfer-Encoding", tmp, sizeof(tmp));
    if (result == CANSUB_RESULT_OK)
    {
        if (cansub_strcasecmp(tmp, "chunked") != 0)
        {
            /* Only plain chunked is supported. */
            return CANSUB_RESULT_CONNECTION_PROTOCOL;
        }

        result = cansub_http_response_header_first(response, data, "Content-Length", tmp, sizeof(tmp));
        if (result == CANSUB_RESULT_OK)
        {
            /* Chunked transfer encoding with a content-length makes no sense. Fail. */
            return CANSUB_RESULT_CONNECTION_PROTOCOL;
        }

        struct phr_chunked_decoder decoder;
        memset(&decoder, 0x00, sizeof(decoder));
        decoder.consume_trailer = 1;
        memcpy(&response->decoder.storage, &decoder, sizeof(decoder));

        response->framing = CANSUB_HTTP_BODY_CHUNKED;
    }
    else if (result != CANSUB_RESULT_NOT_FOUND)
    {
        /* Present, but too long to be a supported transfer encoding. */
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }
    else if ((response->status == 101) || (response->status == 204) || (response->status == 304))
    {
        /* No body, regardless of the headers. */
        response->decoder.expected_size = 0;
        response->framing = CANSUB_HTTP_BODY_LENGTH;
    }
    else if ((response->status >= 100) && (response->status < 200))
    {
        /* Interim responses are never requested. */
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }
    else
    {
        result = cansub_http_response_header_first(response, data, "Content-Length", tmp, sizeof(tmp));
        if (result == CANSUB_RESULT_OK)
        {
            /* The body can never be larger than the buffer. */
            if (!cansub_parse_u32(tmp, ringbuffer->size, &response->decoder.expected_size))
            {
                return CANSUB_RESULT_CONNECTION_PROTOCOL;
            }
            response->framing = CANSUB_HTTP_BODY_LENGTH;
        }
        else if (result == CANSUB_RESULT_NOT_FOUND)
        {
            /* No length, the body ends when the remote closes the transport. */
            response->framing = CANSUB_HTTP_BODY_UNTIL_CLOSE;
        }
        else
        {
            return CANSUB_RESULT_CONNECTION_PROTOCOL;
        }
    }

    return CANSUB_RESULT_OK;
}

static cansub_result cansub_http_parse_body_length(cansub_http_response *response, cansub_ringbuffer const *ringbuffer, uint32_t received)
{
    if (received < response->decoder.expected_size)
    {
        if (cansub_ringbuffer_full(ringbuffer))
        {
            /* Unable to parse the response as the buffer is too small. */
            return CANSUB_RESULT_CONNECTION_PROTOCOL;
        }
        return CANSUB_RESULT_AGAIN;
    }
    if ((received > response->decoder.expected_size) && (response->status != 101))
    {
        /* Additional bytes after the first response, unexpected. After 101 they belong to the upgraded protocol. */
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }

    response->decoder.decoded_size = response->decoder.expected_size;
    response->body_size = response->decoder.decoded_size;

    return CANSUB_RESULT_OK;
}

static cansub_result cansub_http_parse_body_chunked(cansub_http_response *response, cansub_ringbuffer *ringbuffer, char *body, uint32_t received)
{
    /* Only the bytes after the already decoded body are new. */
    size_t raw_size = received - response->decoder.decoded_size;

    struct phr_chunked_decoder decoder;
    memcpy(&decoder, &response->decoder.storage, sizeof(decoder));
    ssize_t const ret = phr_decode_chunked(&decoder, body + response->decoder.decoded_size, &raw_size);
    memcpy(&response->decoder.storage, &decoder, sizeof(decoder));
    response->decoder.decoded_size += (uint32_t)raw_size;

    /* Drop the consumed chunk framing, so new data follows the decoded body. */
    cansub_ringbuffer_truncate(ringbuffer, response->header_size + response->decoder.decoded_size);

    if (ret == -2)
    {
        /* Not yet enough data. */
        if (cansub_ringbuffer_full(ringbuffer))
        {
            /* Unable to parse the response as the buffer is too small. */
            return CANSUB_RESULT_CONNECTION_PROTOCOL;
        }
        return CANSUB_RESULT_AGAIN;
    }
    if (ret == -1)
    {
        /* Unable to parse the response. */
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }
    if (ret > 0)
    {
        /* Additional bytes after the first response, unexpected. */
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }

    response->body_size = response->decoder.decoded_size;

    return CANSUB_RESULT_OK;
}

static cansub_result cansub_http_parse_body_until_close(cansub_http_response *response, cansub_ringbuffer const *ringbuffer, uint32_t received)
{
    /* The body runs until the remote closes the transport, the caller completes it on EOF. */
    response->body_size = received;

    if (cansub_ringbuffer_full(ringbuffer))
    {
        /* Unable to hold the response as the buffer is too small. */
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }

    return CANSUB_RESULT_AGAIN;
}
