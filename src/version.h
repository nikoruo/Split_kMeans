/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef VERSION_H
#define VERSION_H

#define SKM_VERSION "2.0.0"

#ifdef SKM_BUILD_INFO
#    include "build_info.h"
#endif
#ifndef SKM_GIT_COMMIT
#    define SKM_GIT_COMMIT "unknown"
#endif
#ifndef SKM_BUILD_FLAGS
#    define SKM_BUILD_FLAGS "unknown"
#endif

#endif /* VERSION_H */
