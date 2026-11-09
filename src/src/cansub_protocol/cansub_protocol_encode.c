/* SPDX-License-Identifier: MIT */

/***********************************************************************************************************************
 * Includes.
 **********************************************************************************************************************/
/* Standard includes. */

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
/**
 * Encodes msg into buffer data_buf
 * @param msg Input can messages
 * @param data_buf Output buffer
 * @return <0 on error, else success.
 */
static int message_encode(cansub_frame const *msg, struct net_buf_simple *data_buf);

/***********************************************************************************************************************
* Public function definitions.
***********************************************************************************************************************/
int cansub_protocol_encode(cansub_frame const *msg, struct net_buf_simple *data)
{
    int ret = -1;

    do
    {
        /* Temporary message buffer. */
        struct net_buf_simple message_buf;
        uint8_t message_buf_storage[120];

        /* Init message buffer. */
        net_buf_simple_init_with_data(&message_buf, message_buf_storage, sizeof(message_buf_storage));
        net_buf_simple_reset(&message_buf);

        /* Default crc32 value. */
        uint32_t crc32 = 0;

        /* Encode the new message into message buffer. */
        int message_encode_ret = message_encode(msg, &message_buf);
        if (message_encode_ret < 0)
        {
            /* Encoding failed. Restore backup. */
            break;
        }

        /* Check if output buffer already contains a start boundary flag. */
        if (data->len == 0)
        {
            /* No, add flag. */
            net_buf_simple_add_u8(data, HDLC_BOUNDARY_BYTE);
        }
        else
        {
            /* Yes, verify that the first byte is indeed the boundary byte. */
            if (data->data[0] != HDLC_BOUNDARY_BYTE)
            {
                break;
            }

            /* Remove the current end boundary flag (such that more data can be added). */
            uint8_t boundary_flag_end = net_buf_simple_remove_u8(data);
            if (boundary_flag_end != HDLC_BOUNDARY_BYTE)
            {
                break;
            }

            /* Get current CRC (and remove from the end of buffer)
             * WARNING: The CRC bytes can be byte-stuffed and take up more than 4 bytes. */
            {
                bool crc32_destuff_result = true;
                for (size_t i = 0 ; i < 4 ; i++ )
                {
                    if ( data->len < 1)
                    {
                        crc32_destuff_result = false;
                        break;
                    }

                    /* Get byte and check if byte before is escape byte. */
                    uint8_t byte = net_buf_simple_remove_u8(data);
                    if (data->len > 0)
                    {
                        if (data->data[data->len - 1] == HDLC_ESCAPE_BYTE)
                        {
                            /* Yes, byte before is escape byte, de-stuff crc byte and remove escape byte. */
                            byte ^= 0x20;
                            net_buf_simple_remove_u8(data);
                        }
                    }
                    /* NOTE: Be aware that the CRC32 bytes are read from the end of the buffer. */
                    crc32 |= (uint32_t)byte << (i * 8);
                }
                if (crc32_destuff_result == false)
                {
                    break;
                }
            }
        }

        /* Update crc32 with new data. */
        crc32 = crc32_ieee_update(crc32, message_buf.data, message_buf.len);

        /* Add crc32 to message buffer. */
        if (net_buf_simple_tailroom(&message_buf) >= 4)
        {
            net_buf_simple_add_be32(&message_buf, crc32);
        }
        else
        {
            break;
        }

        /* Add the new message to the persistent buffer while applying HDLC byte stuffing. */
        {
            bool stuffing_result = true;
            for (int i = message_buf.len; i > 0 ; i--)
            {
                /* Room for data byte on potential escape byte. */
                if (net_buf_simple_tailroom(data) < 2)
                {
                    /* No, break. */
                    stuffing_result = false;
                    break;
                }

                /* Get message byte. */
                uint8_t byte = net_buf_simple_pull_u8(&message_buf);

                /* Check if bytestuffing in required. */
                if (byte == HDLC_BOUNDARY_BYTE || byte == HDLC_ESCAPE_BYTE)
                {
                    /* Insert escape byte. */
                    net_buf_simple_add_u8(data, HDLC_ESCAPE_BYTE);

                    /* Modify data byte with XOR 0x20. */
                    byte ^= 0x20;
                }

                /* Insert data byte. */
                net_buf_simple_add_u8(data, byte);
            }
            if (stuffing_result == false)
            {
                break;
            }
        }

        /* Insert the boundary end flag. */
        if (net_buf_simple_tailroom(data) > 0)
        {
            net_buf_simple_add_u8(data, HDLC_BOUNDARY_BYTE);
        }

        /* Return updated length of buffer. */
        ret = data->len;

    } while (0);

    return ret;
}

