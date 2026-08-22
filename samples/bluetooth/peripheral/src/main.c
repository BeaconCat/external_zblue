/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/classic.h>
#include <zephyr/bluetooth/classic/rfcomm.h>
#include <zephyr/bluetooth/classic/sdp.h>
#include <zephyr/bluetooth/classic/hid_device.h>
#include <zephyr/bluetooth/classic/avrcp.h>

#define NYABULA_CLASS_OF_DEVICE 0x6c0580
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/services/cts.h>
#include <zephyr/bluetooth/services/hrs.h>
#include <zephyr/bluetooth/services/ias.h>

#include "common/bt_shell_private.h"

/* Custom Service Variables */
#define BT_UUID_CUSTOM_SERVICE_VAL \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

static const struct bt_uuid_128 vnd_uuid = BT_UUID_INIT_128(
	BT_UUID_CUSTOM_SERVICE_VAL);

static const struct bt_uuid_128 vnd_enc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1));

static const struct bt_uuid_128 vnd_auth_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2));

#define VND_MAX_LEN 20
#define BT_HR_HEARTRATE_DEFAULT_MIN 90U
#define BT_HR_HEARTRATE_DEFAULT_MAX 160U

static uint8_t vnd_value[VND_MAX_LEN + 1] = { 'V', 'e', 'n', 'd', 'o', 'r'};
static uint8_t vnd_auth_value[VND_MAX_LEN + 1] = { 'V', 'e', 'n', 'd', 'o', 'r'};
static uint8_t vnd_wwr_value[VND_MAX_LEN + 1] = { 'V', 'e', 'n', 'd', 'o', 'r' };

static ssize_t read_vnd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 strlen(value));
}

static ssize_t write_vnd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset,
			 uint8_t flags)
{
	uint8_t *value = attr->user_data;

	if (offset + len > VND_MAX_LEN) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);
	value[offset + len] = 0;

	return len;
}

static uint8_t simulate_vnd;
static uint8_t indicating;
static struct bt_gatt_indicate_params ind_params;

static void vnd_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	simulate_vnd = (value == BT_GATT_CCC_INDICATE) ? 1 : 0;
}

static void indicate_cb(struct bt_conn *conn,
			struct bt_gatt_indicate_params *params, uint8_t err)
{
	printk("Indication %s\n", err != 0U ? "fail" : "success");
}

static void indicate_destroy(struct bt_gatt_indicate_params *params)
{
	printk("Indication complete\n");
	indicating = 0U;
}

#define VND_LONG_MAX_LEN 74
static uint8_t vnd_long_value[VND_LONG_MAX_LEN + 1] = {
		  'V', 'e', 'n', 'd', 'o', 'r', ' ', 'd', 'a', 't', 'a', '1',
		  'V', 'e', 'n', 'd', 'o', 'r', ' ', 'd', 'a', 't', 'a', '2',
		  'V', 'e', 'n', 'd', 'o', 'r', ' ', 'd', 'a', 't', 'a', '3',
		  'V', 'e', 'n', 'd', 'o', 'r', ' ', 'd', 'a', 't', 'a', '4',
		  'V', 'e', 'n', 'd', 'o', 'r', ' ', 'd', 'a', 't', 'a', '5',
		  'V', 'e', 'n', 'd', 'o', 'r', ' ', 'd', 'a', 't', 'a', '6',
		  '.', ' ' };

static ssize_t write_long_vnd(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr, const void *buf,
			      uint16_t len, uint16_t offset, uint8_t flags)
{
	uint8_t *value = attr->user_data;

	if (flags & BT_GATT_WRITE_FLAG_PREPARE) {
		return 0;
	}

	if (offset + len > VND_LONG_MAX_LEN) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);
	value[offset + len] = 0;

	return len;
}

static const struct bt_uuid_128 vnd_long_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef3));

static struct bt_gatt_cep vnd_long_cep = {
	.properties = BT_GATT_CEP_RELIABLE_WRITE,
};

static int signed_value;

static ssize_t read_signed(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   void *buf, uint16_t len, uint16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 sizeof(signed_value));
}

static ssize_t write_signed(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset,
			    uint8_t flags)
{
	uint8_t *value = attr->user_data;

	if (offset + len > sizeof(signed_value)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);

	return len;
}

