// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/association_reconciler.h"

#include <algorithm>
#include <array>
#include <type_traits>
#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

namespace openarc::workspace {
namespace {

static_assert(!std::is_convertible_v<SpaceId, EntryId>);
static_assert(!std::is_convertible_v<EntryId, SpaceId>);

SpaceId Work() {
  return SpaceId(base::Uuid::ParseLowercase(
      "11111111-1111-4111-8111-111111111111"));
}
SpaceId Personal() {
  return SpaceId(base::Uuid::ParseLowercase(
      "22222222-2222-4222-8222-222222222222"));
}
SpaceId MissingSpace() {
  return SpaceId(base::Uuid::ParseLowercase(
      "33333333-3333-4333-8333-333333333333"));
}
EntryId FirstEntry() {
  return EntryId(base::Uuid::ParseLowercase(
      "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"));
}
EntryId SecondEntry() {
  return EntryId(base::Uuid::ParseLowercase(
      "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"));
}
EntryId MissingEntry() {
  return EntryId(base::Uuid::ParseLowercase(
      "cccccccc-cccc-4ccc-8ccc-cccccccccccc"));
}
WorkspaceSnapshot Snapshot() {
  return {.default_space = Work(),
          .spaces = {Work(), Personal()},
          .saved_entries = {{FirstEntry(), Work()}, {SecondEntry(), Personal()}}};
}
TabAssociation Tab(int tab,
                   int window,
                   SpaceId space,
                   std::optional<EntryId> entry = std::nullopt) {
  return {.window_id = SessionID::FromSerializedValue(window),
          .tab_id = SessionID::FromSerializedValue(tab),
          .space_id = std::move(space),
          .entry_id = std::move(entry)};
}

TEST(AssociationReconcilerTest, EmptyTabInputDoesNotInventTabs) {
  const auto result = ReconcileAssociations(Snapshot(), {});
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->empty());
}

TEST(AssociationReconcilerTest, OrdinaryTabsKeepKnownSpaceAndInputOrder) {
  const std::array tabs{Tab(8, 4, Personal()), Tab(2, 4, Work())};
  const auto result = ReconcileAssociations(Snapshot(), tabs);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, (std::vector<TabAssociation>{tabs.begin(), tabs.end()}));
}

TEST(AssociationReconcilerTest, UnknownOrAbsentSpaceUsesValidDefault) {
  const std::array tabs{Tab(1, 4, MissingSpace()), Tab(2, 4, SpaceId())};
  const auto result = ReconcileAssociations(Snapshot(), tabs);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 2u);
  EXPECT_EQ((*result)[0].space_id, Work());
  EXPECT_EQ((*result)[1].space_id, Work());
  EXPECT_FALSE((*result)[0].entry_id);
}

TEST(AssociationReconcilerTest, BookmarkScopeWinsOverStaleSessionScope) {
  const std::array tabs{Tab(1, 4, Work(), SecondEntry()),
                        Tab(2, 5, MissingSpace(), SecondEntry())};
  const auto result = ReconcileAssociations(Snapshot(), tabs);
  ASSERT_TRUE(result.has_value());
  for (const auto& tab : *result) {
    EXPECT_EQ(tab.space_id, Personal());
    EXPECT_EQ(tab.entry_id, SecondEntry());
  }
}

TEST(AssociationReconcilerTest, MissingOrMalformedEntryDemotesWithoutLosingTab) {
  const std::array tabs{Tab(1, 4, Personal(), MissingEntry()),
                        Tab(2, 4, MissingSpace(), EntryId())};
  const auto result = ReconcileAssociations(Snapshot(), tabs);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 2u);
  EXPECT_EQ((*result)[0].tab_id.id(), 1);
  EXPECT_EQ((*result)[0].space_id, Personal());
  EXPECT_EQ((*result)[1].tab_id.id(), 2);
  EXPECT_EQ((*result)[1].space_id, Work());
  EXPECT_FALSE((*result)[0].entry_id);
  EXPECT_FALSE((*result)[1].entry_id);
}

TEST(AssociationReconcilerTest, FirstClaimWinsPerWindowAndOtherWindowsKeepInstance) {
  const std::array tabs{Tab(99, 4, Personal(), FirstEntry()),
                        Tab(1, 4, Personal(), FirstEntry()),
                        Tab(2, 5, Personal(), FirstEntry())};
  const auto result = ReconcileAssociations(Snapshot(), tabs);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3u);
  EXPECT_EQ((*result)[0].entry_id, FirstEntry());
  EXPECT_FALSE((*result)[1].entry_id);
  EXPECT_EQ((*result)[1].space_id, Work());
  EXPECT_EQ((*result)[1].tab_id.id(), 1);
  EXPECT_EQ((*result)[2].entry_id, FirstEntry());
}