/***********************************************************************************************************************
* Private function definitions.
***********************************************************************************************************************/
static int message_encode(cansub_frame const *msg, struct net_buf_simple *data_buf)
{
    /* Lazy check that buffer can contain worst-case frame. */
    if (net_buf_simple_tailroom(data_buf) < MSG_ENCODE_MAX_SIZE(64))
    {
        return -1;
    }

    if (msg->timestamp_us < CANSUB_PROTOCOL_TIME_BASE_US)
    {
        return -1;
    }

    /* Microseconds (48 bit). */
    net_buf_simple_add_be48(data_buf, msg->timestamp_us - CANSUB_PROTOCOL_TIME_BASE_US);

    /* Error frame. */
    if (!(msg->flags & CANSUB_FRAME_FLAG_ERROR))
    {
        /* No, data frame. */

        /* fd (1 bit), rtr/brs (1 bit), 0/esi (1 bit), tx (1 bit), dlc (4 bit). */
        uint8_t flags = 0;
        flags |= (msg->flags & CANSUB_FRAME_FLAG_FDF) ? 0x80 : 0x00;
        if (!(msg->flags & CANSUB_FRAME_FLAG_FDF))
        {
            flags |= (msg->flags & CANSUB_FRAME_FLAG_RTR) ? 0x40 : 0x00;
        }
        else
        {
            flags |= (msg->flags & CANSUB_FRAME_FLAG_BRS) ? 0x40 : 0x00;
            flags |= (msg->flags & CANSUB_FRAME_FLAG_ESI) ? 0x20 : 0x00;
        }
        flags |= (msg->flags & CANSUB_FRAME_FLAG_TX) ? 0x10 : 0x00;
        flags |= (msg->dlc) & 0x0F;
        net_buf_simple_add_u8(data_buf, flags);

        /* Arbitration ID (bit 31 set on extended). */
        if (!(msg->flags & CANSUB_FRAME_FLAG_IDE))
        {
            net_buf_simple_add_be16(data_buf, msg->arbitration_id & CAN_STD_ID_MSK);
        }
        else
        {
            net_buf_simple_add_be32(data_buf, 0x80000000 | (msg->arbitration_id & CAN_EXT_ID_MSK));
        }

        /* Write payload to buffer (if not RTR frame). */
        if ((msg->flags & CANSUB_FRAME_FLAG_FDF) || (!(msg->flags & CANSUB_FRAME_FLAG_RTR)))
        {
            net_buf_simple_add_mem(data_buf, msg->data,
                can_dlc_to_bytes_safe(msg->flags & CANSUB_FRAME_FLAG_FDF, msg->dlc));
        }
    }
    else
    {
        /* Error type stored in the first byte. */
        cansub_error_frame_type error_type = (cansub_error_frame_type)msg->data[0];

        /* Map to error value using in WS protocol. */
        uint8_t error_value = 0;
        switch (error_type)
        {
            case CANSUB_ERROR_FRAME_TYPE_BIT_ERROR:
            {
                error_value = 0;
                break;
            }
            case CANSUB_ERROR_FRAME_TYPE_ACK_ERROR:
            {
                error_value = 1;
                break;
            }
            case CANSUB_ERROR_FRAME_TYPE_FORM_ERROR:
            {
                error_value = 2;
                break;
            }
            case CANSUB_ERROR_FRAME_TYPE_STUFF_ERROR:
            {
                error_value = 3;
                break;
            }
            case CANSUB_ERROR_FRAME_TYPE_CRC_ERROR:
            {
                error_value = 4;
                break;
            }
            default:
            {
                break;
            }
        }

        /* Add error frame byte (0b001<error_type>). */
        uint8_t flags = 0x20 | (0x1F & error_value);
        net_buf_simple_add_u8(data_buf, flags);
    }
    return 0;
}