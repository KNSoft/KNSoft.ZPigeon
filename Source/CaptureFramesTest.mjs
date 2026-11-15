import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import vm from "node:vm";
import { CaptureFrameDecoder } from "./KNSoft.ZPigeon.Web/wwwroot/capture-frames.mjs";

class MockVideoDecoder {
  static configurations = [];
  static instances = [];
  static supported = () => true;

  static async isConfigSupported(config) {
    this.configurations.push(config);
    return { supported: this.supported(config) };
  }

  constructor(callbacks) {
    this.callbacks = callbacks;
    this.state = "unconfigured";
    this.decoded = [];
    MockVideoDecoder.instances.push(this);
  }

  configure(config) {
    this.config = config;
    this.state = "configured";
  }

  decode(chunk) {
    this.chunk = chunk;
    this.decoded.push(chunk);
  }

  close() {
    this.state = "closed";
  }
}

globalThis.VideoDecoder = MockVideoDecoder;
globalThis.EncodedVideoChunk = class {
  constructor(init) {
    Object.assign(this, init);
  }
};
globalThis.createImageBitmap = async () => ({ close() {} });
const scheduledPaints = new Map();
let nextPaint = 0;
globalThis.requestAnimationFrame = (callback) => {
  scheduledPaints.set(++nextPaint, callback);
  return nextPaint;
};
globalThis.cancelAnimationFrame = (id) => scheduledPaints.delete(id);

function paint() {
  const callbacks = [...scheduledPaints.values()];
  scheduledPaints.clear();
  for (const callback of callbacks) callback(performance.now());
}

function setup(supported) {
  scheduledPaints.clear();
  globalThis.document = {
    hidden: false,
    addEventListener: (name, callback) => { document[name] = callback; },
  };
  MockVideoDecoder.configurations = [];
  MockVideoDecoder.instances = [];
  MockVideoDecoder.supported = supported;
  const drawn = [], renderMilliseconds = [], statistics = [],
    canvas = {
      width: 0,
      height: 0,
      hidden: true,
      getContext: () => ({ drawImage: (image) => drawn.push(image), globalCompositeOperation: "source-over" }),
    },
    status = { textContent: "" },
    socket = {},
    acknowledgements = [],
    reports = [],
    decoder = new CaptureFrameDecoder(
      canvas,
      status,
      (candidate) => candidate === socket,
      (sequence, keyframe, socket, delay) => {
        acknowledgements.push({ sequence, keyframe });
        renderMilliseconds.push(delay);
      },
      (codecs, width, height) => reports.push({ codecs, width, height }),
      (stats) => {
        statistics.push(stats);
        status.title = `P95 ${stats.receiveToDrawP95Milliseconds} ms`;
      },
    );
  return { decoder, status, socket, acknowledgements, reports, drawn, renderMilliseconds, statistics };
}

function frame(type, width = 1918, height = 1078) {
  return {
    type,
    sequence: 1,
    canvasWidth: width,
    canvasHeight: height,
    left: 0,
    top: 0,
    width,
    height,
    image: Uint8Array.of(1),
  };
}

test("uses the exact supported decoder configuration", async () => {
  const context = setup((config) => config.codec.startsWith("avc1"));
  await context.decoder.draw(frame(3), context.socket);

  assert.equal(MockVideoDecoder.configurations.length, 2);
  for (const config of MockVideoDecoder.configurations) {
    assert.equal(config.codedWidth, 1918);
    assert.equal(config.codedHeight, 1078);
    assert.equal(config.hardwareAcceleration, undefined);
  }
  assert.deepEqual(MockVideoDecoder.instances[0].config, MockVideoDecoder.configurations[0]);
  assert.deepEqual(context.reports, [{ codecs: 1, width: 1918, height: 1078 }]);
  assert.deepEqual(context.acknowledgements, []);
  let closed = false;
  MockVideoDecoder.instances[0].callbacks.output({ timestamp: 1000, close() { closed = true; } });
  paint();
  assert.equal(closed, true);
  assert.deepEqual(context.acknowledgements, [{ sequence: 1, keyframe: false }]);
});

