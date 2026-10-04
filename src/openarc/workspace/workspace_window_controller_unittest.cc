// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/workspace_window_controller.h"

#include <array>
#include <map>
#include <memory>
#include <utility>

#include "base/test/bind.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/test_tab_strip_model_delegate.h"
#include "chrome/test/base/testing_profile.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/common/bookmark_metrics.h"
#include "components/bookmarks/test/test_bookmark_client.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "openarc/workspace/workspace_tab_state.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace openarc::workspace {
namespace {

class DeferredCloseDelegate : public TestTabStripModelDelegate {
 public:
  void CloseTab(const tabs::TabInterface* tab,
                CloseTabSource,
                base::OnceCallback<void(CloseTabSource)>) override {
    requested_close = tab->GetHandle();
  }
  tabs::TabHandle requested_close;
};

class SelectionReasonObserver : public TabStripModelObserver {
 public:
  void OnTabStripModelChanged(TabStripModel*,
                              const TabStripModelChange&,
                              const TabStripSelectionChange& selection) override {
    if (selection.active_tab_changed()) {
      reason = selection.reason;
    }
  }
  int reason = CHANGE_REASON_NONE;
};

class WorkspaceWindowControllerTest : public testing::Test {
 protected:
  void SetUp() override {
    model_ = bookmarks::TestBookmarkClient::CreateModel();
    catalog_ = std::make_unique<SavedEntryCatalog>(*model_);
    bookmarks::BookmarkNode::MetaInfoMap metadata;
    metadata[kSpaceRootMetadataKey] = space_.value().AsLowercaseString();
    root_ = model_->AddFolder(model_->other_node(), 0, u"Default", &metadata);
    const std::array roots = {SpaceRootBinding{
        space_, {BookmarkStorage::kLocalOrSyncable, root_->uuid()}}};
    ASSERT_TRUE(catalog_->SetSpaceRoots(roots).has_value());
    strip_ = std::make_unique<TabStripModel>(&delegate_, &profile_);
    controller_ = CreateController(*strip_);
  }

  void TearDown() override {
    controller_.reset();
    // This fixture owns the features which production TabFeatures will own.
    // Destroy them while their real tabs/user-data hosts are still alive.
    states_.clear();
    strip_.reset();
    catalog_.reset();
    root_ = nullptr;
    model_.reset();
  }

  std::unique_ptr<WorkspaceWindowController> CreateController(
      TabStripModel& strip) {
    return std::make_unique<WorkspaceWindowController>(
        strip, *catalog_, space_,
        base::BindLambdaForTesting([this, &strip](const GURL& url) {
          ++creations_;
          last_created_url_ = url;
          return AddTab(strip, url);
        }));
  }

  tabs::TabHandle AddTab(TabStripModel& strip,
                         const GURL& url = GURL("https://ordinary.example/")) {
    auto contents = content::WebContentsTester::CreateTestWebContents(
        strip.profile(), nullptr);
    content::WebContentsTester::For(contents.get())->NavigateAndCommit(url);
    auto tab = std::make_unique<tabs::TabModel>(std::move(contents), &strip);
    const auto handle = tab->GetHandle();
    states_.emplace(handle, std::make_unique<WorkspaceTabState>(*tab));
    strip.AppendTab(std::move(tab), true);
    return handle;
  }

  const bookmarks::BookmarkNode* AddSaved(
      EntryId id,
      const GURL& url = GURL("https://saved.example/")) {
    bookmarks::BookmarkNode::MetaInfoMap metadata;
    metadata[kSavedEntryMetadataKey] = id.value().AsLowercaseString();
    return model_->AddURL(root_, root_->children().size(), u"Saved", url,
                          &metadata);
  }

  BookmarkLocator Locator(const bookmarks::BookmarkNode* node) {
    return {BookmarkStorage::kLocalOrSyncable, node->uuid()};
  }

  void RemoveBookmark(const bookmarks::BookmarkNode* node) {
    model_->Remove(node, bookmarks::metrics::BookmarkEditSource::kExtension,
                   FROM_HERE);
  }

  void ConfirmRemoval(tabs::TabHandle handle) {
    auto detached =
        strip_->DetachTabAtForInsertion(strip_->GetIndexOfTab(handle.Get()));
    states_.erase(handle);
  }

