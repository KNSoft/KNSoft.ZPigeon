import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

const source = await readFile(new URL("./KNSoft.ZPigeon.Web/wwwroot/capture-stream.mjs", import.meta.url), "utf8");

function setup({ enabled = true, honorAbort = true } = {}) {
  const sockets = [], attempts = [], trace = [], received = [], routes = [], statuses = [], closed = [], warnings = [];
  const activeHandles = new Map();
  let nextId = 0, rtcClosed = 0;

  class Socket {
    static CONNECTING = 0;
    static OPEN = 1;
    static CLOSING = 2;
    static CLOSED = 3;
    readyState = Socket.CONNECTING;
    closeCount = 0;
    sent = [];

    constructor(url) {
      this.url = new URL(url);
      this.index = sockets.length;
      sockets.push(this);
      trace.push(`socket:${this.index}`);
    }

    open() {
      this.readyState = Socket.OPEN;
      trace.push(`open:${this.index}`);
      this.onopen?.();
    }

    message(data) {
      this.onmessage?.({ data: Uint8Array.from(data).buffer });
    }

    send(data) {
      this.sent.push(data);
    }

    close(code = 1000, reason = "") {
      if (this.readyState === Socket.CLOSED) return;
      this.closeCount++;
      this.readyState = Socket.CLOSED;
      trace.push(`close:${this.index}`);
      this.onclose?.({ code, reason });
    }
  }

  const rtc = {
    enabled,
    open(receive, status, disconnected, signal) {
      trace.push("rtc.open");
      return new Promise((resolve, reject) => {
        const attempt = { receive, status, disconnected, signal, aborted: false };
        const abort = () => {
          attempt.aborted = true;
          if (honorAbort) reject(new DOMException("canceled", "AbortError"));
        };
        attempt.resolve = () => {
          signal.removeEventListener("abort", abort);
          const handle = {
            id: ++nextId,
            closeCount: 0,
            close() {
              if (this.closeCount) return;
              this.closeCount++;
              activeHandles.delete(this.id);
              trace.push(`direct.close:${this.id}`);
            },
          };
          activeHandles.set(handle.id, handle);
          attempt.handle = handle;
          resolve(handle);
          return handle;
        };
        attempt.reject = (error) => {
          signal.removeEventListener("abort", abort);
          reject(error);
        };
        attempts.push(attempt);
        signal.addEventListener("abort", abort, { once: true });
      });
    },
    close() { rtcClosed++; },
  };
  const decoder = {
    resets: 0,
    reset() { this.resets++; trace.push("decoder.reset"); },
    receive(data, socket) { received.push({ data: [...data], socket }); },
  };
  const context = {
    URL,
    Uint8Array,
    AbortController,
    WebSocket: Socket,
    location: { href: "http://127.0.0.1:9983/client?client=1", protocol: "http:" },
    apiUrl: (input) => {
      const url = new URL(input);
      url.searchParams.set("client", "1");
      return url;
    },
    console: { warn: (error) => warnings.push(error) },
  };
  vm.runInNewContext(
    `${source.replace(/^import .*\r?\n/, "").replace("export class CaptureStream", "class CaptureStream")}\n` +
      "this.CaptureStream = CaptureStream;",
    context,
  );
  const stream = new context.CaptureStream({
    rtc,
    url: "/api/window/stream?handle=0&frameRate=30",
    decoder,
    route: (socket, direct) => {
      routes.push({ socket, direct });
      trace.push(`route:${socket?.index ?? "none"}`);
    },
    closed: (event) => closed.push(event),
    releaseInput: (socket) => {
      if (!socket) return;
      trace.push(`release:${socket.index}`);
      if (socket.readyState === Socket.OPEN) socket.send("key-and-button-up");
    },
    status: (text) => statuses.push(text),
  });
  return {
    stream, rtc, sockets, attempts, trace, received, routes, statuses, closed, warnings, decoder, activeHandles,
    get rtcClosed() { return rtcClosed; },
  };
}

async function flush() {
  for (let index = 0; index < 8; index++) await Promise.resolve();
}

async function upgrade(environment) {
  environment.sockets[0].open();
  const handle = environment.attempts[0].resolve();
  await flush();
  return handle;
}

test("creates relay before requesting RTC and continues relay frames while upgrade is pending", async () => {
  const environment = setup(), relay = environment.sockets[0];
  assert.equal(relay.url.searchParams.get("directStreamId"), "0");
  assert.equal(relay.url.searchParams.get("client"), "1");
  assert.equal(environment.attempts.length, 0);
  relay.open();
  assert.equal(environment.attempts.length, 1);
  assert(environment.trace.indexOf("open:0") < environment.trace.indexOf("rtc.open"));
  relay.message([1, 2]);
  assert.deepEqual(environment.received, [{ data: [1, 2], socket: relay }]);
  assert.equal(relay.closeCount, 0);
  environment.stream.stop();
  await flush();
});

test("an unsuccessful RTC attempt leaves relay running without retries", async () => {
  const environment = setup(), relay = environment.sockets[0];
  relay.open();
  environment.attempts[0].reject(new Error("P2P unavailable"));
  await flush();
  assert.equal(environment.sockets.length, 1);
  assert.equal(relay.closeCount, 0);
  relay.message([3]);
  assert.deepEqual(environment.received, [{ data: [3], socket: relay }]);
  relay.onopen();
  assert.equal(environment.attempts.length, 1);
  assert.equal(environment.warnings.length, 1);
  environment.stream.stop();
});

