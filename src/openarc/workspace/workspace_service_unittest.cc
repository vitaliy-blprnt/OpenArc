// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/workspace_service.h"

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/common/bookmark_metrics.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace openarc::workspace {
namespace {

using State = WorkspaceServiceState;
using Status = WorkspaceCommandStatus;
using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;
constexpr auto kRegular = PersistenceContext::kRegularProfile;
constexpr auto kEdit = bookmarks::metrics::BookmarkEditSource::kExtension;

template <class Result, class Start>
Result Await(Start start) {
  std::optional<Result> result;
  base::RunLoop loop;
  start(base::BindOnce(
      [](std::optional<Result>* result, base::OnceClosure quit, Result value) {
        *result = std::move(value);
        std::move(quit).Run();
      }, &result, loop.QuitClosure()));
  loop.Run();
  return std::move(*result);
}

class WorkspaceServiceTest : public testing::Test {
 protected:
  void SetUp() override {
    base::ThreadPoolInstance::CreateAndStartWithDefaultParams("WorkspaceTests");
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    file_runner_ = base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});
    model_ = bookmarks::TestBookmarkClient::CreateModel();
  }
  void TearDown() override {
    service_.reset();
    model_.reset();
    acknowledgements_.clear();
    Drain();
    file_runner_.reset();
    base::ThreadPoolInstance::Get()->Shutdown();
    base::ThreadPoolInstance::Get()->JoinForTesting();
    base::ThreadPoolInstance::Set(nullptr);
  }
  void Drain() {
    // File replies can schedule a subsequent file write, so drain both queues.
    for (int i = 0; i < 6; ++i) {
      base::ThreadPoolInstance::Get()->FlushForTesting();
      base::RunLoop().RunUntilIdle();
    }
  }
  void StartService() {
    auto created = WorkspaceService::Create(
        kRegular, *model_, directory_.GetPath(), file_runner_,
        Committer());
    ASSERT_TRUE(created.has_value());
    service_ = std::move(*created);
    service_->Start();
    Drain();
  }
  WorkspaceService::BookmarkCommit Committer() {
    return base::BindRepeating(&WorkspaceServiceTest::Acknowledge,
                               base::Unretained(this));
  }
  void Acknowledge(base::OnceCallback<void(bool)> callback) {
    ++ack_requests_;
    acknowledgements_.push_back(std::move(callback));
  }
  void Reply(bool success) {
    ASSERT_FALSE(acknowledgements_.empty());
    auto callback = std::move(acknowledgements_.front());
    acknowledgements_.pop_front();
    std::move(callback).Run(success);
    Drain();
  }
  void CreateDefault() {
    const auto result = Await<WorkspaceCommandResult>([&](auto callback) {
      service_->CreateDefaultSpace(std::move(callback));
    });
    ASSERT_EQ(result.status, Status::kDurable);
    ASSERT_EQ(service_->GetSnapshot().state, State::kReady);
  }
  void BeginSave(std::optional<WorkspaceCommandResult>& result) {
    service_->SaveBookmark(
        GURL("https://saved.example/original"), u"Original title",
        base::BindOnce(
            [](std::optional<WorkspaceCommandResult>* target,
               WorkspaceCommandResult value) { *target = std::move(value); },
            &result));
    Drain();
  }
  const BookmarkNode* Find(const BookmarkLocator& locator) {
    return model_->GetNodeByUuid(
        locator.uuid, BookmarkModel::NodeTypeForUuidLookup::kLocalOrSyncableNodes);
  }
  base::FilePath Primary() {
    return directory_.GetPath().AppendASCII(WorkspaceRecordStore::kFileName);
  }
  base::FilePath Previous() {
    return directory_.GetPath().AppendASCII(
        WorkspaceRecordStore::kPreviousFileName);
  }
  std::string Read(const base::FilePath& path) {
    std::string data;
    EXPECT_TRUE(base::ReadFileToString(path, &data));
    return data;
  }
  std::unique_ptr<WorkspaceRecordStore> Store(
      const base::FilePath& path = base::FilePath()) {
    auto created = WorkspaceRecordStore::Create(
        kRegular, path.empty() ? directory_.GetPath() : path, file_runner_);
    EXPECT_TRUE(created.has_value());
    return std::move(*created);
  }
  WorkspaceRecordLoadResult Load(WorkspaceRecordStore& store) {
    return Await<WorkspaceRecordLoadResult>(
        [&](auto callback) { store.Load(std::move(callback)); });
  }
  WorkspaceRecordWriteResult Commit(WorkspaceRecordStore& store,
                                     const WorkspaceRecord& record) {
    return Await<WorkspaceRecordWriteResult>(
        [&](auto callback) { store.Commit(record, std::move(callback)); });
  }
  WorkspaceRecord Record() {
    WorkspaceRecord record;
    record.default_space_id = SpaceId(base::Uuid::GenerateRandomV4());
    return record;
  }

  base::SingleThreadTaskExecutor executor_;
  base::ScopedTempDir directory_;
  scoped_refptr<base::SequencedTaskRunner> file_runner_;
  std::unique_ptr<BookmarkModel> model_;
  std::unique_ptr<WorkspaceService> service_;
  std::deque<base::OnceCallback<void(bool)>> acknowledgements_;
  int ack_requests_ = 0;
};

