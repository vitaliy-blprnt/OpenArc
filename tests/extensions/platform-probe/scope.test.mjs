// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause
import test from "node:test";
import assert from "node:assert/strict";
import {prefix, fixtureURL, exampleURL, ownsURL, ownsBookmark, bookmarkCleanupOrder, validateLedger, ProbeError, Blocked, diagnosticMessage, validNativeReply} from "./scope.mjs";
import {checkNativeHost} from "./native.mjs";
import {pinRoundTrip, groupRoundTrip, removeOwnedTab} from "./tab-structure.mjs";

const run = "c638f088-bfe0-4a18-a667-15b293258055";
const other = "a638f088-bfe0-4a18-a667-15b293258055";
const base = "chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/";
const record = {id: "13", parentId: "12", title: `${prefix(run)} link`, url: exampleURL(run)};

test("ownership accepts only this run's packaged fixture and exact example page", () => {
  assert.equal(ownsURL(fixtureURL(base, run, "first"), base, run), true);
  assert.equal(ownsURL(exampleURL(run), base, run), true);
  for (const url of [
    "https://example.com/", exampleURL(other), "https://private.example/",
    fixtureURL(base, other, "first"), fixtureURL(base.replaceAll("a", "b"), run, "first"),
    fixtureURL(base, run, "first").replace("fixture.html", "dashboard.html"),
    `${exampleURL(run)}#changed`, `${fixtureURL(base, run, "first")}&unrelated=1`,
  ]) assert.equal(ownsURL(url, base, run), false, url);
});

test("bookmark cleanup preserves changed, moved, renamed, and foreign entries", () => {
  assert.equal(ownsBookmark({...record}, record, run), true);
  for (const change of [
    {id: "99"}, {parentId: "1"}, {title: "A personal bookmark"},
    {url: "https://example.com/private"}, {url: undefined},
  ]) assert.equal(ownsBookmark({...record, ...change}, record, run), false);
  assert.equal(ownsBookmark({...record}, record, other), false);
});

test("cleanup orders moved folders after their current descendants", () => {
  const records = [
    {id: "root", parentId: "external"}, {id: "A", parentId: "B"},
    {id: "B", parentId: "root"}, {id: "leaf", parentId: "A"},
  ];
  assert.deepEqual(bookmarkCleanupOrder(records).map((node) => node.id), ["leaf", "A", "B", "root"]);
  assert.deepEqual(records.map((node) => node.id), ["root", "A", "B", "leaf"]);
});

test("invalid cleanup journals fail closed", () => {
  const valid = {version: 1, runId: run, tabs: [4], bookmarks: [record], menu: null, intent: null};
  assert.equal(validateLedger(valid), valid);
  for (const change of [
    {version: 2}, {runId: "not-a-run"}, {tabs: [-1]}, {tabs: [4, 4]},
    {tabs: ["4"]}, {bookmarks: [{...record, url: "https://private.example/"}]},
    {bookmarks: [{...record, title: "not a fixture"}]}, {menu: "another-menu"},
  ]) assert.throws(() => validateLedger({...valid, ...change}), /Invalid resource journal/);
});

test("pending creation requires exact run-owned resources", () => {
  const valid = {version: 1, runId: run, tabs: [], bookmarks: [], menu: null,
    intent: {kind: "tab", title: `${prefix(run)} first`, url: fixtureURL(base, run, "first")}};
  assert.equal(validateLedger(valid, base), valid);
  assert.throws(() => validateLedger({...valid, intent: {...valid.intent, url: fixtureURL(base, other, "first")}}, base));
  assert.throws(() => validateLedger({...valid, intent: {kind: "bookmark", title: "Personal bookmark"}}, base));
});

test("reports withhold arbitrary API errors that may contain unrelated URLs", () => {
  const error = new Error("Cannot access page https://private.example/account?token=secret");
  assert.equal(diagnosticMessage(error).includes("private.example"), false);
  assert.equal(diagnosticMessage(error).includes("secret"), false);
  assert.equal(diagnosticMessage(new ProbeError("Fixture changed; preserved.")), "Fixture changed; preserved.");
});

