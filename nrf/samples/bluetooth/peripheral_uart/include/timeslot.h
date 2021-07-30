/*
 * Copyright (c) 2021 Daniel Veilleux
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef TIMESLOT_H__
#define TIMESLOT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <mpsl_timeslot.h>

/**
 * A hardware interrupt vector to use with the Radio Notification feature as well as lowering the
 * priority of the MPSL callback (Zero Latency IRQ workaround). As of NCS v1.6 this doesn't
 * handle MPSL_TIMESLOT_SIGNAL_TIMER0 signals correctly unless it uses priority MPSL_LOW_PRIO
 * or higher.
 */
#define TIMESLOT_IRQN          QDEC_IRQn
#define TIMESLOT_IRQ_NODELABEL qdec
#define TIMESLOT_IRQ_PRIO      4

/**
 * If TIMESLOT_CALLS_RADIO_IRQHANDLER is set then RADIO_IRQHandler() will be called
 * directly instead of executing the radio_irq callback.
 */
#define TIMESLOT_CALLS_RADIO_IRQHANDLER 1

enum TIMESLOT_ERROR
{
    /**
     * Could not get a granted timeslot using an "earliest" request. len_us is probably
     * too long for the current Connection Interval.
     */
    TIMESLOT_ERROR_REQUESTS_FAILED = 94,
    /** The MPSL complained because the timeslot did not close on time. */
    TIMESLOT_ERROR_OVERSTAYED = 93,
    /** Something unexpected happened. */
    TIMESLOT_ERROR_INTERNAL = 92,
    /** The timeslot_open function was called twice. */
    TIMESLOT_ERROR_SESSION_ALREADY_OPENED = 91,
    /**
     * The timeslot_start function was called before timeslot_stop completed (and executed
     * the stopped callback).
     */
    TIMESLOT_ERROR_TIMESLOT_ALREADY_STARTED = 90,
    /** The timeslot_stop function was called twice. */
    TIMESLOT_ERROR_NO_TIMESLOT_STARTED = 89,
    /** A required pointer was not included in an argument. */
    TIMESLOT_ERROR_INVALID_PARAM = 88
};

struct timeslot_config {
    /**
     * High frequency clock source, see MPSL_TIMESLOT_HFCLK_CFG.
     */
    uint8_t hfclk;        
    /**
     * Amount of time before a request times out.
     */
    uint32_t timeout_us;
    /**
     * Close the timeslot this amount of time before the end to ensure that it closes cleanly.
     */
    uint32_t safety_margin_us;
    /**
     * The number of skipped timeslots before an error is raised.
     */
    uint8_t skipped_tolerance;
};

#define TS_DEFAULT_CONFIG { \
    .hfclk             = MPSL_TIMESLOT_HFCLK_CFG_XTAL_GUARANTEED, \
    .timeout_us        = 2000000, /* Default is 2s. */            \
    .safety_margin_us  = 100, /* Default is 100us */              \
    .skipped_tolerance = 5 /* Default is 5 */                     \
}

struct timeslot_cb {
    /**
     * A (potentially unrecoverable) error has occurred. The err param will be set to a
     * TIMESLOT_ERROR or an error returned by mpsl_timeslot_request.
     */
    void (*error)(int err);
    /**
     * Called at the beginning of every timeslot.
     */
    void (*start)(void);
    /**
     * Called TS_SAFETY_MARGIN_US before the end of every timeslot.
     */
    void (*end)(void);
    /**
     * A timeslot has been blocked or cancelled. The count parameter is set to the number
     * of consecutive timeslots that have been skipped.
     */
    void (*skipped)(uint8_t count);
    /**
     * The recurring timeslot has been stopped (the session is idle).
     */
    void (*stopped)(void);

#if !TIMESLOT_USE_RADIO_IRQHANDLER
    /**
     * Some like e.g. the ESB library defines its own RADIO_IRQHandler function. Setting
     * TIMESLOT_USE_RADIO_IRQHANDLER causes MPSL_TIMESLOT_SIGNAL_RADIO signals to call
     * RADIO_IRQHandler directly. Otherwise, this callback will be used whenever RADIO_IRQHandler
     * is called.
     */
    void (*radio_irq)(void);
#endif
};

/** @brief Open an MPSL session
 * 
 * @note Opening a session is always the first step and closing a session is not implemented
 *       because there's no obvious reason to ever do it.
 * 
 * @param[in] p_config      Pointer to a timeslot_config (should be static/global)
 * @param[in] p_cb          Pointer to a timeslot_cb (should be static/global)
 * 
 * @retval 0                                      Success
 * @retval -TIMESLOT_ERROR_SESSION_ALREADY_OPENED The session is already open
 * @retval -TIMESLOT_ERROR_INVALID_PARAM          One of the pointers was NULL
 */
int timeslot_open(struct timeslot_config *p_config, struct timeslot_cb *p_cb);

/** @brief Request a recurring timeslot of len_us based on the given interval
 * 
 * @param[in] len_us      Usable length will be minus safety_margin_us
 * 
 * @retval 0                                        Success
 * @retval -TIMESLOT_ERROR_TIMESLOT_ALREADY_STARTED The session is already open
 * @retval -NRF_EINVAL                              Invalid parameter
 * @retval -NRF_ENOENT                              The session is not open
 * @retval -NRF_EAGAIN                              The session is not IDLE
 */
int timeslot_start(uint32_t len_us);

/** @brief Stop requesting the recurring timeslot and allow the session to go idle
 *
 * @retval 0                                   Success
 * @retval -TIMESLOT_ERROR_NO_TIMESLOT_STARTED There is no timeslot to stop
 */
int timeslot_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* TIMESLOT_H__ */

/** @} */
