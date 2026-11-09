/* SPDX-License-Identifier: MIT */

/***********************************************************************************************************************
 * Includes.
 **********************************************************************************************************************/
/* Standard includes. */
#include <assert.h>
#include <string.h>

/* Zephyr includes. */
#include "zephyr/sys/crc.h"

/* Project includes. */
#include "cansub_protocol.h"

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
/* Decodes a HDLC frames from data. Consumes decoded bytes from data. */
static int frames_decode(struct net_buf_simple *data, cansub_protocol_decode_cb_t msg_cb, void* user_data);

/* Decodes a single HDLC frame from data. Consumes decoded bytes from data. */
static int frame_decode(struct net_buf_simple *data, cansub_protocol_decode_cb_t msg_cb, void* user_data);

/**
 * Decodes can messages from data. Consumes decoded bytes from data
 * @param data
 * @param msg_cb
 * @return <0 on error, else success.
 */
static int messages_decode(struct net_buf_simple *data, cansub_protocol_decode_cb_t msg_cb, void* user_data);

/**
 * Decodes a single can message from data. Consumes decoded bytes from data.
 * @param data
 * @param msg_cb
 * @return <0 on error, else success.
 */
static int message_decode(struct net_buf_simple *data, cansub_protocol_decode_cb_t msg_cb, void* user_data);

/**
 * Maps an error value from the WS protocol to the FFI error frame type.
 * @param error_value Error value from the flags byte of an error frame (5 bits).
 * @param error_type Output error frame type.
 * @return <0 if the error value is unknown, else success.
 */
static int error_value_to_type(uint8_t error_value, cansub_error_frame_type *error_type);

/***********************************************************************************************************************
* Public function definitions.
***********************************************************************************************************************/
int cansub_protocol_decode(struct net_buf_simple *data, cansub_protocol_decode_cb_t msg_cb, void* user_data)
{
    assert(data);

    return frames_decode(data, msg_cb, user_data);
}

/***********************************************************************************************************************
* Private function definitions.
***********************************************************************************************************************/
static int frames_decode(struct net_buf_simple *data, cansub_protocol_decode_cb_t msg_cb, void* user_data)
{
    int ret = 0;

    /* Decode all frames in buffer. */
    while (data->len > 0)
    {
        /* Pre-decode length of buffer. */
        uint16_t data_len_pre = data->len;

        /* Decode single frame. */
        ret = frame_decode(data, msg_cb, user_data);

        /* Decode result. */
        if (ret < 0)
        {
            /* Bytes consumed. */
            if (data_len_pre == data->len)
            {
                /* No, likely deadlock, reset buffer. */
                net_buf_simple_reset(data);
            }
        }

        /* Check if bytes consumed. */
        if (data->len == data_len_pre)
        {
            /* No, this is not an error. Just that we need more bytes to parse frame. */
            break;
        }
    }
    return ret;
}