TEST_F(WorkspaceServiceTest, AtomicStoreRoundTripRetainsPreviousCompatibleBytes) {
  auto store = Store();
  auto loaded = Load(*store);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_FALSE(*loaded);
  auto record = Record();
  ASSERT_TRUE(Commit(*store, record).has_value());
  const std::string original = Read(Primary());
  record.revision++;
  record.label = "Work";
  ASSERT_TRUE(Commit(*store, record).has_value());
  EXPECT_EQ(Read(Previous()), original);
  store.reset();
  store = Store();
  loaded = Load(*store);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(*loaded, record);
  EXPECT_FALSE(Commit(*store, record).has_value());  // Cannot reuse revision.
}

TEST_F(WorkspaceServiceTest, RealAtomicWriteFailureDoesNotClaimCommitAndCanRetry) {
  const auto absent_directory = directory_.GetPath().AppendASCII("not-created");
  auto store = Store(absent_directory);
  ASSERT_TRUE(Load(*store).has_value());
  const auto record = Record();
  auto result = Commit(*store, record);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), WorkspaceRecordError::kWriteFailed);
  ASSERT_TRUE(base::CreateDirectory(absent_directory));
  EXPECT_TRUE(Commit(*store, record).has_value());
}

TEST_F(WorkspaceServiceTest, UnknownOrMalformedPrimaryIsNeverDefaulted) {
  for (const std::string bytes : {"[2]", "not json", "[1,1]"}) {
    ASSERT_TRUE(base::WriteFile(Primary(), bytes));
    auto store = Store();
    EXPECT_FALSE(Load(*store).has_value());
    EXPECT_FALSE(Commit(*store, Record()).has_value());
    EXPECT_EQ(Read(Primary()), bytes);
    EXPECT_FALSE(base::PathExists(Previous()));
  }
}

TEST_F(WorkspaceServiceTest, ForeignPrimaryAndPreviousCopiesArePreserved) {
  auto store = Store();
  ASSERT_TRUE(Load(*store).has_value());
  auto record = Record();
  ASSERT_TRUE(Commit(*store, record).has_value());
  const std::string original = Read(Primary());
  ASSERT_TRUE(base::WriteFile(Previous(), "[999]"));
  record.revision++;
  auto result = Commit(*store, record);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), WorkspaceRecordError::kConflict);
  EXPECT_EQ(Read(Primary()), original);
  EXPECT_EQ(Read(Previous()), "[999]");
  auto restarted = Store();
  auto loaded = Load(*restarted);
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error(), WorkspaceRecordError::kUnsupportedVersion);
  ASSERT_TRUE(base::DeleteFile(Previous()));
  ASSERT_TRUE(base::WriteFile(Primary(), "[998]"));
  EXPECT_FALSE(Commit(*store, record).has_value());
  EXPECT_EQ(Read(Primary()), "[998]");
}