test("native transport passes only the exact nonce and protocol contract", () => {
  const reply = {type: "pong", nonce: run, protocol: 1};
  assert.equal(validNativeReply(reply, run), true);
  for (const invalid of [null, [], {}, {...reply, nonce: other}, {...reply, protocol: "1"},
    {...reply, protocol: 2}, {...reply, type: "ping"}, {...reply, extra: "unexpected"}]) {
    assert.equal(validNativeReply(invalid, run), false);
  }
});

function nativeHarness() {
  const event = () => ({listeners: new Set(),
    addListener(callback) { this.listeners.add(callback); },
    removeListener(callback) { this.listeners.delete(callback); },
    emit(value) { for (const callback of [...this.listeners]) callback(value); }});
  const port = {onMessage: event(), onDisconnect: event(), sent: [], disconnectCount: 0,
    postMessage(value) { this.sent.push(value); },
    disconnect() { this.disconnectCount++; }};
  const timers = {callback: null, cleared: false,
    setTimeout(callback, ms) { assert.equal(ms, 10000); this.callback = callback; return 42; },
    clearTimeout(id) { assert.equal(id, 42); this.cleared = true; }};
  const runtime = {lastError: undefined, connectNative(host) {
    assert.equal(host, "org.openarc.platform_probe"); return port;
  }};
  const closed = () => {
    assert.equal(port.disconnectCount, 1);
    assert.equal(port.onMessage.listeners.size, 0);
    assert.equal(port.onDisconnect.listeners.size, 0);
    assert.equal(timers.cleared, true);
  };
  return {port, timers, runtime, closed};
}

test("native success sends one challenge and closes the port and timer", async () => {
  const harness = nativeHarness();
  const result = checkNativeHost(harness.runtime, run, harness.timers);
  assert.deepEqual(harness.port.sent, [{type: "ping", nonce: run}]);
  harness.port.onMessage.emit({type: "pong", nonce: run, protocol: 1});
  await result;
  harness.closed();
});

test("native default timers preserve the global receiver required by browser timers", async (context) => {
  const harness = nativeHarness();
  context.mock.method(globalThis, "setTimeout", function (callback, ms) {
    assert.equal(this, globalThis, "browser timer must receive the global object");
    return harness.timers.setTimeout(callback, ms);
  });
  context.mock.method(globalThis, "clearTimeout", function (id) {
    assert.equal(this, globalThis, "browser timer cleanup must receive the global object");
    return harness.timers.clearTimeout(id);
  });
  const result = checkNativeHost(harness.runtime, run);
  harness.port.onMessage.emit({type: "pong", nonce: run, protocol: 1});
  await result;
  harness.closed();
});

test("native timeout disconnects and a late valid response cannot pass", async () => {
  const harness = nativeHarness();
  const result = checkNativeHost(harness.runtime, run, harness.timers);
  const rejected = assert.rejects(result, /did not answer within 10 seconds/);
  const lateReply = [...harness.port.onMessage.listeners][0];
  harness.timers.callback();
  await rejected;
  harness.closed();
  lateReply({type: "pong", nonce: run, protocol: 1});
  await assert.rejects(result, /did not answer within 10 seconds/);
  assert.equal(harness.port.sent.length, 1);
});

test("native first mismatched response fails even before a subsequent valid reply", async () => {
  const harness = nativeHarness();
  const result = checkNativeHost(harness.runtime, run, harness.timers);
  const rejected = assert.rejects(result, /invalid nonce or protocol/);
  harness.port.onMessage.emit({type: "pong", nonce: other, protocol: 1});
  harness.port.onMessage.emit({type: "pong", nonce: run, protocol: 1});
  await rejected;
  harness.closed();
});

test("native disconnect sanitizes errors while missing host remains blocked", async () => {
  for (const [message, expectedType] of [
    ["Specified native messaging host not found.", Blocked],
    ["A failure at /private/example/secret", ProbeError],
  ]) {
    const harness = nativeHarness();
    const result = checkNativeHost(harness.runtime, run, harness.timers);
    const rejected = assert.rejects(result, (error) => {
      assert.equal(error.constructor, expectedType);
      assert.equal(error.message.includes("secret"), false);
      return true;
    });
    harness.runtime.lastError = {message};
    harness.port.onDisconnect.emit();
    await rejected;
    harness.closed();
  }
});

