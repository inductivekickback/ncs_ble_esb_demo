/*
 * Copyright (c) 2021 Daniel Veilleux
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <stdio.h>

#include <logging/log.h>

#define LOG_MODULE_NAME timeslot
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <mpsl.h>
#include <mpsl_radio_notification.h>
#include <mpsl_timeslot.h>

#define TS_GPIO_DEBUG 1

#if TS_GPIO_DEBUG
#include <hal/nrf_gpio.h>
#define TIMESLOT_OPEN_PIN          4
#define TIMESLOT_BLOCKED_PIN       28
#define TIMESLOT_CANCELLED_PIN     30
#define RADIO_NOTIFICATION_PIN     2
#define REQUEST_PIN                31
#endif

#include <timeslot.h>

/* The radio notification distance in microseconds */
#define TS_RNH_DISTANCE_US         800

/* The (empirical) distance between a request and the resulting timeslot start */
#define TS_REQUEST_DELAY_US        2600

#define TIMESLOT_THREAD_STACK_SIZE 768
#define TIMESLOT_THREAD_PRIORITY   5

#define INVALID_MPSL_SIGNAL        11

enum SIGNAL_CODE
{
    SIGNAL_CODE_START             = 0x00,
    SIGNAL_CODE_TIMER0            = 0x01,
    SIGNAL_CODE_RADIO             = 0x02,
    SIGNAL_CODE_BLOCKED_CANCELLED = 0x03,
    SIGNAL_CODE_OVERSTAYED        = 0x04,
    SIGNAL_CODE_IDLE              = 0x05,
    SIGNAL_CODE_RNH_ACTIVE        = 0x06,
    SIGNAL_CODE_UNEXPECTED        = 0x07,
    SIGNAL_CODE_MPSL_START        = 0x08
};

static uint32_t                ts_len_us;
static uint8_t                 blocked_cancelled_count;
static bool                    session_open;
static bool                    timeslot_started;
static bool                    timeslot_stopping;
static bool                    timeslot_requested;
static uint32_t                mpsl_callback_signal=INVALID_MPSL_SIGNAL;
static struct timeslot_config *p_timeslot_config;
static struct timeslot_cb     *p_timeslot_callbacks;

static struct k_poll_signal timeslot_sig = K_POLL_SIGNAL_INITIALIZER(timeslot_sig);
static struct k_poll_event events[1]     = {
    K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SIGNAL,
                                    K_POLL_MODE_NOTIFY_ONLY,
                                    &timeslot_sig, 0),
};

static mpsl_timeslot_session_id_t mpsl_session_id;

/* NOTE: MPSL return params must be in static scope. */
static mpsl_timeslot_request_t request_earliest = {
    .request_type = MPSL_TIMESLOT_REQ_TYPE_EARLIEST,
    .params.earliest = {
        .priority   = MPSL_TIMESLOT_PRIORITY_NORMAL,
    }
};

static mpsl_timeslot_signal_return_param_t action_none = {
    .callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_NONE
};

static mpsl_timeslot_signal_return_param_t action_end = {
    .callback_action = MPSL_TIMESLOT_SIGNAL_ACTION_END
};

#if TIMESLOT_CALLS_RADIO_IRQHANDLER
void RADIO_IRQHandler(void);
#endif

