/* SPDX-License-Identifier: MIT */

#ifndef CANSUB_HTTP_H
#define CANSUB_HTTP_H

/***********************************************************************************************************************
* Includes.
***********************************************************************************************************************/
/* Standard includes. */

/* Project includes. */
#include "common.h"
#include "ringbuffer.h"

/***********************************************************************************************************************
* Public definitions.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public type declarations.
***********************************************************************************************************************/

/**
 * Helper struct type for storing HTTP headers.
 */
typedef struct cansub_http_header
{
    char const *name;
    char const *value;
} cansub_http_header;

/**
 * Helper struct for storing the details for a HTTP request.
 */
typedef struct cansub_http_request
{
    char const *method;
    char const *path;
    char const *host;
    cansub_http_header const *headers;
    size_t header_count;
    char const *content_type;
    void const *body;
    size_t body_size;
} cansub_http_request;


#define CANSUB_HTTP_MAX_HEADERS 32

typedef struct cansub_http_field
{
    uint32_t name_offset;
    uint32_t name_size;
    uint32_t value_offset;
    uint32_t value_size;
} cansub_http_field;

typedef struct cansub_http_chunked
{
    uint32_t decoded_size;
    uint32_t expected_size;
    uint64_t storage[4];
} cansub_http_chunked;

/** How the end of a response body is found. */
typedef enum
{
    /** Content-Length, or no body at all (101, 204 and 304). */
    CANSUB_HTTP_BODY_LENGTH = 0,
    /** Chunked transfer encoding. */
    CANSUB_HTTP_BODY_CHUNKED,
    /** Neither, the body ends when the remote closes the transport. */
    CANSUB_HTTP_BODY_UNTIL_CLOSE,
} cansub_http_body_framing;

typedef struct cansub_http_response
{
    uint32_t status;
    uint32_t header_size;
    uint32_t body_size;
    /** Only valid once header_size is set. */
    cansub_http_body_framing framing;
    bool close;
    size_t field_count;
    cansub_http_field fields[CANSUB_HTTP_MAX_HEADERS];
    cansub_http_chunked decoder;
} cansub_http_response;

/***********************************************************************************************************************
* Public variable declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public function declarations.
***********************************************************************************************************************/

bool cansub_http_field_valid(char const *field);

bool cansub_http_valid_request(cansub_http_request const *request);

/**
 * Write an HTTP/1.1 request into a buffer.
 *
 * Adds Content-Type and Content-Length if the request has a body.
 *
 * @return Size of the request in bytes.
 */
cansub_result cansub_http_write_request(cansub_http_request const *request, cansub_ringbuffer *ringbuffer);

/**
 * Parse a response from the start of a buffer, continuing where the last call stopped.
 *
 * Linearizes the buffer. The head is parsed once, then the body according to its framing. A chunked body is decoded in
 * place, so the decoded body follows the head directly.
 *
 * @return CANSUB_RESULT_OK once the response is complete, CANSUB_RESULT_AGAIN while more data is needed (always for
 *         CANSUB_HTTP_BODY_UNTIL_CLOSE), or CANSUB_RESULT_CONNECTION_PROTOCOL for an invalid or too large response.
 */
cansub_result cansub_http_parse_response(cansub_http_response *response, cansub_ringbuffer *ringbuffer);

/**
 * Look up the first header of a parsed response with the given name.
 *
 * The name is matched ASCII case-insensitively. Unlike cansub_http_response_header(), repeated headers are not joined.
 *
 * @param data Start of the response, as parsed by cansub_http_parse_response().
 * @return Result.
 * @retval CANSUB_RESULT_OK The value was copied, NUL terminated.
 * @retval CANSUB_RESULT_NOT_FOUND The response has no such header.
 * @retval CANSUB_RESULT_INVALID_ARG The value does not fit into the buffer, nothing was copied.
 */
cansub_result cansub_http_response_header_first(cansub_http_response const *response, void const *data, char const *name, char *buffer, size_t buffer_size);

/**
 * Look up a header of a parsed response, with the semantics of cansub_response_header().
 *
 * The name is matched ASCII case-insensitively. Repeated headers are joined with ", ".
 * The value is only copied if it fits into the buffer, including the NUL terminator.
 *
 * @param data Start of the response, as parsed by cansub_http_parse_response().
 * @return Size of the value including the NUL terminator. A value larger than buffer_size means nothing was copied.
 * @retval CANSUB_RESULT_NOT_FOUND The response has no such header.
 */
cansub_result cansub_http_response_header(cansub_http_response const *response, void const *data, char const *name, char *buffer, size_t buffer_size);

#endif