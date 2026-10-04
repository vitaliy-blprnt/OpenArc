// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/saved_entry_catalog.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_executor.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/common/bookmark_metrics.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace openarc::workspace {
namespace {

using bookmarks::BookmarkModel;
using bookmarks::BookmarkNode;
constexpr auto kEdit = bookmarks::metrics::BookmarkEditSource::kExtension;

SpaceId NewSpace() { return SpaceId(base::Uuid::GenerateRandomV4()); }
EntryId NewEntry() { return EntryId(base::Uuid::GenerateRandomV4()); }

class SavedEntryCatalogTest : public testing::Test {
 protected:
  SavedEntryCatalogTest()
      : model_(bookmarks::TestBookmarkClient::CreateModel()), catalog_(*model_) {}

  const BookmarkNode* Root(SpaceId id, const BookmarkNode* parent = nullptr,
                           std::optional<base::Uuid> uuid = std::nullopt) {
    if (!parent) {
      parent = model_->other_node();
    }
    BookmarkNode::MetaInfoMap meta;
    meta[kSpaceRootMetadataKey] = id.value().AsLowercaseString();
    return model_->AddFolder(parent, parent->children().size(), u"Root", &meta,
                             std::nullopt, uuid);
  }
  const BookmarkNode* Entry(const BookmarkNode* parent, EntryId id,
                            std::u16string title = u"Saved") {
    BookmarkNode::MetaInfoMap meta;
    meta[kSavedEntryMetadataKey] = id.value().AsLowercaseString();
    return model_->AddURL(parent, parent->children().size(), title,
                          GURL("https://saved.example/"), &meta);
  }
  SpaceRootBinding Binding(SpaceId space, const BookmarkNode* root,
                           BookmarkStorage storage =
                               BookmarkStorage::kLocalOrSyncable) {
    return {space, {storage, root->uuid()}};
  }
  void Configure(std::initializer_list<SpaceRootBinding> bindings) {
    ASSERT_TRUE(catalog_.SetSpaceRoots(base::span(bindings)).has_value());
  }
  const SavedCatalogSnapshot& snapshot() { return catalog_.GetSnapshot(); }
  const SavedCatalogSpace& space(size_t index = 0) {
    return snapshot().spaces[index];
  }

