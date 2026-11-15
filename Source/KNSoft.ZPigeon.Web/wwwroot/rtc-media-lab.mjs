import { localize, observeLocalization } from "./i18n.mjs";

const MAXIMUM_TRACE = 300;
const MAXIMUM_FRAME_SAMPLES = 6000;
const ui = Object.fromEntries(
  [...document.querySelectorAll("[id]")].map((element) => [element.id, element]),
);
let active, lastResult;

localize();
observeLocalization();

const capabilities = typeof RTCRtpSender === "undefined" ? [] : RTCRtpSender.getCapabilities("video")?.codecs ?? [];
for (const mime of [...new Set(capabilities.map((codec) => codec.mimeType))]) {
  if (/^video\/(rtx|red|ulpfec|flexfec)/i.test(mime)) continue;
  ui.codec.add(new Option(mime.slice(6), mime));
}

ui.settings.addEventListener("submit", async (event) => {
  event.preventDefault();
  if (active || !ui.settings.reportValidity()) return;
  const settings = readSettings();
  const run = {
    settings,
    closed: false,
    started: performance.now(),
    bridgeBytes: 0,
    bridgeFrames: 0,
    bridgeTimes: Object.assign([], { next: 0 }),
    latencies: Object.assign([], { next: 0 }),
    result: {
      version: 1,
      experiment: "browser-local-rtp",
      nativeBridgeIntegrated: false,
      startedAt: new Date().toISOString(),
      userAgent: navigator.userAgent,
      settings,
      parameterChanges: [],
      samples: [],
    },
  };
  active = run;
  setRunning(true);
  ui.status.textContent = "正在建立实验；桌面模式请在浏览器提示中选择分享源。";
  try {
    if (!window.isSecureContext) throw new Error("屏幕分享与 WebCodecs 需要安全上下文或 localhost。 ");
    if (settings.mode === "rgba" &&
        (!window.MediaStreamTrackProcessor || !window.MediaStreamTrackGenerator || !window.VideoFrame)) {
      throw new Error("当前运行时未提供原始帧处理器、生成轨或 VideoFrame，无法运行 RGBA 实验。 ");
    }
    run.capture = settings.source === "display"
      ? await navigator.mediaDevices.getDisplayMedia({
        video: { frameRate: { max: settings.maxFramerate } }, audio: false,
      })
      : createSynthetic(run);
    if (run.closed) {
      run.capture.getTracks().forEach((track) => track.stop());
      return;
    }
    run.started = performance.now();
    run.setupTimer = setTimeout(() => stop(run, "本机媒体管线建立超时。"), 30000);
    run.sourceTrack = run.capture.getVideoTracks()[0];
    run.result.captureSettings = run.sourceTrack.getSettings();
    run.sourceTrack.addEventListener("ended", () => stop(run, "分享源已结束。"), { once: true });
    run.sendTrack = settings.mode === "rgba" ? createRawBridge(run) : run.sourceTrack;
    run.sendTrack.contentHint = settings.hint;
    await connect(run);
    if (run.closed) return;
    await apply(run, settings);
    if (run.closed) return;
    clearTimeout(run.setupTimer);
    run.timer = setTimeout(() => stop(run, "实验时间已到，采集和连接已关闭。"), settings.duration * 1000);
    run.result.connectedAtMilliseconds = performance.now() - run.started;
    ui.apply.disabled = false;
    ui.status.textContent = "运行中。可调整内容偏好、退化偏好、码率和帧率，再点击应用编码参数。";
    watchPresentation(run);
    await sample(run);
  } catch (error) {
    stop(run, `实验结束：${error.message}`);
  }
});

ui.apply.addEventListener("click", async () => {
  if (!active || !ui.settings.reportValidity()) return;
  const run = active;
  ui.apply.disabled = true;
  try {
    await apply(run, readSettings());
    if (!run.closed) ui.status.textContent = "编码参数已应用；实际发送仍受浏览器和网络限制。";
  } catch (error) {
    if (!run.closed) ui.status.textContent = `应用失败：${error.message}`;
  } finally {
    if (!run.closed) ui.apply.disabled = false;
  }
});

ui.stop.addEventListener("click", () => active && stop(active, "实验已停止，采集和连接已关闭。"));
window.addEventListener("pagehide", () => active && stop(active, "页面已关闭。"));
ui.download.addEventListener("click", () => {
  const result = active?.result ?? lastResult;
  if (!result) return;
  if (active) result.summary = summarize(active);
  const url = URL.createObjectURL(new Blob([JSON.stringify(result, null, 2)], { type: "application/json" }));
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = `zpigeon-rtc-media-${result.startedAt.replace(/[:.]/g, "-")}.json`;
  anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});

