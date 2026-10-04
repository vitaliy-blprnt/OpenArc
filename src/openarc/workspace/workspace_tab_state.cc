// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause

#include "openarc/workspace/workspace_tab_state.h"

#include <utility>

namespace openarc::workspace {

DEFINE_USER_DATA(WorkspaceTabState);

WorkspaceTabState::WorkspaceTabState(tabs::TabInterface& tab)
    : ContentsObservingTabFeature(tab),
      scoped_data_(tab.GetUnownedUserDataHost(), *this) {}

WorkspaceTabState::~WorkspaceTabState() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

WorkspaceTabState* WorkspaceTabState::From(tabs::TabInterface& tab) {
  return Get(tab.GetUnownedUserDataHost());
}

std::optional<TabWorkspaceBinding> WorkspaceTabState::GetAssociation() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return association_;
}

base::CallbackListSubscription WorkspaceTabState::ObserveChanges(
    base::RepeatingClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return changes_.Add(std::move(callback));
}

bool WorkspaceTabState::SetAssociation(
    std::optional<TabWorkspaceBinding> association) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (association &&
      (!association->space_id.value().is_valid() ||
       (association->entry_id && !association->entry_id->value().is_valid()))) {
    return false;
  }
  if (association_ != association) {
    association_ = std::move(association);
    changes_.Notify();
  }
  return true;
}

}  // namespace openarc::workspace