test("reports an unsupported incoming codec without retrying it", async () => {
  const context = setup((config) => config.codec.startsWith("avc1"));
  await context.decoder.draw(frame(5), context.socket);

  assert.equal(MockVideoDecoder.instances.length, 0);
  assert.deepEqual(context.reports, [{ codecs: 1, width: 1918, height: 1078 }]);
  assert.deepEqual(context.acknowledgements, [{ sequence: 1, keyframe: false }]);
});

test("removes a codec after an asynchronous decoder failure", async () => {
  const context = setup(() => true),
    warn = console.warn;
  await context.decoder.draw(frame(5), context.socket);
  console.warn = () => {};
  try {
    MockVideoDecoder.instances[0].callbacks.error(new DOMException("Unsupported configuration", "OperationError"));
  } finally {
    console.warn = warn;
  }

  assert.deepEqual(context.reports, [
    { codecs: 3, width: 1918, height: 1078 },
    { codecs: 1, width: 1918, height: 1078 },
  ]);
  assert.deepEqual(context.acknowledgements, [{ sequence: 1, keyframe: true }]);
  assert.equal(context.decoder.videoDecoder, null);
});

test("an asynchronous decoder failure acknowledges and closes the latest frame waiting for paint", async () => {
  const context = setup(() => true), warn = console.warn;
  await context.decoder.draw(frame(5), context.socket);
  await context.decoder.draw({ ...frame(6), sequence: 2 }, context.socket);
  const decoder = MockVideoDecoder.instances[0];
  let closed = 0;
  decoder.callbacks.output({ timestamp: 1000, close() { closed++; } });
  decoder.callbacks.output({ timestamp: 2000, close() { closed++; } });
  const canceled = [...scheduledPaints.values()][0];
  assert.equal(context.decoder.videoFrames.size, 0);
  console.warn = () => {};
  try {
    decoder.callbacks.error(new DOMException("Decoder failed after output", "OperationError"));
  } finally {
    console.warn = warn;
  }
  assert.equal(closed, 2);
  assert.equal(scheduledPaints.size, 0);
  assert.deepEqual(context.acknowledgements, [{ sequence: 2, keyframe: true }]);
  assert.deepEqual(context.renderMilliseconds, [0]);
  await context.decoder.draw({ ...frame(3), sequence: 3 }, context.socket);
  MockVideoDecoder.instances[1].callbacks.output({ timestamp: 3000, close() { closed++; } });
  canceled();
  assert.deepEqual(context.drawn, []);
  paint();
  assert.equal(closed, 3);
  assert.equal(context.drawn.length, 1);
  assert.deepEqual(context.acknowledgements, [{ sequence: 2, keyframe: true }, { sequence: 3, keyframe: false }]);
});

test("probes the exact even video size while rendering an odd image size", async () => {
  const context = setup(() => true);
  await context.decoder.draw(frame(1, 1919, 1079), context.socket);

  for (const config of MockVideoDecoder.configurations) {
    assert.equal(config.codedWidth, 1918);
    assert.equal(config.codedHeight, 1078);
  }
  assert.deepEqual(context.reports, [{ codecs: 3, width: 1918, height: 1078 }]);
});


test("bounds submitted video frames and requests recovery before the ninth frame", async () => {
  const context = setup(() => true);
  for (let sequence = 1; sequence <= 9; sequence++) {
    await context.decoder.draw({ ...frame(sequence === 1 ? 3 : 4), sequence }, context.socket);
    assert.ok(context.decoder.videoFrames.size <= 8);
  }
  assert.equal(context.decoder.videoDecoder, null);
  assert.equal(context.decoder.videoFrames.size, 0);
  assert.deepEqual(context.acknowledgements, [{ sequence: 9, keyframe: true }]);
});

test("a cumulative recovery suppresses stale decoder output and acknowledgements", async () => {
  const context = setup(() => true);
  await context.decoder.draw(frame(3), context.socket);
  const decoder = MockVideoDecoder.instances[0];
  await context.decoder.draw({ ...frame(4), sequence: 2 }, context.socket);
  context.decoder.recover({ sequence: 2 }, context.socket);
  let closed = false;
  decoder.callbacks.output({ timestamp: 1000, close() { closed = true; } });
  context.decoder.acknowledgeOnce(1, false, context.socket);
  assert.equal(closed, true);
  assert.deepEqual(context.acknowledgements, [{ sequence: 2, keyframe: true }]);
});

