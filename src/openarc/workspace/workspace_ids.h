// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#ifndef OPENARC_WORKSPACE_WORKSPACE_IDS_H_
#define OPENARC_WORKSPACE_WORKSPACE_IDS_H_

#include "base/types/strong_alias.h"
#include "base/uuid.h"

namespace openarc::workspace {

using SpaceId = base::StrongAlias<class SpaceIdTag, base::Uuid>;
using EntryId = base::StrongAlias<class EntryIdTag, base::Uuid>;

}  // namespace openarc::workspace

#endif  // OPENARC_WORKSPACE_WORKSPACE_IDS_H_
