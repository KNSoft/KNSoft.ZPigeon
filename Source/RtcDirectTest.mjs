import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";

const rtcSource = await readFile(new URL("./KNSoft.ZPigeon.Web/wwwroot/rtc.mjs", import.meta.url), "utf8"),
  nativeSource = await readFile(new URL("./Modules/Rtc/Client.cpp", import.meta.url), "utf8");

class MockEvents {
  listeners = new Map();

  addEventListener(type, listener, options) {
    const listeners = this.listeners.get(type) || [];
    listeners.push({ listener, once: options?.once });
    this.listeners.set(type, listeners);
  }

  removeEventListener(type, listener) {
    this.listeners.set(type, (this.listeners.get(type) || []).filter((entry) => entry.listener !== listener));
  }

  emit(type, event = {}) {
    for (const entry of [...(this.listeners.get(type) || [])]) {
      if (entry.once) this.removeEventListener(type, entry.listener);
      entry.listener(event);
    }
    this[`on${type}`]?.(event);
  }
}

function browser({ createOffer } = {}) {
  const sockets = [], peers = [], timers = new Map(), notices = [];
  let now = 0, timerId = 0, streamId = 0;

  class Socket extends MockEvents {
    static CONNECTING = 0;
    static OPEN = 1;
    static CLOSING = 2;
    static CLOSED = 3;
    readyState = Socket.CONNECTING;
    sent = [];
    closeCount = 0;

    constructor(url) {
      super();
      this.url = url;
      sockets.push(this);
    }

    message(value) {
      this.readyState = Socket.OPEN;
      this.emit("message", { data: JSON.stringify(value) });
    }

    send(value) {
      this.sent.push(JSON.parse(value));
    }

    close() {
      if (this.readyState === Socket.CLOSED) return;
      this.closeCount++;
      this.readyState = Socket.CLOSED;
      this.emit("close", { reason: "closed" });
    }
  }

  class Channel extends MockEvents {
    readyState = "connecting";
    closeCount = 0;

    open() {
      this.readyState = "open";
      this.emit("open");
    }

    close() {
      if (this.readyState === "closed") return;
      this.closeCount++;
      this.readyState = "closed";
      this.emit("close");
    }
  }

  class Peer extends MockEvents {
    connectionState = "new";
    iceGatheringState = "complete";
    closeCount = 0;

    constructor(configuration) {
      super();
      this.configuration = configuration;
      peers.push(this);
    }

    createDataChannel(label, options) {
      this.label = label;
      this.options = options;
      return (this.channel = new Channel());
    }

    async createOffer() {
      if (createOffer) return createOffer(this);
      return { type: "offer", sdp: "test-offer" };
    }

    async setLocalDescription(description) {
      this.localDescription = description;
    }

    async setRemoteDescription(description) {
      this.remoteDescription = description;
      this.connectionState = "connected";
    }

    close() {
      if (this.connectionState === "closed") return;
      this.closeCount++;
      this.connectionState = "closed";
      this.emit("connectionstatechange");
    }
  }

  const context = {
    URL,
    ArrayBuffer,
    Uint8Array,
    Uint32Array,
    DataView,
    AbortController,
    DOMException,
    WebSocket: Socket,
    RTCPeerConnection: Peer,
    location: { href: "http://127.0.0.1:9983/client?client=1", protocol: "http:" },
    apiUrl: (url) => url,
    crypto: { getRandomValues: (array) => { array[0] = ++streamId; return array; } },
    performance: { now: () => now },
    setTimeout: (callback, delay) => {
      timers.set(++timerId, { callback, due: now + delay });
      return timerId;
    },
    clearTimeout: (id) => timers.delete(id),
  };
  vm.runInNewContext(
    `${rtcSource.replace(/^import .*\r?\n/, "").replace("export class RtcDirect", "class RtcDirect")}\n` +
      "this.RtcDirect = RtcDirect;",
    context,
  );
  const rtc = new context.RtcDirect((notice) => notices.push(notice));
  return {
    rtc,
    sockets,
    peers,
    timers,
    notices,
    reuseStreamId(id) { streamId = id - 1; },
    advance(milliseconds) {
      now += milliseconds;
      for (const [id, timer] of [...timers]) {
        if (timer.due > now) continue;
        timers.delete(id);
        timer.callback();
      }
    },
  };
}