static const struct bt_uuid_128 vnd_signed_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x13345678, 0x1234, 0x5678, 0x1334, 0x56789abcdef3));

static const struct bt_uuid_128 vnd_write_cmd_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef4));

static ssize_t write_without_rsp_vnd(struct bt_conn *conn,
				     const struct bt_gatt_attr *attr,
				     const void *buf, uint16_t len, uint16_t offset,
				     uint8_t flags)
{
	uint8_t *value = attr->user_data;

	if (!(flags & BT_GATT_WRITE_FLAG_CMD)) {
		/* Write Request received. Reject it since this Characteristic
		 * only accepts Write Without Response.
		 */
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
	}

	if (offset + len > VND_MAX_LEN) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);
	value[offset + len] = 0;

	return len;
}

/* Vendor Primary Service Declaration */
BT_GATT_SERVICE_DEFINE(vnd_svc,
	BT_GATT_PRIMARY_SERVICE(&vnd_uuid),
	BT_GATT_CHARACTERISTIC(&vnd_enc_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
			       BT_GATT_CHRC_INDICATE,
			       BT_GATT_PERM_READ_ENCRYPT |
			       BT_GATT_PERM_WRITE_ENCRYPT,
			       read_vnd, write_vnd, vnd_value),
	BT_GATT_CCC(vnd_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
	BT_GATT_CHARACTERISTIC(&vnd_auth_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ_AUTHEN |
			       BT_GATT_PERM_WRITE_AUTHEN,
			       read_vnd, write_vnd, vnd_auth_value),
	BT_GATT_CHARACTERISTIC(&vnd_long_uuid.uuid, BT_GATT_CHRC_READ |
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_EXT_PROP,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE |
			       BT_GATT_PERM_PREPARE_WRITE,
			       read_vnd, write_long_vnd, &vnd_long_value),
	BT_GATT_CEP(&vnd_long_cep),
	BT_GATT_CHARACTERISTIC(&vnd_signed_uuid.uuid, BT_GATT_CHRC_READ |
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_AUTH,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_signed, write_signed, &signed_value),
	BT_GATT_CHARACTERISTIC(&vnd_write_cmd_uuid.uuid,
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL,
			       write_without_rsp_vnd, &vnd_wwr_value),
);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_CTS_VAL)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_CUSTOM_SERVICE_VAL),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	printk("Updated MTU: TX: %d RX: %d bytes\n", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = mtu_updated
};

static struct bt_conn *active_conn;
static struct bt_conn *classic_conn;
static struct k_work_delayable advertising_restart_work;
static struct k_thread controller_recovery_thread;
static K_THREAD_STACK_DEFINE(controller_recovery_stack, 8192);
static atomic_t controller_recovery_started;
static bool gatt_client_mode;
static bool a2dp_source_mode;

static void bt_ready(void);
static struct bt_conn_cb peripheral;
static struct bt_conn_auth_info_cb auth_info;
static struct bt_gatt_cb gatt_callbacks;
#if defined(CONFIG_BT_CLASSIC) && defined(CONFIG_BT_RFCOMM)
static void rfcomm_server_start(void);
#endif

static void controller_recovery(void *p1, void *p2, void *p3)
{
	int err;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	k_sleep(K_SECONDS(4));
	for (;;) {
		err = bt_disable();
		printk("Bluetooth host recovery disable: %d\n", err);
		if (!err) {
			break;
		}
		k_sleep(K_SECONDS(1));
	}

	for (;;) {
		err = bt_enable(NULL);
		printk("Bluetooth host recovery enable: %d\n", err);
		if (!err) {
			break;
		}
		k_sleep(K_SECONDS(1));
	}

	printk("Bluetooth host recovery conn callback: %d\n",
	       bt_conn_cb_register(&peripheral));
	printk("Bluetooth host recovery auth callback: %d\n",
	       bt_conn_auth_info_cb_register(&auth_info));
	bt_gatt_cb_register(&gatt_callbacks);
	bt_ready();
#if defined(CONFIG_BT_CLASSIC) && defined(CONFIG_BT_RFCOMM)
	rfcomm_server_start();
#endif
	atomic_clear(&controller_recovery_started);
}

