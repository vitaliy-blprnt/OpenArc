// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause
import {LEDGER_KEY, REPORT_KEY, prefix, fixtureURL, exampleURL, ownsURL, ownsBookmark, bookmarkCleanupOrder, validateLedger, ProbeError, Blocked, diagnosticMessage} from "./scope.mjs";
import {checkNativeHost} from "./native.mjs";
import {pinRoundTrip, groupRoundTrip, removeOwnedTab} from "./tab-structure.mjs";

const base = chrome.runtime.getURL("/");
const ui = Object.fromEntries(["run", "cleanup", "export", "isolated", "status", "results", "native", "native-status"].map((id) => [id, document.getElementById(id)]));
let busy = false;
let ledger = null;
let report = null;
const CHECKS = [
  ["worker", "Runtime service-worker message"], ["local", "storage.local round trip"],
  ["session", "storage.session round trip"], ["menu", "Context menu registration"],
  ["panel", "Side-panel configuration"], ["window", "Synthetic window setup"],
  ["create", "tabs.create"], ["query", "tabs.query (fixture only)"],
  ["move", "tabs.move"], ["pin", "tabs.update pin/unpin and real ordering"],
  ["groups", "Tab-group metadata and membership round trip"],
  ["update", "tabs.update"], ["remove", "tabs.remove"],
  ["bookmarks-create", "bookmarks.create"], ["bookmarks-move", "bookmarks.move"],
  ["bookmarks-update", "bookmarks.update"], ["bookmarks-remove", "bookmarks.remove"],
  ["example", "Synthetic example.com tab"], ["inject", "Content-script injection"],
  ["restore", "sessions.restore (own closed tab)"], ["cleanup", "Fixture cleanup"],
];

function assert(condition, message) { if (!condition) throw new ProbeError(message); }
function need(value, message) { if (!value) throw new Blocked(message); return value; }
const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const writeLedger = () => chrome.storage.local.set({[LEDGER_KEY]: ledger});

function render() {
  ui.results.replaceChildren();
  for (const result of report?.checks || CHECKS.map(([id, label]) => ({id, label, state: "not run", detail: ""}))) {
    const row = document.createElement("tr");
    row.dataset.state = result.state;
    for (const value of [result.label, result.state, result.detail]) {
      const cell = document.createElement("td"); cell.textContent = value; row.append(cell);
    }
    ui.results.append(row);
  }
  ui.run.disabled = busy || !ui.isolated.checked || ledger !== null;
  ui.cleanup.disabled = busy || !ui.isolated.checked || ledger === null;
  ui.export.disabled = busy || report === null;
  ui.native.disabled = busy || !ui.isolated.checked;
  if (report?.nativeMessaging) {
    ui["native-status"].textContent = `${report.nativeMessaging.state}: ${report.nativeMessaging.detail}`;
  } else ui["native-status"].textContent = "Not run. This check is separate from Run checks.";
}

async function saveReport() {
  await chrome.storage.local.set({[REPORT_KEY]: report});
}

async function step(id, operation) {
  const result = report.checks.find((item) => item.id === id);
  result.state = "running"; render();
  try {
    result.detail = await operation() || "Observed expected result.";
    result.state = "pass";
  } catch (error) {
    result.state = error instanceof Blocked ? "blocked" : "fail";
    result.detail = diagnosticMessage(error).slice(0, 400);
  }
  render();
  // Keep reporting failures in the UI even if the storage API itself fails.
  try { await saveReport(); } catch { ui.status.textContent = "Report persistence failed; keep this page open to inspect results."; }
}

async function rememberTab(tab) {
  assert(Number.isInteger(tab.id), "Created tab did not return an ID.");
  if (!ledger.tabs.includes(tab.id)) ledger.tabs.push(tab.id);
  ledger.intent = null; await writeLedger(); return tab;
}

async function tabIntent(url) {
  need(!ledger.intent, "An earlier creation outcome is unresolved; cleanup is required.");
  const parsed = new URL(url);
  ledger.intent = {kind: "tab", url,
    title: url === exampleURL(ledger.runId) ? null : `${prefix(ledger.runId)} ${parsed.searchParams.get("label")}`};
  await writeLedger();
}

async function createTab(properties) {
  await tabIntent(properties.url);
  return rememberTab(await chrome.tabs.create(properties));
}