test("cumulative acknowledgements handle sequence wrap and reject duplicates", () => {
  const context = setup(() => true);
  context.decoder.acknowledgeOnce(0xffffffff, false, context.socket);
  context.decoder.acknowledgeOnce(1, false, context.socket);
  context.decoder.acknowledgeOnce(0xffffffff, false, context.socket);
  context.decoder.acknowledgeOnce(1, false, context.socket);
  assert.deepEqual(context.acknowledgements, [
    { sequence: 0xffffffff, keyframe: false }, { sequence: 1, keyframe: false },
  ]);
});

test("reset invalidates an image decode already waiting on a bitmap", async () => {
  const context = setup(() => true), original = globalThis.createImageBitmap;
  let finish;
  globalThis.createImageBitmap = () => new Promise((resolve) => { finish = resolve; });
  try {
    const pending = context.decoder.draw(frame(1), context.socket);
    while (!finish) await Promise.resolve();
    context.decoder.reset();
    let closed = false;
    finish({ close() { closed = true; } });
    await pending;
    assert.equal(closed, true);
    assert.deepEqual(context.acknowledgements, []);
    assert.equal(context.decoder.sequence, 0);
  } finally {
    globalThis.createImageBitmap = original;
  }
});

test("receiver metrics keep bounded samples and record only displayed frames", async () => {
  const context = setup(() => true);
  await context.decoder.draw(frame(3), context.socket);
  assert.equal(context.decoder.statistics.displayedFrames, 0);
  MockVideoDecoder.instances[0].callbacks.output({ timestamp: 1000, close() {} });
  paint();
  assert.equal(context.decoder.statistics.displayedFrames, 1);
  for (let sequence = 2; sequence <= 150; sequence++) context.decoder.rendered({ sequence });
  assert.ok(context.decoder.renderDelays.length <= 120);
  assert.ok(context.decoder.displayTimes.length <= 120);
  assert.match(context.status.title, /P95/);
});

function wireFrame(type, sequence, bytes = 1) {
  const data = new Uint8Array(21 + bytes), view = new DataView(data.buffer);
  view.setUint8(0, type);
  view.setUint32(1, sequence, true);
  for (const offset of [5, 7, 13, 15]) view.setUint16(offset, 128, true);
  view.setUint32(17, bytes, true);
  return data;
}

test("parser applies backpressure to an incoming burst before allocating unbounded decode work", async () => {
  const context = setup(() => true);
  for (let sequence = 1; sequence <= 9; sequence++)
    context.decoder.receive(wireFrame(sequence === 1 ? 3 : 4, sequence), context.socket);
  await context.decoder.decode;
  assert.equal(context.decoder.queuedFrames, 0);
  assert.equal(context.decoder.queuedBytes, 0);
  assert.deepEqual(context.acknowledgements, [{ sequence: 9, keyframe: true }]);
});

test("parser assembles a split header and body and acknowledges only displayed output", async () => {
  const context = setup(() => true), data = wireFrame(3, 1, 4);
  context.decoder.receive(data.subarray(0, 10), context.socket);
  context.decoder.receive(data.subarray(10, 22), context.socket);
  context.decoder.receive(data.subarray(22), context.socket);
  await context.decoder.decode;
  assert.deepEqual(context.acknowledgements, []);
  MockVideoDecoder.instances[0].callbacks.output({ timestamp: 1000, close() {} });
  paint();
  assert.deepEqual(context.acknowledgements, [{ sequence: 1, keyframe: false }]);
  assert.equal(context.decoder.statistics.receivedBytes, data.length);
});


test("video decoder byte budget is enforced even with fewer than eight frames", async () => {
  const context = setup(() => true), image = new Uint8Array(0x1000000);
  for (let sequence = 1; sequence <= 3; sequence++) {
    await context.decoder.draw({ ...frame(sequence === 1 ? 3 : 4), sequence, image }, context.socket);
    assert.ok(context.decoder.videoBytes <= 0x2000000);
  }
  assert.equal(context.decoder.videoDecoder, null);
  assert.equal(context.decoder.videoBytes, 0);
  assert.deepEqual(context.acknowledgements, [{ sequence: 3, keyframe: true }]);
});


