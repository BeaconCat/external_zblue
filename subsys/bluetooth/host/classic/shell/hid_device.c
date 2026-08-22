/*
 * Copyright 2025 Xiaomi Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/hid_device.h>
#include <zephyr/bluetooth/hci.h>

#if defined(CONFIG_BT_SHELL)
#include <zephyr/shell/shell.h>
#include "host/shell/bt.h"
#include "common/bt_shell_private.h"
#endif

#define HELP_NONE "[none]"

#define BT_HID_DEVICE_VERSION      0x0101
#define BT_HID_PARSER_VERSION      0x0111
#define BT_HID_DEVICE_SUBCLASS     0x80
#define BT_HID_DEVICE_COUNTRY_CODE 0x00
#define BT_HID_PROTO_INTERRUPT     0x0013

#define BT_HID_LANG_ID_ENGLISH 0x0409
#define BT_HID_LANG_ID_OFFSET  0x0100

#define BT_HID_SUPERVISION_TIMEOUT 0x0c80
#define BT_HID_MAX_LATENCY         0x0640
#define BT_HID_MIN_LATENCY         0x0c80

static uint8_t hid_descriptor[] = {
	0x05, 0x01, /* Usage Page (Generic Desktop) */
	0x09, 0x02, /* Usage (Mouse) */
	0xa1, 0x01, /* Collection (Application) */
	0x85, 0x01, /* Report ID (1) */
	0x09, 0x01, /* Usage (Pointer) */
	0xa1, 0x00, /* Collection (Physical) */
	0x05, 0x09, /* Usage Page (Buttons) */
	0x19, 0x01, /* Usage Minimum (1) */
	0x29, 0x03, /* Usage Maximum (3) */
	0x15, 0x00, /* Logical Minimum (0) */
	0x25, 0x01, /* Logical Maximum (1) */
	0x75, 0x01, /* Report Size (1) */
	0x95, 0x03, /* Report Count (3) */
	0x81, 0x02, /* Input (Data, Variable, Absolute) */
	0x75, 0x05, /* Report Size (5) */
	0x95, 0x01, /* Report Count (1) */
	0x81, 0x01, /* Input (Constant) */
	0x05, 0x01, /* Usage Page (Generic Desktop) */
	0x09, 0x30, /* Usage (X) */
	0x09, 0x31, /* Usage (Y) */
	0x15, 0x81, /* Logical Minimum (-127) */
	0x25, 0x7f, /* Logical Maximum (127) */
	0x75, 0x08, /* Report Size (8) */
	0x95, 0x02, /* Report Count (2) */
	0x81, 0x06, /* Input (Data, Variable, Relative) */
	0xc0,       /* End Collection (Physical) */
	0xc0        /* End Collection (Application) */
};