TEST_F(WorkspaceServiceTest, MissingPrimaryWithPreviousRequiresRecovery) {
  auto store = Store();
  ASSERT_TRUE(Load(*store).has_value());
  auto record = Record();
  ASSERT_TRUE(Commit(*store, record).has_value());
  record.revision++;
  ASSERT_TRUE(Commit(*store, record).has_value());
  const std::string previous = Read(Previous());
  ASSERT_TRUE(base::DeleteFile(Primary()));
  store = Store();
  const auto result = Load(*store);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), WorkspaceRecordError::kRecoveryRequired);
  EXPECT_EQ(Read(Previous()), previous);
  EXPECT_FALSE(base::PathExists(Primary()));
}

TEST_F(WorkspaceServiceTest, PrivateContextRefusedBeforeStoreConstruction) {
  for (auto context : {PersistenceContext::kOffTheRecord,
                        static_cast<PersistenceContext>(99)}) {
    auto created = WorkspaceService::Create(
        context, *model_, directory_.GetPath(), file_runner_,
        Committer());
    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error(),
              WorkspaceRecordError::kPrivatePersistenceDisallowed);
  }
  Drain();
  EXPECT_FALSE(base::PathExists(Primary()));
  EXPECT_FALSE(base::PathExists(Previous()));
  EXPECT_TRUE(model_->other_node()->children().empty());
}

TEST_F(WorkspaceServiceTest, SaveJournalsFirstThenAwaitsBookmarkAndRecordSuccess) {
  StartService();
  EXPECT_EQ(service_->GetSnapshot().state, State::kUninitialized);
  CreateDefault();
  EXPECT_TRUE(model_->other_node()->children().empty());
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  EXPECT_FALSE(result);
  EXPECT_EQ(service_->GetSnapshot().state, State::kAwaitingBookmarkWrite);
  ASSERT_TRUE(service_->GetSnapshot().record->pending);
  const auto pending = *service_->GetSnapshot().record->pending;
  EXPECT_NE(Read(Primary()).find(pending.operation_id.AsLowercaseString()),
            std::string::npos);
  EXPECT_EQ(Read(Primary()).find("saved.example"), std::string::npos);
  EXPECT_EQ(Read(Primary()).find("Original title"), std::string::npos);
  auto independent_store = Store();
  auto durable_intent = Load(*independent_store);
  ASSERT_TRUE(durable_intent.has_value());
  ASSERT_TRUE(*durable_intent);
  EXPECT_EQ((*durable_intent)->pending, pending);
  ASSERT_EQ(ack_requests_, 1);
  Reply(true);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kDurable);
  ASSERT_TRUE(result->saved);
  EXPECT_EQ(result->saved->bookmark.uuid, pending.bookmark_uuid);
  EXPECT_FALSE(service_->GetSnapshot().record->pending);
  EXPECT_EQ(Find(result->saved->bookmark)->url(),
            GURL("https://saved.example/original"));
}

TEST_F(WorkspaceServiceTest, BookmarkFailureKeepsIntentAndAppliedIdentity) {
  StartService();
  CreateDefault();
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  const auto bytes = Read(Primary());
  Reply(false);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kPendingRecovery);
  ASSERT_TRUE(result->saved);
  EXPECT_TRUE(Find(result->saved->bookmark));
  EXPECT_EQ(Read(Primary()), bytes);
  EXPECT_TRUE(service_->GetSnapshot().record->pending);
  EXPECT_EQ(service_->GetSnapshot().state, State::kRecoveryRequired);
}