  // LoadEmptyForTest does not create stores or read a real browser profile.
  base::SingleThreadTaskExecutor executor_;
  std::unique_ptr<BookmarkModel> model_;
  SavedEntryCatalog catalog_;
};

TEST(SavedEntryCatalogLoadingTest, WaitsForLoadedModelAndReportsMissingRoot) {
  base::SingleThreadTaskExecutor executor;
  BookmarkModel model(std::make_unique<bookmarks::TestBookmarkClient>());
  SavedEntryCatalog catalog(model);
  const std::array roots = {SpaceRootBinding{
      NewSpace(), {BookmarkStorage::kLocalOrSyncable,
                   base::Uuid::GenerateRandomV4()}}};
  ASSERT_TRUE(catalog.SetSpaceRoots(roots).has_value());
  EXPECT_EQ(catalog.GetSnapshot().readiness, CatalogReadiness::kLoading);
  ASSERT_EQ(catalog.GetSnapshot().spaces.size(), 1u);
  EXPECT_EQ(catalog.GetSnapshot().spaces[0].status, CatalogRootStatus::kPending);
  EXPECT_TRUE(catalog.GetSnapshot().spaces[0].rows.empty());
  int changes = 0;
  auto subscription = catalog.ObserveChanges(
      base::BindRepeating([](int* count) { ++*count; }, &changes));
  model.LoadEmptyForTest();
  EXPECT_EQ(changes, 1);
  EXPECT_EQ(catalog.GetSnapshot().readiness, CatalogReadiness::kReady);
  EXPECT_EQ(catalog.GetSnapshot().spaces[0].status, CatalogRootStatus::kMissing);
}

TEST(SavedEntryCatalogLoadingTest, ManagedDescendantCannotBecomeSpaceRoot) {
  base::SingleThreadTaskExecutor executor;
  auto client = std::make_unique<bookmarks::TestBookmarkClient>();
  const BookmarkNode* managed = client->EnableManagedNode();
  auto model = bookmarks::TestBookmarkClient::CreateModelWithClient(
      std::move(client));
  const SpaceId id = NewSpace();
  BookmarkNode::MetaInfoMap meta;
  meta[kSpaceRootMetadataKey] = id.value().AsLowercaseString();
  const BookmarkNode* folder = model->AddFolder(managed, 0, u"Policy", &meta);
  SavedEntryCatalog catalog(*model);
  const std::array bindings = {SpaceRootBinding{
      id, {BookmarkStorage::kLocalOrSyncable, folder->uuid()}}};
  ASSERT_TRUE(catalog.SetSpaceRoots(bindings).has_value());
  EXPECT_EQ(catalog.GetSnapshot().spaces[0].status,
            CatalogRootStatus::kNotUserFolder);
  EXPECT_TRUE(catalog.GetSnapshot().spaces[0].rows.empty());
}

TEST_F(SavedEntryCatalogTest, ReflectsBookmarkAuthorityAndFolderOrder) {
  const SpaceId id = NewSpace();
  const BookmarkNode* root = Root(id);
  const BookmarkNode* folder = model_->AddFolder(root, 0, u"Folder");
  const EntryId nested_id = NewEntry();
  const BookmarkNode* nested = Entry(folder, nested_id, u"Nested");
  const BookmarkNode* sibling = Entry(root, NewEntry(), u"Sibling");
  Configure({Binding(id, root)});
  ASSERT_EQ(space().rows.size(), 3u);
  EXPECT_EQ(space().rows[0].status, CatalogEntryStatus::kFolder);
  EXPECT_FALSE(space().rows[0].url);
  EXPECT_FALSE(space().rows[0].entry_id);
  EXPECT_EQ(space().rows[1].entry_id, nested_id);
  EXPECT_EQ(space().rows[1].locator.uuid, nested->uuid());
  EXPECT_EQ(space().rows[1].parent.uuid, folder->uuid());
  EXPECT_EQ(space().rows[1].depth, 1u);
  EXPECT_EQ(space().rows[1].sibling_index, 0u);
  EXPECT_EQ(space().rows[2].locator.uuid, sibling->uuid());
  EXPECT_EQ(space().rows[2].depth, 0u);
  EXPECT_EQ(space().rows[2].sibling_index, 1u);
  const uint64_t generation = snapshot().generation;
  model_->SetTitle(nested, u"Renamed by extension", kEdit);
  model_->SetURL(nested, GURL("https://changed.example/new"), kEdit);
  EXPECT_EQ(space().rows[1].title, u"Renamed by extension");
  EXPECT_EQ(space().rows[1].url, GURL("https://changed.example/new"));
  EXPECT_EQ(space().rows[1].entry_id, nested_id);
  EXPECT_GT(snapshot().generation, generation);
}

TEST_F(SavedEntryCatalogTest, RetainsUnbindableRowsAndDoesNotRepairMetadata) {
  const SpaceId id = NewSpace();
  const BookmarkNode* root = Root(id);
  const BookmarkNode* missing = model_->AddURL(
      root, 0, u"Missing", GURL("https://missing.example/"));
  const BookmarkNode* invalid = Entry(root, NewEntry(), u"Invalid");
  model_->SetNodeMetaInfo(invalid, kSavedEntryMetadataKey, "NOT-A-UUID");
  Configure({Binding(id, root)});
  ASSERT_EQ(space().rows.size(), 2u);
  EXPECT_EQ(space().rows[0].status, CatalogEntryStatus::kMissingId);
  EXPECT_EQ(space().rows[0].locator.uuid, missing->uuid());
  EXPECT_EQ(space().rows[0].url, missing->url());
  EXPECT_FALSE(space().rows[0].entry_id);
  EXPECT_EQ(space().rows[1].status, CatalogEntryStatus::kInvalidId);
  EXPECT_EQ(space().rows[1].locator.uuid, invalid->uuid());
  EXPECT_FALSE(space().rows[1].entry_id);
  std::string value;
  EXPECT_FALSE(missing->GetMetaInfo(kSavedEntryMetadataKey, &value));
  ASSERT_TRUE(invalid->GetMetaInfo(kSavedEntryMetadataKey, &value));
  EXPECT_EQ(value, "NOT-A-UUID");
  const EntryId adopted = NewEntry();
  model_->SetNodeMetaInfo(missing, kSavedEntryMetadataKey,
                          adopted.value().AsLowercaseString());
  EXPECT_EQ(space().rows[0].entry_id, adopted);
  model_->DeleteNodeMetaInfo(missing, kSavedEntryMetadataKey);
  EXPECT_EQ(space().rows[0].status, CatalogEntryStatus::kMissingId);
}

TEST_F(SavedEntryCatalogTest, DuplicateEntryIdsAreUnbindableAcrossSpaces) {
  const SpaceId first = NewSpace();
  const SpaceId second = NewSpace();
  const BookmarkNode* a = Root(first);
  const BookmarkNode* b = Root(second);
  const EntryId duplicate = NewEntry();
  const BookmarkNode* one = Entry(a, duplicate);
  const BookmarkNode* two = Entry(b, duplicate);
  Configure({Binding(first, a), Binding(second, b)});
  for (const auto& scope : snapshot().spaces) {
    ASSERT_EQ(scope.rows.size(), 1u);
    EXPECT_EQ(scope.rows[0].status, CatalogEntryStatus::kDuplicateId);
    EXPECT_FALSE(scope.rows[0].entry_id);
  }
  EXPECT_EQ(space(0).rows[0].locator.uuid, one->uuid());
  EXPECT_EQ(space(1).rows[0].locator.uuid, two->uuid());
  std::string value;
  ASSERT_TRUE(two->GetMetaInfo(kSavedEntryMetadataKey, &value));
  EXPECT_EQ(value, duplicate.value().AsLowercaseString());
  model_->Remove(two, kEdit, FROM_HERE);
  EXPECT_EQ(space(0).rows[0].entry_id, duplicate);
  EXPECT_EQ(space(0).rows[0].status, CatalogEntryStatus::kReady);
  EXPECT_TRUE(space(1).rows.empty());
}

TEST_F(SavedEntryCatalogTest, SameSpaceDuplicateBecomesValidAfterCorrection) {
  const SpaceId id = NewSpace();
  const BookmarkNode* root = Root(id);
  const EntryId duplicate = NewEntry();
  Entry(root, duplicate);
  const BookmarkNode* copy = Entry(root, duplicate);
  Configure({Binding(id, root)});
  EXPECT_EQ(space().rows[0].status, CatalogEntryStatus::kDuplicateId);
  EXPECT_EQ(space().rows[1].status, CatalogEntryStatus::kDuplicateId);
  const EntryId corrected = NewEntry();
  model_->SetNodeMetaInfo(copy, kSavedEntryMetadataKey,
                          corrected.value().AsLowercaseString());
  EXPECT_EQ(space().rows[0].entry_id, duplicate);
  EXPECT_EQ(space().rows[1].entry_id, corrected);
}

TEST_F(SavedEntryCatalogTest, MovesInheritDestinationSpaceAndOutsideLeavesIndex) {
  const SpaceId first = NewSpace();
  const SpaceId second = NewSpace();
  const BookmarkNode* a = Root(first);
  const BookmarkNode* b = Root(second);
  const BookmarkNode* folder = model_->AddFolder(a, 0, u"Moved folder");
  const EntryId entry = NewEntry();
  Entry(folder, entry);
  Configure({Binding(first, a), Binding(second, b)});
  model_->Move(folder, b, 0);
  EXPECT_TRUE(space(0).rows.empty());
  ASSERT_EQ(space(1).rows.size(), 2u);
  EXPECT_EQ(space(1).rows[1].entry_id, entry);
  EXPECT_EQ(space(1).rows[0].parent.uuid, b->uuid());
  model_->Move(folder, model_->bookmark_bar_node(), 0);
  EXPECT_TRUE(space(1).rows.empty());
  EXPECT_EQ(folder->children().size(), 1u);
}

TEST_F(SavedEntryCatalogTest, AncestorRemovalInvalidatesAllDescendants) {
  const SpaceId id = NewSpace();
  const BookmarkNode* root = Root(id);
  const BookmarkNode* ancestor = model_->AddFolder(root, 0, u"Ancestor");
  const BookmarkNode* nested = model_->AddFolder(ancestor, 0, u"Nested");
  Entry(nested, NewEntry());
  Entry(ancestor, NewEntry());
  Configure({Binding(id, root)});
  ASSERT_EQ(space().rows.size(), 4u);
  model_->Remove(ancestor, kEdit, FROM_HERE);
  EXPECT_TRUE(space().rows.empty());
  EXPECT_EQ(space().status, CatalogRootStatus::kAvailable);
}

TEST_F(SavedEntryCatalogTest, ReorderUpdatesNativeSiblingIndicesAndTraversal) {
  const SpaceId id = NewSpace();
  const BookmarkNode* root = Root(id);
  const BookmarkNode* folder = model_->AddFolder(root, 0, u"Folder");
  const BookmarkNode* child = Entry(folder, NewEntry());
  const BookmarkNode* first = Entry(root, NewEntry());
  const BookmarkNode* second = Entry(root, NewEntry());
  Configure({Binding(id, root)});
  model_->ReorderChildren(root, {second, folder, first});
  ASSERT_EQ(space().rows.size(), 4u);
  EXPECT_EQ(space().rows[0].locator.uuid, second->uuid());
  EXPECT_EQ(space().rows[0].sibling_index, 0u);
  EXPECT_EQ(space().rows[1].locator.uuid, folder->uuid());
  EXPECT_EQ(space().rows[1].sibling_index, 1u);
  EXPECT_EQ(space().rows[2].locator.uuid, child->uuid());
  EXPECT_EQ(space().rows[2].depth, 1u);
  EXPECT_EQ(space().rows[3].locator.uuid, first->uuid());
  EXPECT_EQ(space().rows[3].sibling_index, 2u);
}

TEST_F(SavedEntryCatalogTest, DeletedRootNeverAdoptsAnotherMatchingMarker) {
  const SpaceId id = NewSpace();
  const BookmarkNode* root = Root(id);
  Entry(root, NewEntry());
  const SpaceRootBinding original = Binding(id, root);
  Configure({original});
  model_->Remove(root, kEdit, FROM_HERE);
  const BookmarkNode* replacement = Root(id);
  Entry(replacement, NewEntry());
  EXPECT_EQ(space().binding, original);
  EXPECT_EQ(space().status, CatalogRootStatus::kMissing);
  EXPECT_TRUE(space().rows.empty());
  EXPECT_NE(space().binding.root.uuid, replacement->uuid());
  Configure({Binding(id, replacement)});
  EXPECT_EQ(space().status, CatalogRootStatus::kAvailable);
  EXPECT_EQ(space().rows.size(), 1u);
}

TEST_F(SavedEntryCatalogTest, RootMarkerMismatchIsDistinctFromMissingRoot) {
  const SpaceId id = NewSpace();
  const BookmarkNode* root = Root(id);
  Entry(root, NewEntry());
  Configure({Binding(id, root)});
  model_->SetNodeMetaInfo(root, kSpaceRootMetadataKey, "invalid");
  EXPECT_EQ(space().status, CatalogRootStatus::kMetadataMismatch);
  EXPECT_TRUE(space().rows.empty());
  model_->SetNodeMetaInfo(root, kSpaceRootMetadataKey,
                          id.value().AsLowercaseString());
  EXPECT_EQ(space().status, CatalogRootStatus::kAvailable);
  EXPECT_EQ(space().rows.size(), 1u);
  model_->SetTitle(root, u"Root renamed independently", kEdit);
  EXPECT_EQ(space().status, CatalogRootStatus::kAvailable);
}

TEST_F(SavedEntryCatalogTest, NestedConfiguredRootsDoNotAdoptEachOthersEntries) {
  const SpaceId first = NewSpace();
  const SpaceId second = NewSpace();
  const BookmarkNode* a = Root(first);
  const BookmarkNode* b = Root(second, a);
  Entry(b, NewEntry());
  Configure({Binding(first, a), Binding(second, b)});
  EXPECT_EQ(space(0).status, CatalogRootStatus::kOverlappingRoots);
  EXPECT_EQ(space(1).status, CatalogRootStatus::kOverlappingRoots);
  EXPECT_TRUE(space(0).rows.empty());
  EXPECT_TRUE(space(1).rows.empty());
  model_->SetNodeMetaInfo(b, kSpaceRootMetadataKey, "temporarily malformed");
  EXPECT_EQ(space(0).status, CatalogRootStatus::kOverlappingRoots);
  model_->Move(b, model_->other_node(), 1);
  EXPECT_EQ(space(0).status, CatalogRootStatus::kAvailable);
  EXPECT_EQ(space(1).status, CatalogRootStatus::kMetadataMismatch);
  EXPECT_TRUE(space(0).rows.empty());
}

TEST_F(SavedEntryCatalogTest, StorageIsPartOfRootAndRowIdentity) {
  model_->CreateAccountPermanentFolders();
  const base::Uuid shared = base::Uuid::GenerateRandomV4();
  const SpaceId local_id = NewSpace();
  const SpaceId account_id = NewSpace();
  const BookmarkNode* local = Root(local_id, nullptr, shared);
  const BookmarkNode* account =
      Root(account_id, model_->account_other_node(), shared);
  Entry(local, NewEntry(), u"Local");
  Entry(account, NewEntry(), u"Account");
  Configure({Binding(local_id, local),
             Binding(account_id, account, BookmarkStorage::kAccount)});
  ASSERT_EQ(space(0).rows.size(), 1u);
  ASSERT_EQ(space(1).rows.size(), 1u);
  EXPECT_EQ(space(0).rows[0].title, u"Local");
  EXPECT_EQ(space(1).rows[0].title, u"Account");
  EXPECT_EQ(space(1).rows[0].locator.storage, BookmarkStorage::kAccount);
  model_->Remove(local, kEdit, FROM_HERE);
  EXPECT_EQ(space(0).status, CatalogRootStatus::kMissing);
  EXPECT_EQ(space(1).status, CatalogRootStatus::kAvailable);
}

TEST_F(SavedEntryCatalogTest, ExtensiveChangesHideStaleRowsUntilOuterBatchEnds) {
  const SpaceId id = NewSpace();
  const BookmarkNode* root = Root(id);
  const BookmarkNode* entry = Entry(root, NewEntry());
  Configure({Binding(id, root)});
  int changes = 0;
  auto subscription = catalog_.ObserveChanges(
      base::BindRepeating([](int* count) { ++*count; }, &changes));
  model_->BeginExtensiveChanges();
  EXPECT_EQ(snapshot().readiness, CatalogReadiness::kUpdating);
  EXPECT_TRUE(space().rows.empty());
  EXPECT_EQ(changes, 1);
  model_->BeginExtensiveChanges();
  model_->Remove(entry, kEdit, FROM_HERE);
  Entry(root, NewEntry(), u"After batch");
  model_->EndExtensiveChanges();
  EXPECT_EQ(snapshot().readiness, CatalogReadiness::kUpdating);
  EXPECT_EQ(changes, 1);
  model_->EndExtensiveChanges();
  EXPECT_EQ(snapshot().readiness, CatalogReadiness::kReady);
  EXPECT_EQ(changes, 2);
  ASSERT_EQ(space().rows.size(), 1u);
  EXPECT_EQ(space().rows[0].title, u"After batch");
}

TEST_F(SavedEntryCatalogTest, RemoveAllAndModelDestructionClearPublishedRows) {
  const SpaceId id = NewSpace();
  const BookmarkNode* root = Root(id);
  Entry(root, NewEntry());
  Configure({Binding(id, root)});
  model_->RemoveAllUserBookmarks(FROM_HERE);
  EXPECT_EQ(snapshot().readiness, CatalogReadiness::kReady);
  EXPECT_EQ(space().status, CatalogRootStatus::kMissing);
  EXPECT_TRUE(space().rows.empty());
  model_.reset();
  EXPECT_EQ(snapshot().readiness, CatalogReadiness::kModelDeleted);
  EXPECT_EQ(space().status, CatalogRootStatus::kPending);
  EXPECT_TRUE(space().rows.empty());
}

TEST_F(SavedEntryCatalogTest, InvalidConfigurationCannotReplaceValidSnapshot) {
  const SpaceId id = NewSpace();
  const BookmarkNode* root = Root(id);
  const SpaceRootBinding binding = Binding(id, root);
  Configure({binding});
  const SavedCatalogSnapshot before = snapshot();
  const std::array duplicates = {binding, binding};
  auto result = catalog_.SetSpaceRoots(duplicates);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), CatalogConfigError::kDuplicateBinding);
  auto invalid = std::array{binding};
  invalid[0].space_id = SpaceId(base::Uuid());
  EXPECT_EQ(catalog_.SetSpaceRoots(invalid).error(), CatalogConfigError::kInvalidId);
  invalid[0] = binding;
  invalid[0].root.storage = static_cast<BookmarkStorage>(99);
  EXPECT_EQ(catalog_.SetSpaceRoots(invalid).error(),
            CatalogConfigError::kInvalidStorage);
  EXPECT_EQ(snapshot().generation, before.generation);
  EXPECT_EQ(snapshot().spaces, before.spaces);
}