test("repeated recovery keeps stale queued images in the byte and frame budget until they settle", async () => {
  const context = setup(() => true), original = globalThis.createImageBitmap;
  let finish;
  globalThis.createImageBitmap = () => new Promise((resolve) => { finish = resolve; });
  try {
    context.decoder.receive(wireFrame(1, 1), context.socket);
    while (!finish) await Promise.resolve();
    for (let sequence = 2; sequence <= 40; sequence++)
      context.decoder.receive(wireFrame(2, sequence), context.socket);
    assert.equal(context.decoder.queuedFrames, 8);
    assert.equal(context.decoder.queuedBytes, 8);
    assert.equal(context.decoder.acknowledgedSequence, 40);
    finish({ close() {} });
    await context.decoder.decode;
    assert.equal(context.decoder.queuedFrames, 0);
    assert.equal(context.decoder.queuedBytes, 0);
    assert.equal(context.decoder.statistics.displayedFrames, 0);
    assert.deepEqual(context.statistics, []);
  } finally {
    globalThis.createImageBitmap = original;
  }
});

test("video presentation keeps the latest decoded frame per paint and cumulatively acknowledges it", async () => {
  const context = setup(() => true);
  await context.decoder.draw(frame(3), context.socket);
  await context.decoder.draw({ ...frame(4), sequence: 2 }, context.socket);
  const decoder = MockVideoDecoder.instances[0], closed = [0, 0];
  const first = { timestamp: 1000, close() { closed[0]++; } };
  const latest = { timestamp: 2000, close() { closed[1]++; } };
  decoder.callbacks.output(first);
  decoder.callbacks.output(latest);
  assert.deepEqual(decoder.decoded.map((chunk) => chunk.type), ["key", "delta"]);
  assert.deepEqual(context.drawn, []);
  assert.deepEqual(context.acknowledgements, []);
  assert.deepEqual(closed, [1, 0]);
  paint();
  assert.deepEqual(context.drawn, [latest]);
  assert.deepEqual(closed, [1, 1]);
  assert.deepEqual(context.acknowledgements, [{ sequence: 2, keyframe: false }]);
  assert.equal(context.decoder.statistics.displayedFrames, 1);
  const hiddenClosed = [0, 0];
  await context.decoder.draw({ ...frame(4), sequence: 3 }, context.socket);
  decoder.callbacks.output({ timestamp: 3000, close() { hiddenClosed[0]++; } });
  const canceled = [...scheduledPaints.values()][0];
  await context.decoder.draw({ ...frame(4), sequence: 4 }, context.socket);
  document.hidden = true;
  document.visibilitychange();
  assert.deepEqual(hiddenClosed, [1, 0]);
  assert.equal(context.acknowledgements.at(-1).sequence, 3);
  assert.equal(context.renderMilliseconds.at(-1), 0);
  assert.equal(scheduledPaints.size, 0);
  decoder.callbacks.output({ timestamp: 4000, close() { hiddenClosed[1]++; } });
  assert.deepEqual(hiddenClosed, [1, 1]);
  assert.equal(context.acknowledgements.at(-1).sequence, 4);
  assert.equal(context.renderMilliseconds.at(-1), 0);
  assert.equal(context.decoder.statistics.displayedFrames, 1);
  document.hidden = false;
  await context.decoder.draw({ ...frame(4), sequence: 5 }, context.socket);
  const foreground = { timestamp: 5000, close() {} };
  decoder.callbacks.output(foreground);
  canceled();
  assert.deepEqual(context.drawn, [latest]);
  paint();
  assert.deepEqual(context.drawn, [latest, foreground]);
  assert.equal(context.acknowledgements.at(-1).sequence, 5);
  assert.equal(decoder.decoded.length, 5);
});