TEST_F(WorkspaceServiceTest, FinalRecordFailurePreservesJournalAndRetryUsesSameIds) {
  StartService();
  CreateDefault();
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  const auto intent = *service_->GetSnapshot().record->pending;
  const auto intent_bytes = Read(Primary());
  const auto previous_bytes = Read(Previous());
  ASSERT_TRUE(base::DeleteFile(Previous()));
  ASSERT_TRUE(base::CreateDirectory(Previous()));
  Reply(true);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kPendingRecovery);
  EXPECT_EQ(Read(Primary()), intent_bytes);
  ASSERT_TRUE(base::DeletePathRecursively(Previous()));
  ASSERT_TRUE(base::WriteFile(Previous(), previous_bytes));
  result.reset();
  service_->RetryPendingSave(
      GURL("https://retry.example/"), u"Retry must not overwrite",
      base::BindOnce(
          [](std::optional<WorkspaceCommandResult>* target,
             WorkspaceCommandResult value) { *target = std::move(value); },
          &result));
  Reply(true);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kDurable);
  ASSERT_TRUE(result->saved);
  EXPECT_EQ(result->saved->bookmark.uuid, intent.bookmark_uuid);
  EXPECT_EQ(Find(result->saved->bookmark)->url(),
            GURL("https://saved.example/original"));
  EXPECT_EQ(Find(*service_->GetSnapshot().record->root)->children().size(), 1u);
}

TEST_F(WorkspaceServiceTest, MissingPendingNodesAreNotRecreatedAtStartup) {
  StartService();
  CreateDefault();
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  const auto pending = *service_->GetSnapshot().record->pending;
  service_.reset();
  acknowledgements_.clear();
  model_ = bookmarks::TestBookmarkClient::CreateModel();
  const int before = ack_requests_;
  StartService();
  EXPECT_EQ(service_->GetSnapshot().state, State::kRecoveryRequired);
  EXPECT_TRUE(model_->other_node()->children().empty());
  EXPECT_EQ(ack_requests_, before);
  EXPECT_EQ(service_->GetSnapshot().record->pending, pending);
  result.reset();
  service_->RetryPendingSave(
      GURL("https://explicit-retry.example/"), u"Explicit retry",
      base::BindOnce(
          [](std::optional<WorkspaceCommandResult>* target,
             WorkspaceCommandResult value) { *target = std::move(value); },
          &result));
  Reply(true);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kDurable);
  ASSERT_TRUE(result->saved);
  EXPECT_EQ(result->saved->bookmark.uuid, pending.bookmark_uuid);
  EXPECT_EQ(Find(result->saved->bookmark)->url(),
            GURL("https://explicit-retry.example/"));
}

TEST_F(WorkspaceServiceTest, SurvivingPendingNodesReconcileWithoutDuplicates) {
  StartService();
  CreateDefault();
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  const auto pending = *service_->GetSnapshot().record->pending;
  service_.reset();
  acknowledgements_.clear();
  StartService();
  EXPECT_EQ(service_->GetSnapshot().state, State::kAwaitingBookmarkWrite);
  Reply(true);
  EXPECT_FALSE(service_->GetSnapshot().record->pending);
  const BookmarkNode* root = Find(*service_->GetSnapshot().record->root);
  ASSERT_TRUE(root);
  ASSERT_EQ(root->children().size(), 1u);
  EXPECT_EQ(root->children()[0]->uuid(), pending.bookmark_uuid);
}

TEST_F(WorkspaceServiceTest, RootDeletionKeepsSpaceAndNeedsExplicitNewSave) {
  StartService();
  CreateDefault();
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  Reply(true);
  const auto id = service_->GetSnapshot().record->default_space_id;
  const auto old_root = *service_->GetSnapshot().record->root;
  model_->Remove(Find(old_root), kEdit, FROM_HERE);
  Drain();
  EXPECT_EQ(service_->GetSnapshot().record->default_space_id, id);
  EXPECT_FALSE(service_->GetSnapshot().record->root);
  EXPECT_FALSE(Find(old_root));
  EXPECT_EQ(service_->GetSnapshot().state, State::kReady);
  service_.reset();
  StartService();
  EXPECT_FALSE(service_->GetSnapshot().record->root);
  result.reset();
  BeginSave(result);
  Reply(true);
  EXPECT_NE(service_->GetSnapshot().record->root->uuid, old_root.uuid);
}