async function fixtureTab(id) {
  need(id !== undefined, "Fixture prerequisite did not complete.");
  assert(ledger.tabs.includes(id), "Refusing a tab not recorded by this run.");
  const tab = await chrome.tabs.get(id);
  assert(ownsURL(tab.pendingUrl || tab.url, base, ledger.runId), "Fixture tab changed; refusing to alter it.");
  return tab;
}

async function loaded(id) {
  const deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    const tab = await fixtureTab(id);
    if (tab.status === "complete") return tab;
    await pause(100);
  }
  throw new ProbeError("Fixture page did not finish loading within 15 seconds.");
}

async function removeTab(id) {
  await removeOwnedTab(chrome.tabs, id, fixtureTab);
  ledger.tabs = ledger.tabs.filter((item) => item !== id);
  await writeLedger();
}

async function rememberBookmark(properties) {
  need(!ledger.intent, "An earlier creation outcome is unresolved; cleanup is required.");
  ledger.intent = {kind: "bookmark", ...properties}; await writeLedger();
  const node = await chrome.bookmarks.create(properties);
  ledger.bookmarks.push({id: node.id, parentId: node.parentId, title: node.title, ...(node.url ? {url: node.url} : {})});
  ledger.intent = null;
  await writeLedger(); return node;
}

async function fixtureBookmark(id) {
  need(id, "Bookmark prerequisite did not complete.");
  const record = ledger.bookmarks.find((item) => item.id === id);
  need(record, "Bookmark is not in this run's journal.");
  const [node] = await chrome.bookmarks.get(id);
  assert(ownsBookmark(node, record, ledger.runId), "Fixture bookmark changed; refusing to alter it.");
  return node;
}

async function updateBookmarkRecord(node) {
  const index = ledger.bookmarks.findIndex((item) => item.id === node.id);
  ledger.bookmarks[index] = {id: node.id, parentId: node.parentId, title: node.title, ...(node.url ? {url: node.url} : {})};
  await writeLedger();
}

async function removeBookmark(id) {
  await fixtureBookmark(id);
  // remove, never removeTree: an unexpected child must prevent folder deletion.
  await chrome.bookmarks.remove(id);
  ledger.bookmarks = ledger.bookmarks.filter((item) => item.id !== id);
  await writeLedger();
}

function createMenu(id) {
  return new Promise((resolve, reject) => chrome.contextMenus.create({
    id, title: "OpenArc synthetic menu check", contexts: ["page"], documentUrlPatterns: ["https://example.com/*"],
  }, () => chrome.runtime.lastError ? reject(new Error(chrome.runtime.lastError.message)) : resolve()));
}

async function clean() {
  validateLedger(ledger, base);
  const errors = [];
  if (ledger.intent) {
    try {
      const intent = ledger.intent;
      if (intent.kind === "tab") {
        // Narrow queries to the persisted unique marker, never enumerate general tabs.
        const matches = await chrome.tabs.query(intent.title ? {title: intent.title} : {url: intent.url});
        const owned = matches.filter((tab) => (tab.pendingUrl || tab.url) === intent.url && ownsURL(intent.url, base, ledger.runId));
        assert(matches.length === 1 && owned.length === 1, "Creation outcome is unresolved; inspect the nonce-labeled fixture manually. Journal retained.");
        await rememberTab(owned[0]);
      } else {
        const matches = await chrome.bookmarks.search({title: intent.title});
        const owned = matches.filter((node) => node.title === intent.title && node.url === intent.url &&
          (intent.parentId === undefined || node.parentId === intent.parentId));
        assert(matches.length === 1 && owned.length === 1, "Bookmark creation outcome is unresolved; inspect the nonce-labeled fixture manually. Journal retained.");
        const node = owned[0];
        ledger.bookmarks.push({id: node.id, parentId: node.parentId, title: node.title, ...(node.url ? {url: node.url} : {})});
        ledger.intent = null; await writeLedger();
      }
    } catch (error) { errors.push(diagnosticMessage(error)); }
  }
  // Retry exact run-scoped keys even if a roundtrip's finally failed or was interrupted.
  for (const area of [chrome.storage.local, chrome.storage.session]) {
    try { await area.remove(`openarc.probe.roundtrip.${ledger.runId}`); }
    catch (error) { errors.push(`Storage cleanup: ${diagnosticMessage(error)}`); }
  }
  for (const id of [...ledger.tabs]) {
    try { await removeTab(id); } catch (error) {
      // A stale ID can be absent after a restart. A reused ID with another URL is retained.
      if (/No tab with id|Invalid tab ID/i.test(error.message)) {
        ledger.tabs = ledger.tabs.filter((item) => item !== id); await writeLedger();
      } else errors.push(`Recorded tab cleanup: ${diagnosticMessage(error)}`);
    }
  }
  for (const record of bookmarkCleanupOrder(ledger.bookmarks)) {
    try { await removeBookmark(record.id); } catch (error) {
      if (/Can't find bookmark for id|No bookmark with id/i.test(error.message)) {
        ledger.bookmarks = ledger.bookmarks.filter((item) => item.id !== record.id); await writeLedger();
      } else errors.push(`Recorded bookmark cleanup: ${diagnosticMessage(error)}`);
    }
  }
  if (ledger.menu) {
    try { await chrome.contextMenus.remove(ledger.menu); ledger.menu = null; await writeLedger(); }
    catch (error) {
      if (/Cannot find menu item|No item with id/i.test(error.message)) { ledger.menu = null; await writeLedger(); }
      else errors.push(`Menu cleanup: ${diagnosticMessage(error)}`);
    }
  }
  if (errors.length) throw new ProbeError(`Cleanup incomplete. ${errors.join("; ")}`);
  await chrome.storage.local.remove(LEDGER_KEY); ledger = null;
  return "Created live tabs, bookmarks, and transient menu removed. Synthetic history/recently closed entries are retained.";
}

