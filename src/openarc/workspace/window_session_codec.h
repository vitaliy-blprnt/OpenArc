// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#ifndef OPENARC_WORKSPACE_WINDOW_SESSION_CODEC_H_
#define OPENARC_WORKSPACE_WINDOW_SESSION_CODEC_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/types/expected.h"
#include "components/sessions/core/session_id.h"
#include "openarc/workspace/tab_session_codec.h"
#include "openarc/workspace/workspace_ids.h"

namespace openarc::workspace {

inline constexpr size_t kMaxWindowSessionMetadataBytes = 16 * 1024;
inline constexpr size_t kMaxWindowSpaceSelections = 256;

// A reference to a tab record in the caller's source window/session snapshot.
// This is NOT a current live-tab ID or a durable workspace/tab identity. The
// alias makes callers explicitly cross that boundary; it allocates no IDs.
using PersistedTabId = base::StrongAlias<class PersistedTabIdTag, SessionID>;

struct SpaceTabSelection {
  SpaceId space_id;
  PersistedTabId source_tab_id{SessionID::InvalidValue()};

  bool operator==(const SpaceTabSelection&) const = default;
};

struct WindowWorkspaceState {
  SpaceId active_space_id;
  // At most one last-selected real tab per Space, and one Space per tab. Order
  // is preserved, but does not define Space ordering. Omit an empty Space's
  // selection; the active Space need not have any selected tab.
  std::vector<SpaceTabSelection> selections;

  bool operator==(const WindowWorkspaceState&) const = default;
};

struct WindowSessionMetadata {
  // Explicitly clears active Space and all remembered selections together.
  // To clear selections only, encode a bound state with the active Space and
  // an empty vector. Neither representation means an absent external key.
  std::optional<WindowWorkspaceState> workspace;

  bool operator==(const WindowSessionMetadata&) const = default;
};

enum class WindowSessionCodecError {
  kPrivatePersistenceDisallowed,
  kInvalidIdentifier,
  kDuplicateSpaceSelection,
  kDuplicateTabSelection,
  kTooManySelections,
  kMalformedPayload,
  kUnsupportedVersion,
  kPayloadTooLarge,
  kSerializationFailed,
};

// Identifier-only version 1, with exact array shapes and lowercase UUIDs:
//   [1,"unbound"]
//   [1,"bound","active-space-uuid",[["space-uuid",positive-int32-tab-id],...]]
// There are no URLs, histories, indices, window IDs, or extra properties. All
// encodes require an explicit regular-profile context; OTR and invalid contexts
// are refused even for unbound. Limits fail the whole operation, never truncate.
// The caller must verify that source IDs belong to this window's snapshot and
// provide its actual profile context. This helper performs no I/O.
base::expected<std::string, WindowSessionCodecError> EncodeWindowSessionMetadata(
    const WindowSessionMetadata& metadata,
    PersistenceContext context);

// Pure decoding of a PRESENT value, bounded before parsing. Missing external
// keys are a caller concern. Malformed/future data yields a controlled enum and
// no partial state or raw input; retain the original recoverable bytes rather
// than overwriting them with default/unbound metadata.
//
// RESTORE OBLIGATION: Chromium creates new live SessionIDs. Stage these source
// references until the caller has captured an unambiguous old-record-to-new-tab
// mapping for this window. Resolve against that mapping, then validate current
// window/Space membership after bookmark/association reconciliation. A matching
// integer in another live tab/window, a tab index, or URL equality is not proof.
// Missing, skipped, moved, or ambiguous targets must not select unrelated tabs.
//
// Recently-closed capture creates fresh historical Entry IDs too. Its adapter
// must translate live-to-historical references at capture and historical IDs at
// disk reload/restore; copying this payload unchanged cannot establish that map.
// Full rebuild, recently-closed, browser/session integration, and private-write
// prevention remain unverified. This codec neither resolves nor activates tabs.
base::expected<WindowSessionMetadata, WindowSessionCodecError>
DecodeWindowSessionMetadata(std::string_view payload);

}  // namespace openarc::workspace

#endif  // OPENARC_WORKSPACE_WINDOW_SESSION_CODEC_H_