TEST_F(WorkspaceServiceTest, InterveningEditsRemainBookmarkAuthority) {
  StartService();
  CreateDefault();
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  const auto uuid = service_->GetSnapshot().record->pending->bookmark_uuid;
  const BookmarkNode* node = model_->GetNodeByUuid(
      uuid, BookmarkModel::NodeTypeForUuidLookup::kLocalOrSyncableNodes);
  model_->SetTitle(node, u"Extension edit", kEdit);
  model_->SetURL(node, GURL("https://extension-edit.example/"), kEdit);
  Reply(true);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kDurable);
  EXPECT_EQ(node->GetTitle(), u"Extension edit");
  EXPECT_EQ(node->url(), GURL("https://extension-edit.example/"));
  EXPECT_EQ(Read(Primary()).find("extension-edit"), std::string::npos);
}

TEST_F(WorkspaceServiceTest, DeletionWhileBookmarkWriteWaitsDoesNotResurrect) {
  StartService();
  CreateDefault();
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  const auto uuid = service_->GetSnapshot().record->pending->root_uuid;
  const BookmarkNode* root = model_->GetNodeByUuid(
      uuid, BookmarkModel::NodeTypeForUuidLookup::kLocalOrSyncableNodes);
  model_->Remove(root, kEdit, FROM_HERE);
  Reply(true);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kPendingRecovery);
  EXPECT_FALSE(result->saved);
  EXPECT_FALSE(model_->GetNodeByUuid(
      uuid, BookmarkModel::NodeTypeForUuidLookup::kLocalOrSyncableNodes));
  EXPECT_TRUE(service_->GetSnapshot().record->pending);
}

TEST_F(WorkspaceServiceTest, ShutdownBeforeIntentReplyCannotMutateBookmarks) {
  StartService();
  CreateDefault();
  std::optional<WorkspaceCommandResult> result;
  service_->SaveBookmark(
      GURL("https://shutdown.example/"), u"Shutdown",
      base::BindOnce(
          [](std::optional<WorkspaceCommandResult>* target,
             WorkspaceCommandResult value) { *target = std::move(value); },
          &result));
  service_->Shutdown();
  Drain();
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kShutdown);
  EXPECT_TRUE(model_->other_node()->children().empty());
  EXPECT_EQ(ack_requests_, 0);
  service_.reset();
  StartService();
  EXPECT_EQ(service_->GetSnapshot().state, State::kRecoveryRequired);
  EXPECT_TRUE(service_->GetSnapshot().record->pending);
}

TEST_F(WorkspaceServiceTest, ShutdownDuringBookmarkWaitLeavesIntent) {
  StartService();
  CreateDefault();
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  const auto intent = Read(Primary());
  service_->Shutdown();
  Reply(true);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kShutdown);
  EXPECT_EQ(Read(Primary()), intent);
}

TEST_F(WorkspaceServiceTest, ExistingMarkersWithoutRecordCannotCreateDuplicateRoot) {
  BookmarkNode::MetaInfoMap meta;
  meta[kSpaceRootMetadataKey] = base::Uuid::GenerateRandomV4().AsLowercaseString();
  const BookmarkNode* existing = model_->AddFolder(
      model_->other_node(), 0, u"Existing Space", &meta);
  StartService();
  EXPECT_EQ(service_->GetSnapshot().state, State::kRecoveryRequired);
  const auto result = Await<WorkspaceCommandResult>([&](auto callback) {
    service_->CreateDefaultSpace(std::move(callback));
  });
  EXPECT_EQ(result.status, Status::kRecoveryRequired);
  EXPECT_EQ(model_->other_node()->children().size(), 1u);
  EXPECT_EQ(model_->other_node()->children()[0].get(), existing);
  EXPECT_FALSE(base::PathExists(Primary()));
}