static struct bt_sdp_attribute hid_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
			BT_SDP_ARRAY_16(BT_SDP_HID_SVCLASS)
		}
	    )
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROTO_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 13),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_PROTO_L2CAP)
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(BT_SDP_PROTO_HID)
			}
			)
		},
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_PROTO_HID)
			}
			)
		},
		)
	),
	BT_SDP_LIST(BT_SDP_ATTR_PROFILE_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				BT_SDP_ARRAY_16(BT_SDP_HID_SVCLASS)},
				{
					BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
					BT_SDP_ARRAY_16(BT_HID_DEVICE_VERSION)
				}
			)
		}
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_ADD_PROTO_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 15),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 13),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
				BT_SDP_DATA_ELEM_LIST(
				{
					BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
					BT_SDP_ARRAY_16(BT_SDP_PROTO_L2CAP)
				},
				{
					BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
					BT_SDP_ARRAY_16(BT_HID_PROTO_INTERRUPT)
				}
				)
			},
			{
				BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
				BT_SDP_DATA_ELEM_LIST(
				{
					BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
					BT_SDP_ARRAY_16(BT_SDP_PROTO_HID)
				}
				)
			}
			)
		}
		)
	),
	BT_SDP_SERVICE_NAME("HID CONTROL"),
	{
		BT_SDP_ATTR_HID_DEVICE_RELEASE_NUMBER,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
			BT_SDP_ARRAY_16(BT_HID_DEVICE_VERSION)
		}
	},
	{
		BT_SDP_ATTR_HID_PARSER_VERSION,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
			BT_SDP_ARRAY_16(BT_HID_PARSER_VERSION)
		}
	},
	{
		BT_SDP_ATTR_HID_DEVICE_SUBCLASS,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT8),
			BT_SDP_ARRAY_8(BT_HID_DEVICE_SUBCLASS)
		}
	},
	{
		BT_SDP_ATTR_HID_COUNTRY_CODE,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT8),
			BT_SDP_ARRAY_8(BT_HID_DEVICE_COUNTRY_CODE)
		}
	},
	{
		BT_SDP_ATTR_HID_VIRTUAL_CABLE,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_BOOL),
			BT_SDP_ARRAY_8(0x01)
		}
	},
	{
		BT_SDP_ATTR_HID_RECONNECT_INITIATE,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_BOOL),
			BT_SDP_ARRAY_8(0x01)
		}
	},
	BT_SDP_LIST(
		BT_SDP_ATTR_HID_DESCRIPTOR_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ16, sizeof(hid_descriptor) + 8),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ16, sizeof(hid_descriptor) + 5),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT8),
				BT_SDP_ARRAY_8(0x22),
			},
			{
				BT_SDP_TYPE_SIZE_VAR(BT_SDP_TEXT_STR16,
					sizeof(hid_descriptor)
				),
				hid_descriptor,
			}
			)
		}
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_HID_LANG_ID_BASE_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8),
		BT_SDP_DATA_ELEM_LIST(
		{
			BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			BT_SDP_DATA_ELEM_LIST(
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(BT_HID_LANG_ID_ENGLISH),
			},
			{
				BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				BT_SDP_ARRAY_16(BT_HID_LANG_ID_OFFSET),
			}
			),
		}
		)
	),
	{
		BT_SDP_ATTR_HID_BATTERY_POWER,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_BOOL),
			BT_SDP_ARRAY_8(0x01)
		}
	},
	{
		BT_SDP_ATTR_HID_REMOTE_WAKEUP,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_BOOL),
			BT_SDP_ARRAY_8(0x01)
		}
	},
	{
		BT_SDP_ATTR_HID_PROFILE_VERSION,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
			BT_SDP_ARRAY_16(BT_HID_DEVICE_VERSION)
		}
	},
	{
		BT_SDP_ATTR_HID_BOOT_DEVICE,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_BOOL),
			BT_SDP_ARRAY_8(0x01)
		}
	},
	{
		BT_SDP_ATTR_HID_NORMALLY_CONNECTABLE,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_BOOL),
			BT_SDP_ARRAY_8(0x00)
		}
	},
	{
		BT_SDP_ATTR_HID_SUPERVISION_TIMEOUT,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
			BT_SDP_ARRAY_16(BT_HID_SUPERVISION_TIMEOUT)
		}
	},
	{
		BT_SDP_ATTR_HID_MAX_LATENCY,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
			BT_SDP_ARRAY_16(BT_HID_MAX_LATENCY)
		}
	},
	{
		BT_SDP_ATTR_HID_MIN_LATENCY,
		{
			BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
			BT_SDP_ARRAY_16(BT_HID_MIN_LATENCY)
		}
	},
};

static struct bt_hid_device *default_hid;
static int hid_registered;
static struct bt_sdp_record hid_rec = BT_SDP_RECORD(hid_attrs);

static void hid_connect_cb(struct bt_hid_device *hid)
{
	printk("HID device connected: %p\n", hid);

	default_hid = hid;
}

static void hid_disconnected_cb(struct bt_hid_device *hid)
{
	printk("HID device disconnected: %p\n", hid);

	default_hid = NULL;
}

static struct bt_hid_device_cb hid_cb = {
	.connected = hid_connect_cb,
	.disconnected = hid_disconnected_cb,
};

int bt_shell_hid_device_register(void)
{
	int err;

	if (hid_registered) {
		return 0;
	}

	err = bt_hid_device_register(&hid_cb);
	if (err != 0) {
		printk("HID register callback failed: %d\n", err);
		return err;
	}

	err = bt_sdp_register_service(&hid_rec);
	if (err != 0) {
		printk("HID register SDP failed: %d\n", err);
		return err;
	}

	hid_registered = 1;
	return 0;
}

#if defined(CONFIG_BT_SHELL)
static int cmd_hid_register(const struct shell *sh, int32_t argc, char *argv[])
{
	int err = bt_shell_hid_device_register();

	if (!err) {
		bt_shell_print("success");
	}
	return err;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	hid_device_cmds,
	SHELL_CMD_ARG(register, NULL, "register hid device", cmd_hid_register, 1, 0),
	SHELL_SUBCMD_SET_END);

static int cmd_hid_device(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	bt_shell_print("%s unknown parameter: %s", argv[0], argv[1]);

	return -ENOEXEC;
}

SHELL_CMD_ARG_REGISTER(hid_device, &hid_device_cmds, "Bluetooth HID Device sh commands",
		       cmd_hid_device, 1, 1);
#endif
