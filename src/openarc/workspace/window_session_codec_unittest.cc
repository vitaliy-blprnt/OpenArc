// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/window_session_codec.h"

#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

namespace openarc::workspace {
namespace {

constexpr std::string_view kFirst = "11111111-1111-4111-8111-111111111111";
constexpr std::string_view kSecond = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kRegular = PersistenceContext::kRegularProfile;

SpaceId Space(std::string_view text) {
  return SpaceId(base::Uuid::ParseLowercase(text));
}

PersistedTabId SourceTab(int value) {
  return PersistedTabId(SessionID::FromSerializedValue(value));
}

WindowSessionMetadata Bound() {
  return WindowSessionMetadata{WindowWorkspaceState{
      Space(kFirst), {{Space(kFirst), SourceTab(17)},
                      {Space(kSecond), SourceTab(42)}}}};
}

WindowSessionMetadata WithSelectionCount(size_t count) {
  WindowWorkspaceState state{Space(kFirst), {}};
  for (size_t index = 0; index < count; ++index) {
    const std::string digits = std::to_string(index + 1);
    state.selections.push_back(
        {Space(std::string(8 - digits.size(), '0') + digits +
               "-0000-4000-8000-000000000000"),
         SourceTab(static_cast<int>(index + 1))});
  }
  return WindowSessionMetadata{std::move(state)};
}

static_assert(std::is_enum_v<WindowSessionCodecError>);
static_assert(!std::is_convertible_v<PersistedTabId, SessionID>);
static_assert(!std::is_convertible_v<SessionID, PersistedTabId>);

TEST(WindowSessionCodecTest, MultipleSpacesRoundTripOnlyIdentifiers) {
  const auto input = Bound();
  const auto encoded = EncodeWindowSessionMetadata(input, kRegular);
  ASSERT_TRUE(encoded.has_value());
  EXPECT_EQ(*encoded,
            R"([1,"bound","11111111-1111-4111-8111-111111111111",[["11111111-1111-4111-8111-111111111111",17],["aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",42]]])");
  const auto decoded = DecodeWindowSessionMetadata(*encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, input);
  EXPECT_EQ(EncodeWindowSessionMetadata(*decoded, kRegular), encoded);
}

TEST(WindowSessionCodecTest, ExplicitUnboundClearsWholeState) {
  const auto encoded = EncodeWindowSessionMetadata(
      WindowSessionMetadata{std::nullopt}, kRegular);
  ASSERT_TRUE(encoded.has_value());
  EXPECT_EQ(*encoded, R"([1,"unbound"])");
  const auto decoded = DecodeWindowSessionMetadata(*encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_FALSE(decoded->workspace.has_value());
  const auto absent_value = DecodeWindowSessionMetadata("");
  ASSERT_FALSE(absent_value.has_value());
  EXPECT_EQ(absent_value.error(), WindowSessionCodecError::kMalformedPayload);
}

TEST(WindowSessionCodecTest, EmptySelectionsRetainActiveSpace) {
  auto input = Bound();
  input.workspace->selections.clear();
  const auto encoded = EncodeWindowSessionMetadata(input, kRegular);
  ASSERT_TRUE(encoded.has_value());
  EXPECT_EQ(*encoded,
            R"([1,"bound","11111111-1111-4111-8111-111111111111",[]])");
  const auto decoded = DecodeWindowSessionMetadata(*encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, input);
}

TEST(WindowSessionCodecTest, ActiveSpaceNeedNotHaveASelectedTab) {
  auto input = Bound();
  input.workspace->selections.erase(input.workspace->selections.begin());
  const auto encoded = EncodeWindowSessionMetadata(input, kRegular);
  ASSERT_TRUE(encoded.has_value());
  const auto decoded = DecodeWindowSessionMetadata(*encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, input);
}

TEST(WindowSessionCodecTest, RefusesPrivateAndInvalidContextsForEveryValue) {
  for (const auto context : {PersistenceContext::kOffTheRecord,
                             static_cast<PersistenceContext>(99)}) {
    for (const auto& input :
         {Bound(), WindowSessionMetadata{std::nullopt}}) {
      const auto original = input;
      const auto result = EncodeWindowSessionMetadata(input, context);
      ASSERT_FALSE(result.has_value());
      EXPECT_EQ(result.error(),
                WindowSessionCodecError::kPrivatePersistenceDisallowed);
      EXPECT_EQ(input, original);
    }
  }
}

TEST(WindowSessionCodecTest, EncodeRejectsInvalidActiveSpaceSelectionAndTab) {
  std::array inputs{Bound(), Bound(), Bound()};
  inputs[0].workspace->active_space_id = SpaceId();
  inputs[1].workspace->selections[1].space_id = SpaceId();
  inputs[2].workspace->selections[1].source_tab_id = SourceTab(0);
  for (const auto& input : inputs) {
    const auto original = input;
    const auto result = EncodeWindowSessionMetadata(input, kRegular);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), WindowSessionCodecError::kInvalidIdentifier);
    EXPECT_EQ(input, original);
  }
}

TEST(WindowSessionCodecTest, RejectsDuplicateSpaceWithoutKeepingFirstSelection) {
  auto input = Bound();
  input.workspace->selections[1].space_id = Space(kFirst);
  const auto encoded = EncodeWindowSessionMetadata(input, kRegular);
  ASSERT_FALSE(encoded.has_value());
  EXPECT_EQ(encoded.error(), WindowSessionCodecError::kDuplicateSpaceSelection);
  const auto decoded = DecodeWindowSessionMetadata(
      R"([1,"bound","11111111-1111-4111-8111-111111111111",[["11111111-1111-4111-8111-111111111111",17],["11111111-1111-4111-8111-111111111111",42]]])");
  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error(), WindowSessionCodecError::kDuplicateSpaceSelection);
}

