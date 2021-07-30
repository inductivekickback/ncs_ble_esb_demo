/*
 * Copyright (c) 2021 Daniel Veilleux
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef PROPRIETARY_RF_H__
#define PROPRIETARY_RF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <esb.h>

/* The timeslot length to request once per Connection Interval. Will not be extended. */
#define TS_LEN_US 25000

/* @brief A timeslot has started. */
void proprietary_rf_start(void);

/** @brief A timeslot has ended.
 *
 * @note The timeslot will close safety_margin_us earlier than TS_LEN_US.
 */
void proprietary_rf_end(void);

/** @brief A timeslot was blocked or cancelled.
 * 
 * @note Provided in case the network requires synchnronization, e.g. for channel hopping.
 * 
 * @param[in] count    The number of consecutive skipped timeslots
 */
void proprietary_rf_skipped(uint8_t count);

#ifdef __cplusplus
}
#endif

#endif /* PROPRIETARY_RF_H__ */

/** @} */