static int frame_decode(struct net_buf_simple *data, cansub_protocol_decode_cb_t msg_cb, void* user_data)
{
    int ret = -1;

    do
    {
        /* Find frame boundaries. */
        uint16_t frame_len = 0;

        /* Find frame boundaries. */
        {
            /* Find boundary start. */
            uint8_t *frame_start = memchr(data->data, HDLC_BOUNDARY_BYTE, data->len);

            if (frame_start == NULL)
            {
                /* Boundary start byte should during normal operation always be present.
                 * However, this is not a hard fault. */
                ret = 0;
                break;
            }

            /* Discard any bytes before frame_start. */
            if ( frame_start > data->data )
            {
                (void)net_buf_simple_pull_mem(data, frame_start - data->data);
            }

            /* Find boundary end (starting from boundary_start + 1 and with reduced length). */
            uint8_t *frame_end = memchr(frame_start + 1, HDLC_BOUNDARY_BYTE, data->len - 1);
            if (frame_end == NULL)
            {
                /* No boundary end, wait for more data. Zero bytes consumed. */
                ret = 0;
                break;
            }

            /* Calculate length of frame (including start/stop bytes). */
            frame_len = (uint16_t)((frame_end + 1) - frame_start);
        }

        /* Consume the frame bytes (i.e. remove from start of buffer). */
        uint8_t *frame_start = net_buf_simple_pull_mem(data, frame_len);

        /* Check that we have at least start and stop bytes. */
        if (frame_len < 2)
        {
            break;
        }

        /* Create buffer "scope" containing byte-stuffed bytes without start/stop flags. */
        struct net_buf_simple frame;
        net_buf_simple_init_with_data(&frame, frame_start + 1, frame_len - 2);

        /* Create buffer "scope" for byte-destuffed payload (can use same storage as result never longer). */
        struct net_buf_simple frame_destuffed;
        net_buf_simple_init_with_data(&frame_destuffed, frame.data, frame.len);
        net_buf_simple_reset(&frame_destuffed);

        /* Perform byte de-stuffing.  */
        {
            bool destuffing_result = true;
            while (frame.len > 0)
            {
                /* Read one byte. */
                uint8_t byte = net_buf_simple_pull_u8(&frame);

                /* Check if de-stuffing is required. */
                if (byte == HDLC_ESCAPE_BYTE)
                {
                    /* Yes, check that we have one more byte. */
                    if (frame.len >= 1 )
                    {
                        /* Yes, de-stuff next byte. */
                        byte = net_buf_simple_pull_u8(&frame) ^ 0x20;
                    }
                    else
                    {
                        /* No, this is an error. */
                        destuffing_result = false;
                        break;
                    }
                }

                /* Add de-stuffed byte to the output. */
                net_buf_simple_add_u8(&frame_destuffed, byte);
            }

            if (destuffing_result == false)
            {
                break;
            }
        }

        /* Check frame crc32. */
        if (frame_destuffed.len < 4)
        {
            break;
        }
        uint32_t crc32_expected = net_buf_simple_remove_be32(&frame_destuffed);
        uint32_t crc32_actual = crc32_ieee(frame_destuffed.data, frame_destuffed.len);
        if (crc32_expected != crc32_actual)
        {
            break;
        }

        /* Decode message(s) in frame. */
        if ( messages_decode(&frame_destuffed, msg_cb, user_data) < 0 )
        {
            break;
        }

        ret = 0;

    } while ( 0 );

    return ret;
}

static int messages_decode(struct net_buf_simple *data, cansub_protocol_decode_cb_t msg_cb, void* user_data)
{
    int ret = 0;

    /* Decode all messages in buffer. */
    while (data->len > 0)
    {
        /* Pre-decode length of buffer. */
        uint16_t data_len_pre = data->len;

        /* Decode single message. */
        int message_decode_ret = message_decode(data, msg_cb, user_data);

        /* If decode error or if zero bytes consumed, break. */
        if ((message_decode_ret < 0) || (data_len_pre == data->len))
        {
            ret = -1;
            break;
        }
    }
    return ret;
}