TEST(WindowSessionCodecTest, RejectsSelectingOneSourceTabInTwoSpaces) {
  auto input = Bound();
  input.workspace->selections[1].source_tab_id = SourceTab(17);
  const auto encoded = EncodeWindowSessionMetadata(input, kRegular);
  ASSERT_FALSE(encoded.has_value());
  EXPECT_EQ(encoded.error(), WindowSessionCodecError::kDuplicateTabSelection);
  const auto decoded = DecodeWindowSessionMetadata(
      R"([1,"bound","11111111-1111-4111-8111-111111111111",[["11111111-1111-4111-8111-111111111111",17],["aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",17]]])");
  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error(), WindowSessionCodecError::kDuplicateTabSelection);
}

TEST(WindowSessionCodecTest, DecodeRejectsInvalidAndNoncanonicalIdentifiers) {
  for (std::string_view input : {
           R"([1,"bound","",[]])",
           R"([1,"bound","https://private.example/path",[]])",
           R"([1,"bound","AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA",[]])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[["",17]]])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[["AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA",17]]])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[["aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",0]]])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[["aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",-1]]])",
       }) {
    const auto result = DecodeWindowSessionMetadata(input);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), WindowSessionCodecError::kInvalidIdentifier);
  }
}

TEST(WindowSessionCodecTest, PreservesSourceIdsRatherThanRemappingOrAllocating) {
  auto input = Bound();
  input.workspace->selections[0].source_tab_id = SourceTab(1);
  input.workspace->selections[1].source_tab_id =
      SourceTab(std::numeric_limits<SessionID::id_type>::max());
  const auto encoded = EncodeWindowSessionMetadata(input, kRegular);
  ASSERT_TRUE(encoded.has_value());
  const auto decoded = DecodeWindowSessionMetadata(*encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, input);
  // These remain explicitly typed source references. Only the future adapter's
  // scoped map and membership checks can establish their restored live tabs.
  EXPECT_EQ(decoded->workspace->selections[1].source_tab_id->id(),
            std::numeric_limits<SessionID::id_type>::max());
}

TEST(WindowSessionCodecTest, RejectsUnknownSchemaWithoutMutatingRecoveryBytes) {
  for (const std::string input : {R"([0,"unbound"])", R"([2,"unbound"])",
                                  R"([-1,"unbound"])"}) {
    const auto original = input;
    const auto result = DecodeWindowSessionMetadata(input);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), WindowSessionCodecError::kUnsupportedVersion);
    EXPECT_EQ(input, original);
  }
}

