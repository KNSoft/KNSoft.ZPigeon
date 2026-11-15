const headerLength = 21,
  maximumImageLength = 0x1000000,
  maximumDimension = 7680,
  minimumVideoDimension = 128,
  maximumPendingFrames = 8,
  maximumPendingBytes = 0x2000000;
const h264Codec = "avc1.4D0033",
  h265Codec = "hev1.1.6.L153.B0",
  h264Flag = 1,
  h265Flag = 2;

export class CaptureFrameDecoder {
  constructor(canvas, status, active, acknowledge, reportVideoCodecs, reportStatistics) {
    this.canvas = canvas;
    this.status = status;
    this.active = active;
    this.acknowledge = acknowledge;
    this.reportVideoCodecs = reportVideoCodecs;
    this.reportStatistics = reportStatistics;
    this.reset();
    document.addEventListener("visibilitychange", () => {
      if (document.hidden && this.pendingPaint) {
        cancelAnimationFrame(this.paintRequest);
        this.paintVideo();
      }
    });
  }

  reset() {
    this.generation = (this.generation || 0) + 1;
    this.queueEpoch = (this.queueEpoch || 0) + 1;
    this.acknowledgedSequence = 0;
    this.queuedFrames = this.queuedBytes = 0;
    this.receivedBytes = this.displayedFrames = this.recoveredFrames = 0;
    this.started = performance.now();
    this.displayTimes = [];
    this.renderDelays = [];
    this.resetVideo();
    this.supportedWidth = this.supportedHeight = 0;
    this.supportedCodecs = 0;
    this.reportedWidth = this.reportedHeight = 0;
    this.reportedCodecs = 0;
    this.chunks = [];
    this.available = 0;
    this.pending = null;
    this.sequence = 0;
    this.decode = Promise.resolve();
  }

  resetVideo() {
    if (this.paintRequest != null) cancelAnimationFrame(this.paintRequest);
    this.paintRequest = null;
    this.pendingPaint?.video.close();
    this.pendingPaint = null;
    this.videoFrames?.clear();
    this.videoFrames = new Map();
    this.videoBytes = 0;
    if (this.videoDecoder && this.videoDecoder.state !== "closed") this.videoDecoder.close();
    this.videoDecoder = null;
    this.videoCodec = null;
  }

  receive(data, socket) {
    if (!this.active(socket) || !data.length) return;
    this.receivedBytes += data.length;
    this.chunks.push({ data, offset: 0 });
    this.available += data.length;
    if (this.available > maximumImageLength + headerLength) {
      socket.close(1009, "画面数据过大");
      return;
    }
    while (true) {
      if (!this.pending) {
        if (this.available < headerLength) return;
        const bytes = this.read(headerLength),
          view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        this.pending = {
          type: view.getUint8(0),
          sequence: view.getUint32(1, true),
          canvasWidth: view.getUint16(5, true),
          canvasHeight: view.getUint16(7, true),
          left: view.getUint16(9, true),
          top: view.getUint16(11, true),
          width: view.getUint16(13, true),
          height: view.getUint16(15, true),
          length: view.getUint32(17, true),
        };
        if (this.pending.length > maximumImageLength) {
          socket.close(1009, "画面数据过大");
          return;
        }
      }
      if (!this.pending.length) {
        const frame = this.pending;
        this.pending = null;
        this.recover(frame, socket);
        continue;
      }
      if (this.available < this.pending.length) return;
      const frame = this.pending;
      frame.image = this.read(frame.length);
      this.pending = null;
      if (!valid(frame)) {
        this.recover(frame, socket);
        continue;
      }
      frame.received = performance.now();
      if (this.queuedFrames + this.videoFrames.size + (this.pendingPaint ? 1 : 0) >= maximumPendingFrames ||
          this.queuedBytes + this.videoBytes + frame.length > maximumPendingBytes) {
        this.recover(frame, socket);
        continue;
      }
      const generation = this.generation, queueEpoch = this.queueEpoch;
      this.queuedFrames++;
      this.queuedBytes += frame.length;
      this.decode = this.decode
        .then(() => generation === this.generation && this.draw(frame, socket))
        .catch((error) => {
          if (generation === this.generation) this.recover(frame, socket, error);
        })
        .finally(() => {
          if (queueEpoch !== this.queueEpoch) return;
          this.queuedFrames--;
          this.queuedBytes -= frame.length;
          if (frame.type <= 2 && generation === this.generation && this.active(socket))
            this.reportStatistics(this.statistics);
        });
    }
  }

