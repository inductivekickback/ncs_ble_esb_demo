/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Nordic UART Bridge Service (NUS) sample
 */

#include <stdio.h>
#include <zephyr/types.h>
#include <zephyr.h>
#include <drivers/uart.h>

#include <device.h>
#include <soc.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/hci.h>
#include <bluetooth/services/nus.h>

#include <settings/settings.h>

#include <logging/log.h>

#include <timeslot.h>
#include <proprietary_rf.h>

#define LOG_MODULE_NAME peripheral_uart
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define DESIRED_CONN_INTERVAL 28

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static struct bt_conn *current_conn;
static struct bt_conn *auth_conn;

static bool timeslot_running;

static struct timeslot_config timeslot_config = TS_DEFAULT_CONFIG;

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

void error(void)
{
    LOG_ERR("Error handler");
    while (true) {
        /* Spin for ever */
        k_sleep(K_MSEC(1000));
    }
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Connected %s", log_strdup(addr));

    current_conn = bt_conn_ref(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Disconnected: %s (reason %u)", log_strdup(addr), reason);

    if (auth_conn) {
        bt_conn_unref(auth_conn);
        auth_conn = NULL;
    }

    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    int err = timeslot_stop();
    if (err) {
        LOG_ERR("timeslot_stop failed (err=%d)", err);
        error();
    }
}

static void conn_param_updated(struct bt_conn *conn, uint16_t interval,
                 uint16_t latency, uint16_t timeout)
{
    /* NOTE: This may be called multiple times at the beginning of the connection. */
    LOG_INF("Connection params updated: (interval=%d, SL=%d, timeout=%d)",
                interval, latency, timeout);
    int err;

    if (DESIRED_CONN_INTERVAL != interval) {
        LOG_INF("Requesting new Connection Interval");
        struct bt_le_conn_param param = {
            .interval_min = DESIRED_CONN_INTERVAL,
            .interval_max = DESIRED_CONN_INTERVAL,
            .latency = latency,
            .timeout = timeout,
        };

        err = bt_conn_le_param_update(conn, &param);
        if (err == -EALREADY) {
            /* Connection parameters are already set. */
            err = 0;
        }
        if (err) {
            LOG_ERR("bt_conn_le_param_update failed (err=%d)", err);
        }
    } else {
        if (!timeslot_running) {
            err = timeslot_start(TS_LEN_US);
            if (err) {
                LOG_ERR("timeslot_start failed (err=%d)", err);
            } else {
                timeslot_running = true;
            }
        }        
    }
}

static struct bt_conn_cb conn_callbacks = {
    .connected        = connected,
    .disconnected     = disconnected,
    .le_param_updated = conn_param_updated,
};

static void bt_receive_cb(struct bt_conn *conn, const uint8_t *const data,
              uint16_t len)
{
    char addr[BT_ADDR_LE_STR_LEN] = {0};

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, ARRAY_SIZE(addr));

    LOG_INF("Received data from: %s", log_strdup(addr));
}

static void bt_nus_enabled_cb(enum bt_nus_send_status status)
{
    switch (status) {
    case BT_NUS_SEND_STATUS_ENABLED:
        LOG_INF("NUS TX CCCD enabled");
        break;
    case BT_NUS_SEND_STATUS_DISABLED:
        LOG_INF("NUX TX CCCD disabled");
        break;
    default:
        break;
    }
}

static struct bt_nus_cb nus_cb = {
    .received     = bt_receive_cb,
    .send_enabled = bt_nus_enabled_cb,
};

static void timeslot_err_cb(int err)
{
    LOG_ERR("Timeslot session error: %d", err);
}

static void timeslot_stopped_cb(void)
{
    LOG_INF("Timeslot stopped");
    timeslot_running  = false;
}

#if !TIMESLOT_CALLS_RADIO_IRQHANDLER
static void radio_irq_cb(void)
{
    LOG_DBG("Radio_IRQHandler");
}
#endif

static struct timeslot_cb timeslot_callbacks = {
    .error     = timeslot_err_cb,
    .start     = proprietary_rf_start,
    .end       = proprietary_rf_end,
    .skipped   = proprietary_rf_skipped,
    .stopped   = timeslot_stopped_cb,
#if !TIMESLOT_CALLS_RADIO_IRQHANDLER
    .radio_irq = radio_irq_cb
#endif
};

void main(void)
{
    int err = 0;

    bt_conn_cb_register(&conn_callbacks);

    err = timeslot_open(&timeslot_config, &timeslot_callbacks);
    if (err) {
        LOG_ERR("timeslot_open failed (err: %d)", err);
        error();
    }

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("bt_enable failed (err: %d)", err);
        error();
    }

    LOG_INF("Bluetooth initialized");

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    err = bt_nus_init(&nus_cb);
    if (err) {
        LOG_ERR("Failed to initialize UART service (err: %d)", err);
        error();
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        error();
    }

    for (;;) {
        k_sleep(K_MSEC(500));
    }
}
