// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/saved_entry_catalog.h"

#include <map>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "components/bookmarks/browser/bookmark_client.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"

namespace openarc::workspace {
namespace {

using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;

BookmarkModel::NodeTypeForUuidLookup LookupType(BookmarkStorage storage) {
  return storage == BookmarkStorage::kAccount
             ? BookmarkModel::NodeTypeForUuidLookup::kAccountNodes
             : BookmarkModel::NodeTypeForUuidLookup::kLocalOrSyncableNodes;
}

// Iterative traversal avoids adding a stack-depth limit to bookmark folders.
std::vector<SavedCatalogRow> ReadRows(const BookmarkNode& root,
                                    BookmarkStorage storage) {
  struct Pending {
    raw_ptr<const BookmarkNode> parent;
    size_t index;
    size_t depth;
  };
  std::vector<Pending> pending;
  auto add_children = [&pending](const BookmarkNode* node, size_t depth) {
    for (size_t i = node->children().size(); i > 0; --i) {
      pending.push_back({node, i - 1, depth});
    }
  };
  add_children(&root, 0);
  std::vector<SavedCatalogRow> rows;
  while (!pending.empty()) {
    const Pending current = pending.back();
    pending.pop_back();
    const BookmarkNode& node = *current.parent->children()[current.index];
    SavedCatalogRow row;
    row.locator = {storage, node.uuid()};
    row.parent = {storage, current.parent->uuid()};
    row.sibling_index = current.index;
    row.depth = current.depth;
    row.title = node.GetTitle();
    if (node.is_url()) {
      row.url = node.url();
      std::string value;
      if (!node.GetMetaInfo(kSavedEntryMetadataKey, &value)) {
        row.status = CatalogEntryStatus::kMissingId;
      } else {
        base::Uuid id = base::Uuid::ParseLowercase(value);
        if (!id.is_valid()) {
          row.status = CatalogEntryStatus::kInvalidId;
        } else {
          row.entry_id = EntryId(id);
          row.status = CatalogEntryStatus::kReady;
        }
      }
    } else {
      add_children(&node, current.depth + 1);
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

}  // namespace

SavedEntryCatalog::SavedEntryCatalog(BookmarkModel& model) : model_(&model) {
  observation_.Observe(&model);
  Rebuild();
}

SavedEntryCatalog::~SavedEntryCatalog() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

base::expected<void, CatalogConfigError> SavedEntryCatalog::SetSpaceRoots(
    base::span<const SpaceRootBinding> roots) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::set<SpaceId> spaces;
  std::set<std::pair<BookmarkStorage, base::Uuid>> locations;
  for (const auto& binding : roots) {
    if (!binding.space_id.value().is_valid() || !binding.root.uuid.is_valid()) {
      return base::unexpected(CatalogConfigError::kInvalidId);
    }
    if (binding.root.storage != BookmarkStorage::kAccount &&
        binding.root.storage != BookmarkStorage::kLocalOrSyncable) {
      return base::unexpected(CatalogConfigError::kInvalidStorage);
    }
    if (!spaces.insert(binding.space_id).second ||
        !locations.emplace(binding.root.storage, binding.root.uuid).second) {
      return base::unexpected(CatalogConfigError::kDuplicateBinding);
    }
  }
  roots_.assign(roots.begin(), roots.end());
  Rebuild();
  return {};
}

const SavedCatalogSnapshot& SavedEntryCatalog::GetSnapshot() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return snapshot_;
}

base::CallbackListSubscription SavedEntryCatalog::ObserveChanges(
    base::RepeatingClosure callback) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return changed_->data.Add(base::BindRepeating(
      [](base::WeakPtr<SavedEntryCatalog> self,
         const base::RepeatingClosure& callback) {
        if (self) {
          callback.Run();
        }
      },
      weak_factory_.GetWeakPtr(), std::move(callback)));
}

void SavedEntryCatalog::Rebuild() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SavedCatalogSnapshot next;
  if (!model_) {
    next.readiness = CatalogReadiness::kModelDeleted;
  } else if (!model_->loaded()) {
    next.readiness = CatalogReadiness::kLoading;
  } else if (model_->IsDoingExtensiveChanges()) {
    next.readiness = CatalogReadiness::kUpdating;
  } else {
    next.readiness = CatalogReadiness::kReady;
  }
  for (const auto& binding : roots_) {
    next.spaces.push_back({binding, CatalogRootStatus::kPending, {}});
  }
  if (next.readiness == CatalogReadiness::kReady) {
    std::vector<const BookmarkNode*> resolved;
    for (auto& space : next.spaces) {
      const auto& binding = space.binding;
      const BookmarkNode* node = model_->GetNodeByUuid(
          binding.root.uuid, LookupType(binding.root.storage));
      resolved.push_back(node);
      if (!node) {
        space.status = CatalogRootStatus::kMissing;
      } else if (!node->is_folder() || node->is_permanent_node() ||
                 node == model_->root_node() ||
                 model_->client()->IsNodeManaged(node)) {
        space.status = CatalogRootStatus::kNotUserFolder;
      } else {
        std::string value;
        space.status =
            node->GetMetaInfo(kSpaceRootMetadataKey, &value) &&
                    value == binding.space_id.value().AsLowercaseString()
                ? CatalogRootStatus::kAvailable
                : CatalogRootStatus::kMetadataMismatch;
      }
    }
    // Overlapping configured subtrees have no unambiguous Space ownership.
    // Include resolved invalid roots in this check to avoid silently adopting
    // descendants of a temporarily malformed nested root into another Space.
    for (size_t i = 0; i < resolved.size(); ++i) {
      for (size_t j = i + 1; j < resolved.size(); ++j) {
        if (resolved[i] && resolved[j] &&
            (resolved[i]->HasAncestor(resolved[j]) ||
             resolved[j]->HasAncestor(resolved[i]))) {
          next.spaces[i].status = CatalogRootStatus::kOverlappingRoots;
          next.spaces[j].status = CatalogRootStatus::kOverlappingRoots;
        }
      }
    }
    std::map<EntryId, size_t> counts;
    for (size_t i = 0; i < next.spaces.size(); ++i) {
      auto& space = next.spaces[i];
      if (space.status != CatalogRootStatus::kAvailable) {
        continue;
      }
      space.rows = ReadRows(*resolved[i], space.binding.root.storage);
      for (const auto& row : space.rows) {
        if (row.entry_id) {
          ++counts[*row.entry_id];
        }
      }
    }
    for (auto& space : next.spaces) {
      for (auto& row : space.rows) {
        if (row.entry_id && counts[*row.entry_id] > 1) {
          row.entry_id.reset();
          row.status = CatalogEntryStatus::kDuplicateId;
        }
      }
    }
  }
  if (next.readiness == snapshot_.readiness &&
      next.spaces == snapshot_.spaces) {
    return;
  }
  next.generation = snapshot_.generation + 1;
  snapshot_ = std::move(next);
  // An observer may destroy the catalog. Keep CallbackList alive throughout
  // iteration, and skip later callbacks through their owner weak pointers.
  auto changed = changed_;
  changed->data.Notify();
}