async function flush() {
  for (let index = 0; index < 12; index++) await Promise.resolve();
}

async function configured(environment, index = 0) {
  environment.sockets[index].message({ iceServers: ["stun:test:3478"] });
  await flush();
  assert.equal(environment.sockets[index].sent[0].offer, "test-offer");
  return environment.peers.at(-1);
}

async function connected(environment, index = 0) {
  const peer = await configured(environment, index);
  environment.sockets[index].message({ answer: "test-answer" });
  await flush();
  peer.channel.open();
  await flush();
}

test("one P2P connection routes independent stream IDs and survives closing another handler", async () => {
  const environment = browser(), received = [], disconnected = [];
  const firstOpen = environment.rtc.open((data) => received.push([...data]), null,
    () => disconnected.push("first"));
  await connected(environment);
  const first = await firstOpen;
  const second = await environment.rtc.open(() => {}, null, () => disconnected.push("second"));
  const packet = new ArrayBuffer(6);
  new DataView(packet).setUint32(0, first.id, true);
  new Uint8Array(packet, 4).set([7, 9]);
  environment.peers[0].channel.emit("message", { data: packet });
  assert.deepEqual(received, [[7, 9]]);
  first.close();
  first.close();
  assert.equal(environment.peers[0].closeCount, 0);
  assert.equal(environment.rtc.handlers.size, 1);
  second.close();
  assert.equal(environment.peers[0].closeCount, 1);
  assert.equal(environment.sockets[0].closeCount, 1);
  assert.deepEqual(disconnected, []);
  assert.equal(environment.timers.size, 0);
});

test("canceling the only pending open releases its attempt and allows reopening", async () => {
  const environment = browser(), cancellation = new AbortController();
  const pending = environment.rtc.open(() => {}, null, null, cancellation.signal);
  const rejected = assert.rejects(pending);
  cancellation.abort();
  await rejected;
  assert.equal(environment.sockets[0].closeCount, 1);
  const nextOpen = environment.rtc.open(() => {});
  await connected(environment, 1);
  const next = await nextOpen;
  assert.equal(environment.rtc.handlers.size, 1);
  next.close();
  assert.equal(environment.timers.size, 0);
});

test("an already canceled open creates no signaling attempt", async () => {
  const environment = browser(), cancellation = new AbortController();
  cancellation.abort();
  await assert.rejects(environment.rtc.open(() => {}, null, null, cancellation.signal));
  assert.equal(environment.sockets.length, 0);
  assert.equal(environment.peers.length, 0);
  assert.equal(environment.rtc.pendingOpens, 0);
  assert.equal(environment.timers.size, 0);
});

test("canceling one pending open preserves another caller sharing its attempt", async () => {
  const environment = browser(), cancellation = new AbortController();
  const first = environment.rtc.open(() => {}, null, null, cancellation.signal);
  const second = environment.rtc.open(() => {});
  const rejected = assert.rejects(first);
  cancellation.abort();
  await rejected;
  assert.equal(environment.sockets[0].closeCount, 0);
  await connected(environment);
  const remaining = await second;
  assert.equal(environment.sockets.length, 1);
  assert.equal(environment.rtc.handlers.size, 1);
  remaining.close();
  assert.equal(environment.timers.size, 0);
});

test("a new pending open keeps the channel alive while the last registered handler closes", async () => {
  const environment = browser();
  const firstOpen = environment.rtc.open(() => {});
  await connected(environment);
  const first = await firstOpen;
  const pending = environment.rtc.open(() => {});
  first.close();
  const next = await pending;
  assert.equal(environment.sockets.length, 1);
  assert.equal(environment.peers[0].closeCount, 0);
  assert.equal(environment.rtc.handlers.size, 1);
  next.close();
});

