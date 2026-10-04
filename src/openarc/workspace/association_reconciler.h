// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#ifndef OPENARC_WORKSPACE_ASSOCIATION_RECONCILER_H_
#define OPENARC_WORKSPACE_ASSOCIATION_RECONCILER_H_

#include <optional>
#include <vector>

#include "base/containers/span.h"
#include "base/types/expected.h"
#include "components/sessions/core/session_id.h"
#include "openarc/workspace/workspace_ids.h"

namespace openarc::workspace {

// Supplied only after BookmarkModel loading and copied-ID reconciliation. This
// is an identifier-only snapshot of authority, not a bookmark or tab registry.
struct SavedEntryScope {
  EntryId entry_id;
  SpaceId space_id;

  bool operator==(const SavedEntryScope&) const = default;
};

struct WorkspaceSnapshot {
  SpaceId default_space;
  std::vector<SpaceId> spaces;
  std::vector<SavedEntryScope> saved_entries;

  bool operator==(const WorkspaceSnapshot&) const = default;
};

// IDs identify real tabs/windows in one Profile after Chromium has assigned
// their current session identities. An invalid input space_id means that the
// session had no usable Space. No URL, navigation, or native pin/group state is
// represented here; Chromium remains their owner.
struct TabAssociation {
  SessionID window_id = SessionID::InvalidValue();
  SessionID tab_id = SessionID::InvalidValue();
  SpaceId space_id;
  std::optional<EntryId> entry_id;

  bool operator==(const TabAssociation&) const = default;
};

enum class ReconcileError {
  kInvalidSnapshot,
  kInvalidTabIdentity,
  kDuplicateTabIdentity,
};

// Pure preparation for restore integration; does not read, write, create,
// navigate, close, or persist anything. Failure returns no partial result and
// never changes inputs. Snapshot IDs must be valid and unique, every entry must
// reference a known Space, and the default Space must exist. Supplied tab IDs
// must be valid and unique across all supplied windows.
//
// The caller supplies all participating real tabs from one Profile in collision
// priority order: existing authoritative/live bindings first, then restored tabs
// in stable restore order. The first valid claim of an entry in each window
// wins. Results preserve input order. The caller must revalidate its snapshot
// generation and tab/window ownership before applying the complete result.
//
// Current bookmark scope overrides stale session scope. A missing entry is
// unbound; an unknown Space falls back to the valid default. A colliding tab is
// unbound in the entry's current Space, preserving the caller's real page.
//
// Group consistency is a caller/integration concern and must be reconciled
// before applying these associations. This helper proves neither persistence
// nor browser/extension integration, including private-session non-persistence.
base::expected<std::vector<TabAssociation>, ReconcileError>
ReconcileAssociations(const WorkspaceSnapshot& snapshot,
                      base::span<const TabAssociation> priority_order);

}  // namespace openarc::workspace

#endif  // OPENARC_WORKSPACE_ASSOCIATION_RECONCILER_H_