static int message_decode(struct net_buf_simple *data, cansub_protocol_decode_cb_t msg_cb, void* user_data)
{
    int ret = -1;
    cansub_frame msg;

    do
    {
        /* Store start length before we start consuming bytes. */
        uint16_t data_len_orig = data->len;

        /* Check that we have at least the minimum frame header (timestamp + flags). */
        if (data_len_orig < MSG_ENCODE_HEADER_MIN_SIZE)
        {
            break;
        }

        /* Clear result. */
        memset(&msg, 0, sizeof(msg));

        /* Microseconds (48 bit). */
        msg.timestamp_us = net_buf_simple_pull_be48(data) + CANSUB_PROTOCOL_TIME_BASE_US;

        /* Flags. */
        uint8_t flags = net_buf_simple_pull_u8(data);

        /* Deliver the message to the call-back (cleared for error frames of unknown type). */
        bool deliver = true;

        /* Is regular frame or error frame?
         * NOTE: Error frames have no more bytes after the flag. When the flag has been read, they are fully parsed. */
        if ((flags & 0xE0) != 0x20)
        {
            /* Sufficient bytes to parse as regular frame. */
            if (data_len_orig < MIN(MSG_ENCODE_STD_HEADER_SIZE, MSG_ENCODE_EXT_HEADER_SIZE))
            {
                break;
            }

            /* Frame format. */
            msg.flags |= (flags & 0x80) ? CANSUB_FRAME_FLAG_FDF : 0;

            /* Frame format specific bits. */
            if(!(msg.flags & CANSUB_FRAME_FLAG_FDF))
            {
                msg.flags |= (flags & 0x40) ? CANSUB_FRAME_FLAG_RTR : 0;
            }
            else
            {
                msg.flags |= (flags & 0x40) ? CANSUB_FRAME_FLAG_BRS : 0;
                msg.flags |= (flags & 0x20) ? CANSUB_FRAME_FLAG_ESI : 0;
            }

            /* Get TX. */
            msg.flags |= (flags & 0x10) ? CANSUB_FRAME_FLAG_TX : 0;

            /* DLC. */
            msg.dlc = flags & 0x0F;

            /* ID. Read two bytes first. Determine if IDE and if so, read two more. */
            msg.arbitration_id = (uint32_t)net_buf_simple_pull_be16(data);

            /* Standard or extended ID. */
            if (!(msg.arbitration_id & 0x8000))
            {
                /* Standard. */
                msg.arbitration_id &= CAN_STD_ID_MSK;
            }
            else
            {
                /* Extended frame uses two additional bytes for ID. */
                if (data->len < 2)
                {
                    break;
                }
                /* Extended. */
                msg.flags |= CANSUB_FRAME_FLAG_IDE;
                msg.arbitration_id = msg.arbitration_id << 16 | net_buf_simple_pull_be16(data);
                msg.arbitration_id &= CAN_EXT_ID_MSK;
            }

            /* Read out data if not RTR. */
            if( !(msg.flags & CANSUB_FRAME_FLAG_RTR) )
            {
                    int const payload_len = can_dlc_to_bytes_safe(msg.flags & CANSUB_FRAME_FLAG_FDF, msg.dlc);
                if (data->len >= payload_len)
                {
                    _Static_assert(sizeof(msg.data) >= 64, "Data storage too small");
                    memcpy(msg.data, net_buf_simple_pull_mem(data, payload_len), payload_len);
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            /* Error frame (0b001<error_type>). Error type is stored in the first data byte. */
            msg.flags = CANSUB_FRAME_FLAG_ERROR;

            cansub_error_frame_type error_type;
            if (error_value_to_type(flags & 0x1F, &error_type) == 0)
            {
                msg.data[0] = (uint8_t)error_type;
            }
            else
            {
                /* Unknown error type. The message is fully consumed, so skip it without failing the frame. */
                deliver = false;
            }
        }

        /* Invoke CB with can message. */
        if (deliver && (msg_cb != NULL))
        {
            msg_cb(&msg, user_data);
        }
        ret = 0;

    } while ( 0 );

    return ret;
}

static int error_value_to_type(uint8_t error_value, cansub_error_frame_type *error_type)
{
    int ret = 0;

    switch (error_value)
    {
        case 0:
        {
            *error_type = CANSUB_ERROR_FRAME_TYPE_BIT_ERROR;
            break;
        }
        case 1:
        {
            *error_type = CANSUB_ERROR_FRAME_TYPE_ACK_ERROR;
            break;
        }
        case 2:
        {
            *error_type = CANSUB_ERROR_FRAME_TYPE_FORM_ERROR;
            break;
        }
        case 3:
        {
            *error_type = CANSUB_ERROR_FRAME_TYPE_STUFF_ERROR;
            break;
        }
        case 4:
        {
            *error_type = CANSUB_ERROR_FRAME_TYPE_CRC_ERROR;
            break;
        }
        default:
        {
            *error_type = CANSUB_ERROR_FRAME_TYPE_OTHER_ERROR;
            break;
        }
    }
    return ret;
}