test("DataChannel closure notifies each handler once and releases all resources", async () => {
  const environment = browser(), disconnected = [];
  const firstOpen = environment.rtc.open(() => {}, null, () => disconnected.push("first"));
  await connected(environment);
  await firstOpen;
  await environment.rtc.open(() => {}, null, () => disconnected.push("second"));
  environment.peers[0].channel.close();
  environment.sockets[0].emit("close");
  environment.peers[0].emit("connectionstatechange");
  assert.deepEqual(disconnected, ["first", "second"]);
  assert.equal(environment.rtc.handlers.size, 0);
  assert.equal(environment.peers[0].closeCount, 1);
  assert.equal(environment.sockets[0].closeCount, 1);
  assert.equal(environment.notices.length, 1);
  assert.equal(environment.timers.size, 0);
});

test("events from a closed attempt cannot close a replacement P2P connection", async () => {
  const environment = browser();
  const firstOpen = environment.rtc.open(() => {});
  await connected(environment);
  (await firstOpen).close();
  const nextOpen = environment.rtc.open(() => {});
  await connected(environment, 1);
  const next = await nextOpen;
  environment.sockets[0].emit("close");
  environment.peers[0].channel.emit("close");
  environment.peers[0].emit("connectionstatechange");
  assert.equal(environment.peers[1].closeCount, 0);
  assert.equal(environment.rtc.handlers.size, 1);
  next.close();
});

test("a stale handle cannot unregister a replacement stream that reuses its ID", async () => {
  const environment = browser();
  const firstOpen = environment.rtc.open(() => {});
  await connected(environment);
  const first = await firstOpen;
  environment.peers[0].channel.close();
  environment.reuseStreamId(first.id);
  const nextOpen = environment.rtc.open(() => {});
  await connected(environment, 1);
  const next = await nextOpen;
  assert.equal(next.id, first.id);
  first.close();
  assert.equal(environment.rtc.handlers.size, 1);
  assert.equal(environment.peers[1].closeCount, 0);
  next.close();
});

test("negotiation has one 60 second deadline across configuration, answer and channel opening", async () => {
  const environment = browser();
  const pending = environment.rtc.open(() => {});
  const rejected = assert.rejects(pending);
  environment.advance(25000);
  await configured(environment);
  environment.advance(25000);
  environment.sockets[0].message({ answer: "test-answer" });
  await flush();
  environment.advance(11000);
  await rejected;
  assert.equal(environment.peers[0].closeCount, 1);
  assert.equal(environment.sockets[0].closeCount, 1);
  assert.equal(environment.timers.size, 0);
});

test("the overall negotiation deadline cancels a stalled browser offer operation", { timeout: 1000 }, async () => {
  const environment = browser({ createOffer: () => new Promise(() => {}) });
  const pending = environment.rtc.open(() => {});
  const rejected = assert.rejects(pending);
  environment.sockets[0].message({ iceServers: [] });
  await flush();
  environment.advance(60001);
  await rejected;
  assert.equal(environment.peers[0].closeCount, 1);
  assert.equal(environment.sockets[0].closeCount, 1);
  assert.equal(environment.timers.size, 0);
});

test("cancellation observes a browser operation that rejects after its signal was already aborted", async () => {
  let environment;
  environment = browser({ createOffer: () => {
    environment.advance(60001);
    return Promise.reject(new Error("offer canceled"));
  } });
  const pending = environment.rtc.open(() => {});
  const rejected = assert.rejects(pending);
  environment.sockets[0].message({ iceServers: [] });
  await rejected;
  await flush();
  assert.equal(environment.peers[0].closeCount, 1);
  assert.equal(environment.timers.size, 0);
});