static void controller_hardware_error(uint8_t dev_id, uint8_t hardware_code)
{
	printk("Bluetooth hardware error: dev %u code %u\n", dev_id,
	       hardware_code);
	if (atomic_cas(&controller_recovery_started, 0, 1)) {
		k_thread_create(&controller_recovery_thread,
				controller_recovery_stack,
				K_THREAD_STACK_SIZEOF(controller_recovery_stack),
				controller_recovery, NULL, NULL, NULL,
				K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
		k_thread_name_set(&controller_recovery_thread,
				  "BT recovery");
	}
}

static const struct bt_uuid_128 remote_service_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x9f7a0001, 0x7c3b, 0x4b58, 0xa942,
			   0x5f314e594142));
static const struct bt_uuid_128 remote_char_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x9f7a0002, 0x7c3b, 0x4b58, 0xa942,
			   0x5f314e594142));
static struct bt_gatt_discover_params client_discover;
static struct bt_gatt_read_params client_read;
static struct bt_gatt_write_params client_write;
static const uint8_t client_write_value[] = "K7-GATT-WRITE";

static void gatt_client_write_done(struct bt_conn *conn, uint8_t err,
				   struct bt_gatt_write_params *params)
{
	printk("GATT client write complete: err %u\n", err);
	bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static uint8_t gatt_client_read_done(struct bt_conn *conn, uint8_t err,
				     struct bt_gatt_read_params *params,
				     const void *data, uint16_t length)
{
	int ret;

	if (err) {
		printk("GATT client read failed: err %u\n", err);
		return BT_GATT_ITER_STOP;
	}

	if (data) {
		printk("GATT client read: %u bytes: %.*s\n", length, length,
		       (const char *)data);
		return BT_GATT_ITER_CONTINUE;
	}

	(void)memset(&client_write, 0, sizeof(client_write));
	client_write.func = gatt_client_write_done;
	client_write.handle = client_read.single.handle;
	client_write.data = client_write_value;
	client_write.length = sizeof(client_write_value) - 1;
	ret = bt_gatt_write(conn, &client_write);
	printk("GATT client write start: %d\n", ret);
	return BT_GATT_ITER_STOP;
}

static uint8_t gatt_client_discovered(struct bt_conn *conn,
				      const struct bt_gatt_attr *attr,
				      struct bt_gatt_discover_params *params)
{
	int ret;

	if (!attr) {
		printk("GATT client discovery exhausted\n");
		return BT_GATT_ITER_STOP;
	}

	if (params->type == BT_GATT_DISCOVER_PRIMARY) {
		const struct bt_gatt_service_val *service = attr->user_data;
		char uuid[BT_UUID_STR_LEN];

		bt_uuid_to_str(service->uuid, uuid, sizeof(uuid));
		printk("GATT client service: %s start 0x%04x end 0x%04x\n",
		       uuid, attr->handle, service->end_handle);
		if (bt_uuid_cmp(service->uuid, &remote_service_uuid.uuid)) {
			return BT_GATT_ITER_CONTINUE;
		}
		params->uuid = &remote_char_uuid.uuid;
		params->start_handle = attr->handle + 1;
		params->end_handle = service->end_handle;
		params->type = BT_GATT_DISCOVER_CHARACTERISTIC;
		ret = bt_gatt_discover(conn, params);
		printk("GATT client characteristic discovery start: %d\n", ret);
		return BT_GATT_ITER_STOP;
	}

	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		const struct bt_gatt_chrc *chrc = attr->user_data;

		printk("GATT client characteristic: handle 0x%04x value 0x%04x\n",
		       attr->handle, chrc->value_handle);
		(void)memset(&client_read, 0, sizeof(client_read));
		client_read.func = gatt_client_read_done;
		client_read.handle_count = 1;
		client_read.single.handle = chrc->value_handle;
		ret = bt_gatt_read(conn, &client_read);
		printk("GATT client read start: %d\n", ret);
		return BT_GATT_ITER_STOP;
	}

	return BT_GATT_ITER_CONTINUE;
}