test("reset closes a decoded frame waiting for paint and invalidates its callback", async () => {
  const context = setup(() => true);
  await context.decoder.draw(frame(3), context.socket);
  let closed = 0;
  MockVideoDecoder.instances[0].callbacks.output({ timestamp: 1000, close() { closed++; } });
  const canceled = [...scheduledPaints.values()][0];
  context.decoder.reset();
  await context.decoder.draw(frame(3), context.socket);
  MockVideoDecoder.instances[1].callbacks.output({ timestamp: 1000, close() { closed++; } });
  canceled();
  assert.equal(closed, 1);
  assert.deepEqual(context.drawn, []);
  assert.deepEqual(context.acknowledgements, []);
  paint();
  assert.equal(closed, 2);
  assert.equal(context.drawn.length, 1);
  assert.deepEqual(context.acknowledgements, [{ sequence: 1, keyframe: false }]);
});

test("image statistics report each display after returning its queued frame", async () => {
  const context = setup(() => true);
  context.decoder.receive(wireFrame(1, 1), context.socket);
  context.decoder.receive(wireFrame(2, 2), context.socket);
  await context.decoder.decode;
  assert.equal(context.drawn.length, 2);
  assert.deepEqual(context.statistics.map((stats) => stats.displayedFrames), [1, 2]);
  assert.deepEqual(context.statistics.map((stats) => stats.pendingFrames), [1, 0]);
  assert.equal(context.decoder.queuedFrames, 0);
  assert.equal(context.decoder.queuedBytes, 0);
});

test("a display within the receiving clock tick carries a nonzero acknowledgement delay", async () => {
  const original = Object.getOwnPropertyDescriptor(globalThis, "performance");
  Object.defineProperty(globalThis, "performance", { configurable: true, value: { now: () => 100 } });
  try {
    const context = setup(() => true);
    context.decoder.receive(wireFrame(1, 1), context.socket);
    await context.decoder.decode;
    assert.equal(context.drawn.length, 1);
    context.decoder.recover({ sequence: 2 }, context.socket);
    assert.deepEqual(context.renderMilliseconds, [1, 0]);
  } finally {
    Object.defineProperty(globalThis, "performance", original);
  }
});

test("display acknowledgements serialize capped render delay while recovery acknowledgements carry zero", async () => {
  const original = Object.getOwnPropertyDescriptor(globalThis, "performance");
  let now = 100;
  Object.defineProperty(globalThis, "performance", { configurable: true, value: { now: () => now } });
  try {
    const context = setup(() => true);
    context.decoder.receive(wireFrame(1, 1), context.socket);
    now = 113.2;
    await context.decoder.decode;
    assert.deepEqual(context.renderMilliseconds, [14]);
    assert.equal(context.decoder.rendered({ received: now - 70000 }), 65535);
    context.decoder.recover({ sequence: 2 }, context.socket);
    assert.deepEqual(context.renderMilliseconds, [14, 0]);
    const sources = await Promise.all(["execution.mjs", "management.mjs"].map((name) =>
      readFile(new URL(`./KNSoft.ZPigeon.Web/wwwroot/${name}`, import.meta.url), "utf8")));
    for (const source of sources) {
      const start = source.indexOf("  acknowledgeFrame("),
        end = source.indexOf("\n  reportVideoCodecs(", start),
        method = source.slice(start, end),
        receiver = vm.runInNewContext(`({${method}})`, { WebSocket: { OPEN: 1 }, ArrayBuffer, DataView }),
        sent = [],
        socket = { readyState: 1, send: (data) => sent.push(data) };
      receiver.send = (data, target) => target.send(data);
      receiver.acknowledgeFrame(1, false, socket, context.renderMilliseconds[0]);
      receiver.acknowledgeFrame(2, true, socket, context.renderMilliseconds[1]);
      receiver.acknowledgeFrame(3, false, socket, 65535);
      assert(sent.every((data) => data.byteLength === 8));
      assert.deepEqual([...new Uint8Array(sent[0])], [4, 0, 1, 0, 0, 0, 14, 0]);
      assert.deepEqual([...new Uint8Array(sent[1])], [4, 1, 2, 0, 0, 0, 0, 0]);
      assert.equal(new DataView(sent[2]).getUint16(6, true), 65535);
    }
  } finally {
    Object.defineProperty(globalThis, "performance", original);
  }
});
