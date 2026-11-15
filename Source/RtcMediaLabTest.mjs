import assert from "node:assert/strict";

let now = 0, nextTimer = 1, exported, canvasFrameRate, displayOptions;
const timers = new Map();
const ids = ["settings", "source", "mode", "hint", "degradation", "codec", "bitrate", "fps", "duration",
  "scenario", "start", "apply", "stop", "download", "status", "sendBitrate", "receiveFps", "actualCodec",
  "resolution", "latency", "encodeTime", "bridgeTime", "totalBytes", "received", "trace", "synthetic"];
const controls = Object.fromEntries(ids.map((id) => [id, {
  id, value: "", disabled: false, handlers: {}, options: [],
  addEventListener(name, callback) { this.handlers[name] = callback; },
  add(option) { this.options.push(option); },
  reportValidity() { return true; },
}]));
Object.assign(controls.received, {
  play: async () => {},
  requestVideoFrameCallback(callback) { this.presentation = callback; return 1; },
  cancelVideoFrameCallback() { this.presentation = null; },
});
Object.assign(controls.synthetic, {
  width: 1280, height: 720,
  getContext: () => ({ fillRect() {}, fillText() {} }),
  captureStream: (frameRate) => {
    canvasFrameRate = frameRate;
    return new MediaStream([new Track()]);
  },
});
const settings = { source: "synthetic", mode: "track", hint: "text", degradation: "maintain-resolution",
  codec: "", bitrate: "5", fps: "30", duration: "300", scenario: "Contract regression" };
for (const [id, value] of Object.entries(settings)) controls[id].value = value;
globalThis.document = {
  documentElement: {},
  body: {},
  nodeType: 9,
  querySelectorAll: (selector) => selector === "[id]" ? Object.values(controls) : [],
  createElement: () => ({ click() {} }),
};
globalThis.Node = { ELEMENT_NODE: 1, TEXT_NODE: 3, DOCUMENT_NODE: 9 };
globalThis.Option = class { constructor(text, value) { Object.assign(this, { text, value }); } };
globalThis.performance = { now: () => now };
globalThis.setTimeout = (callback, milliseconds) => {
  const id = nextTimer++;
  timers.set(id, { callback, deadline: now + milliseconds });
  return id;
};
globalThis.clearTimeout = (id) => timers.delete(id);
globalThis.requestAnimationFrame = () => 1;
globalThis.cancelAnimationFrame = () => {};
globalThis.window = { isSecureContext: true, addEventListener() {} };
URL.createObjectURL = (blob) => { exported = blob; return "blob:contract-test"; };
URL.revokeObjectURL = () => {};

class Track {
  stopped = false;
  handlers = {};
  getSettings() { return { width: 1280, height: 720, frameRate: 60 }; }
  addEventListener(name, callback) { this.handlers[name] = callback; }
  async applyConstraints(value) { this.constraints = value; }
  stop() { this.stopped = true; }
}
globalThis.MediaStream = class {
  constructor(tracks) { this.tracks = tracks; }
  getVideoTracks() { return this.tracks; }
  getTracks() { return this.tracks; }
};
globalThis.RTCRtpSender = class {
  static getCapabilities() { return { codecs: [{ mimeType: "video/H264" }, { mimeType: "video/rtx" }] }; }
};
const peers = [];
globalThis.RTCPeerConnection = class {
  iceGatheringState = "complete";
  connectionState = "connected";
  handlers = {};
  parameters = { encodings: [{}] };
  constructor() { peers.push(this); }
  addEventListener(name, callback) { this.handlers[name] = callback; }
  removeEventListener(name) { delete this.handlers[name]; }
  addTrack(track) {
    this.track = track;
    return {
      getParameters: () => structuredClone(this.parameters),
      setParameters: async (value) => { this.parameters = structuredClone(value); },
    };
  }
  getTransceivers() { return [{ setCodecPreferences() {} }]; }
  async createOffer() { return { type: "offer", sdp: "contract-test" }; }
  async createAnswer() { return { type: "answer", sdp: "contract-test" }; }
  async setLocalDescription(value) { this.localDescription = value; }
  async setRemoteDescription() {}
  async getStats() {
    const frames = Math.floor(now / 1000) * 30;
    return new Map([
      ["out", { type: "outbound-rtp", kind: "video", id: "out", timestamp: now, bytesSent: frames * 1000,
        framesEncoded: frames, totalEncodeTime: frames / 1000, packetsSent: frames * 2,
        totalPacketSendDelay: frames / 2000, codecId: "codec", transportId: "transport" }],
      ["in", { type: "inbound-rtp", kind: "video", framesDecoded: frames, frameWidth: 1280, frameHeight: 720 }],
      ["codec", { mimeType: "video/H264" }],
      ["transport", { selectedCandidatePairId: "pair" }],
      ["pair", { currentRoundTripTime: 0.001 }],
    ]);
  }
  close() { this.connectionState = "closed"; }
};
let displayResolve;
Object.defineProperty(globalThis, "navigator", { value: {
  userAgent: "Node WebRTC contract fixture; not actual browser",
  languages: ["zh-CN"],
  language: "zh-CN",
  mediaDevices: { getDisplayMedia: (options) => new Promise((resolve) => {
    displayOptions = options;
    displayResolve = resolve;
  }) },
}, configurable: true });

