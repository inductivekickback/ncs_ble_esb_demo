#include <zephyr.h>
#include <drivers/gpio.h>

#include <proprietary_rf.h>

#include <logging/log.h>

#define LOG_MODULE_NAME proprietary_rf
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define TX_PIPE 0

static const struct device *led_port;
static struct esb_payload   rx_payload;
static bool                 ready      = true;
static struct esb_payload   tx_payload = ESB_CREATE_PAYLOAD(TX_PIPE, 0x01, 0x00, 0x03, 0x04,
                                                                     0x05, 0x06, 0x07, 0x08);
static uint8_t tx_pipe_pid;

static void esb_cb(struct esb_evt const *event)
{
    ready = true;

    switch (event->evt_id) {
    case ESB_EVENT_TX_SUCCESS:
        LOG_INF("ESB TX SUCCESS EVENT");
        break;
    case ESB_EVENT_TX_FAILED:
        LOG_INF("ESB TX FAILED EVENT");
        break;
    case ESB_EVENT_RX_RECEIVED:
        while (esb_read_rx_payload(&rx_payload) == 0) {
            LOG_INF("Packet received, len %d : "
                "0x%02x, 0x%02x, 0x%02x, 0x%02x, "
                "0x%02x, 0x%02x, 0x%02x, 0x%02x",
                rx_payload.length, rx_payload.data[0],
                rx_payload.data[1], rx_payload.data[2],
                rx_payload.data[3], rx_payload.data[4],
                rx_payload.data[5], rx_payload.data[6],
                rx_payload.data[7]);
        }
        break;
    }
}

static int esb_initialize(void)
{
    int err;
    /* These are arbitrary default addresses. In end user products
     * different addresses should be used for each set of devices.
     */
    uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
    uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
    uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8};

    struct esb_config config = ESB_DEFAULT_CONFIG;

    config.protocol           = ESB_PROTOCOL_ESB_DPL;
    config.retransmit_delay   = 600;
    config.bitrate            = ESB_BITRATE_2MBPS;
    config.event_handler      = esb_cb;
    config.mode               = ESB_MODE_PTX;
    config.selective_auto_ack = true;

    err = esb_init(&config);

    if (err) {
        return err;
    }

    err = esb_set_base_address_0(base_addr_0);
    if (err) {
        return err;
    }

    err = esb_set_base_address_1(base_addr_1);
    if (err) {
        return err;
    }

    err = esb_set_prefixes(addr_prefix, ARRAY_SIZE(addr_prefix));
    if (err) {
        return err;
    }

    return 0;
}

static int leds_init(void)
{
    led_port = device_get_binding(DT_GPIO_LABEL(DT_ALIAS(led0), gpios));
    if (!led_port) {
        LOG_ERR("Could not bind to LED port %s",
            DT_GPIO_LABEL(DT_ALIAS(led0), gpios));
        return -EIO;
    }

    const uint8_t pins[] = {DT_GPIO_PIN(DT_ALIAS(led0), gpios),
                 DT_GPIO_PIN(DT_ALIAS(led1), gpios),
                 DT_GPIO_PIN(DT_ALIAS(led2), gpios),
                 DT_GPIO_PIN(DT_ALIAS(led3), gpios)};

    for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
        int err = gpio_pin_configure(led_port, pins[i], GPIO_OUTPUT);

        if (err) {
            LOG_ERR("Unable to configure LED%u, err %d", i, err);
            led_port = NULL;
            return err;
        }
    }

    return 0;
}

static void leds_update(uint8_t value)
{
    bool led0_status = !(value % 8 > 0 && value % 8 <= 4);
    bool led1_status = !(value % 8 > 1 && value % 8 <= 5);
    bool led2_status = !(value % 8 > 2 && value % 8 <= 6);
    bool led3_status = !(value % 8 > 3);

    gpio_port_pins_t mask =
        1 << DT_GPIO_PIN(DT_ALIAS(led0), gpios) |
        1 << DT_GPIO_PIN(DT_ALIAS(led1), gpios) |
        1 << DT_GPIO_PIN(DT_ALIAS(led2), gpios) |
        1 << DT_GPIO_PIN(DT_ALIAS(led3), gpios);

    gpio_port_value_t val =
        led0_status << DT_GPIO_PIN(DT_ALIAS(led0), gpios) |
        led1_status << DT_GPIO_PIN(DT_ALIAS(led1), gpios) |
        led2_status << DT_GPIO_PIN(DT_ALIAS(led2), gpios) |
        led3_status << DT_GPIO_PIN(DT_ALIAS(led3), gpios);

    if (led_port != NULL) {
        (void)gpio_port_set_masked_raw(led_port, mask, val);
    }
}

void proprietary_rf_end(void)
{
    int err = esb_get_pid(TX_PIPE, &tx_pipe_pid);
    if (err) {
        LOG_ERR("esb_get_pid failed (err=%d)", err);
    }    
    esb_disable();
}

void proprietary_rf_skipped(uint8_t count)
{
    LOG_INF("proprietary_rf_skipped(count=%d)", count);
}

void proprietary_rf_start(void)
{
    int err;

    leds_init();

    err = esb_initialize();
    if (err) {
        LOG_ERR("ESB initialization failed, err %d", err);
        return;
    }

    err = esb_set_pid(TX_PIPE, tx_pipe_pid);
    if (err) {
        LOG_ERR("esb_set_pid failed (err=%d)", err);
    }

    tx_payload.noack = false;
    if (ready) {
        ready = false;
        esb_flush_tx();
        leds_update(tx_payload.data[1]);

        err = esb_write_payload(&tx_payload);
        if (err) {
            LOG_ERR("Payload write failed, err %d", err);
        }
        tx_payload.data[1]++;
    }
}
