// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/workspace_service.h"

#include <limits>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "components/bookmarks/browser/bookmark_client.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/browser/scoped_group_bookmark_actions.h"

namespace openarc::workspace {
namespace {

using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;
using State = WorkspaceServiceState;
using Status = WorkspaceCommandStatus;

BookmarkLocator Local(const base::Uuid& uuid) {
  return {BookmarkStorage::kLocalOrSyncable, uuid};
}

const BookmarkNode* Find(BookmarkModel* model, const base::Uuid& uuid) {
  return model->GetNodeByUuid(
      uuid, BookmarkModel::NodeTypeForUuidLookup::kLocalOrSyncableNodes);
}

bool UserFolder(BookmarkModel* model, const BookmarkNode* node) {
  return node && node->is_folder() && !node->is_permanent_node() &&
         node != model->root_node() && !model->client()->IsNodeManaged(node);
}

bool HasMarker(const BookmarkNode* node, const char* key,
               const std::string& value) {
  std::string found;
  return node && node->GetMetaInfo(key, &found) && found == value;
}

template <class Predicate>
bool VisitUntil(const BookmarkNode* root, Predicate predicate) {
  std::vector<const BookmarkNode*> pending{root};
  while (!pending.empty()) {
    const BookmarkNode* node = pending.back();
    pending.pop_back();
    if (predicate(node)) {
      return true;
    }
    for (const auto& child : node->children()) {
      pending.push_back(child.get());
    }
  }
  return false;
}

}  // namespace

base::expected<std::unique_ptr<WorkspaceService>, WorkspaceRecordError>
WorkspaceService::Create(
    PersistenceContext context,
    BookmarkModel& model,
    const base::FilePath& profile_directory,
    scoped_refptr<base::SequencedTaskRunner> file_runner,
    BookmarkCommit bookmark_commit) {
  if (context != PersistenceContext::kRegularProfile) {
    return base::unexpected(
        WorkspaceRecordError::kPrivatePersistenceDisallowed);
  }
  if (!bookmark_commit) {
    return base::unexpected(WorkspaceRecordError::kInvalidRecord);
  }
  auto store = WorkspaceRecordStore::Create(
      context, profile_directory, std::move(file_runner));
  if (!store.has_value()) {
    return base::unexpected(store.error());
  }
  return std::unique_ptr<WorkspaceService>(new WorkspaceService(
      model, std::move(*store), std::move(bookmark_commit)));
}

WorkspaceService::WorkspaceService(
    BookmarkModel& model,
    std::unique_ptr<WorkspaceRecordStore> store,
    BookmarkCommit bookmark_commit)
    : model_(&model),
      store_(std::move(store)),
      bookmark_commit_(std::move(bookmark_commit)),
      catalog_(model) {
  catalog_subscription_ = catalog_.ObserveChanges(base::BindRepeating(
      &WorkspaceService::OnCatalogChanged, weak_factory_.GetWeakPtr()));
}

WorkspaceService::~WorkspaceService() { Shutdown(); }

void WorkspaceService::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (started_ || state_ == State::kShutdown) {
    return;
  }
  started_ = true;
  store_->Load(base::BindOnce(&WorkspaceService::OnRecordLoaded,
                              weak_factory_.GetWeakPtr()));
}

void WorkspaceService::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (state_ == State::kShutdown) {
    return;
  }
  state_ = State::kShutdown;
  weak_factory_.InvalidateWeakPtrs();
  catalog_subscription_ = {};
  store_.reset();
  model_ = nullptr;
  busy_ = false;
  snapshot_.state = State::kShutdown;
  auto completion = std::move(completion_);
  if (completion) {
    std::move(completion).Run({Status::kShutdown, std::nullopt});
  }
}

const WorkspaceServiceSnapshot& WorkspaceService::GetSnapshot() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return snapshot_;
}

const SavedEntryCatalog& WorkspaceService::GetSavedEntryCatalog() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return catalog_;
}

base::CallbackListSubscription WorkspaceService::ObserveChanges(
    base::RepeatingClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return changed_->data.Add(base::BindRepeating(
      [](base::WeakPtr<WorkspaceService> self,
         const base::RepeatingClosure& callback) {
        if (self) {
          callback.Run();
        }
      },
      weak_factory_.GetWeakPtr(), std::move(callback)));
}

