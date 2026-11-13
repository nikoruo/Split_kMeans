/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

/* locale_utils.h ------------------------------------------------------
 * Utilities for controlling numeric locale.
 * Provides set_numeric_locale_finnish(), which attempts to set LC_NUMERIC
 * to a Finnish locale and falls back to "C" if none are available.
 *
 * Notes:
 *  - setlocale() changes process-global state and is not thread-safe.
 *    Consider calling this early during startup, before starting threads.
 *  - Candidates tried (in order): "fi_FI.UTF-8", "fi_FI",
 *    "Finnish_Finland.1252" (Windows). If none succeed, LC_NUMERIC is set
 *    to "C" and a warning is printed to stderr.
 * 
 * Example:
 *   int main(void) {
 *       set_numeric_locale_finnish();  // Call before CSV writes
 *       printf("%.2f\n", 3.14);        // Prints "3,14" on Finnish locale
 *   }
 * --------------------------------------------------------------------*/

/* Update log
* --------------------------------------------------------------------
* Version 1.0.0 - 26-10-2025 by Niko Ruohonen
* -Initial release.
* --------------------------------------------------------------------
* Update 1.1...
* -...
*/

#pragma once

#ifndef LOCALE_UTILS_H
#define LOCALE_UTILS_H

/**
 * @brief Sets the process numeric locale (LC_NUMERIC) to Finnish with safe fallbacks.
 *
 * Tries the following locale names in priority order:
 *  - "fi_FI.UTF-8" (Linux/BSD)
 *  - "fi_FI" (generic POSIX)
 *  - "Finnish_Finland.1252" (Windows/MSVC)
 *
 * On success, LC_NUMERIC is set to the first available candidate.
 * If none are available, a warning is printed to stderr and LC_NUMERIC is set to "C".
 *
 * Behavior notes:
 *  - setlocale() modifies process-global state and is not thread-safe.
 *    Call this early during initialization, before spawning threads.
 *
 * @return void
 */
void set_numeric_locale_finnish(void);

#endif /* LOCALE_UTILS_H */
