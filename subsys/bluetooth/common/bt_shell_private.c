/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>

#include <zephyr/shell/shell.h>

#include "bt_shell_private.h"

extern const struct shell *ctx_shell;

static void bt_shell_vfprintf(enum shell_vt100_color color, const char *fmt,
			      va_list args)
{
	shell_vfprintf(ctx_shell, color, fmt, args);
}

void bt_shell_hexdump(const uint8_t *data, size_t len)
{
	shell_hexdump(ctx_shell, data, len);
}

#define BT_SHELL_FPRINTF_IMPL(name, color) \
	void name(const char *fmt, ...) \
	{ \
		va_list args; \
		va_start(args, fmt); \
		bt_shell_vfprintf(color, fmt, args); \
		va_end(args); \
	}

void bt_shell_fprintf(enum shell_vt100_color color, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	bt_shell_vfprintf(color, fmt, args);
	va_end(args);
}

BT_SHELL_FPRINTF_IMPL(bt_shell_fprintf_info, SHELL_INFO)
BT_SHELL_FPRINTF_IMPL(bt_shell_fprintf_print, SHELL_NORMAL)
BT_SHELL_FPRINTF_IMPL(bt_shell_fprintf_warn, SHELL_WARNING)
BT_SHELL_FPRINTF_IMPL(bt_shell_fprintf_error, SHELL_ERROR)
