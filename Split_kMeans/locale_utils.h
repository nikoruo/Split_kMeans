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
 * --------------------------------------------------------------------
 */

/* Update log
* --------------------------------------------------------------------
* Version 1.0.0 - 2025-10-01 by Niko Ruohonen
* -Initial release.
* --------------------------------------------------------------------
* Update 1.1...
* -...
*/

#pragma once
#include <locale.h>
#include <stdio.h>

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
static void set_numeric_locale_finnish(void)
{
    /* Candidates in priority order
     *  • Linux / BSD:     fi_FI.UTF-8 (generated via locale-gen)
     *  • Generic POSIX:   fi_FI
     *  • Windows / MSVC:  Finnish_Finland.1252
     */
    static const char* candidates[] = {
        "fi_FI.UTF-8",
        "fi_FI",
        "Finnish_Finland.1252",
        NULL
    };

    for (const char** p = candidates; *p; ++p) {
        if (setlocale(LC_NUMERIC, *p)) {
            return;
        }
    }

    fprintf(stderr,
        "Warning: Finnish numeric locale not available; "
        "using \"C\" (decimal point = '.')\n");
    setlocale(LC_NUMERIC, "C");
}
#pragma once