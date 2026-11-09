/** @file
 *  @brief Buffer management.
 */

/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adapted for CANsub library:
 * - Removed all but the simple buffer implementation.
 * - Removed deprecated functions.
 * - Removed all Zephyr specific includes, replaced with common types and utilities.
 *
 */
#ifndef ZEPHYR_INCLUDE_NET_BUF_H_
#define ZEPHYR_INCLUDE_NET_BUF_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Network buffer library
 * @defgroup net_buf Network Buffer Library
 * @since 1.0
 * @version 1.1.0
 * @ingroup os_services
 *
 * The functions that add, push, pull or remove data check with an assertion
 * that the operation fits in the buffer. With
 * @kconfig{CONFIG_NET_BUF_HARDENING} the check is always made, together with a
 * check that the length and pointers of the buffer are consistent, and an
 * operation that fails it leaves the buffer unchanged: functions that return a
 * pointer return NULL, functions that return a value return 0, and functions
 * that return nothing do not write.
 * @{
 */

/* Alignment needed for various parts of the buffer definition */
#if CONFIG_NET_BUF_ALIGNMENT == 0
#define __net_buf_align __aligned(sizeof(void *))
#else
#define __net_buf_align __aligned(CONFIG_NET_BUF_ALIGNMENT)
#endif

/**
 *  @brief Define a net_buf_simple stack variable.
 *
 *  This is a helper macro which is used to define a net_buf_simple object
 *  on the stack.
 *
 *  @param _name Name of the net_buf_simple object.
 *  @param _size Maximum data storage for the buffer.
 */
#define NET_BUF_SIMPLE_DEFINE(_name, _size)     \
	uint8_t net_buf_data_##_name[_size];       \
	struct net_buf_simple _name = {         \
		.data   = net_buf_data_##_name, \
		.len    = 0,                    \
		.size   = _size,                \
		.__buf  = net_buf_data_##_name, \
	}

/**
 *
 * @brief Define a static net_buf_simple variable.
 *
 * This is a helper macro which is used to define a static net_buf_simple
 * object.
 *
 * @param _name Name of the net_buf_simple object.
 * @param _size Maximum data storage for the buffer.
 */
#define NET_BUF_SIMPLE_DEFINE_STATIC(_name, _size)        \
	static __noinit uint8_t net_buf_data_##_name[_size]; \
	static struct net_buf_simple _name = {            \
		.data   = net_buf_data_##_name,           \
		.len    = 0,                              \
		.size   = _size,                          \
		.__buf  = net_buf_data_##_name,           \
	}

/**
 * @brief Simple network buffer representation.
 *
 * This is a simpler variant of the net_buf object (in fact net_buf uses
 * net_buf_simple internally). It doesn't provide any kind of reference
 * counting, user data, dynamic allocation, or in general the ability to
 * pass through kernel objects such as FIFOs.
 *
 * The main use of this is for scenarios where the meta-data of the normal
 * net_buf isn't needed and causes too much overhead. This could be e.g.
 * when the buffer only needs to be allocated on the stack or when the
 * access to and lifetime of the buffer is well controlled and constrained.
 */
struct net_buf_simple {
	/** Pointer to the start of data in the buffer. */
	uint8_t *data;

	/**
	 * Length of the data behind the data pointer.
	 *
	 * The room left for more data is net_buf_simple_tailroom(), not net_buf_simple::size
	 * minus net_buf_simple::len: net_buf_simple::size counts the headroom as well.
	 */
	uint16_t len;

	/** Amount of data that net_buf_simple#__buf can store. */
	uint16_t size;

	/** Start of the data storage. Not to be accessed directly
	 *  (the data pointer should be used instead).
	 */
	uint8_t *__buf;
};

/**
 *
 * @brief Define a net_buf_simple stack variable and get a pointer to it.
 *
 * This is a helper macro which is used to define a net_buf_simple object on
 * the stack and the get a pointer to it as follows:
 *
 * struct net_buf_simple *my_buf = NET_BUF_SIMPLE(10);
 *
 * After creating the object it needs to be initialized by calling
 * net_buf_simple_init().
 *
 * @param _size Maximum data storage for the buffer.
 *
 * @return Pointer to stack-allocated net_buf_simple object.
 */
#define NET_BUF_SIMPLE(_size)                        \
	((struct net_buf_simple *)(&(struct {        \
		struct net_buf_simple buf;           \
		uint8_t data[_size];                 \
	}) {                                         \
		.buf.size = _size,                   \
	}))

/**
 * @brief Initialize a net_buf_simple object.
 *
 * This needs to be called after creating a net_buf_simple object using
 * the NET_BUF_SIMPLE macro.
 *
 * @param buf Buffer to initialize.
 * @param reserve_head Headroom to reserve.
 */
static inline void net_buf_simple_init(struct net_buf_simple *buf,
				       size_t reserve_head)
{
	if (!buf->__buf) {
		buf->__buf = (uint8_t *)buf + sizeof(*buf);
	}

	buf->data = buf->__buf + reserve_head;
	buf->len = 0U;
}

/**
 * @brief Initialize a net_buf_simple object with data.
 *
 * Initialized buffer object with external data.
 *
 * @param buf Buffer to initialize.
 * @param data External data pointer
 * @param size Amount of data the pointed data buffer if able to fit.
 */
void net_buf_simple_init_with_data(struct net_buf_simple *buf,
				   void *data, size_t size);

/**
 * @brief Reset buffer
 *
 * Reset buffer data so it can be reused for other purposes.
 *
 * @param buf Buffer to reset.
 */
static inline void net_buf_simple_reset(struct net_buf_simple *buf)
{
	buf->len  = 0U;
	buf->data = buf->__buf;
}

/**
 * Clone buffer state, using the same data buffer.
 *
 * Initializes a buffer to point to the same data as an existing buffer.
 * Allows operations on the same data without altering the length and
 * offset of the original.
 *
 * @param original Buffer to clone.
 * @param clone The new clone.
 */
void net_buf_simple_clone(const struct net_buf_simple *original,
			  struct net_buf_simple *clone);

/**
 * @brief Prepare data to be added at the end of the buffer
 *
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param len Number of bytes to increment the length with.
 *
 * @return The original tail of the buffer.
 */
void *net_buf_simple_add(struct net_buf_simple *buf, size_t len);

/**
 * @brief Copy given number of bytes from memory to the end of the buffer
 *
 * Increments the data length of the  buffer to account for more data at the
 * end.
 *
 * @param buf Buffer to update.
 * @param mem Location of data to be added.
 * @param len Length of data to be added
 *
 * @return The original tail of the buffer.
 */
void *net_buf_simple_add_mem(struct net_buf_simple *buf, const void *mem,
			     size_t len);

/**
 * @brief Add (8-bit) byte at the end of the buffer
 *
 * Increments the data length of the  buffer to account for more data at the
 * end.
 *
 * @param buf Buffer to update.
 * @param val byte value to be added.
 *
 * @return Pointer to the value added
 */
uint8_t *net_buf_simple_add_u8(struct net_buf_simple *buf, uint8_t val);

/**
 * @brief Add 16-bit value at the end of the buffer
 *
 * Adds 16-bit value in little endian format at the end of buffer.
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param val 16-bit value to be added.
 */
void net_buf_simple_add_le16(struct net_buf_simple *buf, uint16_t val);

/**
 * @brief Add 16-bit value at the end of the buffer
 *
 * Adds 16-bit value in big endian format at the end of buffer.
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param val 16-bit value to be added.
 */
void net_buf_simple_add_be16(struct net_buf_simple *buf, uint16_t val);

/**
 * @brief Add 24-bit value at the end of the buffer
 *
 * Adds 24-bit value in little endian format at the end of buffer.
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param val 24-bit value to be added.
 */
void net_buf_simple_add_le24(struct net_buf_simple *buf, uint32_t val);

/**
 * @brief Add 24-bit value at the end of the buffer
 *
 * Adds 24-bit value in big endian format at the end of buffer.
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param val 24-bit value to be added.
 */
void net_buf_simple_add_be24(struct net_buf_simple *buf, uint32_t val);

/**
 * @brief Add 32-bit value at the end of the buffer
 *
 * Adds 32-bit value in little endian format at the end of buffer.
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param val 32-bit value to be added.
 */
void net_buf_simple_add_le32(struct net_buf_simple *buf, uint32_t val);

/**
 * @brief Add 32-bit value at the end of the buffer
 *
 * Adds 32-bit value in big endian format at the end of buffer.
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param val 32-bit value to be added.
 */
void net_buf_simple_add_be32(struct net_buf_simple *buf, uint32_t val);

/**
 * @brief Add 40-bit value at the end of the buffer
 *
 * Adds 40-bit value in little endian format at the end of buffer.
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param val 40-bit value to be added.
 */
void net_buf_simple_add_le40(struct net_buf_simple *buf, uint64_t val);

/**
 * @brief Add 40-bit value at the end of the buffer
 *
 * Adds 40-bit value in big endian format at the end of buffer.
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param val 40-bit value to be added.
 */
void net_buf_simple_add_be40(struct net_buf_simple *buf, uint64_t val);

/**
 * @brief Add 48-bit value at the end of the buffer
 *
 * Adds 48-bit value in little endian format at the end of buffer.
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param val 48-bit value to be added.
 */
void net_buf_simple_add_le48(struct net_buf_simple *buf, uint64_t val);

/**
 * @brief Add 48-bit value at the end of the buffer
 *
 * Adds 48-bit value in big endian format at the end of buffer.
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param val 48-bit value to be added.
 */
void net_buf_simple_add_be48(struct net_buf_simple *buf, uint64_t val);

/**
 * @brief Add 64-bit value at the end of the buffer
 *
 * Adds 64-bit value in little endian format at the end of buffer.
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param val 64-bit value to be added.
 */
void net_buf_simple_add_le64(struct net_buf_simple *buf, uint64_t val);

/**
 * @brief Add 64-bit value at the end of the buffer
 *
 * Adds 64-bit value in big endian format at the end of buffer.
 * Increments the data length of a buffer to account for more data
 * at the end.
 *
 * @param buf Buffer to update.
 * @param val 64-bit value to be added.
 */
void net_buf_simple_add_be64(struct net_buf_simple *buf, uint64_t val);

/**
 * @brief Remove data from the end of the buffer.
 *
 * Removes data from the end of the buffer by modifying the buffer length.
 *
 * @param buf Buffer to update.
 * @param len Number of bytes to remove.
 *
 * @return New end of the buffer data.
 */
void *net_buf_simple_remove_mem(struct net_buf_simple *buf, size_t len);

/**
 * @brief Remove a 8-bit value from the end of the buffer
 *
 * Same idea as with net_buf_simple_remove_mem(), but a helper for operating
 * on 8-bit values.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return The 8-bit removed value
 */
uint8_t net_buf_simple_remove_u8(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 16 bits from the end of the buffer.
 *
 * Same idea as with net_buf_simple_remove_mem(), but a helper for operating
 * on 16-bit little endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 16-bit value converted from little endian to host endian.
 */
uint16_t net_buf_simple_remove_le16(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 16 bits from the end of the buffer.
 *
 * Same idea as with net_buf_simple_remove_mem(), but a helper for operating
 * on 16-bit big endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 16-bit value converted from big endian to host endian.
 */
uint16_t net_buf_simple_remove_be16(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 24 bits from the end of the buffer.
 *
 * Same idea as with net_buf_simple_remove_mem(), but a helper for operating
 * on 24-bit little endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 24-bit value converted from little endian to host endian.
 */
uint32_t net_buf_simple_remove_le24(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 24 bits from the end of the buffer.
 *
 * Same idea as with net_buf_simple_remove_mem(), but a helper for operating
 * on 24-bit big endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 24-bit value converted from big endian to host endian.
 */
uint32_t net_buf_simple_remove_be24(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 32 bits from the end of the buffer.
 *
 * Same idea as with net_buf_simple_remove_mem(), but a helper for operating
 * on 32-bit little endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 32-bit value converted from little endian to host endian.
 */
uint32_t net_buf_simple_remove_le32(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 32 bits from the end of the buffer.
 *
 * Same idea as with net_buf_simple_remove_mem(), but a helper for operating
 * on 32-bit big endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 32-bit value converted from big endian to host endian.
 */
uint32_t net_buf_simple_remove_be32(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 40 bits from the end of the buffer.
 *
 * Same idea as with net_buf_simple_remove_mem(), but a helper for operating
 * on 40-bit little endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 40-bit value converted from little endian to host endian.
 */
uint64_t net_buf_simple_remove_le40(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 40 bits from the end of the buffer.
 *
 * Same idea as with net_buf_simple_remove_mem(), but a helper for operating
 * on 40-bit big endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 40-bit value converted from big endian to host endian.
 */
uint64_t net_buf_simple_remove_be40(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 48 bits from the end of the buffer.
 *
 * Same idea as with net_buf_simple_remove_mem(), but a helper for operating
 * on 48-bit little endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 48-bit value converted from little endian to host endian.
 */
uint64_t net_buf_simple_remove_le48(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 48 bits from the end of the buffer.
 *
 * Same idea as with net_buf_simple_remove_mem(), but a helper for operating
 * on 48-bit big endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 48-bit value converted from big endian to host endian.
 */
uint64_t net_buf_simple_remove_be48(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 64 bits from the end of the buffer.
 *
 * Same idea as with net_buf_simple_remove_mem(), but a helper for operating
 * on 64-bit little endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 64-bit value converted from little endian to host endian.
 */
uint64_t net_buf_simple_remove_le64(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 64 bits from the end of the buffer.
 *
 * Same idea as with net_buf_simple_remove_mem(), but a helper for operating
 * on 64-bit big endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 64-bit value converted from big endian to host endian.
 */
uint64_t net_buf_simple_remove_be64(struct net_buf_simple *buf);

/**
 * @brief Prepare data to be added to the start of the buffer
 *
 * Modifies the data pointer and buffer length to account for more data
 * in the beginning of the buffer.
 *
 * @param buf Buffer to update.
 * @param len Number of bytes to add to the beginning.
 *
 * @return The new beginning of the buffer data.
 */
void *net_buf_simple_push(struct net_buf_simple *buf, size_t len);

/**
 * @brief Copy given number of bytes from memory to the start of the buffer.
 *
 * Modifies the data pointer and buffer length to account for more data
 * in the beginning of the buffer.
 *
 * @param buf Buffer to update.
 * @param mem Location of data to be added.
 * @param len Length of data to be added.
 *
 * @return The new beginning of the buffer data.
 */
void *net_buf_simple_push_mem(struct net_buf_simple *buf, const void *mem,
			      size_t len);

/**
 * @brief Push 16-bit value to the beginning of the buffer
 *
 * Adds 16-bit value in little endian format to the beginning of the
 * buffer.
 *
 * @param buf Buffer to update.
 * @param val 16-bit value to be pushed to the buffer.
 */
void net_buf_simple_push_le16(struct net_buf_simple *buf, uint16_t val);

/**
 * @brief Push 16-bit value to the beginning of the buffer
 *
 * Adds 16-bit value in big endian format to the beginning of the
 * buffer.
 *
 * @param buf Buffer to update.
 * @param val 16-bit value to be pushed to the buffer.
 */
void net_buf_simple_push_be16(struct net_buf_simple *buf, uint16_t val);

/**
 * @brief Push 8-bit value to the beginning of the buffer
 *
 * Adds 8-bit value the beginning of the buffer.
 *
 * @param buf Buffer to update.
 * @param val 8-bit value to be pushed to the buffer.
 */
void net_buf_simple_push_u8(struct net_buf_simple *buf, uint8_t val);

/**
 * @brief Push 24-bit value to the beginning of the buffer
 *
 * Adds 24-bit value in little endian format to the beginning of the
 * buffer.
 *
 * @param buf Buffer to update.
 * @param val 24-bit value to be pushed to the buffer.
 */
void net_buf_simple_push_le24(struct net_buf_simple *buf, uint32_t val);

/**
 * @brief Push 24-bit value to the beginning of the buffer
 *
 * Adds 24-bit value in big endian format to the beginning of the
 * buffer.
 *
 * @param buf Buffer to update.
 * @param val 24-bit value to be pushed to the buffer.
 */
void net_buf_simple_push_be24(struct net_buf_simple *buf, uint32_t val);

/**
 * @brief Push 32-bit value to the beginning of the buffer
 *
 * Adds 32-bit value in little endian format to the beginning of the
 * buffer.
 *
 * @param buf Buffer to update.
 * @param val 32-bit value to be pushed to the buffer.
 */
void net_buf_simple_push_le32(struct net_buf_simple *buf, uint32_t val);

/**
 * @brief Push 32-bit value to the beginning of the buffer
 *
 * Adds 32-bit value in big endian format to the beginning of the
 * buffer.
 *
 * @param buf Buffer to update.
 * @param val 32-bit value to be pushed to the buffer.
 */
void net_buf_simple_push_be32(struct net_buf_simple *buf, uint32_t val);

/**
 * @brief Push 40-bit value to the beginning of the buffer
 *
 * Adds 40-bit value in little endian format to the beginning of the
 * buffer.
 *
 * @param buf Buffer to update.
 * @param val 40-bit value to be pushed to the buffer.
 */
void net_buf_simple_push_le40(struct net_buf_simple *buf, uint64_t val);

/**
 * @brief Push 40-bit value to the beginning of the buffer
 *
 * Adds 40-bit value in big endian format to the beginning of the
 * buffer.
 *
 * @param buf Buffer to update.
 * @param val 40-bit value to be pushed to the buffer.
 */
void net_buf_simple_push_be40(struct net_buf_simple *buf, uint64_t val);

/**
 * @brief Push 48-bit value to the beginning of the buffer
 *
 * Adds 48-bit value in little endian format to the beginning of the
 * buffer.
 *
 * @param buf Buffer to update.
 * @param val 48-bit value to be pushed to the buffer.
 */
void net_buf_simple_push_le48(struct net_buf_simple *buf, uint64_t val);

/**
 * @brief Push 48-bit value to the beginning of the buffer
 *
 * Adds 48-bit value in big endian format to the beginning of the
 * buffer.
 *
 * @param buf Buffer to update.
 * @param val 48-bit value to be pushed to the buffer.
 */
void net_buf_simple_push_be48(struct net_buf_simple *buf, uint64_t val);

/**
 * @brief Push 64-bit value to the beginning of the buffer
 *
 * Adds 64-bit value in little endian format to the beginning of the
 * buffer.
 *
 * @param buf Buffer to update.
 * @param val 64-bit value to be pushed to the buffer.
 */
void net_buf_simple_push_le64(struct net_buf_simple *buf, uint64_t val);

/**
 * @brief Push 64-bit value to the beginning of the buffer
 *
 * Adds 64-bit value in big endian format to the beginning of the
 * buffer.
 *
 * @param buf Buffer to update.
 * @param val 64-bit value to be pushed to the buffer.
 */
void net_buf_simple_push_be64(struct net_buf_simple *buf, uint64_t val);

/**
 * @brief Remove data from the beginning of the buffer.
 *
 * Removes data from the beginning of the buffer by modifying the data
 * pointer and buffer length.
 *
 * @param buf Buffer to update.
 * @param len Number of bytes to remove.
 *
 * @return New beginning of the buffer data.
 */
void *net_buf_simple_pull(struct net_buf_simple *buf, size_t len);

/**
 * @brief Remove data from the beginning of the buffer.
 *
 * Removes data from the beginning of the buffer by modifying the data
 * pointer and buffer length.
 *
 * @param buf Buffer to update.
 * @param len Number of bytes to remove.
 *
 * @return Pointer to the old location of the buffer data.
 */
void *net_buf_simple_pull_mem(struct net_buf_simple *buf, size_t len);

/**
 * @brief Remove a 8-bit value from the beginning of the buffer
 *
 * Same idea as with net_buf_simple_pull(), but a helper for operating
 * on 8-bit values.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return The 8-bit removed value
 */
uint8_t net_buf_simple_pull_u8(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 16 bits from the beginning of the buffer.
 *
 * Same idea as with net_buf_simple_pull(), but a helper for operating
 * on 16-bit little endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 16-bit value converted from little endian to host endian.
 */
uint16_t net_buf_simple_pull_le16(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 16 bits from the beginning of the buffer.
 *
 * Same idea as with net_buf_simple_pull(), but a helper for operating
 * on 16-bit big endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 16-bit value converted from big endian to host endian.
 */
uint16_t net_buf_simple_pull_be16(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 24 bits from the beginning of the buffer.
 *
 * Same idea as with net_buf_simple_pull(), but a helper for operating
 * on 24-bit little endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 24-bit value converted from little endian to host endian.
 */
uint32_t net_buf_simple_pull_le24(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 24 bits from the beginning of the buffer.
 *
 * Same idea as with net_buf_simple_pull(), but a helper for operating
 * on 24-bit big endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 24-bit value converted from big endian to host endian.
 */
uint32_t net_buf_simple_pull_be24(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 32 bits from the beginning of the buffer.
 *
 * Same idea as with net_buf_simple_pull(), but a helper for operating
 * on 32-bit little endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 32-bit value converted from little endian to host endian.
 */
uint32_t net_buf_simple_pull_le32(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 32 bits from the beginning of the buffer.
 *
 * Same idea as with net_buf_simple_pull(), but a helper for operating
 * on 32-bit big endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 32-bit value converted from big endian to host endian.
 */
uint32_t net_buf_simple_pull_be32(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 40 bits from the beginning of the buffer.
 *
 * Same idea as with net_buf_simple_pull(), but a helper for operating
 * on 40-bit little endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 40-bit value converted from little endian to host endian.
 */
uint64_t net_buf_simple_pull_le40(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 40 bits from the beginning of the buffer.
 *
 * Same idea as with net_buf_simple_pull(), but a helper for operating
 * on 40-bit big endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 40-bit value converted from big endian to host endian.
 */
uint64_t net_buf_simple_pull_be40(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 48 bits from the beginning of the buffer.
 *
 * Same idea as with net_buf_simple_pull(), but a helper for operating
 * on 48-bit little endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 48-bit value converted from little endian to host endian.
 */
uint64_t net_buf_simple_pull_le48(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 48 bits from the beginning of the buffer.
 *
 * Same idea as with net_buf_simple_pull(), but a helper for operating
 * on 48-bit big endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 48-bit value converted from big endian to host endian.
 */
uint64_t net_buf_simple_pull_be48(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 64 bits from the beginning of the buffer.
 *
 * Same idea as with net_buf_simple_pull(), but a helper for operating
 * on 64-bit little endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 64-bit value converted from little endian to host endian.
 */
uint64_t net_buf_simple_pull_le64(struct net_buf_simple *buf);

/**
 * @brief Remove and convert 64 bits from the beginning of the buffer.
 *
 * Same idea as with net_buf_simple_pull(), but a helper for operating
 * on 64-bit big endian data.
 *
 * @param buf A valid pointer on a buffer.
 *
 * @return 64-bit value converted from big endian to host endian.
 */
uint64_t net_buf_simple_pull_be64(struct net_buf_simple *buf);

/**
 * @brief Get the tail pointer for a buffer.
 *
 * Get a pointer to the end of the data in a buffer.
 *
 * @param buf Buffer.
 *
 * @return Tail pointer for the buffer.
 */
static inline uint8_t *net_buf_simple_tail(const struct net_buf_simple *buf)
{
	return buf->data + buf->len;
}

/**
 * @brief Check buffer headroom.
 *
 * Check how much free space there is in the beginning of the buffer.
 *
 * buf A valid pointer on a buffer
 *
 * @return Number of bytes available in the beginning of the buffer.
 */
static inline size_t net_buf_simple_headroom(const struct net_buf_simple *buf)
{
	return buf->data - buf->__buf;
}

/**
 * @brief Check buffer tailroom.
 *
 * Check how much free space there is at the end of the buffer.
 *
 * @param buf A valid pointer on a buffer
 *
 * @return Number of bytes available at the end of the buffer.
 */
static inline size_t net_buf_simple_tailroom(const struct net_buf_simple *buf)
{
	return buf->size - net_buf_simple_headroom(buf) - buf->len;
}

/**
 * @brief Check that a buffer's length and pointers are self-consistent.
 *
 * Validates the invariant that must always hold for a well-formed buffer:
 * @c data points within the backing storage and @c len does not extend past
 * the end of that storage. A buffer whose #net_buf_simple::len has been
 * corrupted (for example wrapped to a large value) or whose #net_buf_simple::data
 * has been moved outside @c __buf..__buf+size fails this check.
 *
 * This does not (and cannot) detect a @c len that was changed to a different
 * but still in-bounds value; it detects only out-of-bounds corruption, which is
 * what turns into an out-of-bounds read or write when the buffer is used.
 *
 * @param buf Buffer to validate.
 *
 * @return true if the buffer is self-consistent, false otherwise.
 */
static inline bool net_buf_simple_is_valid(const struct net_buf_simple *buf)
{
	size_t headroom;

	if (buf == NULL || buf->__buf == NULL || buf->data < buf->__buf) {
		return false;
	}

	headroom = net_buf_simple_headroom(buf);
	if (headroom > buf->size) {
		return false;
	}

	return (size_t)buf->len <= (size_t)(buf->size - headroom);
}

/**
 * @brief Parsing state of a buffer.
 *
 * This is used for temporarily storing the parsing state of a buffer
 * while giving control of the parsing to a routine which we don't
 * control.
 */
struct net_buf_simple_state {
	/** Offset of the data pointer from the beginning of the storage */
	uint16_t offset;
	/** Length of data */
	uint16_t len;
};

/**
 * @brief Save the parsing state of a buffer.
 *
 * Saves the parsing state of a buffer so it can be restored later.
 *
 * @param buf Buffer from which the state should be saved.
 * @param state Storage for the state.
 */
static inline void net_buf_simple_save(const struct net_buf_simple *buf,
				       struct net_buf_simple_state *state)
{
	state->offset = (uint16_t)net_buf_simple_headroom(buf);
	state->len = buf->len;
}

/**
 * @brief Restore the parsing state of a buffer.
 *
 * Restores the parsing state of a buffer from a state previously stored
 * by net_buf_simple_save().
 *
 * @param buf Buffer to which the state should be restored.
 * @param state Stored state.
 */
static inline void net_buf_simple_restore(struct net_buf_simple *buf,
					  struct net_buf_simple_state *state)
{
	buf->data = buf->__buf + state->offset;
	buf->len = state->len;
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NET_BUF_H_ */
