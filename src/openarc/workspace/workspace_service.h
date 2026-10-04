// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#ifndef OPENARC_WORKSPACE_WORKSPACE_SERVICE_H_
#define OPENARC_WORKSPACE_WORKSPACE_SERVICE_H_

#include <memory>
#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "components/keyed_service/core/keyed_service.h"
#include "openarc/workspace/workspace_record_store.h"

namespace openarc::workspace {

inline constexpr char kWorkspaceContainerMetadataKey[] =
    "openarc.workspace.container.v1";

enum class WorkspaceServiceState {
  kLoading,
  kUninitialized,
  kReady,
  kWritingRecord,
  kWritingIntent,
  kAwaitingBookmarkWrite,
  kFinalizing,
  kRecoveryRequired,
  kShutdown,
};

struct SavedBookmarkIdentity {
  SpaceId space_id;
  EntryId entry_id;
  BookmarkLocator bookmark;
  bool operator==(const SavedBookmarkIdentity&) const = default;
};

enum class WorkspaceCommandStatus {
  kDurable,
  kPendingRecovery,
  kNotReady,
  kBusy,
  kInvalidUrl,
  kRecoveryRequired,
  kShutdown,
};

struct WorkspaceCommandResult {
  WorkspaceCommandStatus status;
  // Present only when an exact, currently surviving bookmark is verified. A
  // pending-recovery result may retain this identity after an applied save.
  std::optional<SavedBookmarkIdentity> saved;
};

struct WorkspaceServiceSnapshot {
  uint64_t generation = 0;
  WorkspaceServiceState state = WorkspaceServiceState::kLoading;
  std::optional<WorkspaceRecord> record;
  SavedCatalogSnapshot catalog;
};

// First bounded regular-profile service: create one default Space and save.
// No tab ownership, navigation, general CRUD, removal/Undo UI, or session I/O.
// Upstream BookmarkModel grouping still observes mutations for native Undo.
//
// The future factory MUST reject private profiles before obtaining a model or
// profile path (BookmarkModelFactory redirects OTR). Create also rejects every
// nonregular context before constructing a durable store. Until a separate
// ephemeral backend exists, private custom-workspace UI must stay disabled.
//
// BookmarkCommit is the only extra test seam: the production adapter calls the
// proposed BookmarkModel::CommitPendingWrites, acknowledging a fresh primary
// snapshot. It must not substitute a testing flush or mere task-queue barrier.
class WorkspaceService final : public KeyedService {
 public:
  using Completion = base::OnceCallback<void(WorkspaceCommandResult)>;
  using BookmarkCommit =
      base::RepeatingCallback<void(base::OnceCallback<void(bool)>)>;

  static base::expected<std::unique_ptr<WorkspaceService>, WorkspaceRecordError>
  Create(PersistenceContext context,
         bookmarks::BookmarkModel& model,
         const base::FilePath& profile_directory,
         scoped_refptr<base::SequencedTaskRunner> file_runner,
         BookmarkCommit bookmark_commit);
  ~WorkspaceService() override;

  void Start();
  void Shutdown() override;
  const WorkspaceServiceSnapshot& GetSnapshot() const;
  const SavedEntryCatalog& GetSavedEntryCatalog() const;
  base::CallbackListSubscription ObserveChanges(base::RepeatingClosure callback);

  void CreateDefaultSpace(Completion completion);
  void SaveBookmark(const GURL& url,
                    const std::u16string& title,
                    Completion completion);
  // Explicit user recovery only. If the intended URL node is absent, retry
  // uses this NEW request; it never replays a stored URL/title. Existing nodes
  // must match identity/metadata and are preserved without rewriting content.
  void RetryPendingSave(const GURL& url,
                        const std::u16string& title,
                        Completion completion);
  // Retires the intent only; never removes surviving bookmarks. Unavailable
  // roots remain unavailable until a subsequent explicit save can create them.
  void AbandonPendingSave(Completion completion);

 private:
  WorkspaceService(bookmarks::BookmarkModel& model,
                   std::unique_ptr<WorkspaceRecordStore> store,
                   BookmarkCommit bookmark_commit);
  bool BeginCommand(Completion* completion, bool recovery_allowed = false);
  void OnRecordLoaded(WorkspaceRecordLoadResult result);
  void OnCatalogChanged();
  void Reconcile();
  void ConfigureCatalog();
  void Publish();
  void Finish(WorkspaceCommandStatus status);
  void FailRecovery();
  bool IncrementRevision(WorkspaceRecord& record) const;
  void CommitRecord(WorkspaceRecord next,
                    WorkspaceServiceState state,
                    base::OnceClosure on_success);
  void OnRecordCommitted(WorkspaceRecord next,
                         base::OnceClosure on_success,
                         WorkspaceRecordWriteResult result);
  void ApplyPendingSave(GURL url, std::u16string title, bool explicit_retry);
  void AwaitBookmarkWrite();
  void OnBookmarksCommitted(bool success);
  std::optional<SavedBookmarkIdentity> ResolvePendingIdentity() const;
  std::optional<SavedBookmarkIdentity> ResolveAppliedIdentity() const;
  bool PendingRootsMatch(bool require_existing) const;
  bool HasAnyWorkspaceMarker() const;
  bool MarkerIsUnique(const char* key,
                      const std::string& value,
                      const bookmarks::BookmarkNode* expected) const;

  raw_ptr<bookmarks::BookmarkModel> model_;
  std::unique_ptr<WorkspaceRecordStore> store_;
  const BookmarkCommit bookmark_commit_;
  SavedEntryCatalog catalog_;
  base::CallbackListSubscription catalog_subscription_;
  std::optional<WorkspaceRecord> record_;
  WorkspaceServiceSnapshot snapshot_;
  WorkspaceServiceState state_ = WorkspaceServiceState::kLoading;
  const scoped_refptr<base::RefCountedData<base::RepeatingClosureList>> changed_ =
      base::MakeRefCounted<base::RefCountedData<base::RepeatingClosureList>>();
  Completion completion_;
  std::optional<SavedBookmarkIdentity> applied_identity_;
  bool started_ = false;
  bool record_loaded_ = false;
  bool busy_ = false;
  bool reconcile_posted_ = false;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<WorkspaceService> weak_factory_{this};
};

}  // namespace openarc::workspace

#endif  // OPENARC_WORKSPACE_WORKSPACE_SERVICE_H_
