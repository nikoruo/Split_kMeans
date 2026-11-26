/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef REPORT_H
#define REPORT_H

/* argv excludes "report"; merges by name, requires each dataset folder's dataset.txt and the same input identity
 * (SHA-256, K, N, D) across folders; returns status or exits. */
int commandReport(int argc, char* argv[]);

#endif /* REPORT_H */