function readSettings() {
  return {
    source: ui.source.value,
    mode: ui.mode.value,
    hint: ui.hint.value,
    degradationPreference: ui.degradation.value,
    codec: ui.codec.value,
    maxBitrate: Number(ui.bitrate.value) * 1000000,
    maxFramerate: Number(ui.fps.value),
    duration: Number(ui.duration.value),
    scenario: ui.scenario.value,
  };
}

function setRunning(running) {
  ui.start.disabled = running;
  ui.stop.disabled = !running;
  ui.apply.disabled = true;
  for (const name of ["source", "mode", "codec", "duration"]) ui[name].disabled = running;
}

function createSynthetic(run) {
  const canvas = ui.synthetic, context = canvas.getContext("2d");
  const draw = () => {
    if (run.closed) return;
    const tick = performance.now() - run.started;
    context.fillStyle = "#f5f7fb";
    context.fillRect(0, 0, canvas.width, canvas.height);
    context.fillStyle = "#192b48";
    context.font = "28px Segoe UI, Microsoft YaHei";
    context.fillText("KNSoft ZPigeon · 视频轨合成测试图", 40, 60);
    context.font = "16px Consolas, Microsoft YaHei";
    for (let row = 0; row < 22; row++) {
      context.fillText(`中文小字 / Code details 0123456789 <Frame id=\"${row}\" />`, 40, 105 + row * 24);
    }
    context.fillStyle = "#327be5";
    context.fillRect(800 + Math.sin(tick / 700) * 160, 120 + (tick / 8) % 420, 100, 70);
    context.fillStyle = "#192b48";
    context.fillText(`${(tick / 1000).toFixed(2)} seconds`, 860, 660);
    run.animation = requestAnimationFrame(draw);
  };
  draw();
  return canvas.captureStream(run.settings.maxFramerate);
}

function createRawBridge(run) {
  const processor = new MediaStreamTrackProcessor({ track: run.sourceTrack });
  const generated = new MediaStreamTrackGenerator({ kind: "video" });
  run.reader = processor.readable.getReader();
  run.writer = generated.writable.getWriter();
  run.bridge = pumpRawFrames(run).catch((error) => {
    if (!run.closed) stop(run, `RGBA 桥接结束：${error.message}`);
  });
  return generated;
}

async function pumpRawFrames(run) {
  let pixels;
  while (!run.closed) {
    const { value: frame, done } = await run.reader.read();
    if (done) return;
    let output;
    try {
      const started = performance.now();
      const size = frame.allocationSize({ format: "RGBA" });
      if (pixels?.byteLength !== size) pixels = new Uint8Array(size);
      const layout = await frame.copyTo(pixels, { format: "RGBA" });
      if (run.closed) return;
      output = new VideoFrame(pixels, {
        format: "RGBA",
        codedWidth: frame.visibleRect.width,
        codedHeight: frame.visibleRect.height,
        displayWidth: frame.displayWidth,
        displayHeight: frame.displayHeight,
        timestamp: frame.timestamp,
        colorSpace: frame.colorSpace,
        layout,
      });
      retain(run.bridgeTimes, performance.now() - started);
      run.bridgeBytes += size;
      run.bridgeFrames++;
      await run.writer.write(output);
    } finally {
      output?.close();
      frame.close();
    }
  }
}

async function connect(run) {
  run.senderPeer = new RTCPeerConnection({ iceServers: [] });
  run.receiverPeer = new RTCPeerConnection({ iceServers: [] });
  run.sender = run.senderPeer.addTrack(run.sendTrack, new MediaStream([run.sendTrack]));
  for (const peer of [run.senderPeer, run.receiverPeer]) {
    peer.addEventListener("connectionstatechange", () => {
      if (peer.connectionState === "failed") stop(run, "本机 RTP 连接失败。 ");
    });
  }
  run.receiverPeer.ontrack = (event) => { ui.received.srcObject = new MediaStream([event.track]); };
  if (run.settings.codec) {
    const codecs = capabilities.filter((codec) => codec.mimeType === run.settings.codec);
    if (!codecs.length) throw new Error("当前运行时不支持所选编码器。 ");
    run.senderPeer.getTransceivers()[0].setCodecPreferences([
      ...codecs,
      ...capabilities.filter((codec) => /^video\/(rtx|red|ulpfec|flexfec)/i.test(codec.mimeType)),
    ]);
  }
  await run.senderPeer.setLocalDescription(await run.senderPeer.createOffer());
  await gather(run.senderPeer);
  if (run.closed) return;
  await run.receiverPeer.setRemoteDescription(run.senderPeer.localDescription);
  await run.receiverPeer.setLocalDescription(await run.receiverPeer.createAnswer());
  await gather(run.receiverPeer);
  if (run.closed) return;
  await run.senderPeer.setRemoteDescription(run.receiverPeer.localDescription);
  await ui.received.play();
}

