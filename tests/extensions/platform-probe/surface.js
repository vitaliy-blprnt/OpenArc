// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause
const feedback = document.querySelector("#feedback");
document.querySelector("#dashboard").addEventListener("click", () => {
  chrome.runtime.openOptionsPage().catch(() => { feedback.textContent = "The browser could not open the dashboard."; });
});
document.querySelector("#panel")?.addEventListener("click", () => {
  // Keep this within a real user gesture. No tab query or webpage inspection.
  chrome.windows.getCurrent().then((window) => chrome.sidePanel.open({windowId: window.id}))
    .catch(() => { feedback.textContent = "The browser could not open the side panel from this gesture."; });
});
