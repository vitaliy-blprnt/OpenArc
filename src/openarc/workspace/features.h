// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#ifndef OPENARC_WORKSPACE_FEATURES_H_
#define OPENARC_WORKSPACE_FEATURES_H_

#include "base/feature_list.h"

namespace openarc::workspace::features {

// Development gate for workspace integration. Enabling this does not qualify
// persistence, browser UI, extension compatibility, or private-session safety.
BASE_DECLARE_FEATURE(kOpenArcWorkspaces);

}  // namespace openarc::workspace::features

#endif  // OPENARC_WORKSPACE_FEATURES_H_
