// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/tab_session_codec.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"

namespace openarc::workspace {

base::expected<std::string, TabSessionCodecError> EncodeTabSessionMetadata(
    const TabSessionMetadata& metadata,
    PersistenceContext context) {
  if (context != PersistenceContext::kRegularProfile) {
    return base::unexpected(
        TabSessionCodecError::kPrivatePersistenceDisallowed);
  }
  base::ListValue value;
  value.Append(1);
  if (!metadata.binding) {
    value.Append("unbound");
  } else {
    const auto& binding = *metadata.binding;
    if (!binding.space_id->is_valid() ||
        (binding.entry_id && !(*binding.entry_id)->is_valid())) {
      return base::unexpected(TabSessionCodecError::kInvalidIdentifier);
    }
    value.Append("bound");
    value.Append(binding.space_id->AsLowercaseString());
    if (binding.entry_id) {
      value.Append((*binding.entry_id)->AsLowercaseString());
    }
  }
  auto encoded = base::WriteJson(value, 2);
  if (!encoded) {
    return base::unexpected(TabSessionCodecError::kSerializationFailed);
  }
  if (encoded->size() > kMaxTabSessionMetadataBytes) {
    return base::unexpected(TabSessionCodecError::kPayloadTooLarge);
  }
  return std::move(*encoded);
}

base::expected<TabSessionMetadata, TabSessionCodecError>
DecodeTabSessionMetadata(std::string_view payload) {
  if (payload.size() > kMaxTabSessionMetadataBytes) {
    return base::unexpected(TabSessionCodecError::kPayloadTooLarge);
  }
  auto value = base::JSONReader::ReadList(payload, base::JSON_PARSE_RFC, 2);
  if (!value || value->size() < 2) {
    return base::unexpected(TabSessionCodecError::kMalformedPayload);
  }
  const auto version = (*value)[0].GetIfInt();
  if (!version) {
    return base::unexpected(TabSessionCodecError::kMalformedPayload);
  }
  if (*version != 1) {
    return base::unexpected(TabSessionCodecError::kUnsupportedVersion);
  }
  const std::string* state = (*value)[1].GetIfString();
  if (!state) {
    return base::unexpected(TabSessionCodecError::kMalformedPayload);
  }
  if (*state == "unbound" && value->size() == 2) {
    return TabSessionMetadata{std::nullopt};
  }
  if (*state != "bound" || (value->size() != 3 && value->size() != 4)) {
    return base::unexpected(TabSessionCodecError::kMalformedPayload);
  }
  const std::string* space = (*value)[2].GetIfString();
  const std::string* entry =
      value->size() == 4 ? (*value)[3].GetIfString() : nullptr;
  if (!space || (value->size() == 4 && !entry)) {
    return base::unexpected(TabSessionCodecError::kMalformedPayload);
  }
  TabWorkspaceBinding binding{SpaceId(base::Uuid::ParseLowercase(*space)),
                              std::nullopt};
  if (entry) {
    binding.entry_id = EntryId(base::Uuid::ParseLowercase(*entry));
  }
  if (!binding.space_id->is_valid() ||
      (binding.entry_id && !(*binding.entry_id)->is_valid())) {
    return base::unexpected(TabSessionCodecError::kInvalidIdentifier);
  }
  return TabSessionMetadata{std::move(binding)};
}

}  // namespace openarc::workspace
