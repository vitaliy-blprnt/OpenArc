// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#ifndef OPENARC_WORKSPACE_TAB_SESSION_CODEC_H_
#define OPENARC_WORKSPACE_TAB_SESSION_CODEC_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "base/types/expected.h"
#include "openarc/workspace/workspace_ids.h"

namespace openarc::workspace {

inline constexpr size_t kMaxTabSessionMetadataBytes = 256;

enum class PersistenceContext { kRegularProfile, kOffTheRecord };

struct TabWorkspaceBinding {
  SpaceId space_id;
  std::optional<EntryId> entry_id;

  bool operator==(const TabWorkspaceBinding&) const = default;
};

struct TabSessionMetadata {
  // Empty explicitly clears the entire workspace association, including Space.
  // To clear only a saved-entry association, retain a bound Space with no entry.
  // Neither case means an absent SessionService key.
  std::optional<TabWorkspaceBinding> binding;

  bool operator==(const TabSessionMetadata&) const = default;
};

enum class TabSessionCodecError {
  kPrivatePersistenceDisallowed,
  kInvalidIdentifier,
  kMalformedPayload,
  kUnsupportedVersion,
  kPayloadTooLarge,
  kSerializationFailed,
};

// Identifier-only version 1 format, with exact arity and types:
//   [1,"unbound"]
//   [1,"bound","space-uuid"]
//   [1,"bound","space-uuid","entry-uuid"]
// A fixed array avoids JSON's duplicate-object-key ambiguity. IDs must be valid
// lowercase UUIDs. No URLs, tab IDs, histories, or extra fields are emitted.
// Unbound clears Space and entry together; saved-tab demotion must instead
// encode a bound record retaining space_id with entry_id absent.
// Context is mandatory; every value, including unbound, is refused for OTR.
// This refusal is not proof that future browser integration cannot leak private
// state: the caller must supply its actual profile context and control writes.
base::expected<std::string, TabSessionCodecError> EncodeTabSessionMetadata(
    const TabSessionMetadata& metadata,
    PersistenceContext context);

// Pure decoding of a PRESENT caller-owned value, with a 256-byte limit checked
// before parsing. Missing external keys are the caller's concern. Empty strings,
// invalid data and future schemas return an error, never a default/unbound value.
// Error enums contain no input data. Preserve rejected bytes in the caller's
// existing recovery mechanism; do not overwrite them with an encoded default.
// There is no I/O or private persistence in this module, and browser/session
// integration remains unverified.
base::expected<TabSessionMetadata, TabSessionCodecError>
DecodeTabSessionMetadata(std::string_view payload);

}  // namespace openarc::workspace

#endif  // OPENARC_WORKSPACE_TAB_SESSION_CODEC_H_
