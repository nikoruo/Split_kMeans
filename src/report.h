/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef REPORT_H
#define REPORT_H

/* argv excludes "report"; merges by name and checks K when present, not input contents; returns status or exits. */
int commandReport(int argc, char* argv[]);

#endif /* REPORT_H */
