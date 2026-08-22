/*
 * Copyright 2025 Xiaomi Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "common/assert.h"

#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/classic/hid_device.h>
#include <zephyr/bluetooth/l2cap.h>

#include "host/hci_core.h"
#include "host/conn_internal.h"
#include "host/l2cap_internal.h"

#include "hid_internal.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_hid_device);

#define HID_PAR_REPORT_TYPE_MASK 0x03

/* Get the HID device from CTRL L2CAP channel */
#define HID_DEVICE_BY_CTRL_CHAN(_ch)                                                               \
	CONTAINER_OF(_ch, struct bt_hid_device, ctrl_session.br_chan.chan)

/* Get the HID device from INTR L2CAP channel */
#define HID_DEVICE_BY_INTR_CHAN(_ch)                                                               \
	CONTAINER_OF(_ch, struct bt_hid_device, intr_session.br_chan.chan)

/* Get the HID session from L2CAP channel */
#define HID_SESSION_BY_CHAN(_ch) CONTAINER_OF(_ch, struct bt_hid_session, br_chan.chan)

/* HID device callback */
static struct bt_hid_device_cb *hid_cb;

/* HID device connections */
static struct bt_hid_device connections[CONFIG_BT_MAX_CONN];

static void bt_hid_deinit(struct bt_hid_device *hid);

static int hid_send(struct bt_hid_session *session, uint8_t transaction,
		    uint8_t param, const uint8_t *data, uint16_t len)
{
	struct net_buf *buf;
	int err;

	if (session == NULL || session->br_chan.chan.conn == NULL) {
		return -ENOTCONN;
	}

	buf = bt_l2cap_create_pdu(NULL, 0);
	if (buf == NULL) {
		return -ENOMEM;
	}

	net_buf_add_u8(buf, (transaction << 4) | (param & 0x0f));
	if (len > 0) {
		net_buf_add_mem(buf, data, len);
	}

	err = bt_l2cap_br_chan_send(&session->br_chan.chan, buf);
	if (err < 0) {
		net_buf_unref(buf);
	}
	return err;
}

static int bt_hid_l2cap_ctrl_recv(struct bt_l2cap_chan *chan,
				  struct net_buf *buf)
{
	struct bt_hid_device *hid = HID_DEVICE_BY_CTRL_CHAN(chan);
	uint8_t header;
	uint8_t type;
	uint8_t param;

	if (buf->len < 1) {
		return 0;
	}

	header = net_buf_pull_u8(buf);
	type = header >> 4;
	param = header & 0x0f;
	switch (type) {
	case BT_HID_TYPE_CONTROL:
		if (param == BT_HID_CONTROL_VIRTUAL_CABLE_UNPLUG) {
			if (hid_cb && hid_cb->vc_unplug) {
				hid_cb->vc_unplug(hid);
			}
			return bt_hid_device_disconnect(hid);
		}
		return 0;
	case BT_HID_TYPE_SET_PROTOCOL:
		if (hid_cb && hid_cb->set_protocol) {
			hid_cb->set_protocol(hid, param & BT_HID_PROTOCOL_MASK);
		}
		return hid_send(&hid->ctrl_session, BT_HID_TYPE_HANDSHAKE,
				BT_HID_HANDSHAKE_RSP_SUCCESS, NULL, 0);
	case BT_HID_TYPE_GET_PROTOCOL:
		if (hid_cb && hid_cb->get_protocol) {
			hid_cb->get_protocol(hid);
		}
		return hid_send(&hid->ctrl_session, BT_HID_TYPE_DATA,
				BT_HID_REPORT_TYPE_OTHER, NULL, 0);
	case BT_HID_TYPE_SET_REPORT:
		if (hid_cb && hid_cb->set_report) {
			hid_cb->set_report(hid, buf->data, buf->len);
		}
		return hid_send(&hid->ctrl_session, BT_HID_TYPE_HANDSHAKE,
				BT_HID_HANDSHAKE_RSP_SUCCESS, NULL, 0);
	case BT_HID_TYPE_GET_REPORT:
		if (hid_cb && hid_cb->get_report) {
			hid_cb->get_report(hid, buf->data, buf->len);
			return 0;
		}
		return hid_send(&hid->ctrl_session, BT_HID_TYPE_HANDSHAKE,
				BT_HID_HANDSHAKE_RSP_ERR_UNSUPPORTED_REQ, NULL, 0);
	case BT_HID_TYPE_SET_IDLE:
		return hid_send(&hid->ctrl_session, BT_HID_TYPE_HANDSHAKE,
				BT_HID_HANDSHAKE_RSP_SUCCESS, NULL, 0);
	default:
		return hid_send(&hid->ctrl_session, BT_HID_TYPE_HANDSHAKE,
				BT_HID_HANDSHAKE_RSP_ERR_UNSUPPORTED_REQ, NULL, 0);
	}
}