TEST(AssociationReconcilerTest, CallerPriorityControlsCollisionNotNumericTabId) {
  std::array tabs{Tab(99, 4, Work(), FirstEntry()),
                  Tab(1, 4, Work(), FirstEntry())};
  const auto first = ReconcileAssociations(Snapshot(), tabs);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ((*first)[0].tab_id.id(), 99);
  EXPECT_EQ((*first)[0].entry_id, FirstEntry());
  std::ranges::reverse(tabs);
  const auto reversed = ReconcileAssociations(Snapshot(), tabs);
  ASSERT_TRUE(reversed.has_value());
  EXPECT_EQ((*reversed)[0].tab_id.id(), 1);
  EXPECT_EQ((*reversed)[0].entry_id, FirstEntry());
  EXPECT_FALSE((*reversed)[1].entry_id);
}

TEST(AssociationReconcilerTest, DistinctEntriesRemainDistinctWithoutUrlIdentity) {
  auto snapshot = Snapshot();
  snapshot.saved_entries[1].space_id = Work();
  const std::array tabs{Tab(1, 4, Work(), FirstEntry()),
                        Tab(2, 4, Work(), SecondEntry())};
  const auto result = ReconcileAssociations(snapshot, tabs);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ((*result)[0].entry_id, FirstEntry());
  EXPECT_EQ((*result)[1].entry_id, SecondEntry());
}

TEST(AssociationReconcilerTest, DuplicateRealTabIdentityFailsWholeResult) {
  for (int second_window : {4, 5}) {
    const std::array tabs{Tab(1, 4, Work(), FirstEntry()),
                          Tab(1, second_window, Personal(), SecondEntry())};
    const auto before = tabs;
    const auto result = ReconcileAssociations(Snapshot(), tabs);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ReconcileError::kDuplicateTabIdentity);
    EXPECT_EQ(tabs, before);
  }
}

TEST(AssociationReconcilerTest, InvalidTabOrWindowIdentityFailsWholeResult) {
  for (const auto& invalid : {Tab(0, 4, Work()), Tab(-1, 4, Work()),
                             Tab(1, 0, Work()), Tab(1, -1, Work())}) {
    const std::array tabs{Tab(2, 4, Work(), FirstEntry()), invalid};
    const auto result = ReconcileAssociations(Snapshot(), tabs);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ReconcileError::kInvalidTabIdentity);
  }
}

TEST(AssociationReconcilerTest, InvalidAuthoritativeSnapshotFailsWithoutMutation) {
  std::vector<WorkspaceSnapshot> invalid;
  auto snapshot = Snapshot();
  snapshot.spaces.clear();
  invalid.push_back(snapshot);
  snapshot = Snapshot();
  snapshot.default_space = MissingSpace();
  invalid.push_back(snapshot);
  snapshot = Snapshot();
  snapshot.spaces.push_back(Work());
  invalid.push_back(snapshot);
  snapshot = Snapshot();
  snapshot.spaces.push_back(SpaceId());
  invalid.push_back(snapshot);
  snapshot = Snapshot();
  snapshot.saved_entries.push_back({FirstEntry(), Personal()});
  invalid.push_back(snapshot);
  snapshot = Snapshot();
  snapshot.saved_entries.push_back({EntryId(), Personal()});
  invalid.push_back(snapshot);
  snapshot = Snapshot();
  snapshot.saved_entries[0].space_id = MissingSpace();
  invalid.push_back(snapshot);
  for (const auto& input : invalid) {
    const auto before = input;
    const std::array tabs{Tab(1, 4, Work(), FirstEntry())};
    const auto result = ReconcileAssociations(input, tabs);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ReconcileError::kInvalidSnapshot);
    EXPECT_EQ(input, before);
  }
}

TEST(AssociationReconcilerTest, ReconciliationIsIdempotentAndDoesNotMutateInputs) {
  const auto snapshot = Snapshot();
  const auto before = snapshot;
  const std::array tabs{Tab(1, 4, Personal(), FirstEntry()),
                        Tab(2, 4, MissingSpace(), FirstEntry()),
                        Tab(3, 4, Personal(), MissingEntry())};
  const auto tabs_before = tabs;
  const auto first = ReconcileAssociations(snapshot, tabs);
  ASSERT_TRUE(first.has_value());
  const auto repeated = ReconcileAssociations(snapshot, *first);
  ASSERT_TRUE(repeated.has_value());
  EXPECT_EQ(*first, *repeated);
  EXPECT_EQ(snapshot, before);
  EXPECT_EQ(tabs, tabs_before);
}

}  // namespace
}  // namespace openarc::workspace