  recover(frame, socket, error) {
    if (!this.active(socket)) return;
    if (error) console.warn(error);
    if (!frame.sequence) {
      socket.close(1002, "无效的画面序号");
      return;
    }
    this.generation++;
    this.sequence = 0;
    this.recoveredFrames++;
    this.resetVideo();
    this.status.textContent = "正在重新同步远程画面…";
    this.acknowledgeOnce(frame.sequence, true, socket);
  }

  acknowledgeOnce(sequence, keyframe, socket, renderMilliseconds = 0) {
    if (!this.active(socket)) return;
    if (this.acknowledgedSequence &&
        ((sequence - this.acknowledgedSequence) >>> 0) >= 0x80000000) return;
    if (sequence === this.acknowledgedSequence) return;
    this.acknowledgedSequence = sequence;
    this.acknowledge(sequence, keyframe, socket, renderMilliseconds);
  }

  get statistics() {
    const now = performance.now(),
      ordered = [...this.renderDelays].sort((a, b) => a - b),
      elapsed = Math.max(1, now - this.started);
    return {
      receivedBytes: this.receivedBytes,
      displayedFrames: this.displayedFrames,
      framesPerSecond: this.displayTimes.filter((time) => now - time < 1000).length,
      megabitsPerSecond: (this.receivedBytes * 8) / (elapsed * 1000),
      receiveToDrawP95Milliseconds: ordered[Math.max(0, Math.ceil(ordered.length * 0.95) - 1)] ?? 0,
      pendingFrames: this.queuedFrames + this.videoFrames.size + (this.pendingPaint ? 1 : 0),
      decoderQueueSize: this.videoDecoder?.decodeQueueSize ?? 0,
      recoveredFrames: this.recoveredFrames,
    };
  }

  rendered(frame, report = true) {
    const now = performance.now(), delay = now - (frame.received ?? now);
    this.displayedFrames++;
    this.displayTimes.push(now);
    while (this.displayTimes.length > 120 || now - this.displayTimes[0] >= 1000) this.displayTimes.shift();
    this.renderDelays.push(delay);
    if (this.renderDelays.length > 120) this.renderDelays.shift();
    if (report) this.reportStatistics(this.statistics);
    return Math.min(65535, Math.max(1, Math.ceil(delay)));
  }

  read(length) {
    const first = this.chunks[0],
      remaining = first.data.length - first.offset;
    if (remaining >= length) {
      const result = first.data.subarray(first.offset, first.offset + length);
      first.offset += length;
      if (first.offset === first.data.length) this.chunks.shift();
      this.available -= length;
      return result;
    }
    const result = new Uint8Array(length);
    for (let written = 0; written < length; ) {
      const chunk = this.chunks[0],
        count = Math.min(length - written, chunk.data.length - chunk.offset);
      result.set(chunk.data.subarray(chunk.offset, chunk.offset + count), written);
      chunk.offset += count;
      written += count;
      if (chunk.offset === chunk.data.length) this.chunks.shift();
    }
    this.available -= length;
    return result;
  }

  async draw(frame, socket) {
    if (!this.active(socket)) return;
    if (frame.type <= 2) return this.drawImage(frame, socket);
    return this.drawVideo(frame, socket);
  }

  async drawImage(frame, socket) {
    const generation = this.generation;
    if (
      frame.type === 2 &&
      (!this.sequence ||
        frame.sequence !== nextSequence(this.sequence) ||
        this.canvas.width !== frame.canvasWidth ||
        this.canvas.height !== frame.canvasHeight)
    ) {
      this.status.textContent = "正在重新同步远程画面…";
      this.acknowledgeOnce(frame.sequence, true, socket);
      return;
    }
    await this.ensureVideoCodecs(videoDimension(frame.canvasWidth), videoDimension(frame.canvasHeight), socket);
    if (!this.active(socket) || generation !== this.generation) return;
    this.resetVideo();
    const bitmap = await createImageBitmap(
      new Blob([frame.image], {
        type: frame.type === 1 ? "image/jpeg" : "image/png",
      }),
    );
    if (!this.active(socket) || generation !== this.generation) {
      bitmap.close();
      return;
    }
    if (frame.type === 1) {
      this.canvas.width = frame.canvasWidth;
      this.canvas.height = frame.canvasHeight;
    }
    const context = this.canvas.getContext("2d");
    context.globalCompositeOperation = frame.type === 1 ? "copy" : "source-over";
    context.drawImage(bitmap, frame.left, frame.top, frame.width, frame.height);
    context.globalCompositeOperation = "source-over";
    bitmap.close();
    this.sequence = frame.sequence;
    this.canvas.hidden = false;
    this.status.textContent = `${frame.canvasWidth} × ${frame.canvasHeight} · 实时`;
    this.acknowledgeOnce(frame.sequence, false, socket, this.rendered(frame, false));
  }