globalThis.VideoFrame = window.VideoFrame = class {
  constructor(buffer, options) { this.buffer = buffer; this.options = options; }
  close() { this.closed = true; }
};
const inputFrames = [], outputFrames = [];
globalThis.MediaStreamTrackProcessor = window.MediaStreamTrackProcessor = class {
  readable = { getReader: () => {
    let remaining = 2;
    return {
      read: async () => {
        if (!remaining--) return { done: true };
        const frame = {
          timestamp: now * 1000, visibleRect: { width: 4, height: 4 }, displayWidth: 4, displayHeight: 4,
          allocationSize: () => 64,
          copyTo: async () => { now += 2; return [{ offset: 0, stride: 16 }]; },
          close() { this.closed = true; },
        };
        inputFrames.push(frame);
        return { done: false, value: frame };
      },
      cancel: async () => {},
    };
  } };
};
globalThis.MediaStreamTrackGenerator = window.MediaStreamTrackGenerator = class extends Track {
  writable = { getWriter: () => ({
    write: async (frame) => { outputFrames.push(frame); frame.close(); }, abort: async () => {},
  }) };
};

const lab = await import("./KNSoft.ZPigeon.Web/wwwroot/rtc-media-lab.mjs");
assert.equal(document.documentElement.lang, "zh-CN", "The lab must initialize the application localization module.");
const start = () => controls.settings.handlers.submit({ preventDefault() {} });
const stop = () => controls.stop.handlers.click();
const download = async () => {
  controls.download.handlers.click();
  return JSON.parse(await exported.text());
};
const advance = async (milliseconds) => {
  now += milliseconds;
  for (const [id, timer] of [...timers]) {
    if (timer.deadline > now) continue;
    timers.delete(id);
    await timer.callback();
  }
  for (let index = 0; index < 5; index++) await Promise.resolve();
};

assert.equal(lab.percentile([]), undefined);
assert.equal(lab.percentile([undefined, NaN, 1, 2, 3, 4]), 4);
await start();
assert.match(controls.status.textContent, /^运行中/);
assert.equal(peers.at(-2).parameters.encodings[0].maxBitrate, 5000000);
assert.equal(peers.at(-2).parameters.degradationPreference, "maintain-resolution");
assert.equal(peers.at(-2).track.contentHint, "text");
assert.equal(canvasFrameRate, 30, "Synthetic source must respect the requested initial frame rate.");
for (let index = 0; index < 6200; index++) {
  controls.received.presentation(now, { captureTime: now, expectedDisplayTime: now + index });
}
controls.bitrate.value = "1.5";
await controls.apply.handlers.click();
assert.equal(peers.at(-2).parameters.encodings[0].maxBitrate, 1500000);
for (let second = 0; second < 301; second++) await advance(1000);
const direct = await download();
assert.equal(direct.summary.traceCount, 300);
assert.equal(direct.summary.captureToPresentationSampleCount, 6000);
assert.equal(direct.summary.captureToPresentationP95Milliseconds, 5899);
assert.equal(direct.parameterChanges.length, 2);
assert(Math.abs(direct.summary.perSecondMeanEncodeP95Milliseconds - 1) < 1e-9);
assert.equal(direct.nativeBridgeIntegrated, false);
assert(peers.every((peer) => peer.connectionState === "closed"));
assert(peers.at(-2).track.stopped);
assert.equal(controls.start.disabled, false);

controls.mode.value = "rgba";
controls.duration.value = "5";
controls.fps.value = "15";
await start();
assert.equal(canvasFrameRate, 15, "RGBA bridge must not copy a fixed 60 fps source when 15 fps was requested.");
for (let index = 0; index < 10; index++) await Promise.resolve();
stop();
const raw = await download();
assert.equal(raw.summary.rawFrames, 2);
assert.equal(raw.summary.rawBytes, 128);
assert.equal(raw.summary.rawFrameMaterializationSampleCount, 2);
assert(inputFrames.every((frame) => frame.closed));
assert(outputFrames.every((frame) => frame.closed));
assert.equal(outputFrames[0].options.format, "RGBA");
assert.equal(outputFrames[0].options.layout[0].stride, 16);

controls.mode.value = "track";
controls.source.value = "display";
const pending = start();
assert.equal(displayOptions.video.frameRate.max, 15, "Screen capture must receive maxFramerate.");
stop();
const lateTrack = new Track();
displayResolve(new MediaStream([lateTrack]));
await pending;
assert(lateTrack.stopped, "Capture granted after cancellation must be stopped immediately.");
assert.equal(controls.start.disabled, false);
console.log("PASS: real module contract — RTP settings, 300-row trace, 6,000-frame rolling P95, parameter changes, " +
  "timeout cleanup, RGBA frame ownership, cancelled capture cleanup. No actual browser/RTP performance was measured.");
