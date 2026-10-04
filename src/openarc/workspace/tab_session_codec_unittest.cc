// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/tab_session_codec.h"

#include <array>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

namespace openarc::workspace {
namespace {

constexpr std::string_view kSpace = "11111111-1111-4111-8111-111111111111";
constexpr std::string_view kEntry = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr auto kRegular = PersistenceContext::kRegularProfile;

TabSessionMetadata Bound(bool with_entry = true) {
  TabWorkspaceBinding binding{SpaceId(base::Uuid::ParseLowercase(kSpace)),
                              std::nullopt};
  if (with_entry) {
    binding.entry_id = EntryId(base::Uuid::ParseLowercase(kEntry));
  }
  return TabSessionMetadata{std::move(binding)};
}

static_assert(std::is_enum_v<TabSessionCodecError>);
static_assert(!std::is_convertible_v<SpaceId, EntryId>);

TEST(TabSessionCodecTest, BoundRoundTripEmitsOnlyVersionStateAndIdentifiers) {
  const auto original = Bound();
  const auto encoded = EncodeTabSessionMetadata(original, kRegular);
  ASSERT_TRUE(encoded.has_value());
  EXPECT_EQ(*encoded,
            R"([1,"bound","11111111-1111-4111-8111-111111111111","aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"])");
  EXPECT_LE(encoded->size(), kMaxTabSessionMetadataBytes);
  const auto decoded = DecodeTabSessionMetadata(*encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, original);
}

TEST(TabSessionCodecTest, OrdinarySpaceBindingDoesNotInventAnEntry) {
  const auto encoded = EncodeTabSessionMetadata(Bound(false), kRegular);
  ASSERT_TRUE(encoded.has_value());
  EXPECT_EQ(*encoded, R"([1,"bound","11111111-1111-4111-8111-111111111111"])");
  const auto decoded = DecodeTabSessionMetadata(*encoded);
  ASSERT_TRUE(decoded.has_value());
  ASSERT_TRUE(decoded->binding.has_value());
  EXPECT_FALSE(decoded->binding->entry_id.has_value());
}

TEST(TabSessionCodecTest, ExplicitUnboundDiffersFromMalformedPresentValue) {
  const auto encoded =
      EncodeTabSessionMetadata(TabSessionMetadata{std::nullopt}, kRegular);
  ASSERT_TRUE(encoded.has_value());
  EXPECT_EQ(*encoded, R"([1,"unbound"])");
  const auto decoded = DecodeTabSessionMetadata(*encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_FALSE(decoded->binding.has_value());
  const auto empty = DecodeTabSessionMetadata("");
  ASSERT_FALSE(empty.has_value());
  EXPECT_EQ(empty.error(), TabSessionCodecError::kMalformedPayload);
}

TEST(TabSessionCodecTest, RefusesAllEncodingInPrivateContext) {
  for (const auto& input :
       {Bound(), Bound(false), TabSessionMetadata{std::nullopt}}) {
    const auto original = input;
    const auto result = EncodeTabSessionMetadata(
        input, PersistenceContext::kOffTheRecord);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(),
              TabSessionCodecError::kPrivatePersistenceDisallowed);
    EXPECT_EQ(input, original);
  }
}

TEST(TabSessionCodecTest, InvalidContextCannotImplicitlyEnablePersistence) {
  const auto result = EncodeTabSessionMetadata(
      Bound(), static_cast<PersistenceContext>(99));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TabSessionCodecError::kPrivatePersistenceDisallowed);
}

TEST(TabSessionCodecTest, EncodeRejectsInvalidIdentifiersWithoutPartialValue) {
  std::array inputs{Bound(), Bound()};
  inputs[0].binding->space_id = SpaceId();
  inputs[1].binding->entry_id = EntryId();
  for (const auto& input : inputs) {
    const auto result = EncodeTabSessionMetadata(input, kRegular);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TabSessionCodecError::kInvalidIdentifier);
  }
}

TEST(TabSessionCodecTest, DecodeRejectsInvalidOrNoncanonicalIdentifiers) {
  for (std::string_view input : {
           R"([1,"bound",""])",
           R"([1,"bound","https://private.example/path"])",
           R"([1,"bound","AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA"])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",""])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111","not-an-id"])",
       }) {
    const auto result = DecodeTabSessionMetadata(input);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TabSessionCodecError::kInvalidIdentifier);
  }
}

TEST(TabSessionCodecTest, RejectsUnsupportedSchemaWithoutChangingOriginalBytes) {
  for (const std::string input : {R"([0,"unbound"])", R"([2,"unbound"])",
                                  R"([-1,"unbound"])"}) {
    const auto original = input;
    const auto result = DecodeTabSessionMetadata(input);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TabSessionCodecError::kUnsupportedVersion);
    EXPECT_EQ(input, original);
  }
}

TEST(TabSessionCodecTest, RejectsTypeConfusionArityAndUnknownFields) {
  for (std::string_view input : {
           R"(null)", R"({})", R"([])", R"([1])", R"([true,"unbound"])",
           R"(["1","unbound"])", R"([1.0,"unbound"])",
           R"([1,null])", R"([1,"other"])", R"([1,"bound"])",
           R"([1,"unbound",null])", R"([1,"bound",null])",
           R"([1,"bound",{}])", R"([1,"bound",123])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",null])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111",false])",
           R"([1,"bound","11111111-1111-4111-8111-111111111111","aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa","extra"])",
           R"({"version":2,"version":1,"state":"unbound"})",
       }) {
    const auto result = DecodeTabSessionMetadata(input);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TabSessionCodecError::kMalformedPayload);
  }
}

TEST(TabSessionCodecTest, SizeLimitCountsBytesBeforeParsing) {
  std::string input = R"([1,"unbound"] )";
  input.resize(kMaxTabSessionMetadataBytes, ' ');
  EXPECT_TRUE(DecodeTabSessionMetadata(input).has_value());
  input.push_back(' ');
  const auto oversized = DecodeTabSessionMetadata(input);
  ASSERT_FALSE(oversized.has_value());
  EXPECT_EQ(oversized.error(), TabSessionCodecError::kPayloadTooLarge);
  std::string multibyte;
  for (int index = 0; index < 129; ++index) {
    multibyte += "\xc3\xa9";
  }
  const auto oversized_utf8 = DecodeTabSessionMetadata(multibyte);
  ASSERT_FALSE(oversized_utf8.has_value());
  EXPECT_EQ(oversized_utf8.error(), TabSessionCodecError::kPayloadTooLarge);
}

TEST(TabSessionCodecTest, StrictJsonRejectsExtensionsMalformedUtf8AndNesting) {
  for (std::string_view input : {R"([1,"unbound",])",
                                R"([1,/* comment */"unbound"])",
                                R"([1,"unbound"] trailing)",
                                R"([1,["unbound"]])",
                                R"([1,"\x75nbound"])",
                                "[1,\"\xff\"]"}) {
    const auto result = DecodeTabSessionMetadata(input);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TabSessionCodecError::kMalformedPayload);
  }
}

TEST(TabSessionCodecTest, PureDecodeAcceptsJsonWhitespaceWithoutPersisting) {
  const auto input = Bound();
  const auto encoded = EncodeTabSessionMetadata(input, kRegular);
  ASSERT_TRUE(encoded.has_value());
  const auto decoded = DecodeTabSessionMetadata(" \n" + *encoded + "\t ");
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, input);
  EXPECT_EQ(EncodeTabSessionMetadata(*decoded, kRegular), encoded);
}

}  // namespace
}  // namespace openarc::workspace