  async drawVideo(frame, socket) {
    const generation = this.generation,
      key = frame.type === 3 || frame.type === 5,
      codec = frame.type <= 4 ? h264Codec : h265Codec,
      codecFlag = frame.type <= 4 ? h264Flag : h265Flag,
      supportedCodecs = await this.ensureVideoCodecs(
        videoDimension(frame.canvasWidth),
        videoDimension(frame.canvasHeight),
        socket,
      );
    if (!this.active(socket) || generation !== this.generation) return;
    if (!(supportedCodecs & codecFlag)) {
      this.resetVideo();
      this.sequence = 0;
      this.status.textContent = "正在重新同步远程画面…";
      this.acknowledgeOnce(frame.sequence, false, socket);
      return;
    }
    if (
      !key &&
      (!this.sequence ||
        frame.sequence !== nextSequence(this.sequence) ||
        this.videoCodec !== codec ||
        this.canvas.width !== frame.canvasWidth ||
        this.canvas.height !== frame.canvasHeight)
    ) {
      this.status.textContent = "正在重新同步远程画面…";
      this.acknowledgeOnce(frame.sequence, true, socket);
      return;
    }
    if (
      !this.videoDecoder ||
      this.videoCodec !== codec ||
      this.canvas.width !== frame.canvasWidth ||
      this.canvas.height !== frame.canvasHeight
    ) {
      if (!key) {
        this.acknowledgeOnce(frame.sequence, true, socket);
        return;
      }
      this.resetVideo();
      this.canvas.width = frame.canvasWidth;
      this.canvas.height = frame.canvasHeight;
      this.videoCodec = codec;
      const decoder = new VideoDecoder({
        output: (video) => this.videoOutput(video, decoder),
        error: (error) => this.videoError(error, socket, decoder, codec),
      });
      this.videoDecoder = decoder;
      try {
        decoder.configure(videoConfig(codec, frame.canvasWidth, frame.canvasHeight));
      } catch (error) {
        this.videoError(error, socket, decoder, codec);
        this.acknowledgeOnce(frame.sequence, false, socket);
        return;
      }
    }
    const length = frame.image.byteLength;
    if (this.videoFrames.size + (this.pendingPaint ? 1 : 0) >= maximumPendingFrames ||
        this.videoBytes + length > maximumPendingBytes ||
        (this.videoDecoder.decodeQueueSize ?? 0) >= maximumPendingFrames) {
      this.recover(frame, socket);
      return;
    }
    const timestamp = frame.sequence * 1000;
    this.videoFrames.set(timestamp, { frame, socket, length });
    this.videoBytes += length;
    try {
      this.videoDecoder.decode(
        new EncodedVideoChunk({
          type: key ? "key" : "delta",
          timestamp,
          data: frame.image,
        }),
      );
    } catch (error) {
      this.videoFrames.delete(timestamp);
      this.videoBytes -= length;
      this.videoError(error, socket, this.videoDecoder, codec);
      this.acknowledgeOnce(frame.sequence, false, socket);
      return;
    }
    delete frame.image;
    this.sequence = frame.sequence;
  }

  videoOutput(video, decoder) {
    const pending = this.videoFrames.get(video.timestamp);
    if (this.videoDecoder !== decoder || !pending) {
      video.close();
      return;
    }
    this.videoFrames.delete(video.timestamp);
    this.videoBytes -= pending.length;
    const { frame, socket } = pending;
    if (!this.active(socket)) {
      video.close();
      return;
    }
    if (document.hidden) {
      if (this.paintRequest != null) cancelAnimationFrame(this.paintRequest);
      this.paintRequest = null;
      this.pendingPaint?.video.close();
      this.pendingPaint = null;
      video.close();
      this.acknowledgeOnce(frame.sequence, false, socket);
      return;
    }
    this.pendingPaint?.video.close();
    this.pendingPaint = { video, frame, socket, generation: this.generation };
    if (this.paintRequest == null) {
      const generation = this.generation,
        request = requestAnimationFrame(() => {
          if (this.paintRequest === request && generation === this.generation && this.videoDecoder === decoder)
            this.paintVideo();
        });
      this.paintRequest = request;
    }
  }