static bool gatt_client_ad_parse(struct bt_data *data, void *user_data)
{
	bool *found = user_data;

	if ((data->type == BT_DATA_UUID128_ALL ||
	     data->type == BT_DATA_UUID128_SOME) &&
	    data->data_len >= sizeof(remote_service_uuid.val) &&
	    memcmp(data->data, remote_service_uuid.val,
		   sizeof(remote_service_uuid.val)) == 0) {
		*found = true;
		return false;
	}

	return true;
}

static void gatt_client_device_found(const bt_addr_le_t *addr, int8_t rssi,
				     uint8_t type,
				     struct net_buf_simple *ad)
{
	bool found = false;
	int err;

	if (active_conn || (type != BT_GAP_ADV_TYPE_ADV_IND &&
			    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND)) {
		return;
	}

	bt_data_parse(ad, gatt_client_ad_parse, &found);
	if (!found) {
		return;
	}

	printk("GATT client target found: RSSI %d\n", rssi);
	if (bt_le_scan_stop()) {
		return;
	}

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
				BT_LE_CONN_PARAM_DEFAULT, &active_conn);
	printk("GATT client connect start: %d\n", err);
}

static void gatt_client_start(void)
{
	int err = bt_le_scan_start(BT_LE_SCAN_ACTIVE,
				   gatt_client_device_found);

	printk("GATT client scan start: %d\n", err);
}

#if defined(CONFIG_BT_A2DP)
NET_BUF_POOL_FIXED_DEFINE(bt_a2dp_tx_pool, CONFIG_BT_MAX_CONN,
			  BT_L2CAP_BUF_SIZE(CONFIG_BT_L2CAP_TX_MTU),
			  CONFIG_BT_CONN_TX_USER_DATA_SIZE, NULL);
#endif

#if defined(CONFIG_BT_RFCOMM)
#define SDP_CLIENT_BUF_SIZE 512
NET_BUF_POOL_FIXED_DEFINE(sdp_client_pool, 1, SDP_CLIENT_BUF_SIZE, 8, NULL);

static struct bt_sdp_attribute spp_attrs[] = {
	BT_SDP_NEW_SERVICE,
	BT_SDP_LIST(
		BT_SDP_ATTR_SVCLASS_ID_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
		BT_SDP_DATA_ELEM_LIST(
			{ BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
			  BT_SDP_ARRAY_16(BT_SDP_SERIAL_PORT_SVCLASS) },
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROTO_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 12),
		BT_SDP_DATA_ELEM_LIST(
			{ BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
			  BT_SDP_DATA_ELEM_LIST(
				{ BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				  BT_SDP_ARRAY_16(BT_SDP_PROTO_L2CAP) },
			  ) },
			{ BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 5),
			  BT_SDP_DATA_ELEM_LIST(
				{ BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				  BT_SDP_ARRAY_16(BT_SDP_PROTO_RFCOMM) },
				{ BT_SDP_TYPE_SIZE(BT_SDP_UINT8),
				  BT_SDP_ARRAY_8(BT_RFCOMM_CHAN_SPP) },
			  ) },
		)
	),
	BT_SDP_LIST(
		BT_SDP_ATTR_PROFILE_DESC_LIST,
		BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8),
		BT_SDP_DATA_ELEM_LIST(
			{ BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
			  BT_SDP_DATA_ELEM_LIST(
				{ BT_SDP_TYPE_SIZE(BT_SDP_UUID16),
				  BT_SDP_ARRAY_16(BT_SDP_SERIAL_PORT_SVCLASS) },
				{ BT_SDP_TYPE_SIZE(BT_SDP_UINT16),
				  BT_SDP_ARRAY_16(0x0102) },
			  ) },
		)
	),
	BT_SDP_SERVICE_NAME("Nyabula Serial Port"),
};

static struct bt_sdp_record spp_record = BT_SDP_RECORD(spp_attrs);

static void rfcomm_connected(struct bt_rfcomm_dlc *dlc)
{
	printk("RFCOMM connected: mtu %u\n", dlc->mtu);
}

static void rfcomm_disconnected(struct bt_rfcomm_dlc *dlc)
{
	printk("RFCOMM disconnected\n");
}

