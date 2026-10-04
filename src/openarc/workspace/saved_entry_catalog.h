// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#ifndef OPENARC_WORKSPACE_SAVED_ENTRY_CATALOG_H_
#define OPENARC_WORKSPACE_SAVED_ENTRY_CATALOG_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "base/types/expected.h"
#include "components/bookmarks/browser/bookmark_model_observer.h"
#include "openarc/workspace/workspace_ids.h"
#include "url/gurl.h"

namespace bookmarks {
class BookmarkModel;
}

namespace openarc::workspace {

// Version 1 metadata values are canonical lowercase UUID strings. The future
// WorkspaceService writes/repairs these keys; the catalog never writes them.
inline constexpr char kSpaceRootMetadataKey[] = "openarc.workspace.space_id.v1";
inline constexpr char kSavedEntryMetadataKey[] =
    "openarc.workspace.entry_id.v1";

enum class BookmarkStorage { kLocalOrSyncable, kAccount };

struct BookmarkLocator {
  BookmarkStorage storage = BookmarkStorage::kLocalOrSyncable;
  base::Uuid uuid;
  bool operator==(const BookmarkLocator&) const = default;
};

// The workspace record supplies the authoritative locator. A matching metadata
// marker elsewhere never silently replaces a deleted/moved root.
struct SpaceRootBinding {
  SpaceId space_id;
  BookmarkLocator root;
  bool operator==(const SpaceRootBinding&) const = default;
};

enum class CatalogReadiness { kLoading, kUpdating, kReady, kModelDeleted };
enum class CatalogRootStatus {
  kPending,
  kAvailable,
  kMissing,
  kNotUserFolder,
  kMetadataMismatch,
  kOverlappingRoots,
};
enum class CatalogEntryStatus {
  kFolder,
  kReady,
  kMissingId,
  kInvalidId,
  kDuplicateId,
};

// Depth-first rows preserve the actual bookmark sibling order. Folders have no
// URL or EntryId. URL rows with invalid/ambiguous IDs remain present by locator
// but have no bindable EntryId. These values are a current projection, never a
// second durable URL/title/order store or a set of live tabs.
struct SavedCatalogRow {
  BookmarkLocator locator;
  BookmarkLocator parent;
  size_t sibling_index = 0;
  size_t depth = 0;
  std::u16string title;
  std::optional<GURL> url;
  std::optional<EntryId> entry_id;
  CatalogEntryStatus status = CatalogEntryStatus::kFolder;
  bool operator==(const SavedCatalogRow&) const = default;
};

struct SavedCatalogSpace {
  SpaceRootBinding binding;
  CatalogRootStatus status = CatalogRootStatus::kPending;
  std::vector<SavedCatalogRow> rows;
  bool operator==(const SavedCatalogSpace&) const = default;
};

struct SavedCatalogSnapshot {
  uint64_t generation = 0;
  CatalogReadiness readiness = CatalogReadiness::kLoading;
  std::vector<SavedCatalogSpace> spaces;
};

enum class CatalogConfigError {
  kInvalidId,
  kInvalidStorage,
  kDuplicateBinding,
};

// Read-only, sequence-bound BookmarkModel observer. Construction may precede
// model loading. GetSnapshot() is available then, but rows are publishable only
// when readiness is kReady; individual roots may still be unavailable. Extensive
// changes expose kUpdating with no rows, then one rebuilt ready snapshot.
// No BookmarkNode pointer is retained across model callbacks.
//
// The caller must provide the correct regular-profile BookmarkModel. This class
// does not obtain a profile, redirect OTR models, or implement private storage.
// Browser/profile factory, metadata adoption/repair, persistence, and tab binding
// are deliberately outside this catalog. Consumers revalidate generation and
// resolve locators again before acting on a snapshot.
class SavedEntryCatalog final : private bookmarks::BookmarkModelObserver {
 public:
  explicit SavedEntryCatalog(bookmarks::BookmarkModel& model);
  ~SavedEntryCatalog() override;
  SavedEntryCatalog(const SavedEntryCatalog&) = delete;
  SavedEntryCatalog& operator=(const SavedEntryCatalog&) = delete;

  // Validates IDs/storage and unique Space/root bindings before replacing any
  // configuration. Missing/malformed/nested roots are snapshot statuses rather
  // than configuration failures; bookmarks may change independently.
  base::expected<void, CatalogConfigError> SetSpaceRoots(
      base::span<const SpaceRootBinding> roots);
  // Reference remains valid until the next catalog change; copy to retain it.
  const SavedCatalogSnapshot& GetSnapshot() const;
  // No initial callback. Callbacks should read the current snapshot. Invoking
  // BookmarkModel mutations synchronously from a callback is unsupported.
  base::CallbackListSubscription ObserveChanges(
      base::RepeatingClosure callback) const;

 private:
  void Rebuild();
  void BookmarkModelLoaded(bool ids_reassigned) override;
  void BookmarkModelBeingDeleted() override;
  void BookmarkNodeMoved(const bookmarks::BookmarkNode*,
                         size_t,
                         const bookmarks::BookmarkNode*,
                         size_t) override;
  void BookmarkNodeAdded(const bookmarks::BookmarkNode*, size_t, bool) override;
  void BookmarkNodeRemoved(const bookmarks::BookmarkNode*,
                           size_t,
                           const bookmarks::BookmarkNode*,
                           const std::set<GURL>&,
                           const base::Location&) override;
  void BookmarkNodeChanged(const bookmarks::BookmarkNode*) override;
  void BookmarkMetaInfoChanged(const bookmarks::BookmarkNode*) override;
  void BookmarkNodeFaviconChanged(const bookmarks::BookmarkNode*) override;
  void BookmarkNodeChildrenReordered(const bookmarks::BookmarkNode*) override;
  void BookmarkAllUserNodesRemoved(const std::set<GURL>&,
                                  const base::Location&) override;
  void ExtensiveBookmarkChangesBeginning() override;
  void ExtensiveBookmarkChangesEnded() override;

  raw_ptr<bookmarks::BookmarkModel> model_;
  std::vector<SpaceRootBinding> roots_;
  SavedCatalogSnapshot snapshot_;
  const scoped_refptr<base::RefCountedData<base::RepeatingClosureList>> changed_ =
      base::MakeRefCounted<base::RefCountedData<base::RepeatingClosureList>>();
  base::ScopedObservation<bookmarks::BookmarkModel,
                          bookmarks::BookmarkModelObserver> observation_{this};
  SEQUENCE_CHECKER(sequence_checker_);
  mutable base::WeakPtrFactory<SavedEntryCatalog> weak_factory_{this};
};

}  // namespace openarc::workspace

#endif  // OPENARC_WORKSPACE_SAVED_ENTRY_CATALOG_H_