void SavedEntryCatalog::BookmarkModelLoaded(bool) { Rebuild(); }
void SavedEntryCatalog::BookmarkModelBeingDeleted() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  observation_.Reset();
  model_ = nullptr;
  Rebuild();
}
void SavedEntryCatalog::BookmarkNodeMoved(const BookmarkNode*,
                                         size_t,
                                         const BookmarkNode*,
                                         size_t) {
  Rebuild();
}
void SavedEntryCatalog::BookmarkNodeAdded(const BookmarkNode*, size_t, bool) {
  Rebuild();
}
void SavedEntryCatalog::BookmarkNodeRemoved(const BookmarkNode*,
                                           size_t,
                                           const BookmarkNode*,
                                           const std::set<GURL>&,
                                           const base::Location&) {
  Rebuild();
}
void SavedEntryCatalog::BookmarkNodeChanged(const BookmarkNode*) { Rebuild(); }
void SavedEntryCatalog::BookmarkMetaInfoChanged(const BookmarkNode*) {
  Rebuild();
}
void SavedEntryCatalog::BookmarkNodeFaviconChanged(const BookmarkNode*) {}
void SavedEntryCatalog::BookmarkNodeChildrenReordered(const BookmarkNode*) {
  Rebuild();
}
void SavedEntryCatalog::BookmarkAllUserNodesRemoved(const std::set<GURL>&,
                                                   const base::Location&) {
  Rebuild();
}
void SavedEntryCatalog::ExtensiveBookmarkChangesBeginning() { Rebuild(); }
void SavedEntryCatalog::ExtensiveBookmarkChangesEnded() { Rebuild(); }

}  // namespace openarc::workspace