function gather(peer) {
  if (peer.iceGatheringState === "complete") return Promise.resolve();
  return new Promise((resolve, reject) => {
    const finish = (error) => {
      clearTimeout(timer);
      peer.removeEventListener("icegatheringstatechange", changed);
      peer.removeEventListener("connectionstatechange", changed);
      error ? reject(error) : resolve();
    };
    const changed = () => {
      if (peer.connectionState === "closed") finish(new Error("实验已停止。 "));
      else if (peer.iceGatheringState === "complete") finish();
    };
    const timer = setTimeout(() => finish(new Error("本机 ICE 收集超时。 ")), 10000);
    peer.addEventListener("icegatheringstatechange", changed);
    peer.addEventListener("connectionstatechange", changed);
  });
}

async function apply(run, settings) {
  const parameters = run.sender.getParameters();
  if (parameters.encodings.length !== 1) throw new Error("实验需要已协商的单路视频编码。 ");
  run.sendTrack.contentHint = settings.hint;
  parameters.degradationPreference = settings.degradationPreference;
  parameters.encodings[0].maxBitrate = settings.maxBitrate;
  parameters.encodings[0].maxFramerate = settings.maxFramerate;
  await run.sender.setParameters(parameters);
  if (run.closed) return;
  await run.sourceTrack.applyConstraints({ frameRate: { max: settings.maxFramerate } });
  if (run.closed) return;
  if (run.result.parameterChanges.length < MAXIMUM_TRACE) {
    run.result.parameterChanges.push({
      elapsedMilliseconds: performance.now() - run.started,
      contentHint: run.sendTrack.contentHint,
      requested: {
        maxBitrate: settings.maxBitrate,
        maxFramerate: settings.maxFramerate,
        degradationPreference: settings.degradationPreference,
      },
      applied: run.sender.getParameters(),
    });
  }
}

function watchPresentation(run) {
  if (!ui.received.requestVideoFrameCallback) return;
  const presented = (_, metadata) => {
    if (run.closed) return;
    if (Number.isFinite(metadata.captureTime) && Number.isFinite(metadata.expectedDisplayTime)) {
      const milliseconds = metadata.expectedDisplayTime - metadata.captureTime;
      if (milliseconds >= 0 && milliseconds < 30000) retain(run.latencies, milliseconds);
    }
    run.presentation = ui.received.requestVideoFrameCallback(presented);
  };
  run.presentation = ui.received.requestVideoFrameCallback(presented);
}

async function sample(run) {
  if (run.closed) return;
  try {
    const [sent, received] = await Promise.all([run.senderPeer.getStats(), run.receiverPeer.getStats()]);
    if (run.closed) return;
    const outbound = [...sent.values()].find((entry) => entry.type === "outbound-rtp" && entry.kind === "video");
    const inbound = [...received.values()].find((entry) => entry.type === "inbound-rtp" && entry.kind === "video");
    const codec = outbound && sent.get(outbound.codecId);
    const transport = outbound && sent.get(outbound.transportId);
    const pair = transport && sent.get(transport.selectedCandidatePairId);
    const previous = run.previous;
    const elapsed = previous && outbound ? (outbound.timestamp - previous.outbound.timestamp) / 1000 : undefined;
    const record = {
      elapsedMilliseconds: performance.now() - run.started,
      rtpPayloadBytes: outbound?.bytesSent,
      sentBitsPerSecond: elapsed > 0 ? (outbound.bytesSent - previous.outbound.bytesSent) * 8 / elapsed : undefined,
      decodedFrames: inbound?.framesDecoded,
      receivedFramesPerSecond: elapsed > 0 && inbound && previous.inbound
        ? (inbound.framesDecoded - previous.inbound.framesDecoded) / elapsed : undefined,
      frameWidth: inbound?.frameWidth,
      frameHeight: inbound?.frameHeight,
      codec: codec?.mimeType,
      codecParameters: codec?.sdpFmtpLine,
      encoderImplementation: outbound?.encoderImplementation,
      powerEfficientEncoder: outbound?.powerEfficientEncoder,
      qualityLimitationReason: outbound?.qualityLimitationReason,
      roundTripMilliseconds: pair?.currentRoundTripTime === undefined ? undefined : pair.currentRoundTripTime * 1000,
      availableOutgoingBitrate: pair?.availableOutgoingBitrate,
      droppedFrames: inbound?.framesDropped,
      lostPackets: inbound?.packetsLost,
      retransmittedPackets: outbound?.retransmittedPacketsSent,
      nackCount: outbound?.nackCount,
      pliCount: outbound?.pliCount,
      meanEncodeMilliseconds: elapsed > 0 && outbound.framesEncoded > previous.outbound.framesEncoded
        ? (outbound.totalEncodeTime - previous.outbound.totalEncodeTime) * 1000 /
          (outbound.framesEncoded - previous.outbound.framesEncoded) : undefined,
      meanPacketQueueMilliseconds: elapsed > 0 && outbound.packetsSent > previous.outbound.packetsSent
        ? (outbound.totalPacketSendDelay - previous.outbound.totalPacketSendDelay) * 1000 /
          (outbound.packetsSent - previous.outbound.packetsSent) : undefined,
      bridgeFrames: run.bridgeFrames,
      bridgeBytes: run.bridgeBytes,
      bridgeBytesPerSecond: elapsed > 0 ? (run.bridgeBytes - previous.bridgeBytes) / elapsed : undefined,
    };
    for (const [name, value] of Object.entries(record)) {
      if (typeof value === "number" && !Number.isFinite(value)) delete record[name];
    }
    if (outbound) run.previous = { outbound, inbound, bridgeBytes: run.bridgeBytes };
    if (run.result.samples.length < MAXIMUM_TRACE) run.result.samples.push(record);
    render(run, record);
    run.sampleTimer = setTimeout(() => sample(run), 1000);
  } catch (error) {
    if (!run.closed) stop(run, `统计采样失败：${error.message}`);
  }
}