static mpsl_timeslot_signal_return_param_t*
mpsl_cb(mpsl_timeslot_session_id_t session_id, uint32_t signal)
{
    switch (signal) {
    case MPSL_TIMESLOT_SIGNAL_START:
#if TS_GPIO_DEBUG
        nrf_gpio_pin_write(TIMESLOT_OPEN_PIN, 1);
#endif
        if (timeslot_stopping) {
#if TS_GPIO_DEBUG
            nrf_gpio_pin_write(TIMESLOT_OPEN_PIN, 0);
            nrf_gpio_pin_write(TIMESLOT_OPEN_PIN, 0);
            nrf_gpio_pin_write(TIMESLOT_OPEN_PIN, 0);
            nrf_gpio_pin_write(TIMESLOT_OPEN_PIN, 0);
            nrf_gpio_pin_write(TIMESLOT_OPEN_PIN, 0);
            nrf_gpio_pin_write(TIMESLOT_OPEN_PIN, 0);
            nrf_gpio_pin_write(TIMESLOT_OPEN_PIN, 1);
#endif
            return &action_end;
        }

        /* TIMER0 is pre-configured for 1MHz mode by the MPSL. */
        NRF_TIMER0->CC[0]    = (ts_len_us - p_timeslot_config->safety_margin_us);
        NRF_TIMER0->INTENSET = (TIMER_INTENSET_COMPARE0_Set<<TIMER_INTENSET_COMPARE0_Pos);
        mpsl_callback_signal = MPSL_TIMESLOT_SIGNAL_START;
        NVIC_EnableIRQ(TIMER0_IRQn);
        NVIC_SetPendingIRQ(TIMESLOT_IRQN);
        break;

    case MPSL_TIMESLOT_SIGNAL_TIMER0:
#if TS_GPIO_DEBUG
        nrf_gpio_pin_write(TIMESLOT_OPEN_PIN, 0);
#endif
        NRF_TIMER0->TASKS_STOP = 1;
        mpsl_callback_signal   = MPSL_TIMESLOT_SIGNAL_TIMER0;
        NVIC_SetPendingIRQ(TIMESLOT_IRQN);
        return &action_end;

    case MPSL_TIMESLOT_SIGNAL_RADIO:
        if (timeslot_stopping) {
            return &action_end;
        }
#if TIMESLOT_CALLS_RADIO_IRQHANDLER
        RADIO_IRQHandler();
#else
        mpsl_callback_signal = MPSL_TIMESLOT_SIGNAL_RADIO;
        NVIC_SetPendingIRQ(TIMESLOT_IRQN);
#endif
        break;

    case MPSL_TIMESLOT_SIGNAL_BLOCKED:
#if TS_GPIO_DEBUG
        nrf_gpio_pin_write(TIMESLOT_BLOCKED_PIN, 1);
#endif
        k_poll_signal_raise(&timeslot_sig, SIGNAL_CODE_BLOCKED_CANCELLED);
        break;

    case MPSL_TIMESLOT_SIGNAL_CANCELLED:
#if TS_GPIO_DEBUG
        nrf_gpio_pin_write(TIMESLOT_CANCELLED_PIN, 1);
#endif
        k_poll_signal_raise(&timeslot_sig, SIGNAL_CODE_BLOCKED_CANCELLED);
        break;

    case MPSL_TIMESLOT_SIGNAL_SESSION_IDLE:
        k_poll_signal_raise(&timeslot_sig, SIGNAL_CODE_IDLE);
        break;

    case MPSL_TIMESLOT_SIGNAL_EXTEND_FAILED:
        /* Intentional fall-through */
    case MPSL_TIMESLOT_SIGNAL_EXTEND_SUCCEEDED:
        /* Intentional fall-through */
    case MPSL_TIMESLOT_SIGNAL_INVALID_RETURN:
        /* Intentional fall-through */
    case MPSL_TIMESLOT_SIGNAL_SESSION_CLOSED:
        k_poll_signal_raise(&timeslot_sig, SIGNAL_CODE_UNEXPECTED);
        break;

    case MPSL_TIMESLOT_SIGNAL_OVERSTAYED:
        k_poll_signal_raise(&timeslot_sig, SIGNAL_CODE_OVERSTAYED);
        break;

    default:
        break;
    };

    return &action_none;
}

