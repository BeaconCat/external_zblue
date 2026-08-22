/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BT_SHELL_PRIVATE_H
#define __BT_SHELL_PRIVATE_H

#if defined(CONFIG_BT_SHELL)
#include <zephyr/shell/shell.h>

void bt_shell_fprintf(enum shell_vt100_color color, const char *fmt, ...);
void bt_shell_fprintf_info(const char *fmt, ...);
void bt_shell_fprintf_print(const char *fmt, ...);
void bt_shell_fprintf_warn(const char *fmt, ...);
void bt_shell_fprintf_error(const char *fmt, ...);
void bt_shell_hexdump(const uint8_t *data, size_t len);
#endif

#if defined(CONFIG_BT_HID_DEVICE)
int bt_shell_hid_device_register(void);
#endif

#if defined(CONFIG_BT_A2DP)
struct bt_conn;
int bt_shell_a2dp_register(void);
int bt_shell_a2dp_source_test_enable(void);
int bt_shell_a2dp_source_test_connect(struct bt_conn *conn);
#endif

#if defined(CONFIG_BT_SHELL)
#define bt_shell_info(_ft, ...) \
	bt_shell_fprintf_info(_ft "\n", ##__VA_ARGS__)
#define bt_shell_print(_ft, ...) \
	bt_shell_fprintf_print(_ft "\n", ##__VA_ARGS__)
#define bt_shell_warn(_ft, ...) \
	bt_shell_fprintf_warn(_ft "\n", ##__VA_ARGS__)
#define bt_shell_error(_ft, ...) \
	bt_shell_fprintf_error(_ft "\n", ##__VA_ARGS__)
#endif

#endif /* __BT_SHELL_PRIVATE_H */
