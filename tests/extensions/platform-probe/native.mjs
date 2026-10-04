// Copyright 2026 OpenArc contributors. SPDX-License-Identifier: BSD-3-Clause
import {Blocked, ProbeError, validNativeReply} from "./scope.mjs";

function nativeFailure(error) {
  // Use raw API text only for classification; never expose it in a report.
  return /native messaging host.*not found|native host.*not registered/i.test(error?.message || "")
    ? new Blocked("Synthetic host is not configured for this browser/profile and extension ID. Follow the fixture installation instructions.")
    : new ProbeError("Synthetic native connection failed or closed before a valid reply; raw error details are withheld.");
}

export async function checkNativeHost(runtime, nonce, timers = {
  // Browser timers require their Window receiver; do not invoke copied methods
  // with this adapter object as `this`.
  setTimeout: (callback, delay) => globalThis.setTimeout(callback, delay),
  clearTimeout: (id) => globalThis.clearTimeout(id),
}) {
  let port;
  let timeout;
  let onMessage;
  let onDisconnect;
  try {
    try { port = runtime.connectNative("org.openarc.platform_probe"); }
    catch (error) { throw nativeFailure(error); }
    return await new Promise((resolve, reject) => {
      // The first reply settles the check; no later response can repair a mismatch.
      onMessage = (reply) => validNativeReply(reply, nonce)
        ? resolve()
        : reject(new ProbeError("Synthetic host returned an invalid nonce or protocol response."));
      onDisconnect = () => reject(nativeFailure(runtime.lastError));
      port.onMessage.addListener(onMessage);
      port.onDisconnect.addListener(onDisconnect);
      timeout = timers.setTimeout(() => reject(new ProbeError("Synthetic host did not answer within 10 seconds.")), 10000);
      port.postMessage({type: "ping", nonce});
    });
  } finally {
    timers.clearTimeout(timeout);
    if (port) {
      if (onMessage) port.onMessage.removeListener(onMessage);
      if (onDisconnect) port.onDisconnect.removeListener(onDisconnect);
      // Closing the port also closes the native host's stdin; our host exits on EOF.
      // A transport that already closed can reject disconnect without changing the result.
      try { port.disconnect(); } catch {}
    }
  }
}