TEST_F(WorkspaceServiceTest, DestroyedStoreRepliesFailureOnOrigin) {
  auto store = Store();
  auto loaded = Await<WorkspaceRecordLoadResult>([&](auto callback) {
    store->Load(std::move(callback));
    store.reset();
  });
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error(), WorkspaceRecordError::kReadFailed);
  store = Store();
  ASSERT_TRUE(Load(*store).has_value());
  auto written = Await<WorkspaceRecordWriteResult>([&](auto callback) {
    store->Commit(Record(), std::move(callback));
    store.reset();
  });
  ASSERT_FALSE(written.has_value());
  EXPECT_EQ(written.error(), WorkspaceRecordError::kWriteFailed);
  // Cancellation cannot promise that an already posted atomic write was undone.
  auto restarted = Store();
  loaded = Load(*restarted);
  ASSERT_TRUE(loaded.has_value());
  ASSERT_TRUE(*loaded);
}

TEST_F(WorkspaceServiceTest, BookmarkFailureCannotBeRetriedByBookmarkNotification) {
  StartService();
  CreateDefault();
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  const auto bookmark = service_->GetSnapshot().record->pending->bookmark_uuid;
  const std::string intent = Read(Primary());
  Reply(false);
  const int before = ack_requests_;
  model_->SetTitle(model_->GetNodeByUuid(
                       bookmark,
                       BookmarkModel::NodeTypeForUuidLookup::kLocalOrSyncableNodes),
                   u"Unrelated edit", kEdit);
  Drain();
  EXPECT_EQ(service_->GetSnapshot().state, State::kRecoveryRequired);
  EXPECT_EQ(ack_requests_, before);
  EXPECT_EQ(Read(Primary()), intent);
}

TEST_F(WorkspaceServiceTest, FinalNotificationCannotOvertakeCommandCompletion) {
  StartService();
  CreateDefault();
  std::optional<WorkspaceCommandResult> result;
  std::optional<WorkspaceCommandResult> reentered;
  auto subscription = service_->ObserveChanges(base::BindRepeating(
      [](decltype(this) self,
         std::optional<WorkspaceCommandResult>* reentered) {
        const auto& snapshot = self->service_->GetSnapshot();
        if (snapshot.state == State::kReady && snapshot.record->root) {
          self->service_->CreateDefaultSpace(base::BindOnce(
              [](std::optional<WorkspaceCommandResult>* target,
                 WorkspaceCommandResult value) { *target = std::move(value); },
              reentered));
        }
      }, this, &reentered));
  BeginSave(result);
  Reply(true);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kDurable);
  ASSERT_TRUE(reentered);
  EXPECT_EQ(reentered->status, Status::kBusy);
}

TEST_F(WorkspaceServiceTest, FinalObserverCanDestroyServiceAndSkipsLaterObservers) {
  StartService();
  CreateDefault();
  int later_calls = 0;
  auto destroy = service_->ObserveChanges(base::BindRepeating(
      [](decltype(this) self) {
        const auto& snapshot = self->service_->GetSnapshot();
        if (snapshot.state == State::kReady && snapshot.record->root) {
          self->service_.reset();
        }
      }, this));
  auto later = service_->ObserveChanges(base::BindRepeating(
      [](int* calls) { ++*calls; }, &later_calls));
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  const int calls_before = later_calls;
  Reply(true);
  EXPECT_FALSE(service_);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kShutdown);
  EXPECT_FALSE(result->saved);
  // Finalizing is published before the final ready notification destroys owner.
  EXPECT_EQ(later_calls, calls_before + 1);
}