static void rfcomm_received(struct bt_rfcomm_dlc *dlc, struct net_buf *buf)
{
	static const char response[] = "Nyabula SPP ack";
	struct net_buf *tx;
	int err;

	printk("RFCOMM received: %u bytes\n", buf->len);
	tx = bt_rfcomm_create_pdu(NULL);
	if (!tx) {
		return;
	}
	net_buf_add_mem(tx, response, sizeof(response) - 1);
	err = bt_rfcomm_dlc_send(dlc, tx);
	printk("RFCOMM send response: %d\n", err);
	if (err < 0) {
		net_buf_unref(tx);
	}
}

static struct bt_rfcomm_dlc_ops rfcomm_ops = {
	.connected = rfcomm_connected,
	.disconnected = rfcomm_disconnected,
	.recv = rfcomm_received,
};

static struct bt_rfcomm_dlc rfcomm_dlc = {
	.ops = &rfcomm_ops,
	.mtu = 1000,
};

static int rfcomm_accept(struct bt_conn *conn,
			 struct bt_rfcomm_server *server,
			 struct bt_rfcomm_dlc **dlc)
{
	if (rfcomm_dlc.session) {
		return -ENOMEM;
	}

	*dlc = &rfcomm_dlc;
	printk("RFCOMM incoming connection\n");
	return 0;
}

static struct bt_rfcomm_server rfcomm_server = {
	.channel = BT_RFCOMM_CHAN_SPP,
	.accept = rfcomm_accept,
};

static void rfcomm_server_start(void)
{
	int err;

	err = bt_rfcomm_server_register(&rfcomm_server);
	printk("RFCOMM server register: %d channel %u\n", err,
	       rfcomm_server.channel);
	if (!err) {
		printk("SPP SDP register: %d\n", bt_sdp_register_service(&spp_record));
	}

	printk("BR/EDR local name: %d\n", bt_br_write_local_name("Nyabula-SPP"));
	printk("BR/EDR class of device: %d\n",
	       bt_br_set_class_of_device(NYABULA_CLASS_OF_DEVICE));
	printk("BR/EDR connectable: %d\n", bt_br_set_connectable(true));
	printk("BR/EDR discoverable: %d\n", bt_br_set_discoverable(true));
}

static uint8_t sdp_spp_result(struct bt_conn *conn,
			      struct bt_sdp_client_result *result,
			      const struct bt_sdp_discover_params *params)
{
	uint16_t channel = 0;
	int err;

	if (!result || !result->resp_buf) {
		printk("SDP SPP service not found\n");
		return BT_SDP_DISCOVER_UUID_STOP;
	}

	err = bt_sdp_get_proto_param(result->resp_buf, BT_SDP_PROTO_RFCOMM,
				     &channel);
	printk("SDP SPP RFCOMM channel: err %d channel %u\n", err, channel);
	if (!err) {
		err = bt_rfcomm_dlc_connect(conn, &rfcomm_dlc, (uint8_t)channel);
		printk("RFCOMM connect request: %d\n", err);
	}

	return BT_SDP_DISCOVER_UUID_STOP;
}

static struct bt_sdp_discover_params sdp_spp = {
	.type = BT_SDP_DISCOVER_SERVICE_SEARCH_ATTR,
	.uuid = BT_UUID_DECLARE_16(BT_SDP_SERIAL_PORT_SVCLASS),
	.func = sdp_spp_result,
	.pool = &sdp_client_pool,
};
#endif