static int bt_hid_l2cap_intr_recv(struct bt_l2cap_chan *chan,
				  struct net_buf *buf)
{
	struct bt_hid_device *hid = HID_DEVICE_BY_INTR_CHAN(chan);
	uint8_t header;

	if (buf->len < 1) {
		return 0;
	}

	header = net_buf_pull_u8(buf);
	if ((header >> 4) == BT_HID_TYPE_DATA && hid_cb && hid_cb->intr_data) {
		hid_cb->intr_data(hid, buf->data, buf->len);
	}
	return 0;
}

static struct bt_hid_device *hid_get_connection(struct bt_conn *conn)
{
	struct bt_hid_device *hid = &connections[bt_conn_index(conn)];

	if (hid->conn == NULL) {
		/* Clean the memory area before returning */
		(void)memset(hid, 0, sizeof(*hid));
	}

	return hid;
}

static enum bt_hid_session_role get_session_role_from_chan(struct bt_l2cap_chan *chan)
{
	struct bt_hid_session *session;

	session = HID_SESSION_BY_CHAN(chan);
	if (session == NULL) {
		LOG_ERR("HID session not found for channel %p", chan);
		return BT_HID_SESSION_ROLE_UNKNOWN;
	}

	return session->role;
}

static void bt_hid_l2cap_ctrl_connected(struct bt_l2cap_chan *chan)
{
	struct bt_hid_device *hid;
	enum bt_hid_session_role role;
	int err;

	if (chan == NULL) {
		LOG_ERR("Invalid hid chan");
		return;
	}

	hid = HID_DEVICE_BY_CTRL_CHAN(chan);
	if (hid == NULL) {
		LOG_ERR("HID not found");
		return;
	}

	role = get_session_role_from_chan(chan);

	LOG_DBG("HID session:%d connected, state %d", role, hid->state);

	if (role != BT_HID_SESSION_ROLE_CTRL) {
		LOG_ERR("HID invalid role:%d", role);
		return;
	}

	hid->state = BT_HID_STATE_CTRL_CONNECTED;
	if (hid->role == BT_HID_ROLE_INITIATOR) {
		hid->state = BT_HID_STATE_INTR_CONNECTING;
		err = bt_l2cap_chan_connect(hid->conn,
					&hid->intr_session.br_chan.chan,
					BT_L2CAP_PSM_HID_INT);
		if (err < 0) {
			LOG_ERR("HID interrupt connection failed: %d", err);
			(void)bt_l2cap_chan_disconnect(chan);
		}
	}

}

static void bt_hid_l2cap_intr_connected(struct bt_l2cap_chan *chan)
{
	struct bt_hid_device *hid;
	enum bt_hid_session_role role;

	if (chan == NULL) {
		LOG_ERR("Invalid hid chan");
		return;
	}

	hid = HID_DEVICE_BY_INTR_CHAN(chan);
	if (hid == NULL) {
		LOG_ERR("HID not found");
		return;
	}

	role = get_session_role_from_chan(chan);

	LOG_DBG("HID session:%d connected, state %d", role, hid->state);

	if (role != BT_HID_SESSION_ROLE_INTR) {
		LOG_ERR("HID invalid role:%d", role);
		return;
	}

	hid->state = BT_HID_STATE_CONNECTED;

	if (hid_cb && hid_cb->connected) {
		hid_cb->connected(hid);
	}
}

