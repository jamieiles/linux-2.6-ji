/**
 * Copyright 2008-2011 Picochip, All Rights Reserved.
 * http://www.picochip.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This file defines an API for configuring the Fractional-N synthesizer in
 * the picoChip PC302 device.
 */
#ifndef __PC302FRACN_H__
#define __PC302FRACN_H__

/**
 * Get the current M value.
 *
 * \param val The destination to store the value of M.
 * \return Returns zero on success, non-zero on failure.
 */
extern int fracn_get_m(u8 *val);

/**
 * Set a new value of M.
 *
 * \param val The new value of M.
 * \return Returns zero on success, non-zero on failure.
 */
extern int fracn_set_m(u8 val);

/**
 * Get the current N value.
 *
 * \param val The destination to store the value of N.
 * \return Returns zero on success, non-zero on failure.
 */
extern int fracn_get_n(u8 *val);

/**
 * Store a new value of N.
 *
 * \param val The new value of N.
 * \return Returns zero on success, non-zero on failure.
 */
extern int fracn_set_n(u8 val);

/**
 * Get the current K value.
 *
 * \param val The destination to store the value of K.
 * \return Returns zero on success, non-zero on failure.
 */
extern int fracn_get_k(u32 *val);

/**
 * Store a new value of K.
 *
 * \param val The new value of K.
 * \return Returns zero on success, non-zero on failure.
 */
extern int fracn_set_k(u32 val);

/**
 * Get the current control voltage pulse lower limit.
 *
 * \param val The destination to store the result in.
 * \return Returns zero on success, non-zero on failure.
 */
extern int fracn_get_cv_pulse_ll(u16 *val);

/**
 * Set a new control voltage pulse lower limit.
 *
 * \param val The new value to set.
 * \return Returns zero on success, non-zero on failure. */
extern int fracn_set_cv_pulse_ll(u16 val);

/**
 * Get the current control voltage pulse upper limit.
 *
 * \param val The destination to store the result in.
 * \return Returns zero on success, non-zero on failure.
 */
extern int fracn_get_cv_pulse_ul(u16 *val);

/**
 * Set a new control voltage pulse upper limit.
 *
 * \param val The new value to set.
 * \return Returns zero on success, non-zero on failure. */
extern int fracn_set_cv_pulse_ul(u16 val);

/**
 * Get the contents of the Frac-N status register.
 *
 * \param val The destination to store the status in.
 * \return Returns zero on success, non-zero on failure.
 */
extern int fracn_get_status(u16 *val);

/**
 * Reset the Frac-N synth.
 *
 * \return Returns zero on success, non-zero on failure.
 */
extern int fracn_reset(void);

/**
 * Load the new values of M, N and K into the Frac-N synth.
 *
 * \return Returns zero on success, non-zero on failure.
 */
extern int fracn_load(void);

#endif /* __PC302FRACN_H__ */