function summarize(run) {
  const samples = run.result.samples, final = samples.at(-1);
  return {
    elapsedMilliseconds: performance.now() - run.started,
    traceCount: samples.length,
    rtpPayloadBytes: final?.rtpPayloadBytes,
    captureToPresentationP95Milliseconds: percentile(run.latencies),
    captureToPresentationSampleCount: run.latencies.length,
    perSecondMeanEncodeP95Milliseconds: percentile(samples.map((sample) => sample.meanEncodeMilliseconds)),
    rawFrameMaterializationP95Milliseconds: percentile(run.bridgeTimes),
    rawFrameMaterializationSampleCount: run.bridgeTimes.length,
    rawFrames: run.bridgeFrames,
    rawBytes: run.bridgeBytes,
  };
}

function render(run, record) {
  const summary = summarize(run);
  run.result.summary = summary;
  ui.sendBitrate.textContent = format(record.sentBitsPerSecond / 1000000, " Mbps");
  ui.receiveFps.textContent = format(record.receivedFramesPerSecond, " fps");
  ui.actualCodec.textContent = record.codec ?? "—";
  ui.resolution.textContent = record.frameWidth ? `${record.frameWidth} × ${record.frameHeight}` : "—";
  ui.latency.textContent = format(summary.captureToPresentationP95Milliseconds, " ms");
  ui.encodeTime.textContent = format(summary.perSecondMeanEncodeP95Milliseconds, " ms");
  ui.bridgeTime.textContent = format(summary.rawFrameMaterializationP95Milliseconds, " ms");
  ui.totalBytes.textContent = format(record.rtpPayloadBytes / 1048576, " MiB");
  ui.trace.textContent = JSON.stringify(record, null, 2);
  ui.download.disabled = false;
}

function format(value, suffix) {
  return Number.isFinite(value) ? `${value.toFixed(2)}${suffix}` : "—";
}

function retain(samples, value) {
  if (samples.length < MAXIMUM_FRAME_SAMPLES) samples.push(value);
  else {
    samples[samples.next] = value;
    samples.next = (samples.next + 1) % MAXIMUM_FRAME_SAMPLES;
  }
}

export function percentile(samples) {
  const sorted = samples.filter(Number.isFinite).sort((left, right) => left - right);
  return sorted.length ? sorted[Math.ceil(sorted.length * 0.95) - 1] : undefined;
}

function stop(run, message) {
  if (run.closed) return;
  run.closed = true;
  clearTimeout(run.timer);
  clearTimeout(run.setupTimer);
  clearTimeout(run.sampleTimer);
  cancelAnimationFrame(run.animation);
  if (run.presentation !== undefined) ui.received.cancelVideoFrameCallback(run.presentation);
  run.reader?.cancel().catch(() => {});
  run.writer?.abort().catch(() => {});
  run.sendTrack?.stop();
  run.capture?.getTracks().forEach((track) => track.stop());
  run.senderPeer?.close();
  run.receiverPeer?.close();
  if (active !== run) return;
  run.result.endedAt = new Date().toISOString();
  run.result.stopReason = message;
  run.result.summary = summarize(run);
  lastResult = run.result;
  active = null;
  ui.received.srcObject = null;
  setRunning(false);
  ui.download.disabled = false;
  ui.status.textContent = message;
}