static void advertising_restart(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd,
			      ARRAY_SIZE(sd));
	if (err && err != -EALREADY) {
		printk("Advertising restart failed (err %d)\n", err);
		if (err == -ENOMEM || err == -EAGAIN || err == -EBUSY) {
			k_work_reschedule(&advertising_restart_work, K_MSEC(500));
		}
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
	} else {
		(void)k_work_cancel_delayable(&advertising_restart_work);
		printk("Connected\n");
		if (gatt_client_mode) {
			(void)memset(&client_discover, 0,
			             sizeof(client_discover));
			client_discover.uuid = NULL;
			client_discover.func = gatt_client_discovered;
			client_discover.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
			client_discover.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
			client_discover.type = BT_GATT_DISCOVER_PRIMARY;
			printk("GATT client service discovery start: %d\n",
			       bt_gatt_discover(conn, &client_discover));
			return;
		}
#if defined(CONFIG_BT_RFCOMM)
		if (conn == classic_conn) {
			int ret = bt_sdp_discover(conn, &sdp_spp);

			printk("SDP SPP discovery request: %d\n", ret);
			return;
		}
#endif
		active_conn = bt_conn_ref(conn);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (conn == classic_conn) {
		bt_conn_unref(classic_conn);
		classic_conn = NULL;
	} else if (active_conn) {
		bt_conn_unref(active_conn);
		active_conn = NULL;
		if (!gatt_client_mode) {
			k_work_schedule(&advertising_restart_work, K_MSEC(500));
		}
	}

	printk("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
#if defined(CONFIG_BT_HID_DEVICE)
	struct bt_conn_info info;
#endif

	printk("Security changed: level %u err %u\n", level, err);
#if defined(CONFIG_BT_HID_DEVICE)
	if (err == BT_SECURITY_ERR_SUCCESS && level >= BT_SECURITY_L3 &&
	    bt_conn_get_info(conn, &info) == 0 &&
	    info.type == BT_CONN_TYPE_BR) {
		printk("HID reconnect initiated: %s\n",
		       bt_hid_device_connect(conn) ? "yes" : "no");
#if defined(CONFIG_BT_A2DP)
		if (a2dp_source_mode) {
			printk("A2DP source connect schedule: %d\n",
			       bt_shell_a2dp_source_test_connect(conn));
		}
#endif
	}
#else
	ARG_UNUSED(conn);
#endif
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	printk("Pairing complete: bonded %u\n", bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	printk("Pairing failed: reason %u\n", reason);
}

static void alert_stop(void)
{
	printk("Alert stopped\n");
}

static void alert_start(void)
{
	printk("Mild alert started\n");
}

static void alert_high_start(void)
{
	printk("High alert started\n");
}

static struct bt_conn_cb peripheral = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static struct bt_conn_auth_info_cb auth_info = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

#if defined(CONFIG_BT_AVRCP_CONTROLLER)
static void avrcp_ct_connected(struct bt_conn *conn, struct bt_avrcp_ct *ct)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(ct);
	printk("AVRCP controller connected\n");
}

static void avrcp_ct_disconnected(struct bt_avrcp_ct *ct)
{
	ARG_UNUSED(ct);
	printk("AVRCP controller disconnected\n");
}

static const struct bt_avrcp_ct_cb avrcp_ct_callbacks = {
	.connected = avrcp_ct_connected,
	.disconnected = avrcp_ct_disconnected,
};
#endif

#if defined(CONFIG_BT_AVRCP_TARGET)
static void avrcp_tg_connected(struct bt_conn *conn, struct bt_avrcp_tg *tg)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(tg);
	printk("AVRCP target connected\n");
}

static void avrcp_tg_disconnected(struct bt_avrcp_tg *tg)
{
	ARG_UNUSED(tg);
	printk("AVRCP target disconnected\n");
}

static const struct bt_avrcp_tg_cb avrcp_tg_callbacks = {
	.connected = avrcp_tg_connected,
	.disconnected = avrcp_tg_disconnected,
};
#endif

#if defined(CONFIG_BT_CLASSIC)
static struct bt_br_discovery_result br_results[8];

static void br_remote_name_complete(const bt_addr_t *address,
				    const char *name, uint8_t status)
{
	char address_string[BT_ADDR_STR_LEN];

	bt_addr_to_str(address, address_string, sizeof(address_string));
	printk("BR/EDR remote name: %s status %u name %s\n", address_string,
	       status, status == 0 ? name : "<unavailable>");
	if (status == 0) {
		classic_conn = bt_conn_create_br(address, BT_BR_CONN_PARAM_DEFAULT);
		printk("BR/EDR connect request: %s\n",
		       classic_conn ? "started" : "failed");
	}
}

static void br_discovery_complete(const struct bt_br_discovery_result *results,
				  size_t count)
{
	char address[BT_ADDR_STR_LEN];

	printk("BR/EDR discovery complete: count %u\n", (unsigned int)count);
	for (size_t i = 0; i < count; i++) {
		bt_addr_to_str(&results[i].addr, address, sizeof(address));
		printk("BR/EDR device: %s RSSI %d\n", address, results[i].rssi);
	}

	if (count > 0) {
		int err = bt_br_remote_name_request(&results[0].addr,
					    br_remote_name_complete);

		printk("BR/EDR remote name request: %d\n", err);
	}
}

