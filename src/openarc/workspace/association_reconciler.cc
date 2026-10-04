// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/association_reconciler.h"

#include <map>
#include <set>
#include <utility>

namespace openarc::workspace {

base::expected<std::vector<TabAssociation>, ReconcileError>
ReconcileAssociations(const WorkspaceSnapshot& snapshot,
                      base::span<const TabAssociation> priority_order) {
  std::set<SpaceId> spaces;
  for (const auto& space : snapshot.spaces) {
    if (!space->is_valid() || !spaces.insert(space).second) {
      return base::unexpected(ReconcileError::kInvalidSnapshot);
    }
  }
  if (!spaces.contains(snapshot.default_space)) {
    return base::unexpected(ReconcileError::kInvalidSnapshot);
  }

  std::map<EntryId, SpaceId> entry_scopes;
  for (const auto& entry : snapshot.saved_entries) {
    if (!entry.entry_id->is_valid() || !spaces.contains(entry.space_id) ||
        !entry_scopes.emplace(entry.entry_id, entry.space_id).second) {
      return base::unexpected(ReconcileError::kInvalidSnapshot);
    }
  }

  std::set<SessionID> tab_ids;
  for (const auto& tab : priority_order) {
    if (!tab.tab_id.is_valid() || !tab.window_id.is_valid()) {
      return base::unexpected(ReconcileError::kInvalidTabIdentity);
    }
    if (!tab_ids.insert(tab.tab_id).second) {
      return base::unexpected(ReconcileError::kDuplicateTabIdentity);
    }
  }

  std::set<std::pair<SessionID, EntryId>> claimed_entries;
  std::vector<TabAssociation> result;
  result.reserve(priority_order.size());
  for (const auto& tab : priority_order) {
    auto resolved = tab;
    if (!spaces.contains(resolved.space_id)) {
      resolved.space_id = snapshot.default_space;
    }
    if (resolved.entry_id) {
      const auto entry = entry_scopes.find(*resolved.entry_id);
      if (entry == entry_scopes.end()) {
        resolved.entry_id.reset();
      } else {
        resolved.space_id = entry->second;
        if (!claimed_entries.emplace(tab.window_id, entry->first).second) {
          resolved.entry_id.reset();
        }
      }
    }
    result.push_back(std::move(resolved));
  }
  return result;
}

}  // namespace openarc::workspace
