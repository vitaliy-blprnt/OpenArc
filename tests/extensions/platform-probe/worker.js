// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause
const MENU = "openarc-probe-open-dashboard";

chrome.runtime.onInstalled.addListener(() => {
  // This is the only installation-time mutation. Never create test resources here.
  chrome.contextMenus.remove(MENU, () => {
    void chrome.runtime.lastError;
    chrome.contextMenus.create({
      id: MENU,
      title: "OpenArc: open diagnostic dashboard",
      contexts: ["page"],
      documentUrlPatterns: ["https://example.com/*"],
    }, () => {
      const error = chrome.runtime.lastError;
      if (error) console.error("Probe menu registration failed:", error.message);
    });
  });
});

chrome.contextMenus.onClicked.addListener((info) => {
  if (info.menuItemId === MENU) chrome.runtime.openOptionsPage();
});

chrome.runtime.onMessage.addListener((message, sender, respond) => {
  if (sender.id !== chrome.runtime.id || message?.type !== "probe-ping") return;
  respond({type: "probe-pong", nonce: message.nonce, version: chrome.runtime.getManifest().version});
});