static struct bt_br_discovery_cb br_discovery_cb = {
	.timeout = br_discovery_complete,
};

static void br_discovery_start(void)
{
	struct bt_br_discovery_param param = {
		.length = 8,
		.limited = false,
	};
	int err;

	bt_br_discovery_cb_register(&br_discovery_cb);
	err = bt_br_discovery_start(&param, br_results, ARRAY_SIZE(br_results));
	printk("BR/EDR discovery start: %d\n", err);
}
#endif

BT_IAS_CB_DEFINE(ias_callbacks) = {
	.no_alert = alert_stop,
	.mild_alert = alert_start,
	.high_alert = alert_high_start,
};

static void bt_ready(void)
{
	int err;

	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	ARG_UNUSED(conn);
	printk("Pairing passkey: %06u\n", passkey);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	printk("Pairing passkey confirm: %06u result %d\n", passkey,
	       bt_conn_auth_passkey_confirm(conn));
}

static struct bt_conn_auth_cb peripheral_auth = {
	.passkey_display = auth_passkey_display,
	.passkey_confirm = auth_passkey_confirm,
	.cancel = auth_cancel,
};

static void bas_notify(void)
{
	int err;
	uint8_t battery_level = bt_bas_get_battery_level();

	if (!active_conn) {
		return;
	}

	battery_level--;

	if (!battery_level) {
		battery_level = 100U;
	}

	err = bt_bas_set_battery_level_conn(active_conn, battery_level);
	if (err && err != -EINVAL) {
		printk("Battery notification failed (err %d)\n", err);
	}
}

static void gatt_client_pairing_confirm(struct bt_conn *conn)
{
	printk("GATT client pairing confirm: %d\n",
	       bt_conn_auth_pairing_confirm(conn));
}

static struct bt_conn_auth_cb gatt_client_auth = {
	.pairing_confirm = gatt_client_pairing_confirm,
	.cancel = auth_cancel,
};

static uint8_t bt_heartrate = BT_HR_HEARTRATE_DEFAULT_MIN;

static void hrs_notify(void)
{
	int err;

	if (!active_conn) {
		return;
	}

	/* Heartrate measurements simulation */
	bt_heartrate++;
	if (bt_heartrate == BT_HR_HEARTRATE_DEFAULT_MAX) {
		bt_heartrate = BT_HR_HEARTRATE_DEFAULT_MIN;
	}

	err = bt_hrs_notify_conn(active_conn, bt_heartrate);
	if (err && err != -EINVAL) {
		printk("Heart rate notification failed (err %d)\n", err);
	}
}

/**
 * variable to hold reference milliseconds to epoch when device booted
 * this is only for demo purpose, for more precise synchronization please
 * review clock_settime API implementation.
 */
static int64_t unix_ms_ref;
static bool cts_notification_enabled;

void bt_cts_notification_changed(bool enabled)
{
	cts_notification_enabled = enabled;
}

int bt_cts_cts_time_write(struct bt_cts_time_format *cts_time)
{
	int err;
	int64_t unix_ms;

	if (IS_ENABLED(CONFIG_BT_CTS_HELPER_API)) {
		err = bt_cts_time_to_unix_ms(cts_time, &unix_ms);
		if (err) {
			return err;
		}
	} else {
		return -ENOTSUP;
	}

	/* recalculate reference value */
	unix_ms_ref = unix_ms - k_uptime_get();
	return 0;
}

int bt_cts_fill_current_cts_time(struct bt_cts_time_format *cts_time)
{
	int64_t unix_ms = unix_ms_ref + k_uptime_get();

	if (IS_ENABLED(CONFIG_BT_CTS_HELPER_API)) {
		return bt_cts_time_from_unix_ms(cts_time, unix_ms);
	} else {
		return -ENOTSUP;
	}
}

const struct bt_cts_cb cts_cb = {
	.notification_changed = bt_cts_notification_changed,
	.cts_time_write = bt_cts_cts_time_write,
	.fill_current_cts_time = bt_cts_fill_current_cts_time,
};