bool WorkspaceService::BeginCommand(Completion* completion,
                                    bool recovery_allowed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<Status> rejected;
  if (state_ == State::kShutdown) {
    rejected = Status::kShutdown;
  } else if (busy_) {
    rejected = Status::kBusy;
  } else if (!record_loaded_ || !model_ ||
             catalog_.GetSnapshot().readiness != CatalogReadiness::kReady) {
    rejected = Status::kNotReady;
  } else if (state_ == State::kRecoveryRequired && !recovery_allowed) {
    rejected = Status::kRecoveryRequired;
  }
  if (rejected) {
    std::move(*completion).Run({*rejected, std::nullopt});
    return false;
  }
  completion_ = std::move(*completion);
  applied_identity_.reset();
  busy_ = true;
  return true;
}

void WorkspaceService::OnRecordLoaded(WorkspaceRecordLoadResult result) {
  if (!result.has_value()) {
    state_ = State::kRecoveryRequired;
    Publish();
    return;
  }
  record_ = std::move(*result);
  record_loaded_ = true;
  const auto weak = weak_factory_.GetWeakPtr();
  ConfigureCatalog();
  if (weak) {
    Reconcile();
  }
}

void WorkspaceService::OnCatalogChanged() {
  if (catalog_.GetSnapshot().readiness == CatalogReadiness::kModelDeleted) {
    model_ = nullptr;
  }
  if (!reconcile_posted_) {
    reconcile_posted_ = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WorkspaceService::Reconcile,
                                   weak_factory_.GetWeakPtr()));
  }
}

void WorkspaceService::ConfigureCatalog() {
  std::vector<SpaceRootBinding> roots;
  if (record_) {
    auto root = record_->root;
    if (!root && record_->pending) {
      root = Local(record_->pending->root_uuid);
    }
    if (root) {
      roots.push_back({record_->default_space_id, *root});
    }
  }
  CHECK(catalog_.SetSpaceRoots(roots).has_value());
}

void WorkspaceService::Publish() {
  snapshot_.generation++;
  snapshot_.state = state_;
  snapshot_.record = record_;
  snapshot_.catalog = catalog_.GetSnapshot();
  auto changed = changed_;
  changed->data.Notify();
}

std::optional<SavedBookmarkIdentity>
WorkspaceService::ResolveAppliedIdentity() const {
  auto identity = ResolvePendingIdentity();
  if (!identity && applied_identity_ &&
      catalog_.GetSnapshot().readiness == CatalogReadiness::kReady) {
    for (const auto& space : catalog_.GetSnapshot().spaces) {
      if (space.status != CatalogRootStatus::kAvailable ||
          space.binding.space_id != applied_identity_->space_id) {
        continue;
      }
      for (const auto& row : space.rows) {
        if (row.status == CatalogEntryStatus::kReady &&
            row.entry_id == applied_identity_->entry_id &&
            row.locator == applied_identity_->bookmark) {
          identity = applied_identity_;
        }
      }
    }
  }
  return identity;
}

void WorkspaceService::Finish(Status status) {
  if (status == Status::kDurable) {
    state_ = record_ ? State::kReady : State::kUninitialized;
  }
  auto completion = std::move(completion_);
  const auto weak = weak_factory_.GetWeakPtr();
  Publish();
  // Keep the command busy until observers have seen its final snapshot. A
  // completion may start the next command, but an observer must not overtake it.
  // Publish may also destroy or shut down the service. Its callback-list storage
  // survives independently, so only locals can be used when the weak ptr dies.
  if (!weak) {
    if (completion) {
      std::move(completion).Run({Status::kShutdown, std::nullopt});
    }
    return;
  }
  auto identity = ResolveAppliedIdentity();
  busy_ = false;
  if (completion) {
    std::move(completion).Run({status, identity});
  }
}

void WorkspaceService::FailRecovery() {
  state_ = State::kRecoveryRequired;
  Finish(Status::kPendingRecovery);
}

bool WorkspaceService::IncrementRevision(WorkspaceRecord& record) const {
  if (record.revision == std::numeric_limits<int>::max()) {
    return false;
  }
  ++record.revision;
  return true;
}

