import assert from "node:assert/strict";
import test from "node:test";
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
    MockVideoDecoder.instances.push(this);
  }

  configure(config) {
    this.config = config;
    this.state = "configured";
  }

  decode(chunk) {
    this.chunk = chunk;
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

function setup(supported) {
  MockVideoDecoder.configurations = [];
  MockVideoDecoder.instances = [];
  MockVideoDecoder.supported = supported;
  const canvas = {
      width: 0,
      height: 0,
      hidden: true,
      getContext: () => ({ drawImage() {}, globalCompositeOperation: "source-over" }),
    },
    status = { textContent: "" },
    socket = {},
    acknowledgements = [],
    reports = [],
    decoder = new CaptureFrameDecoder(
      canvas,
      status,
      (candidate) => candidate === socket,
      (sequence, keyframe) => acknowledgements.push({ sequence, keyframe }),
      (codecs, width, height) => reports.push({ codecs, width, height }),
    );
  return { decoder, status, socket, acknowledgements, reports };
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
  assert.deepEqual(context.acknowledgements, [{ sequence: 1, keyframe: false }]);
  assert.equal(context.decoder.videoDecoder, null);
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