TEST_F(SavedEntryCatalogTest, UnrelatedBookmarksDoNotInvalidateSnapshot) {
  const SpaceId id = NewSpace();
  const BookmarkNode* root = Root(id);
  Entry(root, NewEntry());
  Configure({Binding(id, root)});
  const uint64_t generation = snapshot().generation;
  int changes = 0;
  auto subscription = catalog_.ObserveChanges(
      base::BindRepeating([](int* count) { ++*count; }, &changes));
  const BookmarkNode* unrelated = Entry(model_->bookmark_bar_node(), NewEntry());
  model_->SetTitle(unrelated, u"Unrelated edit", kEdit);
  model_->Remove(unrelated, kEdit, FROM_HERE);
  EXPECT_EQ(snapshot().generation, generation);
  EXPECT_EQ(changes, 0);
}

TEST_F(SavedEntryCatalogTest, PermanentAndUrlRootsAreUnavailable) {
  const SpaceId id = NewSpace();
  Configure({Binding(id, model_->other_node())});
  EXPECT_EQ(space().status, CatalogRootStatus::kNotUserFolder);
  const BookmarkNode* url = Entry(model_->other_node(), NewEntry());
  Configure({Binding(id, url)});
  EXPECT_EQ(space().status, CatalogRootStatus::kNotUserFolder);
  EXPECT_TRUE(space().rows.empty());
}

}  // namespace
}  // namespace openarc::workspace