void WorkspaceService::CommitRecord(WorkspaceRecord next,
                                   State state,
                                   base::OnceClosure on_success) {
  state_ = state;
  store_->Commit(next, base::BindOnce(&WorkspaceService::OnRecordCommitted,
                                     weak_factory_.GetWeakPtr(), next,
                                     std::move(on_success)));
  Publish();
}

void WorkspaceService::OnRecordCommitted(WorkspaceRecord next,
                                        base::OnceClosure on_success,
                                        WorkspaceRecordWriteResult result) {
  if (!result.has_value()) {
    FailRecovery();
    return;
  }
  record_ = std::move(next);
  ConfigureCatalog();
  std::move(on_success).Run();
}

bool WorkspaceService::HasAnyWorkspaceMarker() const {
  return VisitUntil(model_->root_node(), [](const BookmarkNode* node) {
    std::string value;
    return node->GetMetaInfo(kWorkspaceContainerMetadataKey, &value) ||
           node->GetMetaInfo(kSpaceRootMetadataKey, &value) ||
           node->GetMetaInfo(kSavedEntryMetadataKey, &value);
  });
}

bool WorkspaceService::MarkerIsUnique(const char* key,
                                     const std::string& value,
                                     const BookmarkNode* expected) const {
  bool seen = false;
  const bool conflict = VisitUntil(
      model_->root_node(), [&](const BookmarkNode* node) {
        if (!HasMarker(node, key, value)) {
          return false;
        }
        if (node != expected || seen) {
          return true;
        }
        seen = true;
        return false;
      });
  return !conflict && (seen == (expected != nullptr));
}

void WorkspaceService::Reconcile() {
  reconcile_posted_ = false;
  if (state_ == State::kShutdown) {
    return;
  }
  if (!record_loaded_) {
    return;
  }
  if (!model_) {
    FailRecovery();
    return;
  }
  if (busy_) {
    Publish();
    return;
  }
  // A failed persistence operation requires an explicit recovery command.
  // Unrelated bookmark notifications must not silently retry it or reopen writes.
  if (state_ == State::kRecoveryRequired) {
    Publish();
    return;
  }
  if (catalog_.GetSnapshot().readiness != CatalogReadiness::kReady) {
    state_ = State::kLoading;
    Publish();
    return;
  }
  if (!record_) {
    state_ = HasAnyWorkspaceMarker() ? State::kRecoveryRequired
                                      : State::kUninitialized;
    Publish();
    return;
  }
  if (record_->pending) {
    // Reconciliation can retire a surviving operation; it never creates nodes.
    if (ResolvePendingIdentity()) {
      busy_ = true;
      AwaitBookmarkWrite();
    } else {
      state_ = State::kRecoveryRequired;
      Publish();
    }
    return;
  }
  auto next = *record_;
  const auto text = record_->default_space_id->AsLowercaseString();
  const BookmarkNode* root = record_->root
                                 ? Find(model_, record_->root->uuid)
                                 : nullptr;
  const BookmarkNode* container = record_->container
                                      ? Find(model_, record_->container->uuid)
                                      : nullptr;
  if ((root && (!UserFolder(model_, root) ||
                !MarkerIsUnique(kSpaceRootMetadataKey, text, root))) ||
      (container &&
       (!UserFolder(model_, container) ||
        !MarkerIsUnique(kWorkspaceContainerMetadataKey, text, container))) ||
      (!root && !MarkerIsUnique(kSpaceRootMetadataKey, text, nullptr))) {
    state_ = State::kRecoveryRequired;
    Publish();
    return;
  }
  if (!root) {
    next.root.reset();
  }
  if (!container) {
    next.container.reset();
  }
  if (next != *record_) {
    if (!IncrementRevision(next)) {
      FailRecovery();
      return;
    }
    busy_ = true;
    CommitRecord(std::move(next), State::kWritingRecord,
                 base::BindOnce(&WorkspaceService::Finish,
                                weak_factory_.GetWeakPtr(), Status::kDurable));
    return;
  }
  state_ = State::kReady;
  Publish();
}