TEST(WindowSessionCodecTest, RejectsTypeConfusionArityAndAdditionalFields) {
  for (std::string_view input : {
           R"(null)", R"({})", R"([])", R"([1])", R"([true,"unbound"])",
           R"([1.0,"unbound"])", R"(["1","unbound"])", R"([1,null])",
           R"([1,"other"])", R"([1,"unbound",[]])", R"([1,"bound"])",
           R"([1,"bound",null,[]])", R"([1,"bound",42,[]])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111"])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",{}])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[],null])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[{}]])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[[]]])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[["aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"]]])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[["aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",1,2]]])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[[false,1]]])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[["aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa","1"]]])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[["aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",true]]])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[["aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",1.0]]])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",[["aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",2147483648]]])",
           R"({"version":2,"version":1,"state":"unbound"})",
       }) {
    const auto result = DecodeWindowSessionMetadata(input);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), WindowSessionCodecError::kMalformedPayload);
  }
}

TEST(WindowSessionCodecTest, EnforcesSelectionLimitWithoutTruncation) {
  const auto maximum = WithSelectionCount(kMaxWindowSpaceSelections);
  const auto encoded = EncodeWindowSessionMetadata(maximum, kRegular);
  ASSERT_TRUE(encoded.has_value());
  EXPECT_LE(encoded->size(), kMaxWindowSessionMetadataBytes);
  const auto decoded = DecodeWindowSessionMetadata(*encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, maximum);

  const auto too_many = WithSelectionCount(kMaxWindowSpaceSelections + 1);
  const auto rejected_encode = EncodeWindowSessionMetadata(too_many, kRegular);
  ASSERT_FALSE(rejected_encode.has_value());
  EXPECT_EQ(rejected_encode.error(), WindowSessionCodecError::kTooManySelections);

  std::string oversized_list = *encoded;
  // Insert one additional syntactically valid selection before the list closes.
  oversized_list.insert(oversized_list.size() - 2,
                       R"(,["ffffffff-ffff-4fff-8fff-ffffffffffff",999])");
  ASSERT_LE(oversized_list.size(), kMaxWindowSessionMetadataBytes);
  const auto rejected_decode = DecodeWindowSessionMetadata(oversized_list);
  ASSERT_FALSE(rejected_decode.has_value());
  EXPECT_EQ(rejected_decode.error(), WindowSessionCodecError::kTooManySelections);
}

TEST(WindowSessionCodecTest, EnforcesByteLimitBeforeParsing) {
  std::string input = R"([1,"unbound"] )";
  input.resize(kMaxWindowSessionMetadataBytes, ' ');
  EXPECT_TRUE(DecodeWindowSessionMetadata(input).has_value());
  input.push_back(' ');
  const auto result = DecodeWindowSessionMetadata(input);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), WindowSessionCodecError::kPayloadTooLarge);
  std::string utf8;
  for (size_t i = 0; i <= kMaxWindowSessionMetadataBytes / 2; ++i) {
    utf8 += "\xc3\xa9";
  }
  const auto multibyte = DecodeWindowSessionMetadata(utf8);
  ASSERT_FALSE(multibyte.has_value());
  EXPECT_EQ(multibyte.error(), WindowSessionCodecError::kPayloadTooLarge);
}

TEST(WindowSessionCodecTest, StrictJsonRejectsExtensionsUtf8AndDeepNesting) {
  for (std::string_view input : {R"([1,"unbound",])",
                                R"([1,/* comment */"unbound"])",
                                R"([1,"unbound"] trailing)",
                                R"([1,"\x75nbound"])",
                                R"([[[[[1]]]]])", "[1,\"\xff\"]"}) {
    const auto result = DecodeWindowSessionMetadata(input);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), WindowSessionCodecError::kMalformedPayload);
  }
}

TEST(WindowSessionCodecTest, AcceptsRfcWhitespaceAndEscapedEquivalentIdentifiers) {
  const auto result = DecodeWindowSessionMetadata(
      " \n[1,\"bound\",\"11111111-1111-4111-8111-111111111111\","
      "[[\"\\u0061aaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\",42]]] \t");
  ASSERT_TRUE(result.has_value());
  auto expected = Bound();
  expected.workspace->selections.erase(expected.workspace->selections.begin());
  EXPECT_EQ(*result, expected);
}

}  // namespace
}  // namespace openarc::workspace
