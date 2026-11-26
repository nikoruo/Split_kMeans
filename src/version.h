/* SPDX-License-Identifier: AGPL-3.0-only
 * Copyright (C) 2025 Niko Ruohonen and contributors
 */

#ifndef VERSION_H
#define VERSION_H

/* Release number; version.rc also uses it for the Windows version resource. */
#define SKM_VERSION_MAJOR 2
#define SKM_VERSION_MINOR 0
#define SKM_VERSION_PATCH 0

#define SKM_STRINGIFY_(x) #x
#define SKM_STRINGIFY(x) SKM_STRINGIFY_(x)
#define SKM_VERSION SKM_STRINGIFY(SKM_VERSION_MAJOR.SKM_VERSION_MINOR.SKM_VERSION_PATCH)

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