static int bt_hrs_ctrl_point_write(uint8_t request)
{
	printk("HRS Control point request: %d\n", request);
	if (request != BT_HRS_CONTROL_POINT_RESET_ENERGY_EXPANDED_REQ) {
		return -ENOTSUP;
	}

	bt_heartrate = BT_HR_HEARTRATE_DEFAULT_MIN;
	return 0;
}

static struct bt_hrs_cb hrs_cb = {
	.ctrl_point_write = bt_hrs_ctrl_point_write,
};

extern void z_sys_init(void);

int main(int argc, char *argv[])
{
	struct bt_gatt_attr *vnd_ind_attr;
	char str[BT_UUID_STR_LEN];
	int err;

	z_sys_init();
	k_work_init_delayable(&advertising_restart_work, advertising_restart);
	bt_hci_hardware_error_cb_register(controller_hardware_error);
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	bt_conn_cb_register(&peripheral);
	bt_conn_auth_info_cb_register(&auth_info);
	if (argc > 1 && strcmp(argv[1], "gatt-client") == 0) {
		gatt_client_mode = true;
		printk("GATT client auth register: %d\n",
		       bt_conn_auth_cb_register(&gatt_client_auth));
		if (IS_ENABLED(CONFIG_SETTINGS)) {
			settings_load();
		}
		gatt_client_start();
		while (1) {
			k_sleep(K_SECONDS(1));
		}
	}
	printk("Peripheral auth register: %d\n",
	       bt_conn_auth_cb_register(&peripheral_auth));
	bt_ready();
#if defined(CONFIG_BT_CLASSIC)
#if defined(CONFIG_BT_RFCOMM)
	rfcomm_server_start();
#else
	br_discovery_start();
#endif
#if defined(CONFIG_BT_HID_DEVICE)
	printk("HID device register: %d\n", bt_shell_hid_device_register());
#endif
#if defined(CONFIG_BT_A2DP)
	if (argc > 1 && strcmp(argv[1], "a2dp-source") == 0) {
		a2dp_source_mode = true;
		printk("A2DP source test enable: %d\n",
		       bt_shell_a2dp_source_test_enable());
	}
	printk("A2DP register: %d\n", bt_shell_a2dp_register());
#endif
#if defined(CONFIG_BT_AVRCP_CONTROLLER)
	printk("AVRCP controller register: %d\n",
	       bt_avrcp_ct_register_cb(&avrcp_ct_callbacks));
#endif
#if defined(CONFIG_BT_AVRCP_TARGET)
	printk("AVRCP target register: %d\n",
	       bt_avrcp_tg_register_cb(&avrcp_tg_callbacks));
#endif
#endif
	bt_cts_init(&cts_cb);
	bt_hrs_cb_register(&hrs_cb);

	bt_gatt_cb_register(&gatt_callbacks);

	vnd_ind_attr = bt_gatt_find_by_uuid(vnd_svc.attrs, vnd_svc.attr_count,
					    &vnd_enc_uuid.uuid);
	bt_uuid_to_str(&vnd_enc_uuid.uuid, str, sizeof(str));
	printk("Indicate VND attr %p (UUID %s)\n", vnd_ind_attr, str);

	/* Implement notification. At the moment there is no suitable way
	 * of starting delayed work so we do it here
	 */
	while (1) {
		k_sleep(K_SECONDS(1));

		/* Current time update notification example
		 * For testing purposes, we send a manual update notification every second.
		 * In production `bt_cts_send_notification` should only be used when time is changed
		 */
		if (cts_notification_enabled) {
			bt_cts_send_notification(BT_CTS_UPDATE_REASON_MANUAL);
		}
		/* Heartrate measurements simulation */
		hrs_notify();

		/* Battery level simulation */
		bas_notify();

		/* Vendor indication simulation */
		if (simulate_vnd && vnd_ind_attr) {
			if (indicating) {
				continue;
			}

			ind_params.attr = vnd_ind_attr;
			ind_params.func = indicate_cb;
			ind_params.destroy = indicate_destroy;
			ind_params.data = &indicating;
			ind_params.len = sizeof(indicating);

			if (bt_gatt_indicate(NULL, &ind_params) == 0) {
				indicating = 1U;
			}
		}
	}
	return 0;
}