static void radio_notify_cb(const void *context)
{
    if (!timeslot_started)
    {
        /* Ignore RNH events until the timeslot is started. */
        return;
    }

    if (INVALID_MPSL_SIGNAL != mpsl_callback_signal) {
        /* This is an MPSL callback. */
        switch (mpsl_callback_signal) {
        case MPSL_TIMESLOT_SIGNAL_START:
            k_poll_signal_raise(&timeslot_sig, SIGNAL_CODE_START);
            break;
        case MPSL_TIMESLOT_SIGNAL_RADIO:
            k_poll_signal_raise(&timeslot_sig, SIGNAL_CODE_RADIO);
            break;
        case MPSL_TIMESLOT_SIGNAL_TIMER0:
            k_poll_signal_raise(&timeslot_sig, SIGNAL_CODE_TIMER0);
            break;
        default:
            k_poll_signal_raise(&timeslot_sig, SIGNAL_CODE_UNEXPECTED);
            break;
        };
        mpsl_callback_signal = INVALID_MPSL_SIGNAL;
    } else {
        /* This is a radio notification. */
#if TS_GPIO_DEBUG
        nrf_gpio_pin_set(RADIO_NOTIFICATION_PIN);
        nrf_gpio_pin_set(RADIO_NOTIFICATION_PIN);
        nrf_gpio_pin_set(RADIO_NOTIFICATION_PIN);
        nrf_gpio_pin_set(RADIO_NOTIFICATION_PIN);
        nrf_gpio_pin_set(RADIO_NOTIFICATION_PIN);
        nrf_gpio_pin_set(RADIO_NOTIFICATION_PIN);
        nrf_gpio_pin_clear(RADIO_NOTIFICATION_PIN);
#endif
        k_poll_signal_raise(&timeslot_sig, SIGNAL_CODE_RNH_ACTIVE);
    }
}

int timeslot_stop(void)
{
    if (!session_open || !timeslot_started) {
        return -TIMESLOT_ERROR_NO_TIMESLOT_STARTED;
    }
    timeslot_stopping = true;
    LOG_INF("timeslot_stop()");
    return 0;
}

int timeslot_start(uint32_t len_us)
{
    if (!session_open || timeslot_started || timeslot_stopping) {
        return -TIMESLOT_ERROR_TIMESLOT_ALREADY_STARTED;
    }

    LOG_INF("timeslot_start(len_us: %d)", len_us);
    ts_len_us               = len_us;
    blocked_cancelled_count = 0;
    timeslot_started        = true;

    request_earliest.params.earliest.length_us = len_us;
    return 0;
}

int timeslot_open(struct timeslot_config *p_config, struct timeslot_cb *p_cb)
{
    if (session_open) {
        return -TIMESLOT_ERROR_SESSION_ALREADY_OPENED;
    }

    if (0 == p_config) {
        return -TIMESLOT_ERROR_INVALID_PARAM;
    }

    if ((0 == p_cb) || (0 == p_cb->error) || (0 == p_cb->start) || (0 == p_cb->end)) {
        return -TIMESLOT_ERROR_INVALID_PARAM;
    }
#if !TIMESLOT_CALLS_RADIO_IRQHANDLER
    if (0 == p_cb->radio_irq) {
        return -TIMESLOT_ERROR_INVALID_PARAM;
    }
#endif

    LOG_INF("timeslot_open(...)");
    IRQ_CONNECT(DT_IRQN(DT_NODELABEL(TIMESLOT_IRQ_NODELABEL)), TIMESLOT_IRQ_PRIO,
                radio_notify_cb, NULL, 0);
    irq_enable(DT_IRQN(DT_NODELABEL(TIMESLOT_IRQ_NODELABEL)));

    p_timeslot_config    = p_config;
    p_timeslot_callbacks = p_cb;
    session_open         = true;

    request_earliest.params.earliest.hfclk      = p_timeslot_config->hfclk;
    request_earliest.params.earliest.timeout_us = p_timeslot_config->timeout_us;

#if TS_GPIO_DEBUG
    nrf_gpio_cfg_output(TIMESLOT_OPEN_PIN);
    nrf_gpio_cfg_output(TIMESLOT_BLOCKED_PIN);
    nrf_gpio_cfg_output(TIMESLOT_CANCELLED_PIN);
    nrf_gpio_cfg_output(RADIO_NOTIFICATION_PIN);
    nrf_gpio_cfg_output(REQUEST_PIN);
    nrf_gpio_pin_clear(TIMESLOT_OPEN_PIN);
    nrf_gpio_pin_clear(TIMESLOT_BLOCKED_PIN);
    nrf_gpio_pin_clear(TIMESLOT_CANCELLED_PIN);
    nrf_gpio_pin_clear(RADIO_NOTIFICATION_PIN);
    nrf_gpio_pin_clear(REQUEST_PIN);
#endif

    k_poll_signal_raise(&timeslot_sig, SIGNAL_CODE_MPSL_START);
    return 0;
}