function embeddedPage() {
  const handlers = new Map(), messages = [], intervals = [], released = [];
  const literals = nativeSource.split("static const WCHAR ZpRtcPage[] =")[1].split(/\r?\n\r?\nstatic/)[0];
  const page = [...literals.matchAll(/L("(?:[^"\\]|\\.)*")/g)].map((match) => JSON.parse(match[1])).join("");
  const source = page.split("<script>")[1].split("</script>")[0];
  const channel = {
    readyState: "open",
    bufferedAmount: 0,
    send(buffer) { this.bufferedAmount += buffer.byteLength; },
  };
  let peer;
  class Peer {
    constructor(configuration) {
      peer = this;
      this.configuration = configuration;
      this.connectionState = "connected";
      this.iceGatheringState = "complete";
      this.localDescription = { sdp: "test-answer" };
      this.pair = { id: "path1", type: "candidate-pair", bytesSent: 0, timestamp: 1000 };
    }
    async setRemoteDescription() { this.ondatachannel({ channel }); }
    async createAnswer() { return { type: "answer", sdp: "test-answer" }; }
    async setLocalDescription() {}
    async getStats() {
      return new Map([
        ["transport", { type: "transport", selectedCandidatePairId: this.pair.id }],
        [this.pair.id, this.pair],
      ]);
    }
  }
  vm.runInNewContext(source, {
    chrome: { webview: {
      postMessage: (value) => messages.push(value),
      addEventListener: (type, callback) => handlers.set(type, callback),
      releaseBuffer: (buffer) => released.push(buffer),
    } },
    RTCPeerConnection: Peer,
    setInterval: (callback) => intervals.push(callback),
    setTimeout,
    clearTimeout,
  });
  return { handlers, messages, intervals, released, channel, get peer() { return peer; } };
}

test("native RTC queue follows throughput and resets its bounded budget after a path change", async () => {
  const environment = embeddedPage();
  await environment.handlers.get("message")({ data: "CONFIG\nstun:test:3478" });
  await environment.handlers.get("message")({ data: "OFFER\ntest-offer" });
  assert.equal(environment.peer.configuration.iceServers[0].urls, "stun:test:3478");
  environment.channel.onopen();
  assert.equal(environment.channel.bufferedAmountLowThreshold, 16384);
  await environment.intervals[0]();
  environment.peer.pair = { ...environment.peer.pair, bytesSent: 1000000, timestamp: 2000 };
  await environment.intervals[0]();
  assert.equal(environment.channel.bufferedAmountLowThreshold, 131072);
  environment.peer.pair = { ...environment.peer.pair, bytesSent: 1001000, timestamp: 3000 };
  await environment.intervals[0]();
  assert.equal(environment.channel.bufferedAmountLowThreshold, 16384);
  environment.peer.pair = { ...environment.peer.pair, bytesSent: 2001000, timestamp: 4000 };
  await environment.intervals[0]();
  assert.equal(environment.channel.bufferedAmountLowThreshold, 131072);
  environment.peer.pair = { ...environment.peer.pair, id: "path2", bytesSent: 0, timestamp: 5000 };
  await environment.intervals[0]();
  assert.equal(environment.channel.bufferedAmountLowThreshold, 16384);
});

test("native RTC drain acknowledges a new buffer once and releases shared buffers on send errors", async () => {
  const environment = embeddedPage();
  await environment.handlers.get("message")({ data: "OFFER\ntest-offer" });
  environment.channel.onopen();
  const buffer = new ArrayBuffer(16388);
  environment.handlers.get("sharedbufferreceived")({ getBuffer: () => buffer });
  assert.equal(environment.messages.filter((message) => message === "DRAIN").length, 0);
  environment.channel.bufferedAmount = 0;
  environment.channel.onbufferedamountlow();
  environment.channel.onbufferedamountlow();
  await environment.intervals[0]();
  assert.equal(environment.messages.filter((message) => message === "DRAIN").length, 1);
  environment.channel.readyState = "closed";
  environment.handlers.get("sharedbufferreceived")({ getBuffer: () => buffer });
  assert.equal(environment.released.length, 2);
  assert(environment.messages.some((message) => message.startsWith("ERROR\n")));
});