  paintVideo() {
    const pending = this.pendingPaint;
    this.pendingPaint = null;
    this.paintRequest = null;
    if (!pending) return;
    const { video, frame, socket, generation } = pending;
    try {
      if (generation !== this.generation || !this.active(socket)) return;
      if (document.hidden) {
        this.acknowledgeOnce(frame.sequence, false, socket);
        return;
      }
      const context = this.canvas.getContext("2d");
      context.globalCompositeOperation = "copy";
      context.drawImage(video, 0, 0, this.canvas.width, this.canvas.height);
      context.globalCompositeOperation = "source-over";
      this.canvas.hidden = false;
      this.status.textContent = `${frame.canvasWidth} × ${frame.canvasHeight} · ${frame.type <= 4 ? "H.264" : "H.265"}`;
      this.acknowledgeOnce(frame.sequence, false, socket, this.rendered(frame));
    } finally {
      video.close();
    }
  }

  videoError(error, socket, decoder, codec) {
    if (this.videoDecoder !== decoder || !this.active(socket)) return;
    console.warn(error);
    const latest = [...this.videoFrames.values()].at(-1) ?? this.pendingPaint;
    this.resetVideo();
    this.sequence = 0;
    if (latest) this.acknowledgeOnce(latest.frame.sequence, true, socket);
    this.status.textContent = "正在重新同步远程画面…";
    this.supportedCodecs &= ~(codec === h264Codec ? h264Flag : h265Flag);
    this.sendVideoCodecs(socket);
  }

  async ensureVideoCodecs(width, height, socket) {
    const generation = this.generation;
    if (this.supportedWidth !== width || this.supportedHeight !== height) {
      const codecs = await supportedVideoCodecs(width, height);
      if (!this.active(socket) || generation !== this.generation) return 0;
      this.supportedWidth = width;
      this.supportedHeight = height;
      this.supportedCodecs = codecs;
    }
    this.sendVideoCodecs(socket);
    return this.supportedCodecs;
  }

  sendVideoCodecs(socket) {
    if (
      this.reportedWidth === this.supportedWidth &&
      this.reportedHeight === this.supportedHeight &&
      this.reportedCodecs === this.supportedCodecs
    )
      return;
    this.reportVideoCodecs(this.supportedCodecs, this.supportedWidth, this.supportedHeight, socket);
    this.reportedWidth = this.supportedWidth;
    this.reportedHeight = this.supportedHeight;
    this.reportedCodecs = this.supportedCodecs;
  }
}

function valid(frame) {
  return (
    frame.type >= 1 &&
    frame.type <= 6 &&
    frame.sequence &&
    frame.canvasWidth &&
    frame.canvasWidth <= maximumDimension &&
    frame.canvasHeight &&
    frame.canvasHeight <= maximumDimension &&
    frame.width &&
    frame.height &&
    frame.left < frame.canvasWidth &&
    frame.top < frame.canvasHeight &&
    frame.width <= frame.canvasWidth - frame.left &&
    frame.height <= frame.canvasHeight - frame.top &&
    frame.length &&
    frame.length <= maximumImageLength &&
    (frame.type === 2 ||
      (!frame.left && !frame.top && frame.width === frame.canvasWidth && frame.height === frame.canvasHeight))
  );
}

function nextSequence(sequence) {
  return sequence === 0xffffffff ? 1 : sequence + 1;
}

export async function configureCaptureEncoding(select) {
  const codecs = await supportedVideoCodecs(1280, 720),
    h264 = !!(codecs & h264Flag),
    h265 = !!(codecs & h265Flag);
  select.querySelector("[value=h264]").disabled = !h264;
  select.querySelector("[value=h265]").disabled = !h265;
  select.querySelector("[value=auto]").disabled = !(h264 || h265);
  select.dataset.autoCodec = h265 ? "1" : "0";
  if (!(h264 || h265)) select.value = "image";
}

function videoConfig(codec, width, height) {
  return { codec, codedWidth: width, codedHeight: height, optimizeForLatency: true };
}

function videoDimension(value) {
  return Math.max(value & ~1, minimumVideoDimension);
}

async function supportedVideoCodecs(width, height) {
  if (typeof VideoDecoder === "undefined") return 0;
  const supported = async (codec) => {
    try {
      return (await VideoDecoder.isConfigSupported(videoConfig(codec, width, height))).supported;
    } catch {
      return false;
    }
  };
  const [h264, h265] = await Promise.all([supported(h264Codec), supported(h265Codec)]);
  return (h264 ? h264Flag : 0) | (h265 ? h265Flag : 0);
}

export function captureEncodingOptions(select) {
  return select.value === "image"
    ? { captureMode: 0, videoCodec: 0 }
    : select.value === "auto"
      ? { captureMode: 1, videoCodec: Number(select.dataset.autoCodec || 0) }
      : { captureMode: 2, videoCodec: select.value === "h265" ? 1 : 0 };
}