static void bt_hid_l2cap_ctrl_disconnected(struct bt_l2cap_chan *chan)
{
	struct bt_hid_device *hid;
	enum bt_hid_session_role role;
	int err;

	if (chan == NULL) {
		LOG_ERR("Invalid hid chan");
		return;
	}

	hid = HID_DEVICE_BY_CTRL_CHAN(chan);
	if (hid == NULL) {
		LOG_ERR("HID not found");
		return;
	}

	role = get_session_role_from_chan(chan);

	LOG_DBG("HID session:%d connected, state %d", role, hid->state);

	if (role != BT_HID_SESSION_ROLE_CTRL) {
		LOG_ERR("HID invalid role:%d", role);
		return;
	}

	err = bt_l2cap_chan_disconnect(&hid->intr_session.br_chan.chan);
	if (err < 0) {
		if (hid->state == BT_HID_STATE_CONNECTED && hid_cb &&
		    hid_cb->disconnected) {
			hid_cb->disconnected(hid);
		}
		bt_hid_deinit(hid);
	}
}

static void bt_hid_l2cap_intr_disconnected(struct bt_l2cap_chan *chan)
{
	struct bt_hid_device *hid;
	enum bt_hid_session_role role;

	if (chan == NULL) {
		LOG_ERR("Invalid hid chan");
		return;
	}

	hid = HID_DEVICE_BY_INTR_CHAN(chan);
	if (hid == NULL) {
		LOG_ERR("HID not found");
		return;
	}

	role = get_session_role_from_chan(chan);

	LOG_DBG("HID session:%d connected, state %d", role, hid->state);

	if (role != BT_HID_SESSION_ROLE_INTR) {
		LOG_ERR("HID invalid role:%d", role);
		return;
	}

	if (hid_cb && hid_cb->disconnected) {
		hid_cb->disconnected(hid);
	}

	bt_hid_deinit(hid);
}

static void bt_hid_init(struct bt_hid_device *hid, struct bt_conn *conn, enum bt_hid_role role)
{
	static const struct bt_l2cap_chan_ops ctrl_ops = {
		.connected = bt_hid_l2cap_ctrl_connected,
		.disconnected = bt_hid_l2cap_ctrl_disconnected,
		.recv = bt_hid_l2cap_ctrl_recv,
	};

	static const struct bt_l2cap_chan_ops intr_ops = {
		.connected = bt_hid_l2cap_intr_connected,
		.disconnected = bt_hid_l2cap_intr_disconnected,
		.recv = bt_hid_l2cap_intr_recv,
	};

	hid->ctrl_session.br_chan.chan.ops = (struct bt_l2cap_chan_ops *)&ctrl_ops;
	hid->ctrl_session.br_chan.rx.mtu = BT_HID_MAX_MTU;
	hid->ctrl_session.role = BT_HID_SESSION_ROLE_CTRL;

	hid->intr_session.br_chan.chan.ops = (struct bt_l2cap_chan_ops *)&intr_ops;
	hid->intr_session.br_chan.rx.mtu = BT_HID_MAX_MTU;
	hid->intr_session.role = BT_HID_SESSION_ROLE_INTR;

	hid->role = role;
	hid->state = BT_HID_STATE_CTRL_CONNECTING;
	hid->conn = conn;
	hid->buf = NULL;
	hid->pending_vc_unplug = 0;
}

static void bt_hid_deinit(struct bt_hid_device *hid)
{

	if (hid->buf != NULL) {
		net_buf_unref(hid->buf);
		hid->buf = NULL;
	}

	hid->conn = NULL;
	hid->pending_vc_unplug = 0;
	hid->state = BT_HID_STATE_DISTCONNECTED;
}

static int hid_l2cap_ctrl_accept(struct bt_conn *conn, struct bt_l2cap_server *server,
				 struct bt_l2cap_chan **chan)
{
	struct bt_hid_device *hid;

	hid = hid_get_connection(conn);
	if (hid == NULL) {
		LOG_ERR("Cannot allocate memory for HID device");
		return -ENOMEM;
	}

	bt_hid_init(hid, conn, BT_HID_ROLE_ACCEPTOR);
	*chan = &hid->ctrl_session.br_chan.chan;

	return 0;
}