async function runWithLock() {
  if (busy || ledger || !ui.isolated.checked) return;
  busy = true;
  const runId = crypto.randomUUID();
  ledger = {version: 1, runId, tabs: [], bookmarks: [], menu: null, intent: null};
  report = {schema: 1, runId, startedAt: new Date().toISOString(), extensionVersion: chrome.runtime.getManifest().version,
    userAgent: navigator.userAgent, qualification: "Unpacked diagnostic only; no store, signing, native integration, or universal compatibility claim.",
    manualSurfaces: "not automatically verified", checks: CHECKS.map(([id, label]) => ({id, label, state: "not run", detail: ""}))};
  let windowId, first, second, example, root, folderA, folderB, bookmark;
  ui.status.textContent = "Running explicit fixture checks. Leave the synthetic window and bookmarks untouched.";
  render();
  try {
    // Refuse resource creation if recovery metadata cannot be persisted.
    await writeLedger();
    await step("worker", async () => {
      const response = await chrome.runtime.sendMessage({type: "probe-ping", nonce: runId});
      assert(response?.type === "probe-pong" && response.nonce === runId, "Worker did not echo the challenge.");
    });
    for (const [id, area] of [["local", chrome.storage.local], ["session", chrome.storage.session]]) {
      await step(id, async () => {
        const key = `openarc.probe.roundtrip.${runId}`;
        try {
          await area.set({[key]: {nonce: runId, number: 37}});
          const value = (await area.get(key))[key];
          assert(value?.nonce === runId && value.number === 37, "Storage round trip did not match.");
        } finally { await area.remove(key); }
      });
    }
    await step("menu", async () => {
      ledger.menu = `openarc-probe-${runId}`; await writeLedger();
      await createMenu(ledger.menu);
      await chrome.contextMenus.update(ledger.menu, {title: "OpenArc synthetic menu verified"});
      await chrome.contextMenus.remove(ledger.menu); ledger.menu = null; await writeLedger();
      return "Creation/update/removal acknowledged. Visible context-menu interaction still requires manual review.";
    });
    await step("panel", async () => {
      const options = await chrome.sidePanel.getOptions({});
      assert(options.path === "sidepanel.html" && options.enabled !== false, "Packaged side panel is not configured as expected.");
      return "Packaged side-panel path enabled. Rendering and user-gesture opening are manual checks.";
    });
    await step("window", async () => {
      const url = fixtureURL(base, runId, "first");
      await tabIntent(url);
      const window = await chrome.windows.create({url, type: "normal", focused: false});
      windowId = window.id;
      first = (await rememberTab(need(window.tabs?.[0], "Synthetic window returned no initial tab."))).id;
      await loaded(first);
    });
    await step("create", async () => {
      need(windowId !== undefined && first !== undefined, "Synthetic window setup failed.");
      second = (await createTab({windowId, url: fixtureURL(base, runId, "second"), active: false})).id;
      await loaded(second);
    });
    await step("query", async () => {
      await loaded(first);
      const title = `${prefix(runId)} first`;
      const matches = await chrome.tabs.query({windowId, title});
      assert(matches.length === 1 && matches[0].id === first, "Query did not return exactly the nonce-labeled fixture tab.");
    });
    await step("move", async () => {
      await fixtureTab(second);
      const moved = await chrome.tabs.move(second, {windowId, index: 0});
      assert(moved.id === second && moved.index === 0, "Move result did not match the fixture tab/index.");
    });
    const structureContext = () => ({base, runId, windowId, ids: [first, second], recordedIds: ledger.tabs});
    await step("pin", () => pinRoundTrip(chrome.tabs, structureContext()));
    await step("groups", () => groupRoundTrip(chrome.tabs, chrome.tabGroups, structureContext()));
    await step("update", async () => {
      await fixtureTab(second);
      const updated = await chrome.tabs.update(second, {url: fixtureURL(base, runId, "updated"), muted: true});
      assert(updated.id === second, "Updated tab ID changed.");
      const ready = await loaded(second);
      assert(ready.mutedInfo?.muted && ready.url === fixtureURL(base, runId, "updated"), "Tab navigation/mute did not match.");
    });
    await step("remove", async () => { await removeTab(second); });
    await step("bookmarks-create", async () => {
      root = await rememberBookmark({title: `${prefix(runId)} root`});
      folderA = await rememberBookmark({parentId: root.id, title: `${prefix(runId)} A`});
      folderB = await rememberBookmark({parentId: root.id, title: `${prefix(runId)} B`});
      bookmark = await rememberBookmark({parentId: folderA.id, title: `${prefix(runId)} link`, url: exampleURL(runId)});
      await fixtureBookmark(bookmark.id);
    });
    await step("bookmarks-move", async () => {
      await fixtureBookmark(bookmark?.id); await fixtureBookmark(folderB?.id);
      const moved = await chrome.bookmarks.move(bookmark.id, {parentId: folderB.id});
      await updateBookmarkRecord(moved);
      assert(moved.parentId === folderB.id, "Bookmark did not move to its fixture folder.");
      await fixtureBookmark(folderA?.id);
      const movedFolder = await chrome.bookmarks.move(folderA.id, {parentId: folderB.id});
      await updateBookmarkRecord(movedFolder);
      assert(movedFolder.parentId === folderB.id, "Folder did not move to its fixture parent.");
      return "Moved the synthetic bookmark and folder within this run's own tree.";
    });
    await step("bookmarks-update", async () => {
      await fixtureBookmark(folderA?.id);
      const title = `${prefix(runId)} renamed`;
      const updated = await chrome.bookmarks.update(folderA.id, {title});
      await updateBookmarkRecord(updated);
      assert(updated.title === title, "Folder rename did not match.");
      return "Renamed a synthetic folder; saved URL remains unchanged.";
    });
    await step("bookmarks-remove", async () => { await removeBookmark(bookmark?.id); });
    await step("example", async () => {
      need(windowId !== undefined, "Synthetic window setup failed.");
      example = (await createTab({windowId, url: exampleURL(runId), active: false})).id;
      await loaded(example);
    });
    await step("inject", async () => {
      await loaded(example);
      const results = await chrome.scripting.executeScript({target: {tabId: example}, args: [runId], func: (nonce) => {
        const current = new URL(location.href);
        if (current.origin !== "https://example.com" || current.searchParams.get("openarc_probe") !== nonce) return {matched: false};
        document.documentElement.dataset.openarcProbe = nonce;
        const matched = document.documentElement.dataset.openarcProbe === nonce;
        delete document.documentElement.dataset.openarcProbe;
        return {matched};
      }});
      assert(results.length === 1 && results[0].result?.matched, "Content script did not confirm its nonce on example.com.");
    });
    await step("restore", async () => {
      await loaded(example);
      await removeTab(example);
      let candidate;
      for (let attempt = 0; attempt < 10; attempt++) {
        const [entry] = await chrome.sessions.getRecentlyClosed({maxResults: 1});
        if (entry?.tab?.url === exampleURL(runId) && entry.tab.sessionId) {
          candidate = entry.tab;
          break;
        }
        await pause(100);
      }
      // The API has no owner filter. Compare only this marker; never log foreign records.
      if (!candidate) {
        throw new Blocked("Newest closed entry is not this fixture; nothing restored. Retry without concurrent tab closing.");
      }
      await tabIntent(exampleURL(runId));
      const restored = await chrome.sessions.restore(candidate.sessionId);
      // Journal its identity before any assertion: an unexpected URL must be preserved,
      // visible as unfinished cleanup, and never become an untracked live resource.
      if (restored.tab) await rememberTab(restored.tab);
      assert(restored.tab && ownsURL(restored.tab.pendingUrl || restored.tab.url, base, runId), "Restore did not return the owned fixture page.");
      return "Restored only the verified nonce-bearing closed page.";
    });
  } catch (error) {
    report.fatalError = diagnosticMessage(error).slice(0, 400);
  } finally {
    if (ledger) await step("cleanup", clean);
    report.finishedAt = new Date().toISOString();
    const incomplete = report.checks.filter((result) => result.state !== "pass");
    report.automatedResult = incomplete.length || report.fatalError ? "incomplete or failed" : "passed observed checks";
    ui.status.textContent = incomplete.length || report.fatalError
      ? `Run has failed, blocked, or unrun checks. ${ledger ? "Fixture cleanup needs attention." : "Inspect each result."}`
      : "Automated checks passed for this run. Manual surfaces, store delivery, native integration, and broad compatibility remain unverified.";
    try { await saveReport(); } catch { ui.status.textContent += " Report could not be saved locally."; }
    busy = false; render();
  }
}

