// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/window_session_codec.h"

#include <set>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"

namespace openarc::workspace {
namespace {

std::optional<WindowSessionCodecError> ValidateState(
    const WindowWorkspaceState& state) {
  if (state.selections.size() > kMaxWindowSpaceSelections) {
    return WindowSessionCodecError::kTooManySelections;
  }
  if (!state.active_space_id->is_valid()) {
    return WindowSessionCodecError::kInvalidIdentifier;
  }
  std::set<SpaceId> spaces;
  std::set<SessionID> tabs;
  for (const auto& selection : state.selections) {
    if (!selection.space_id->is_valid() ||
        !selection.source_tab_id->is_valid()) {
      return WindowSessionCodecError::kInvalidIdentifier;
    }
    if (!spaces.insert(selection.space_id).second) {
      return WindowSessionCodecError::kDuplicateSpaceSelection;
    }
    if (!tabs.insert(selection.source_tab_id.value()).second) {
      return WindowSessionCodecError::kDuplicateTabSelection;
    }
  }
  return std::nullopt;
}

}  // namespace

base::expected<std::string, WindowSessionCodecError> EncodeWindowSessionMetadata(
    const WindowSessionMetadata& metadata,
    PersistenceContext context) {
  if (context != PersistenceContext::kRegularProfile) {
    return base::unexpected(
        WindowSessionCodecError::kPrivatePersistenceDisallowed);
  }
  base::ListValue value;
  value.Append(1);
  if (!metadata.workspace) {
    value.Append("unbound");
  } else {
    const auto& state = *metadata.workspace;
    if (auto error = ValidateState(state)) {
      return base::unexpected(*error);
    }
    value.Append("bound");
    value.Append(state.active_space_id->AsLowercaseString());
    base::ListValue selections;
    for (const auto& selection : state.selections) {
      base::ListValue row;
      row.Append(selection.space_id->AsLowercaseString());
      row.Append(selection.source_tab_id->id());
      selections.Append(std::move(row));
    }
    value.Append(std::move(selections));
  }
  auto encoded = base::WriteJson(value, 4);
  if (!encoded) {
    return base::unexpected(WindowSessionCodecError::kSerializationFailed);
  }
  if (encoded->size() > kMaxWindowSessionMetadataBytes) {
    return base::unexpected(WindowSessionCodecError::kPayloadTooLarge);
  }
  return std::move(*encoded);
}

base::expected<WindowSessionMetadata, WindowSessionCodecError>
DecodeWindowSessionMetadata(std::string_view payload) {
  if (payload.size() > kMaxWindowSessionMetadataBytes) {
    return base::unexpected(WindowSessionCodecError::kPayloadTooLarge);
  }
  auto value = base::JSONReader::ReadList(payload, base::JSON_PARSE_RFC, 4);
  if (!value || value->size() < 2) {
    return base::unexpected(WindowSessionCodecError::kMalformedPayload);
  }
  const auto version = (*value)[0].GetIfInt();
  if (!version) {
    return base::unexpected(WindowSessionCodecError::kMalformedPayload);
  }
  if (*version != 1) {
    return base::unexpected(WindowSessionCodecError::kUnsupportedVersion);
  }
  const std::string* mode = (*value)[1].GetIfString();
  if (!mode) {
    return base::unexpected(WindowSessionCodecError::kMalformedPayload);
  }
  if (*mode == "unbound" && value->size() == 2) {
    return WindowSessionMetadata{std::nullopt};
  }
  if (*mode != "bound" || value->size() != 4) {
    return base::unexpected(WindowSessionCodecError::kMalformedPayload);
  }
  const std::string* active_space = (*value)[2].GetIfString();
  const auto* selections = (*value)[3].GetIfList();
  if (!active_space || !selections) {
    return base::unexpected(WindowSessionCodecError::kMalformedPayload);
  }
  if (selections->size() > kMaxWindowSpaceSelections) {
    return base::unexpected(WindowSessionCodecError::kTooManySelections);
  }
  WindowWorkspaceState state{
      SpaceId(base::Uuid::ParseLowercase(*active_space)), {}};
  state.selections.reserve(selections->size());
  for (const auto& selection : *selections) {
    const auto* row = selection.GetIfList();
    if (!row || row->size() != 2) {
      return base::unexpected(WindowSessionCodecError::kMalformedPayload);
    }
    const std::string* space = (*row)[0].GetIfString();
    const auto tab = (*row)[1].GetIfInt();
    if (!space || !tab) {
      return base::unexpected(WindowSessionCodecError::kMalformedPayload);
    }
    state.selections.push_back(
        {SpaceId(base::Uuid::ParseLowercase(*space)),
         PersistedTabId(SessionID::FromSerializedValue(*tab))});
  }
  if (auto error = ValidateState(state)) {
    return base::unexpected(*error);
  }
  return WindowSessionMetadata{std::move(state)};
}

}  // namespace openarc::workspace