static int hid_l2cap_intr_accept(struct bt_conn *conn, struct bt_l2cap_server *server,
				 struct bt_l2cap_chan **chan)
{
	struct bt_hid_device *hid;

	hid = hid_get_connection(conn);
	if (hid == NULL || hid->conn != conn ||
	    hid->state != BT_HID_STATE_CTRL_CONNECTED) {
		LOG_ERR("Cannot get HID device");
		return -ENOTCONN;
	}

	hid->state = BT_HID_STATE_INTR_CONNECTING;
	*chan = &hid->intr_session.br_chan.chan;

	return 0;
}

int bt_hid_device_send_ctrl_data(struct bt_hid_device *hid, uint8_t type, uint8_t *data,
				 uint16_t len)
{
	if (hid == NULL || hid->state < BT_HID_STATE_CTRL_CONNECTED ||
	    hid->state >= BT_HID_STATE_DISCONNECTING) {
		return -ENOTCONN;
	}
	return hid_send(&hid->ctrl_session, BT_HID_TYPE_DATA, type, data, len);
}

int bt_hid_device_send_intr_data(struct bt_hid_device *hid, uint8_t type, uint8_t *data,
				 uint16_t len)
{
	if (hid == NULL || hid->state != BT_HID_STATE_CONNECTED) {
		return -ENOTCONN;
	}
	return hid_send(&hid->intr_session, BT_HID_TYPE_DATA, type, data, len);
}

int bt_hid_device_report_error(struct bt_hid_device *hid, uint8_t error)
{
	if (hid == NULL) {
		return -EINVAL;
	}
	return hid_send(&hid->ctrl_session, BT_HID_TYPE_HANDSHAKE, error, NULL, 0);
}

struct bt_hid_device *bt_hid_device_connect(struct bt_conn *conn)
{
	struct bt_hid_device *hid;
	int err;

	if (conn == NULL) {
		return NULL;
	}

	hid = hid_get_connection(conn);
	if (hid == NULL || hid->conn != NULL) {
		return NULL;
	}

	bt_hid_init(hid, conn, BT_HID_ROLE_INITIATOR);
	err = bt_l2cap_chan_connect(conn, &hid->ctrl_session.br_chan.chan,
				    BT_L2CAP_PSM_HID_CTL);
	if (err < 0) {
		bt_hid_deinit(hid);
		return NULL;
	}

	return hid;
}

int bt_hid_device_disconnect(struct bt_hid_device *hid)
{
	int err;

	if (hid == NULL || hid->conn == NULL) {
		return -EINVAL;
	}

	hid->state = BT_HID_STATE_DISCONNECTING;
	err = bt_l2cap_chan_disconnect(&hid->intr_session.br_chan.chan);
	if (err < 0 && err != -ENOTCONN) {
		return err;
	}
	return bt_l2cap_chan_disconnect(&hid->ctrl_session.br_chan.chan);
}

int bt_hid_device_register(struct bt_hid_device_cb *cb)
{
	LOG_DBG("");

	hid_cb = cb;
	return 0;
}

int bt_hid_dev_init(void)
{
	int err;
	static struct bt_l2cap_server hiddev_ctrl_l2cap = {
		.psm = BT_L2CAP_PSM_HID_CTL,
		.sec_level = BT_SECURITY_L2,
		.accept = hid_l2cap_ctrl_accept,
	};
	static struct bt_l2cap_server hiddev_intr_l2cap = {
		.psm = BT_L2CAP_PSM_HID_INT,
		.sec_level = BT_SECURITY_L2,
		.accept = hid_l2cap_intr_accept,
	};

	LOG_DBG("");

	/* Register HID CTRL PSM with L2CAP */
	err = bt_l2cap_br_server_register(&hiddev_ctrl_l2cap);
	if (err < 0) {
		LOG_ERR("HID ctrl L2CAP registration failed, err:%d", err);
		return err;
	}

	/* Register HID INTR PSM with L2CAP */
	err = bt_l2cap_br_server_register(&hiddev_intr_l2cap);
	if (err < 0) {
		LOG_ERR("HID intr L2CAP registration failed, err:%d", err);
		return err;
	}

	return err;
}