ui.isolated.addEventListener("change", render);
async function exclusive(operation) {
  if (busy || !ui.isolated.checked) return;
  try {
    await navigator.locks.request("openarc-platform-probe", {ifAvailable: true}, async (lock) => {
      if (!lock) { ui.status.textContent = "Another dashboard is running or cleaning the fixture. Wait for it to finish."; return; }
      const saved = await chrome.storage.local.get([LEDGER_KEY, REPORT_KEY]);
      ledger = saved[LEDGER_KEY] ? validateLedger(saved[LEDGER_KEY], base) : null;
      report = saved[REPORT_KEY] || report;
      await operation();
    });
  } catch (error) { ui.status.textContent = diagnosticMessage(error); }
  finally { render(); }
}
ui.run.addEventListener("click", () => exclusive(async () => {
  if (ledger) { ui.status.textContent = "Unfinished fixture resources exist. Clean up before starting another run."; return; }
  await runWithLock();
}));
ui.cleanup.addEventListener("click", () => exclusive(async () => {
  if (!ledger) { ui.status.textContent = "No unfinished fixture journal remains."; return; }
  busy = true; render();
  try { ui.status.textContent = await clean(); }
  catch (error) { ui.status.textContent = diagnosticMessage(error); }
  finally { busy = false; }
}));
ui.native.addEventListener("click", () => exclusive(async () => {
  busy = true;
  if (!report) report = {schema: 1, runId: crypto.randomUUID(), startedAt: new Date().toISOString(),
    extensionVersion: chrome.runtime.getManifest().version, userAgent: navigator.userAgent,
    qualification: "Unpacked diagnostic only; no store, signing, vendor native integration, or universal compatibility claim.",
    automatedResult: "not run", manualSurfaces: "not automatically verified",
    checks: CHECKS.map(([id, label]) => ({id, label, state: "not run", detail: ""}))};
  report.nativeMessaging = {state: "running", detail: "Waiting for the synthetic host challenge response.", checkedAt: new Date().toISOString()};
  render();
  try {
    await checkNativeHost(chrome.runtime, crypto.randomUUID());
    report.nativeMessaging.state = "pass";
    report.nativeMessaging.detail = "Observed exact nonce/pong protocol 1 reply. Synthetic transport only; vendor trust and credentials remain unverified.";
  } catch (error) {
    report.nativeMessaging.state = error instanceof Blocked ? "blocked" : "fail";
    report.nativeMessaging.detail = diagnosticMessage(error);
  } finally {
    try { await saveReport(); } catch { ui.status.textContent = "Native result is visible but could not be saved locally."; }
    busy = false; render();
  }
}));
ui.export.addEventListener("click", () => {
  const url = URL.createObjectURL(new Blob([JSON.stringify(report, null, 2)], {type: "application/json"}));
  const anchor = document.createElement("a"); anchor.href = url; anchor.download = `openarc-probe-${report.runId}.json`; anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});

try {
  const saved = await chrome.storage.local.get([LEDGER_KEY, REPORT_KEY]);
  if (saved[LEDGER_KEY]) ledger = validateLedger(saved[LEDGER_KEY], base);
  report = saved[REPORT_KEY] || null;
  if (ledger) ui.status.textContent = "Unfinished fixture resources found. Clean up before another run; changed resources are preserved.";
  else if (report) ui.status.textContent = `Previous run: ${report.automatedResult || "interrupted"}. Results below are historical, not a new browser check.`;
} catch (error) {
  busy = true; ui.status.textContent = diagnosticMessage(error);
}
render();