test("native post failure still removes listeners and disconnects", async () => {
  const harness = nativeHarness();
  harness.port.postMessage = () => { throw new Error("Underlying transport failed at /private/secret"); };
  await assert.rejects(checkNativeHost(harness.runtime, run, harness.timers), (error) => {
    assert.equal(diagnosticMessage(error).includes("secret"), false);
    return true;
  });
  harness.closed();
});

function structureHarness() {
  const context = {base, runId: run, windowId: 7, ids: [11, 12], recordedIds: [11, 12]};
  let pages = context.ids.map((id, index) => ({id, windowId: 7, index, pinned: false,
    groupId: -1, url: fixtureURL(base, run, String(index))}));
  let group = null;
  const mutations = [];
  const reindex = () => pages.forEach((tab, index) => { tab.index = index; });
  const tabs = {
    async query(query) { return pages.filter((tab) => tab.windowId === query.windowId &&
      (query.groupId === undefined || query.groupId === tab.groupId)).map((tab) => ({...tab})); },
    async update(id, properties) {
      mutations.push(["update", id]);
      Object.assign(pages.find((tab) => tab.id === id), properties);
      pages.sort((a, b) => Number(b.pinned) - Number(a.pinned)); reindex();
    },
    async move(id, properties) {
      mutations.push(["move", id]);
      const index = pages.findIndex((tab) => tab.id === id);
      pages.splice(properties.index, 0, ...pages.splice(index, 1)); reindex();
    },
    async group(options) {
      mutations.push(["group", ...options.tabIds]);
      group = {id: 88, windowId: options.createProperties.windowId, color: "grey", collapsed: false, shared: false};
      pages.forEach((tab) => { if (options.tabIds.includes(tab.id)) tab.groupId = group.id; });
      return group.id;
    },
    async ungroup(ids) {
      const selected = Array.isArray(ids) ? ids : [ids];
      mutations.push(["ungroup", ...selected]);
      pages.forEach((tab) => { if (selected.includes(tab.id)) tab.groupId = -1; });
      if (!pages.some((tab) => tab.groupId >= 0)) group = null;
    },
    async remove(id) { mutations.push(["remove", id]); pages = pages.filter((tab) => tab.id !== id); reindex(); },
  };
  const groups = {
    async get() { return {...group}; },
    async update(id, properties) { mutations.push(["group-update", id]); Object.assign(group, properties); },
    async query(query) { return group && group.windowId === query.windowId && group.title === query.title ? [{...group}] : []; },
  };
  const readOwned = async (id) => {
    const tab = pages.find((item) => item.id === id);
    if (!context.recordedIds.includes(id) || !tab || !ownsURL(tab.pendingUrl || tab.url, base, run)) {
      throw new ProbeError("Fixture changed; preserved.");
    }
    return {...tab};
  };
  return {context, tabs, groups, mutations, pages: () => pages, readOwned};
}

test("pin and group round trips restore ordinary synthetic tabs without extra resources", async () => {
  const harness = structureHarness();
  await pinRoundTrip(harness.tabs, harness.context);
  await groupRoundTrip(harness.tabs, harness.groups, harness.context);
  assert.deepEqual(harness.pages().map(({id, index, pinned, groupId}) => ({id, index, pinned, groupId})), [
    {id: 11, index: 0, pinned: false, groupId: -1}, {id: 12, index: 1, pinned: false, groupId: -1},
  ]);
  assert.deepEqual(harness.mutations.filter(([kind]) => kind === "group"), [["group", 11, 12]]);
});