TEST_F(WorkspaceServiceTest, CatalogObserverCanDestroyServiceDuringSaveMutation) {
  StartService();
  CreateDefault();
  int later_calls = 0;
  auto destroy = service_->GetSavedEntryCatalog().ObserveChanges(
      base::BindRepeating([](decltype(this) self) {
        const auto& spaces = self->service_->GetSavedEntryCatalog()
                                 .GetSnapshot().spaces;
        if (!spaces.empty() && !spaces[0].rows.empty()) {
          self->service_.reset();
        }
      }, this));
  auto later = service_->GetSavedEntryCatalog().ObserveChanges(
      base::BindRepeating([](int* calls) { ++*calls; }, &later_calls));
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  EXPECT_FALSE(service_);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kShutdown);
  EXPECT_EQ(ack_requests_, 0);
  EXPECT_EQ(later_calls, 2);  // Missing root, then available empty root.
  auto store = Store();
  auto persisted = Load(*store);
  ASSERT_TRUE(persisted.has_value());
  ASSERT_TRUE(*persisted);
  EXPECT_TRUE((*persisted)->pending);
}

TEST_F(WorkspaceServiceTest, FinalObserverDeletionCannotReturnStaleSavedIdentity) {
  StartService();
  CreateDefault();
  auto subscription = service_->ObserveChanges(base::BindRepeating(
      [](decltype(this) self) {
        const auto& snapshot = self->service_->GetSnapshot();
        if (snapshot.state == State::kReady && snapshot.record->root) {
          const auto* root = self->Find(*snapshot.record->root);
          if (!root->children().empty()) {
            self->model_->Remove(root->children()[0].get(), kEdit, FROM_HERE);
          }
        }
      }, this));
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  Reply(true);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kDurable);
  EXPECT_FALSE(result->saved);
}


TEST_F(WorkspaceServiceTest, RecordLoadWaitsForBookmarkModelReadiness) {
  model_ = std::make_unique<BookmarkModel>(
      std::make_unique<bookmarks::TestBookmarkClient>());
  StartService();
  EXPECT_EQ(service_->GetSnapshot().state, State::kLoading);
  auto result = Await<WorkspaceCommandResult>([&](auto callback) {
    service_->CreateDefaultSpace(std::move(callback));
  });
  EXPECT_EQ(result.status, Status::kNotReady);
  EXPECT_FALSE(base::PathExists(Primary()));
  model_->LoadEmptyForTest();
  Drain();
  EXPECT_EQ(service_->GetSnapshot().state, State::kUninitialized);
  CreateDefault();
}

TEST_F(WorkspaceServiceTest, FailedIntentDoesNotMutateBookmarks) {
  StartService();
  CreateDefault();
  const auto record = service_->GetSnapshot().record;
  ASSERT_TRUE(base::WriteFile(Previous(), "[999]"));
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, Status::kPendingRecovery);
  EXPECT_FALSE(result->saved);
  EXPECT_EQ(service_->GetSnapshot().record, record);
  EXPECT_TRUE(model_->other_node()->children().empty());
  EXPECT_EQ(ack_requests_, 0);
  EXPECT_EQ(Read(Previous()), "[999]");
}

TEST_F(WorkspaceServiceTest, AbandonRetiresIntentWithoutDeletingAppliedBookmark) {
  StartService();
  CreateDefault();
  std::optional<WorkspaceCommandResult> result;
  BeginSave(result);
  Reply(false);
  ASSERT_TRUE(result->saved);
  const auto saved = *result->saved;
  const auto* bookmark = Find(saved.bookmark);
  const auto abandoned = Await<WorkspaceCommandResult>([&](auto callback) {
    service_->AbandonPendingSave(std::move(callback));
  });
  EXPECT_EQ(abandoned.status, Status::kDurable);
  EXPECT_EQ(Find(saved.bookmark), bookmark);
  EXPECT_FALSE(service_->GetSnapshot().record->pending);
  EXPECT_EQ(ack_requests_, 1);
}

}  // namespace
}  // namespace openarc::workspace
