// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/workspace_tab_state.h"

#include <memory>

#include "base/test/bind.h"
#include "components/tabs/public/mock_tab_interface.h"
#include "content/public/test/test_renderer_host.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace openarc::workspace {

class WorkspaceTabStateTestPeer {
 public:
  static bool Set(WorkspaceTabState& state,
                  std::optional<TabWorkspaceBinding> association) {
    return state.SetAssociation(std::move(association));
  }
};

class WorkspaceTabStateTest : public content::RenderViewHostTestHarness {
 public:
  void SetUp() override {
    RenderViewHostTestHarness::SetUp();
    ON_CALL(tab_, GetContents()).WillByDefault(testing::Return(web_contents()));
    ON_CALL(tab_, RegisterWillDiscardContents(testing::_))
        .WillByDefault([this](tabs::TabInterface::WillDiscardContentsCallback cb) {
          return discards_.Add(std::move(cb));
        });
    ON_CALL(tab_, RegisterWillDetach(testing::_))
        .WillByDefault([this](tabs::TabInterface::WillDetach cb) {
          return detaches_.Add(std::move(cb));
        });
    state_ = std::make_unique<WorkspaceTabState>(tab_);
  }

  void TearDown() override {
    state_.reset();
    RenderViewHostTestHarness::TearDown();
  }

 protected:
  TabWorkspaceBinding SavedBinding() const {
    return {SpaceId(base::Uuid::ParseLowercase(
                "00000000-0000-4000-8000-000000000001")),
            EntryId(base::Uuid::ParseLowercase(
                "00000000-0000-4000-8000-000000000002"))};
  }

  tabs::MockTabInterface tab_;
  base::RepeatingCallbackList<void(tabs::TabInterface*, content::WebContents*,
                                 content::WebContents*)>
      discards_;
  base::RepeatingCallbackList<void(tabs::TabInterface*,
                                 tabs::TabInterface::DetachReason)>
      detaches_;
  std::unique_ptr<WorkspaceTabState> state_;
};

TEST_F(WorkspaceTabStateTest, NavigationRetainsSavedAssociation) {
  const auto binding = SavedBinding();
  ASSERT_TRUE(WorkspaceTabStateTestPeer::Set(*state_, binding));
  NavigateAndCommit(GURL("https://example.com/saved"));
  NavigateAndCommit(GURL("https://example.com/navigated"));
  EXPECT_EQ(state_->GetAssociation(), binding);
  EXPECT_EQ(state_->web_contents(), web_contents());
}

TEST_F(WorkspaceTabStateTest, DiscardChangesObservedContentsNotBinding) {
  const auto binding = SavedBinding();
  ASSERT_TRUE(WorkspaceTabStateTestPeer::Set(*state_, binding));
  auto replacement = CreateTestWebContents();
  discards_.Notify(&tab_, web_contents(), replacement.get());
  EXPECT_EQ(state_->web_contents(), replacement.get());
  EXPECT_EQ(state_->GetAssociation(), binding);
  // Destroy the observer before its replacement contents in this fixture.
  state_.reset();
}

TEST_F(WorkspaceTabStateTest, RequestedDeletionDoesNotClearBinding) {
  const auto binding = SavedBinding();
  ASSERT_TRUE(WorkspaceTabStateTestPeer::Set(*state_, binding));
  // WillDetach(kDelete) can precede a canceled beforeunload. Confirmed strip
  // removal belongs to the future window controller; it is not this signal.
  detaches_.Notify(&tab_, tabs::TabInterface::DetachReason::kDelete);
  EXPECT_EQ(state_->GetAssociation(), binding);
}

TEST_F(WorkspaceTabStateTest, DemotionRetainsSpaceAndNotifiesAfterCommit) {
  auto binding = SavedBinding();
  ASSERT_TRUE(WorkspaceTabStateTestPeer::Set(*state_, binding));
  binding.entry_id.reset();
  int notifications = 0;
  auto subscription = state_->ObserveChanges(base::BindLambdaForTesting([&] {
    ++notifications;
    EXPECT_EQ(state_->GetAssociation(), binding);
  }));
  ASSERT_TRUE(WorkspaceTabStateTestPeer::Set(*state_, binding));
  ASSERT_TRUE(WorkspaceTabStateTestPeer::Set(*state_, binding));
  EXPECT_EQ(notifications, 1);
}

TEST_F(WorkspaceTabStateTest, InvalidUpdatePreservesExistingBinding) {
  const auto binding = SavedBinding();
  ASSERT_TRUE(WorkspaceTabStateTestPeer::Set(*state_, binding));
  auto invalid = binding;
  invalid.space_id = SpaceId();
  EXPECT_FALSE(WorkspaceTabStateTestPeer::Set(*state_, invalid));
  invalid = binding;
  invalid.entry_id = EntryId();
  EXPECT_FALSE(WorkspaceTabStateTestPeer::Set(*state_, invalid));
  EXPECT_EQ(state_->GetAssociation(), binding);
}

TEST_F(WorkspaceTabStateTest, LookupAndDiscardSubscriptionFollowFeatureLifetime) {
  EXPECT_EQ(WorkspaceTabState::From(tab_), state_.get());
  EXPECT_FALSE(discards_.empty());
  state_.reset();
  EXPECT_EQ(WorkspaceTabState::From(tab_), nullptr);
  EXPECT_TRUE(discards_.empty());
}

}  // namespace openarc::workspace