void WorkspaceService::CreateDefaultSpace(Completion completion) {
  if (!BeginCommand(&completion)) {
    return;
  }
  if (record_) {
    Finish(Status::kDurable);
    return;
  }
  if (HasAnyWorkspaceMarker()) {
    FailRecovery();
    return;
  }
  WorkspaceRecord next;
  next.default_space_id = SpaceId(base::Uuid::GenerateRandomV4());
  CommitRecord(std::move(next), State::kWritingRecord,
               base::BindOnce(&WorkspaceService::Finish,
                              weak_factory_.GetWeakPtr(), Status::kDurable));
}

void WorkspaceService::SaveBookmark(const GURL& url,
                                    const std::u16string& title,
                                    Completion completion) {
  if (!BeginCommand(&completion)) {
    return;
  }
  if (!record_) {
    state_ = State::kUninitialized;
    Finish(Status::kNotReady);
    return;
  }
  if (!url.is_valid()) {
    Finish(Status::kInvalidUrl);
    return;
  }
  auto next = *record_;
  next.pending = PendingBookmarkCreation{
      base::Uuid::GenerateRandomV4(),
      next.container ? next.container->uuid : base::Uuid::GenerateRandomV4(),
      next.root ? next.root->uuid : base::Uuid::GenerateRandomV4(),
      base::Uuid::GenerateRandomV4(), EntryId(base::Uuid::GenerateRandomV4())};
  if (!IncrementRevision(next)) {
    FailRecovery();
    return;
  }
  CommitRecord(
      std::move(next), State::kWritingIntent,
      base::BindOnce(&WorkspaceService::ApplyPendingSave,
                     weak_factory_.GetWeakPtr(), url, title, false));
}

bool WorkspaceService::PendingRootsMatch(bool require_existing) const {
  if (!model_ || !model_->loaded() || model_->IsDoingExtensiveChanges() ||
      !record_ || !record_->pending) {
    return false;
  }
  const auto& pending = *record_->pending;
  const auto text = record_->default_space_id->AsLowercaseString();
  const BookmarkNode* root = Find(model_, pending.root_uuid);
  const BookmarkNode* container = Find(model_, pending.container_uuid);
  if ((require_existing && !root) ||
      (root && !UserFolder(model_, root)) ||
      (container && !UserFolder(model_, container)) ||
      !MarkerIsUnique(kSpaceRootMetadataKey, text, root) ||
      !MarkerIsUnique(kWorkspaceContainerMetadataKey, text, container)) {
    return false;
  }
  return true;
}

std::optional<SavedBookmarkIdentity>
WorkspaceService::ResolvePendingIdentity() const {
  if (!PendingRootsMatch(true)) {
    return std::nullopt;
  }
  const auto& pending = *record_->pending;
  const BookmarkNode* node = Find(model_, pending.bookmark_uuid);
  if (!node || !node->is_url() || model_->client()->IsNodeManaged(node) ||
      !node->HasAncestor(Find(model_, pending.root_uuid)) ||
      !MarkerIsUnique(kSavedEntryMetadataKey,
                      pending.entry_id->AsLowercaseString(), node)) {
    return std::nullopt;
  }
  return SavedBookmarkIdentity{record_->default_space_id, pending.entry_id,
                               Local(pending.bookmark_uuid)};
}

