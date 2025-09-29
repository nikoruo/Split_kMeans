/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

/* locale_utils.h ------------------------------------------------------
 * Utility functions for managing locale settings, particularly for
 * numeric formatting. Provides a function to set the numeric locale
 * to Finnish or fallback to "C" if unavailable.
 * --------------------------------------------------------------------
 */

/* Update log
* --------------------------------------------------------------------
* Version 1.0 - 2025-09-29 by Niko Ruohonen
* -Initial release.
* -Added `set_numeric_locale_finnish` function to set the numeric
* locale to Finnish(fi_FI) with fallback to "C".
* --------------------------------------------------------------------
* Update 1.1...
*/


#pragma once
#include <locale.h>
#include <stdio.h>

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