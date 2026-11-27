/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#include "locale_utils.h"

#include <locale.h>
#include <stdio.h>

void set_numeric_locale_finnish(void)
{
    /* Candidates in priority order
     *  - Linux / BSD:     fi_FI.UTF-8 (generated via locale-gen)
     *  - Generic POSIX:   fi_FI
     *  - Windows / MSVC:  Finnish_Finland.1252
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