  content::BrowserTaskEnvironment task_environment_;
  content::RenderViewHostTestEnabler renderer_test_enabler_;
  TestingProfile profile_;
  const tabs::TabModel::PreventFeatureInitializationForTesting prevent_features_;
  DeferredCloseDelegate delegate_;
  SpaceId space_{base::Uuid::GenerateRandomV4()};
  EntryId entry_{base::Uuid::GenerateRandomV4()};
  std::unique_ptr<bookmarks::BookmarkModel> model_;
  raw_ptr<const bookmarks::BookmarkNode> root_ = nullptr;
  std::unique_ptr<SavedEntryCatalog> catalog_;
  std::map<tabs::TabHandle, std::unique_ptr<WorkspaceTabState>> states_;
  std::unique_ptr<TabStripModel> strip_;
  std::unique_ptr<WorkspaceWindowController> controller_;
  int creations_ = 0;
  GURL last_created_url_;
};

TEST_F(WorkspaceWindowControllerTest, ActivatesLoadedEntryWithoutNavigation) {
  const auto* bookmark = AddSaved(entry_);
  const GURL saved_url = bookmark->url();
  EXPECT_EQ(controller_->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kCreatedAndBound);
  const auto saved = controller_->FindSavedInstance(entry_);
  ASSERT_NE(saved.Get(), nullptr);
  const GURL navigated("https://saved.example/another-page");
  content::WebContentsTester::For(saved.Get()->GetContents())
      ->NavigateAndCommit(navigated);
  AddTab(*strip_);
  SelectionReasonObserver selection;
  strip_->AddObserver(&selection);
  EXPECT_EQ(controller_->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kActivated);
  EXPECT_TRUE(selection.reason &
              TabStripModelObserver::CHANGE_REASON_USER_GESTURE);
  strip_->RemoveObserver(&selection);
  EXPECT_EQ(creations_, 1);
  EXPECT_EQ(strip_->GetActiveTab(), saved.Get());
  EXPECT_EQ(saved.Get()->GetContents()->GetLastCommittedURL(), navigated);
  EXPECT_EQ(bookmark->url(), saved_url);
  EXPECT_EQ(bookmark->GetTitle(), u"Saved");
  EXPECT_FALSE(controller_->ShouldShowOrdinaryTab(saved));
}

TEST_F(WorkspaceWindowControllerTest, BindingExistingTabPreservesNativePinAndOrder) {
  const auto* bookmark = AddSaved(entry_);
  const auto saved = AddTab(*strip_);
  const auto ordinary = AddTab(*strip_);
  strip_->SetTabPinned(0, true);
  const auto* contents = saved.Get()->GetContents();
  EXPECT_EQ(controller_->BindSavedTab(saved, entry_, Locator(bookmark)),
            WorkspaceWindowCommandResult::kBound);
  EXPECT_EQ(strip_->count(), 2);
  EXPECT_EQ(strip_->GetTabAtIndex(0), saved.Get());
  EXPECT_EQ(strip_->GetTabAtIndex(1), ordinary.Get());
  EXPECT_TRUE(saved.Get()->IsPinned());
  EXPECT_EQ(saved.Get()->GetContents(), contents);
  EXPECT_EQ(creations_, 0);
}

TEST_F(WorkspaceWindowControllerTest, BindingDoesNotChangeNativeGroup) {
  const auto* bookmark = AddSaved(entry_);
  const auto saved = AddTab(*strip_);
  AddTab(*strip_);
  const auto group = strip_->AddToNewGroup({0, 1});
  EXPECT_EQ(controller_->BindSavedTab(saved, entry_, Locator(bookmark)),
            WorkspaceWindowCommandResult::kBound);
  EXPECT_EQ(saved.Get()->GetGroup(), group);
  EXPECT_EQ(strip_->GetTabGroupForTab(1), group);
}

TEST_F(WorkspaceWindowControllerTest, CloseRequestRetainsBindingUntilRemoval) {
  const auto* bookmark = AddSaved(entry_);
  const auto locator = Locator(bookmark);
  ASSERT_EQ(controller_->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kCreatedAndBound);
  const auto saved = controller_->FindSavedInstance(entry_);
  EXPECT_EQ(controller_->CloseSavedInstance(entry_),
            WorkspaceWindowCommandResult::kCloseRequested);
  EXPECT_EQ(delegate_.requested_close, saved);
  EXPECT_EQ(controller_->FindSavedInstance(entry_), saved);
  EXPECT_EQ(states_.at(saved)->GetAssociation()->entry_id, entry_);
  EXPECT_EQ(strip_->count(), 1);
  ConfirmRemoval(saved);
  EXPECT_EQ(controller_->FindSavedInstance(entry_), tabs::TabHandle());
  EXPECT_EQ(catalog_->GetSnapshot().spaces[0].rows[0].locator, locator);
}

TEST_F(WorkspaceWindowControllerTest, BookmarkRemovalDemotesWithoutClosingPage) {
  const auto* bookmark = AddSaved(entry_);
  ASSERT_EQ(controller_->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kCreatedAndBound);
  const auto saved = controller_->FindSavedInstance(entry_);
  const auto* contents = saved.Get()->GetContents();
  RemoveBookmark(bookmark);
  EXPECT_EQ(strip_->count(), 1);
  EXPECT_EQ(saved.Get()->GetContents(), contents);
  EXPECT_TRUE(controller_->ShouldShowOrdinaryTab(saved));
  ASSERT_TRUE(states_.at(saved)->GetAssociation());
  EXPECT_EQ(states_.at(saved)->GetAssociation()->space_id, space_);
  EXPECT_FALSE(states_.at(saved)->GetAssociation()->entry_id);
}

TEST_F(WorkspaceWindowControllerTest, ChangedSavedUrlIsUsedOnlyForNextInstance) {
  const auto* bookmark = AddSaved(entry_);
  ASSERT_EQ(controller_->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kCreatedAndBound);
  const auto saved = controller_->FindSavedInstance(entry_);
  const GURL original = saved.Get()->GetContents()->GetLastCommittedURL();
  const GURL changed("https://changed.example/");
  model_->SetURL(bookmark, changed,
                 bookmarks::metrics::BookmarkEditSource::kExtension);
  EXPECT_EQ(saved.Get()->GetContents()->GetLastCommittedURL(), original);
  ConfirmRemoval(saved);
  EXPECT_EQ(controller_->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kCreatedAndBound);
  EXPECT_EQ(last_created_url_, changed);
}

TEST_F(WorkspaceWindowControllerTest, BookmarkBatchDoesNotEraseValidBinding) {
  AddSaved(entry_);
  ASSERT_EQ(controller_->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kCreatedAndBound);
  const auto saved = controller_->FindSavedInstance(entry_);
  model_->BeginExtensiveChanges();
  EXPECT_EQ(controller_->GetSnapshot().readiness,
            WorkspaceWindowReadiness::kWaitingForCatalog);
  EXPECT_TRUE(controller_->ShouldShowOrdinaryTab(saved));
  EXPECT_EQ(states_.at(saved)->GetAssociation()->entry_id, entry_);
  EXPECT_EQ(controller_->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kNotReady);
  model_->EndExtensiveChanges();
  EXPECT_EQ(controller_->FindSavedInstance(entry_), saved);
  EXPECT_FALSE(controller_->ShouldShowOrdinaryTab(saved));
}

TEST_F(WorkspaceWindowControllerTest, DestinationClaimWinsTransferCollision) {
  AddSaved(entry_);
  ASSERT_EQ(controller_->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kCreatedAndBound);
  const auto existing = controller_->FindSavedInstance(entry_);
  auto other_strip = std::make_unique<TabStripModel>(&delegate_, &profile_);
  auto other_controller = CreateController(*other_strip);
  ASSERT_EQ(other_controller->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kCreatedAndBound);
  const auto incoming = other_controller->FindSavedInstance(entry_);
  auto transfer = other_strip->DetachTabAtForInsertion(0);
  EXPECT_EQ(states_.at(incoming)->GetAssociation()->entry_id, entry_);
  EXPECT_EQ(other_controller->FindSavedInstance(entry_), tabs::TabHandle());
  strip_->InsertDetachedTabAt(0, std::move(transfer), AddTabTypes::ADD_ACTIVE);
  EXPECT_EQ(controller_->FindSavedInstance(entry_), existing);
  EXPECT_FALSE(states_.at(incoming)->GetAssociation()->entry_id);
  EXPECT_TRUE(controller_->ShouldShowOrdinaryTab(incoming));
  EXPECT_EQ(strip_->count(), 2);
  other_controller.reset();
  other_strip.reset();
}

TEST_F(WorkspaceWindowControllerTest, StaleSaveCompletionDoesNotBindNewBookmark) {
  const auto* old = AddSaved(entry_);
  const auto locator = Locator(old);
  const auto ordinary = AddTab(*strip_);
  RemoveBookmark(old);
  AddSaved(entry_);
  EXPECT_EQ(controller_->BindSavedTab(ordinary, entry_, locator),
            WorkspaceWindowCommandResult::kMissingEntry);
  EXPECT_TRUE(controller_->ShouldShowOrdinaryTab(ordinary));
  EXPECT_EQ(controller_->FindSavedInstance(entry_), tabs::TabHandle());
}

TEST_F(WorkspaceWindowControllerTest, NestedActivationDoesNotCreateSecondTab) {
  AddSaved(entry_);
  controller_.reset();
  WorkspaceWindowCommandResult nested = WorkspaceWindowCommandResult::kNotReady;
  controller_ = std::make_unique<WorkspaceWindowController>(
      *strip_, *catalog_, space_,
      base::BindLambdaForTesting([&](const GURL& url) {
        nested = controller_->ActivateSaved(entry_);
        return AddTab(*strip_, url);
      }));
  EXPECT_EQ(controller_->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kCreatedAndBound);
  EXPECT_EQ(nested, WorkspaceWindowCommandResult::kBusy);
  EXPECT_EQ(strip_->count(), 1);
}

TEST_F(WorkspaceWindowControllerTest, CreationCannotClaimAnExistingPage) {
  AddSaved(entry_);
  const auto ordinary = AddTab(*strip_);
  controller_.reset();
  controller_ = std::make_unique<WorkspaceWindowController>(
      *strip_, *catalog_, space_,
      base::BindLambdaForTesting([&](const GURL&) { return ordinary; }));
  EXPECT_EQ(controller_->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kCreationFailed);
  EXPECT_TRUE(controller_->ShouldShowOrdinaryTab(ordinary));
}

TEST_F(WorkspaceWindowControllerTest, PrivateWindowDoesNotUseRegularCatalog) {
  Profile* private_profile = profile_.GetPrimaryOTRProfile(true);
  TabStripModel private_strip(&delegate_, private_profile);
  WorkspaceWindowController private_controller(
      private_strip, *catalog_, space_,
      base::BindLambdaForTesting([](const GURL&) {
        ADD_FAILURE() << "Private navigation callback must not run";
        return tabs::TabHandle();
      }));
  AddSaved(entry_);
  EXPECT_EQ(private_controller.GetSnapshot().readiness,
            WorkspaceWindowReadiness::kDisabled);
  EXPECT_EQ(private_controller.GetSnapshot().generation, 0u);
  EXPECT_EQ(private_controller.ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kNotReady);
}

TEST_F(WorkspaceWindowControllerTest, CreationCallbackMayDestroyController) {
  AddSaved(entry_);
  controller_.reset();
  controller_ = std::make_unique<WorkspaceWindowController>(
      *strip_, *catalog_, space_,
      base::BindLambdaForTesting([&](const GURL&) {
        controller_.reset();
        return tabs::TabHandle();
      }));
  EXPECT_EQ(controller_->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kNotReady);
  EXPECT_EQ(controller_, nullptr);
}

TEST_F(WorkspaceWindowControllerTest, ProjectionObserverMayDestroyController) {
  AddSaved(entry_);
  bool later_called = false;
  auto destroy = controller_->ObserveChanges(
      base::BindLambdaForTesting([&] { controller_.reset(); }));
  auto later = controller_->ObserveChanges(
      base::BindLambdaForTesting([&] { later_called = true; }));
  EXPECT_EQ(controller_->ActivateSaved(entry_),
            WorkspaceWindowCommandResult::kNotReady);
  EXPECT_EQ(controller_, nullptr);
  EXPECT_FALSE(later_called);
}

TEST_F(WorkspaceWindowControllerTest, StateObserverMayDestroyStripDuringBinding) {
  const auto* bookmark = AddSaved(entry_);
  const auto saved = AddTab(*strip_);
  auto destroy = states_.at(saved)->ObserveChanges(base::BindLambdaForTesting([&] {
    states_.clear();
    strip_.reset();
  }));
  EXPECT_EQ(controller_->BindSavedTab(saved, entry_, Locator(bookmark)),
            WorkspaceWindowCommandResult::kNotReady);
  EXPECT_EQ(controller_->GetSnapshot().readiness,
            WorkspaceWindowReadiness::kTabStripDestroyed);
  EXPECT_TRUE(controller_->GetSnapshot().tabs.empty());
}

}  // namespace
}  // namespace openarc::workspace