static void timeslot_stopped(void) {
#if TS_GPIO_DEBUG
    nrf_gpio_pin_write(TIMESLOT_OPEN_PIN, 0);
#endif
    timeslot_stopping = false;
    timeslot_started  = false;
    p_timeslot_callbacks->stopped();
}

static void timeslot_thread_fn(void)
{
    int err;

    while (true) {
        k_poll(events, 1, K_FOREVER);

        switch (events[0].signal->result) {
        case SIGNAL_CODE_START:
            p_timeslot_callbacks->start();
            blocked_cancelled_count = 0;
            break;

        case SIGNAL_CODE_TIMER0:
            p_timeslot_callbacks->end();
            break;

#if !TIMESLOT_CALLS_RADIO_IRQHANDLER
        case SIGNAL_CODE_RADIO:
            p_timeslot_callbacks->radio_irq();
            break;
#endif

        case SIGNAL_CODE_BLOCKED_CANCELLED:
            timeslot_requested = false;
#if TS_GPIO_DEBUG
            nrf_gpio_pin_write(TIMESLOT_BLOCKED_PIN,   0);
            nrf_gpio_pin_write(TIMESLOT_CANCELLED_PIN, 0);
#endif
            blocked_cancelled_count++;
            if (blocked_cancelled_count > p_timeslot_config->skipped_tolerance) {
                blocked_cancelled_count = 0;
                p_timeslot_callbacks->error(-TIMESLOT_ERROR_REQUESTS_FAILED);
                break;
            }
            if (timeslot_stopping) {
                timeslot_stopped();
                break;
            }
            p_timeslot_callbacks->skipped(blocked_cancelled_count);
            break;

        case SIGNAL_CODE_IDLE:
            timeslot_requested = false;
            if (timeslot_stopping) {
                timeslot_stopped();
            }
            break;

        case SIGNAL_CODE_OVERSTAYED:
            /* This is the most probable of the what-could-go-wrong scenarios. */
            p_timeslot_callbacks->error(-TIMESLOT_ERROR_OVERSTAYED);
            break;

        case SIGNAL_CODE_UNEXPECTED:
            /* Something like MPSL_TIMESLOT_SIGNAL_INVALID_RETURN happened. */
            p_timeslot_callbacks->error(-TIMESLOT_ERROR_INTERNAL);
            break;

        case SIGNAL_CODE_RNH_ACTIVE:
            if (timeslot_requested) {
                break;
            }
#if TS_GPIO_DEBUG
            nrf_gpio_pin_write(REQUEST_PIN, 1);
#endif
            k_sleep(K_USEC(CONFIG_SDC_MAX_CONN_EVENT_LEN_DEFAULT -
                               TS_REQUEST_DELAY_US + TS_RNH_DISTANCE_US));
#if TS_GPIO_DEBUG
            nrf_gpio_pin_write(REQUEST_PIN, 0);
#endif
            timeslot_requested = true;
            err = mpsl_timeslot_request(mpsl_session_id, &request_earliest);
            if (err) {
                p_timeslot_callbacks->error(err);
            }
            break;

        case SIGNAL_CODE_MPSL_START:
            err = mpsl_radio_notification_cfg_set(MPSL_RADIO_NOTIFICATION_TYPE_INT_ON_ACTIVE,
                                                    MPSL_RADIO_NOTIFICATION_DISTANCE_800US,
                                                    TIMESLOT_IRQN);
            if (err) {
                p_timeslot_callbacks->error(err);
            }

            err = mpsl_timeslot_session_open(mpsl_cb, &mpsl_session_id);
            if (err) {
                p_timeslot_callbacks->error(err);
            }
            break;

        default:
            p_timeslot_callbacks->error(-TIMESLOT_ERROR_INTERNAL);
            break;
        }

        events[0].signal->signaled = 0;
        events[0].state            = K_POLL_STATE_NOT_READY;
    }
}

K_THREAD_DEFINE(timeslot_thread, TIMESLOT_THREAD_STACK_SIZE,
                    timeslot_thread_fn, NULL, NULL, NULL,
                    K_PRIO_COOP(TIMESLOT_THREAD_PRIORITY), 0, 0);