void WorkspaceService::ApplyPendingSave(GURL url,
                                       std::u16string title,
                                       bool explicit_retry) {
  if (!PendingRootsMatch(false)) {
    FailRecovery();
    return;
  }
  const auto pending = *record_->pending;
  const BookmarkNode* root = Find(model_, pending.root_uuid);
  const BookmarkNode* container = Find(model_, pending.container_uuid);
  const BookmarkNode* existing = Find(model_, pending.bookmark_uuid);
  if (existing) {
    if (!ResolvePendingIdentity()) {
      FailRecovery();
      return;
    }
    AwaitBookmarkWrite();
    return;
  }
  // An intervening external deletion cannot turn a normal save into implicit
  // reconstruction. Only the explicit recovery command may retry these IDs.
  if ((!root && record_->root && !explicit_retry) ||
      (!container && !root && record_->container && !explicit_retry) ||
      !MarkerIsUnique(kSavedEntryMetadataKey,
                      pending.entry_id->AsLowercaseString(), nullptr)) {
    FailRecovery();
    return;
  }
  const auto weak = weak_factory_.GetWeakPtr();
  {
    bookmarks::ScopedGroupBookmarkActions group(model_);
    if (!root) {
      if (!container) {
        BookmarkNode::MetaInfoMap meta;
        meta[kWorkspaceContainerMetadataKey] =
            record_->default_space_id->AsLowercaseString();
        container = model_->AddFolder(
            model_->other_node(), model_->other_node()->children().size(),
            u"OpenArc", &meta, std::nullopt, pending.container_uuid);
        if (!weak) {
          return;
        }
      }
      BookmarkNode::MetaInfoMap meta;
      meta[kSpaceRootMetadataKey] =
          record_->default_space_id->AsLowercaseString();
      root = model_->AddFolder(container, container->children().size(),
                               base::UTF8ToUTF16(record_->label), &meta,
                               std::nullopt, pending.root_uuid);
      if (!weak) {
        return;
      }
    }
    BookmarkNode::MetaInfoMap meta;
    meta[kSavedEntryMetadataKey] = pending.entry_id->AsLowercaseString();
    model_->AddURL(root, root->children().size(), title, url, &meta, std::nullopt,
                    pending.bookmark_uuid, true);
  }
  if (weak) {
    AwaitBookmarkWrite();
  }
}

void WorkspaceService::AwaitBookmarkWrite() {
  applied_identity_ = ResolvePendingIdentity();
  if (!applied_identity_) {
    FailRecovery();
    return;
  }
  state_ = State::kAwaitingBookmarkWrite;
  const auto weak = weak_factory_.GetWeakPtr();
  const auto commit = bookmark_commit_;
  commit.Run(base::BindPostTaskToCurrentDefault(base::BindOnce(
      &WorkspaceService::OnBookmarksCommitted, weak)));
  if (weak) {
    Publish();
  }
}

void WorkspaceService::OnBookmarksCommitted(bool success) {
  if (!success || !ResolvePendingIdentity()) {
    FailRecovery();
    return;
  }
  auto next = *record_;
  next.root = Local(next.pending->root_uuid);
  next.container = Find(model_, next.pending->container_uuid)
                       ? std::make_optional(Local(next.pending->container_uuid))
                       : std::nullopt;
  next.pending.reset();
  if (!IncrementRevision(next)) {
    FailRecovery();
    return;
  }
  CommitRecord(std::move(next), State::kFinalizing,
               base::BindOnce(&WorkspaceService::Finish,
                              weak_factory_.GetWeakPtr(), Status::kDurable));
}

void WorkspaceService::RetryPendingSave(const GURL& url,
                                        const std::u16string& title,
                                        Completion completion) {
  if (!BeginCommand(&completion, true)) {
    return;
  }
  if (!record_ || !record_->pending) {
    Finish(Status::kRecoveryRequired);
    return;
  }
  if (!url.is_valid()) {
    Finish(Status::kInvalidUrl);
    return;
  }
  ApplyPendingSave(url, title, true);
}

void WorkspaceService::AbandonPendingSave(Completion completion) {
  if (!BeginCommand(&completion, true)) {
    return;
  }
  if (!record_ || !record_->pending) {
    Finish(Status::kRecoveryRequired);
    return;
  }
  auto next = *record_;
  const auto text = next.default_space_id->AsLowercaseString();
  const BookmarkNode* root = Find(model_, next.pending->root_uuid);
  const BookmarkNode* container = Find(model_, next.pending->container_uuid);
  next.root = UserFolder(model_, root) &&
                      MarkerIsUnique(kSpaceRootMetadataKey, text, root)
                  ? std::make_optional(Local(root->uuid()))
                  : std::nullopt;
  next.container =
      UserFolder(model_, container) &&
              MarkerIsUnique(kWorkspaceContainerMetadataKey, text, container)
          ? std::make_optional(Local(container->uuid()))
          : std::nullopt;
  next.pending.reset();
  if (!IncrementRevision(next)) {
    FailRecovery();
    return;
  }
  CommitRecord(std::move(next), State::kWritingRecord,
               base::BindOnce(&WorkspaceService::Finish,
                              weak_factory_.GetWeakPtr(), Status::kDurable));
}

}  // namespace openarc::workspace