test("P2P loss reopens relay once and does not restart the P2P attempt", async () => {
  const environment = setup();
  const direct = await upgrade(environment);
  const directSocket = environment.sockets[1];
  assert.equal(directSocket.url.searchParams.get("directStreamId"), String(direct.id));
  directSocket.open();
  environment.attempts[0].disconnected();
  const relay = environment.sockets[2];
  assert.equal(relay.url.searchParams.get("directStreamId"), "0");
  assert.equal(direct.closeCount, 1);
  assert.equal(directSocket.closeCount, 1);
  relay.open();
  environment.attempts[0].disconnected();
  assert.equal(environment.sockets.length, 3);
  assert.equal(environment.attempts.length, 1);
  assert.equal(environment.closed.length, 0);
  relay.message([4]);
  assert.equal(environment.received.at(-1).socket, relay);
  environment.stream.stop();
});

test("a failed direct control socket falls back and a failed relay closes the capture", async () => {
  const environment = setup();
  await upgrade(environment);
  environment.sockets[1].close(1011, "capture timeout");
  assert.equal(environment.sockets.length, 3);
  const relay = environment.sockets[2];
  relay.open();
  relay.close(1011, "server unavailable");
  assert.equal(environment.stream.stopped, true);
  assert.deepEqual(environment.closed, [{ code: 1011, reason: "server unavailable" }]);
  assert.equal(environment.attempts.length, 1);
  assert.equal(environment.sockets.length, 3);
});

test("direct chunks arriving before control socket open retain order and original socket identity", async () => {
  const environment = setup();
  await upgrade(environment);
  const directSocket = environment.sockets[1];
  environment.attempts[0].receive(Uint8Array.of(7, 8));
  environment.attempts[0].receive(Uint8Array.of(9));
  assert.equal(environment.received.length, 0);
  assert.equal(environment.stream.earlyBytes, 3);
  directSocket.open();
  assert.deepEqual(environment.received, [
    { data: [7, 8], socket: directSocket },
    { data: [9], socket: directSocket },
  ]);
  assert.equal(environment.stream.earlyChunks.length, 0);
  assert.equal(environment.stream.earlyBytes, 0);
  environment.stream.stop();
});

test("exceeding the early direct buffer limit drops its chunks and restores relay", async () => {
  const environment = setup();
  const direct = await upgrade(environment);
  const data = new Uint8Array(0x1000000);
  environment.attempts[0].receive(data);
  environment.attempts[0].receive(data);
  assert.equal(environment.stream.earlyBytes, 0x2000000);
  assert.equal(environment.sockets.length, 2);
  environment.attempts[0].receive(Uint8Array.of(1));
  assert.equal(environment.sockets.length, 3);
  assert.equal(environment.stream.direct, null);
  assert.equal(environment.stream.earlyChunks.length, 0);
  assert.equal(environment.stream.earlyBytes, 0);
  assert.equal(environment.received.length, 0);
  assert.equal(direct.closeCount, 1);
  environment.stream.stop();
});

test("stopping a pending upgrade cancels it and closes a late handle without reopening a socket", async () => {
  const environment = setup({ honorAbort: false });
  environment.sockets[0].open();
  environment.stream.stop();
  environment.stream.stop();
  assert.equal(environment.attempts[0].signal.aborted, true);
  assert.equal(environment.attempts[0].aborted, true);
  const direct = environment.attempts[0].resolve();
  await flush();
  assert.equal(direct.closeCount, 1);
  assert.equal(environment.sockets.length, 1);
  assert.equal(environment.sockets[0].closeCount, 1);
  assert.equal(environment.activeHandles.size, 0);
  assert.equal(environment.routes.at(-1).socket, null);
  assert.equal(environment.closed.length, 0);
  assert.equal(environment.warnings.length, 0);
});

test("route changes release old input before closure and ignore old socket events", async () => {
  const environment = setup();
  await upgrade(environment);
  const relay = environment.sockets[0], directSocket = environment.sockets[1];
  assert(environment.trace.indexOf("release:0") < environment.trace.indexOf("close:0"));
  assert.deepEqual(relay.sent, ["key-and-button-up"]);
  const statusCount = environment.statuses.length;
  relay.message([5]);
  relay.onclose({ code: 1011, reason: "old route" });
  relay.onerror();
  relay.onopen();
  assert.equal(environment.received.length, 0);
  assert.equal(environment.statuses.length, statusCount);
  assert.equal(environment.stream.socket, directSocket);
  assert.equal(environment.closed.length, 0);
  directSocket.open();
  environment.stream.stop();
  assert.deepEqual(directSocket.sent, ["key-and-button-up"]);
  assert(environment.trace.indexOf("release:1") < environment.trace.indexOf("close:1"));
});

test("stopping one capture closes only its handle in the shared RTC session", async () => {
  const environment = setup();
  const other = { id: 999, closeCount: 0 };
  environment.activeHandles.set(other.id, other);
  const direct = await upgrade(environment);
  environment.sockets[1].open();
  environment.stream.stop();
  assert.equal(direct.closeCount, 1);
  assert.equal(other.closeCount, 0);
  assert.equal(environment.activeHandles.get(other.id), other);
  assert.equal(environment.rtcClosed, 0);
  environment.attempts[0].receive(Uint8Array.of(1));
  environment.attempts[0].disconnected();
  assert.equal(environment.received.length, 0);
  assert.equal(environment.sockets.length, 2);
});

test("a capture without RTC support remains on relay and reports its final close once", () => {
  const environment = setup({ enabled: false }), relay = environment.sockets[0];
  relay.open();
  relay.message([6]);
  assert.equal(environment.attempts.length, 0);
  assert.equal(environment.received[0].socket, relay);
  relay.close();
  relay.onclose({ code: 1011, reason: "duplicate" });
  assert.equal(environment.closed.length, 1);
  assert.equal(environment.routes.at(-1).socket, null);
});
