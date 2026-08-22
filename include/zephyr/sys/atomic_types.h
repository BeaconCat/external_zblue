/* Copyright (c) 1997-2015, Wind River Systems, Inc.
 * Copyright (c) 2021 Intel Corporation
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SYS_ATOMIC_TYPES_H_
#define ZEPHYR_INCLUDE_SYS_ATOMIC_TYPES_H_

#include <stdint.h>

#include <nuttx/atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t atomic_val_t;
#if INTPTR_MAX >= INT64_MAX
typedef volatile int64_t atomic_ptr_t;
typedef int64_t atomic_ptr_val_t;
#else
typedef volatile int32_t atomic_ptr_t;
typedef int32_t atomic_ptr_val_t;
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_ATOMIC_TYPES_H_ */
