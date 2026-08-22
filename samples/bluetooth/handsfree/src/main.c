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

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/classic/hfp_hf.h>
#include <zephyr/settings/settings.h>

static struct k_work_delayable audio_connect_work;
static struct k_work_delayable sco_tx_work;
static struct bt_hfp_hf *active_hf;
static uint32_t sco_rx_packets;
static uint32_t sco_rx_bytes;

static const uint8_t msbc_silence[60] = {
	0x01, 0x38, 0xad, 0x00, 0x00, 0xc5, 0x00, 0x00, 0x00, 0x00,
	0x77, 0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6d,
	0xdd, 0xb6, 0xdb, 0x77, 0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7,
	0x76, 0xdb, 0x6d, 0xdd, 0xb6, 0xdb, 0x77, 0x6d, 0xb6, 0xdd,
	0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6d, 0xdd, 0xb6, 0xdb, 0x77,
	0x6d, 0xb6, 0xdd, 0xdb, 0x6d, 0xb7, 0x76, 0xdb, 0x6c, 0x00,
};

static void audio_connect(struct k_work *work)
{
	ARG_UNUSED(work);
	if (active_hf != NULL) {
		printk("HFP HF audio connect: %d\n",
		       Z_API(bt_hfp_hf_audio_connect)(active_hf));
	}
}

static void sco_tx(struct k_work *work)
{
	ARG_UNUSED(work);
	if (active_hf == NULL) {
		return;
	}
	for (int i = 0; i < 3; i++) {
		printk("HF SCO TX %d/3: %d\n", i + 1,
		       Z_API(bt_hfp_hf_sco_send)(active_hf, msbc_silence,
						 sizeof(msbc_silence)));
	}
}

static void hf_connected(struct bt_conn *conn, struct bt_hfp_hf *hf)
{
	printk("HFP HF Connected!\n");
	active_hf = hf;
	k_work_reschedule(&audio_connect_work, K_SECONDS(1));
}

static void hf_disconnected(struct bt_hfp_hf *hf)
{
	printk("HFP HF Disconnected!\n");
	active_hf = NULL;
	k_work_cancel_delayable(&audio_connect_work);
}

static void hf_sco_connected(struct bt_hfp_hf *hf, struct bt_conn *sco_conn)
{
	printk("HF SCO connected\n");
	k_work_reschedule(&sco_tx_work, K_MSEC(100));
}

static void hf_sco_recv(struct bt_hfp_hf *hf, const uint8_t *data,
			size_t len, uint8_t packet_status)
{
	ARG_UNUSED(hf);
	ARG_UNUSED(data);
	sco_rx_packets++;
	sco_rx_bytes += len;
	if (sco_rx_packets <= 5 || sco_rx_packets % 100 == 0) {
		printk("HF SCO RX packets=%lu bytes=%lu status=%u\n",
		       (unsigned long)sco_rx_packets,
		       (unsigned long)sco_rx_bytes, packet_status);
	}
}

static void hf_sco_disconnected(struct bt_conn *sco_conn, uint8_t reason)
{
	printk("HF SCO disconnected\n");
}

static void hf_service(struct bt_hfp_hf *hf, uint32_t value)
{
	printk("Service indicator value: %u\n", value);
}

static void hf_outgoing(struct bt_hfp_hf *hf, struct bt_hfp_hf_call *call)
{
	printk("HF call %p outgoing\n", call);
}

static void hf_remote_ringing(struct bt_hfp_hf_call *call)
{
	printk("HF remote call %p start ringing\n", call);
}

static void hf_incoming(struct bt_hfp_hf *hf, struct bt_hfp_hf_call *call)
{
	printk("HF call %p incoming\n", call);
}

static void hf_incoming_held(struct bt_hfp_hf_call *call)
{
	printk("HF call %p is held\n", call);
}

static void hf_accept(struct bt_hfp_hf_call *call)
{
	printk("HF call %p accepted\n", call);
}

static void hf_reject(struct bt_hfp_hf_call *call)
{
	printk("HF call %p rejected\n", call);
}

static void hf_terminate(struct bt_hfp_hf_call *call)
{
	printk("HF call %p terminated\n", call);
}

static void hf_signal(struct bt_hfp_hf *hf, uint32_t value)
{
	printk("Signal indicator value: %u\n", value);
}

static void hf_roam(struct bt_hfp_hf *hf, uint32_t value)
{
	printk("Roaming indicator value: %u\n", value);
}

static void hf_battery(struct bt_hfp_hf *hf, uint32_t value)
{
	printk("Battery indicator value: %u\n", value);
}

static void hf_ring_indication(struct bt_hfp_hf_call *call)
{
	printk("HF call %p ring\n", call);
}

static void hf_codec_negotiate(struct bt_hfp_hf *hf, uint8_t id)
{
	printk("HFP HF codec negotiate: %u result %d\n", id,
	       Z_API(bt_hfp_hf_select_codec)(hf, id));
}

static struct bt_hfp_hf_cb hf_cb = {
	.connected = hf_connected,
	.disconnected = hf_disconnected,
	.sco_connected = hf_sco_connected,
	.sco_recv = hf_sco_recv,
	.sco_disconnected = hf_sco_disconnected,
	.service = hf_service,
	.outgoing = hf_outgoing,
	.remote_ringing = hf_remote_ringing,
	.incoming = hf_incoming,
	.incoming_held = hf_incoming_held,
	.accept = hf_accept,
	.reject = hf_reject,
	.terminate = hf_terminate,
	.signal = hf_signal,
	.roam = hf_roam,
	.battery = hf_battery,
	.ring_indication = hf_ring_indication,
	.codec_negotiate = hf_codec_negotiate,
};

static void handsfree_enable(void);

static void bt_ready(uint8_t dev_id, int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}
	handsfree_enable();

	printk("Bluetooth initialized\n");

	err = bt_br_set_connectable(true);
	if (err) {
		printk("BR/EDR set/rest connectable failed (err %d)\n", err);
		return;
	}
	err = bt_br_set_discoverable(true);
	if (err) {
		printk("BR/EDR set discoverable failed (err %d)\n", err);
		return;
	}

	printk("BR/EDR set connectable and discoverable done\n");
}

static void handsfree_enable(void)
{
	int err;

	err = Z_API(bt_hfp_hf_register)(&hf_cb);
	if (err < 0) {
		printk("HFP HF Registration failed (err %d)\n", err);
	}
}

int main(void)
{
	int err;
	extern void z_sys_init(void);

	z_sys_init();
	k_work_init_delayable(&audio_connect_work, audio_connect);
	k_work_init_delayable(&sco_tx_work, sco_tx);
	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}

	k_sleep(K_FOREVER);

	return 0;
}