test("structure checks refuse foreign tabs, changed destinations, moved tabs, and unjournaled IDs before mutation", async () => {
  for (const mutate of [
    (h) => h.pages().push({id: 99, windowId: 7, index: 2, url: "https://private.example/secret"}),
    (h) => { h.pages()[0].pendingUrl = "https://private.example/secret"; },
    (h) => { h.pages()[0].windowId = 9; },
    (h) => { h.context.recordedIds = [11]; },
    (h) => { h.pages()[0].groupId = 123; },
  ]) {
    for (const operation of [pinRoundTrip, (tabs, context) => groupRoundTrip(tabs, {}, context)]) {
      const harness = structureHarness(); mutate(harness);
      await assert.rejects(operation(harness.tabs, harness.context), (error) => {
        assert.equal(diagnosticMessage(error).includes("private.example"), false);
        return error instanceof ProbeError;
      });
      assert.deepEqual(harness.mutations, []);
    }
  }
});

test("pinning fails when the API sets pinned without preserving real pinned ordering", async () => {
  const harness = structureHarness();
  harness.tabs.update = async (id, properties) => Object.assign(harness.pages().find((tab) => tab.id === id), properties);
  await assert.rejects(pinRoundTrip(harness.tabs, harness.context), /real pinned tab/);
});

test("a newly added foreign tab prevents synthetic group metadata mutation", async () => {
  const harness = structureHarness();
  const group = harness.tabs.group;
  harness.tabs.group = async (options) => {
    const id = await group(options);
    harness.pages().push({id: 99, windowId: 7, index: 2, groupId: id, url: "https://private.example/secret"});
    return id;
  };
  await assert.rejects(groupRoundTrip(harness.tabs, harness.groups, harness.context), /membership or page ownership changed/);
  assert.deepEqual(harness.mutations, [["group", 11, 12]]);
});

test("group metadata read cannot authorize a later rename after a foreign member arrives", async () => {
  const harness = structureHarness();
  const get = harness.groups.get;
  harness.groups.get = async (id) => {
    const group = await get(id);
    harness.pages().push({id: 99, windowId: 7, index: 2, groupId: id, url: "https://private.example/secret"});
    return group;
  };
  await assert.rejects(groupRoundTrip(harness.tabs, harness.groups, harness.context), /membership or page ownership changed/);
  assert.deepEqual(harness.mutations, [["group", 11, 12]]);
});

test("interrupted grouping is cleaned using only journaled owned tab IDs", async () => {
  const harness = structureHarness();
  await harness.tabs.group({tabIds: harness.context.ids, createProperties: {windowId: 7}});
  harness.mutations.length = 0;
  for (const id of harness.context.recordedIds) await removeOwnedTab(harness.tabs, id, harness.readOwned);
  assert.deepEqual(harness.mutations, [["ungroup", 11], ["remove", 11], ["ungroup", 12], ["remove", 12]]);
  assert.equal(harness.pages().length, 0);
});

test("group recovery preserves an unrelated member and never mutates its group metadata", async () => {
  const harness = structureHarness();
  await harness.tabs.group({tabIds: harness.context.ids, createProperties: {windowId: 7}});
  const foreign = {id: 99, windowId: 7, index: 2, groupId: 88, url: "https://private.example/secret"};
  harness.pages().push(foreign);
  harness.mutations.length = 0;
  for (const id of harness.context.recordedIds) await removeOwnedTab(harness.tabs, id, harness.readOwned);
  assert.deepEqual(harness.pages().map(({id, groupId, url}) => ({id, groupId, url})),
    [{id: 99, groupId: 88, url: foreign.url}]);
  assert.deepEqual(harness.mutations, [["ungroup", 11], ["remove", 11], ["ungroup", 12], ["remove", 12]]);
});

test("cleanup revalidates ownership after ungrouping before closing a tab", async () => {
  const harness = structureHarness();
  await harness.tabs.group({tabIds: harness.context.ids, createProperties: {windowId: 7}});
  harness.mutations.length = 0;
  const ungroup = harness.tabs.ungroup;
  harness.tabs.ungroup = async (id) => {
    await ungroup(id);
    harness.pages().find((tab) => tab.id === id).pendingUrl = "https://private.example/secret";
  };
  await assert.rejects(removeOwnedTab(harness.tabs, 11, harness.readOwned), /Fixture changed/);
  assert.deepEqual(harness.mutations, [["ungroup", 11]]);
  assert.equal(harness.pages().length, 2);
});
