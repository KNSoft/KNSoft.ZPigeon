import { clientId, postJson } from "./client-context.mjs";
import { localize, observeLocalization, t } from "./i18n.mjs";

const parameters = new URL(location.href).searchParams,
  sessionId = parameters.get("session"),
  guid = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

if (!sessionId || !guid.test(sessionId)) {
  location.replace(`/client.html?client=${clientId}`);
  throw new Error(t("browserControl.invalidSession"));
}

localize();
observeLocalization();
document.querySelector("[data-role=back]").href = `/client.html?client=${clientId}`;

class RemoteBrowser {
  constructor() {
    this.tabs = document.querySelector("[data-role=tabs]");
    this.address = document.querySelector("[data-role=address]");
    this.canvas = document.querySelector("[data-role=screen]");
    this.keyboard = document.querySelector("[data-role=keyboard]");
    this.empty = document.querySelector("[data-role=empty]");
    this.status = document.querySelector("[data-role=status]");
    this.context = this.canvas.getContext("2d", { alpha: false });
    this.requestId = 100;
    this.socketGeneration = 0;
    this.buttons = 0;
    this.history = null;
    this.address.onkeydown = (event) => {
      if (event.key !== "Enter" || event.isComposing) return;
      event.preventDefault();
      this.navigate();
    };
    document.querySelector("[data-action=back]").onclick = () => this.move(-1);
    document.querySelector("[data-action=forward]").onclick = () => this.move(1);
    document.querySelector("[data-action=reload]").onclick = () => this.send("Page.reload");
    document.querySelector("[data-action=stop]").onclick = () => this.send("Page.stopLoading");
    document.querySelector("[data-action=targets]").onclick = () => this.loadTargets(this.targetId);
    document.querySelector("[data-action=new-target]").onclick = () => this.newTarget();
    document.querySelector("[data-action=devtools]").onclick = () => this.openDevTools();
    document.querySelector("[data-action=fullscreen]").onclick = () =>
      document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen();
    document.querySelector("[data-role=viewport]").onchange = () => this.connect();
    document.querySelector("[data-role=quality]").onchange = () => this.connect();
    this.bindInput();
  }

  async start() {
    try {
      const sessions = await postJson("/api/remote/cdp/sessions"),
        session = sessions.find((item) => item.id === sessionId);
      if (!session) throw new Error(t("browserControl.sessionEnded"));
      this.session = session;
      document.querySelector("[data-role=session]").textContent = `${session.browser} · ${session.profile}`;
      await this.loadTargets();
    } catch (error) {
      this.fail(error);
    }
  }

  async loadTargets(selected) {
    try {
      let targets = await postJson("/api/remote/cdp/targets", { id: sessionId });
      if (!targets.length) {
        const target = await postJson("/api/remote/cdp/target", { id: sessionId, url: "about:blank" });
        targets = [target];
      }
      this.targets = targets;
      this.targetId = targets.some((target) => target.id === selected) ? selected : targets[0]?.id;
      this.renderTabs();
      await this.connect();
    } catch (error) {
      this.fail(error);
    }
  }

  async newTarget() {
    const url = prompt(t("browserControl.newTabAddress"), "about:blank");
    if (url === null) return;
    try {
      const target = await postJson("/api/remote/cdp/target", { id: sessionId, url: url.trim() || "about:blank" });
      await this.loadTargets(target.id);
    } catch (error) {
      this.fail(error);
    }
  }

  renderTabs() {
    this.tabs.replaceChildren(
      ...this.targets.map((target) => {
        const tab = document.createElement("div"),
          activate = document.createElement("button"),
          label = document.createElement("span"),
          close = document.createElement("button");
        tab.className = "browser-control-tab";
        tab.classList.toggle("active", target.id === this.targetId);
        activate.type = close.type = "button";
        activate.className = "browser-control-tab-label";
        label.textContent = target.title || target.url || t("browserControl.newTab");
        label.title = target.url;
        close.className = "browser-control-tab-close";
        close.textContent = "×";
        close.title = t("browserControl.closeTab");
        activate.append(label);
        activate.onclick = () => {
          if (target.id === this.targetId) return;
          this.targetId = target.id;
          this.renderTabs();
          this.connect();
        };
        close.onclick = () => this.closeTarget(target.id);
        tab.append(activate, close);
        return tab;
      }),
    );
  }

  async closeTarget(targetId = this.targetId) {
    if (!targetId) return;
    try {
      if (this.targets.length === 1) {
        await postJson("/api/remote/cdp/target", { id: sessionId, url: "about:blank" });
      }
      if (targetId === this.targetId) this.disconnect();
      await postJson("/api/remote/cdp/target/close", { id: sessionId, target: targetId });
      await this.loadTargets();
    } catch (error) {
      this.fail(error);
    }
  }

  async connect() {
    this.disconnect();
    if (!this.targetId) return;
    const generation = ++this.socketGeneration,
      [width, height] = document.querySelector("[data-role=viewport]").value.split("x").map(Number),
      quality = Number(document.querySelector("[data-role=quality]").value),
      url = new URL("/api/remote/cdp/control", location.href);
    this.width = width;
    this.height = height;
    url.protocol = location.protocol === "https:" ? "wss:" : "ws:";
    url.searchParams.set("client", clientId);
    url.searchParams.set("id", sessionId);
    url.searchParams.set("target", this.targetId);
    url.searchParams.set("width", width);
    url.searchParams.set("height", height);
    url.searchParams.set("quality", quality);
    const socket = (this.socket = new WebSocket(url));
    socket.binaryType = "arraybuffer";
    this.empty.hidden = false;
    this.empty.textContent = t("browserControl.connecting");
    this.status.textContent = t("browserControl.connecting");
    socket.onopen = () => {
      if (generation !== this.socketGeneration) return;
      this.status.textContent = t("common.connected");
      this.send("Page.getNavigationHistory");
    };
    socket.onmessage = (event) => {
      if (generation !== this.socketGeneration) return;
      if (typeof event.data === "string") this.receiveControl(event.data);
      else this.receiveFrame(event.data, generation);
    };
    socket.onerror = () => {
      if (generation === this.socketGeneration) this.status.textContent = t("browserControl.connectionFailed");
    };
    socket.onclose = (event) => {
      if (generation !== this.socketGeneration) return;
      this.socket = null;
      this.status.textContent = event.code === 1000 ? t("common.disconnected") : t("browserControl.connectionFailed");
      this.empty.hidden = false;
      this.empty.textContent = event.reason || this.status.textContent;
    };
  }

  disconnect() {
    this.socketGeneration++;
    if (this.socket?.readyState < WebSocket.CLOSING) this.socket.close(1000);
    this.socket = null;
    this.pendingFrame = null;
    this.pendingFrameMetadata = null;
    if (this.pointerFrame) cancelAnimationFrame(this.pointerFrame);
    this.pointerFrame = 0;
    this.renderingFrame = 0;
  }

  receiveControl(text) {
    let message;
    try {
      message = JSON.parse(text);
    } catch {
      return;
    }
    if (message.method === "Page.frameNavigated" && !message.params?.frame?.parentId) {
      const url = message.params.frame.url || "";
      this.address.value = url;
      const target = this.targets.find((value) => value.id === this.targetId);
      if (target) {
        target.url = url;
        target.title = url;
        this.renderTabs();
      }
      this.status.textContent = t("common.connected");
      this.send("Page.getNavigationHistory");
    } else if (message.method === "ZPigeon.screencastFrame") {
      this.pendingFrameMetadata = message.params;
    } else if (message.method === "Page.javascriptDialogOpening") {
      this.handleDialog(message.params);
    } else if (message.method === "Page.loadEventFired") {
      this.status.textContent = t("common.connected");
      this.send("Page.getNavigationHistory");
    } else if (message.method === "ZPigeon.error") {
      this.status.textContent =
        message.params?.code === "invalidControlMessage"
          ? t("browserControl.invalidControlMessage")
          : message.params?.message || t("common.failed");
    } else if (message.error) {
      this.status.textContent = message.error.message || t("common.failed");
    }
    if (message.result?.entries && Number.isInteger(message.result.currentIndex)) {
      this.history = message.result;
      const current = message.result.entries[message.result.currentIndex];
      if (current?.url) this.address.value = current.url;
      document.querySelector("[data-action=back]").disabled = message.result.currentIndex <= 0;
      document.querySelector("[data-action=forward]").disabled =
        message.result.currentIndex >= message.result.entries.length - 1;
    }
  }

  receiveFrame(data, generation) {
    this.pendingFrame = { data, info: this.pendingFrameMetadata };
    this.pendingFrameMetadata = null;
    if (this.renderingFrame === generation) return;
    this.renderingFrame = generation;
    const render = async () => {
      while (this.pendingFrame && generation === this.socketGeneration) {
        const frame = this.pendingFrame;
        this.pendingFrame = null;
        try {
          const image = await createImageBitmap(new Blob([frame.data], { type: "image/jpeg" }));
          if (generation !== this.socketGeneration) {
            image.close();
            break;
          }
          if (this.canvas.width !== image.width || this.canvas.height !== image.height) {
            this.canvas.width = image.width;
            this.canvas.height = image.height;
          }
          this.context.drawImage(image, 0, 0);
          this.frameMetadata = frame.info?.metadata || null;
          image.close();
          this.empty.hidden = true;
        } catch {
        } finally {
          if (
            generation === this.socketGeneration &&
            frame.info?.sessionId &&
            this.socket?.readyState === WebSocket.OPEN
          )
            this.socket.send(
              JSON.stringify({ method: "ZPigeon.screencastFrameAck", params: { sessionId: frame.info.sessionId } }),
            );
        }
      }
      if (this.renderingFrame === generation) this.renderingFrame = 0;
    };
    render();
  }

  navigate() {
    let url = this.address.value.trim();
    if (!url) return;
    if (!/^[a-z][a-z\d+.-]*:/i.test(url)) url = `https://${url}`;
    this.status.textContent = t("browserControl.loadingPage");
    this.send("Page.navigate", { url });
  }

  move(offset) {
    if (!this.history) return;
    const entry = this.history.entries[this.history.currentIndex + offset];
    if (entry) this.send("Page.navigateToHistoryEntry", { entryId: entry.id });
  }

  handleDialog(dialog) {
    let accept = true,
      promptText;
    if (dialog.type === "confirm") accept = confirm(dialog.message);
    else if (dialog.type === "prompt") {
      const value = prompt(dialog.message, dialog.defaultPrompt || "");
      accept = value !== null;
      promptText = value || "";
    } else alert(dialog.message);
    this.send("Page.handleJavaScriptDialog", { accept, ...(accept && promptText !== undefined ? { promptText } : {}) });
  }

  send(method, params) {
    if (this.socket?.readyState !== WebSocket.OPEN) return;
    this.socket.send(JSON.stringify({ id: ++this.requestId, method, ...(params ? { params } : {}) }));
  }

  openDevTools() {
    const target = this.targets?.find((value) => value.id === this.targetId);
    if (!this.session || !target?.devtoolsFrontendUrl) {
      alert(t("browserControl.noDevToolsTarget"));
      return;
    }
    const host = location.hostname.includes(":") ? `[${location.hostname}]` : location.hostname;
    const authority = `${host}:${this.session.forward.port}`,
      path = target.devtoolsFrontendUrl;
    const url = new URL(path, `http://${authority}`);
    url.searchParams.set("ws", `${authority}/devtools/page/${target.id}`);
    window.open(url, "_blank", "noopener");
  }

  bindInput() {
    const buttonName = (button) => ["left", "middle", "right", "back", "forward"][button] || "none",
      buttonMask = (button) => [1, 4, 2, 8, 16][button] || 0,
      modifiers = (event) =>
        (event.altKey ? 1 : 0) | (event.ctrlKey ? 2 : 0) | (event.metaKey ? 4 : 0) | (event.shiftKey ? 8 : 0),
      point = (event) => {
        const bounds = this.canvas.getBoundingClientRect(),
          metadata = this.frameMetadata,
          scaleX = (metadata?.deviceWidth || this.canvas.width) / bounds.width,
          scaleY = (metadata?.deviceHeight || this.canvas.height) / bounds.height;
        return {
          x: (event.clientX - bounds.left) * scaleX,
          y: (event.clientY - bounds.top) * scaleY - (this.activeOffsetTop ?? metadata?.offsetTop ?? 0),
        };
      };
    this.canvas.oncontextmenu = (event) => event.preventDefault();
    this.canvas.onpointerdown = (event) => {
      event.preventDefault();
      this.canvas.setPointerCapture(event.pointerId);
      this.buttons |= buttonMask(event.button);
      this.activeOffsetTop ??= this.frameMetadata?.offsetTop || 0;
      this.keyboard.focus({ preventScroll: true });
      this.send("Input.dispatchMouseEvent", {
        type: "mousePressed",
        ...point(event),
        button: buttonName(event.button),
        buttons: this.buttons,
        clickCount: event.detail || 1,
        modifiers: modifiers(event),
      });
    };
    this.canvas.onpointerup = (event) => {
      event.preventDefault();
      this.buttons &= ~buttonMask(event.button);
      this.send("Input.dispatchMouseEvent", {
        type: "mouseReleased",
        ...point(event),
        button: buttonName(event.button),
        buttons: this.buttons,
        clickCount: event.detail || 1,
        modifiers: modifiers(event),
      });
      if (!this.buttons) this.activeOffsetTop = null;
    };
    this.canvas.onpointercancel = () => {
      this.buttons = 0;
      this.activeOffsetTop = null;
    };
    this.canvas.onpointermove = (event) => {
      this.pointerMove = { point: point(event), buttons: this.buttons, modifiers: modifiers(event) };
      if (this.pointerFrame) return;
      this.pointerFrame = requestAnimationFrame(() => {
        this.pointerFrame = 0;
        this.send("Input.dispatchMouseEvent", {
          type: "mouseMoved",
          ...this.pointerMove.point,
          button: "none",
          buttons: this.pointerMove.buttons,
          modifiers: this.pointerMove.modifiers,
        });
      });
    };
    this.canvas.onwheel = (event) => {
      event.preventDefault();
      const position = point(event),
        bounds = this.canvas.getBoundingClientRect(),
        scaleX = (this.frameMetadata?.deviceWidth || this.canvas.width) / bounds.width,
        scaleY = (this.frameMetadata?.deviceHeight || this.canvas.height) / bounds.height;
      this.send("Input.dispatchMouseEvent", {
        type: "mouseWheel",
        x: position.x,
        y: position.y,
        deltaX: event.deltaX * scaleX,
        deltaY: event.deltaY * scaleY,
        modifiers: modifiers(event),
      });
    };
    this.keyboard.onkeydown = (event) => {
      const paste = (event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "v",
        altGraph = event.getModifierState("AltGraph");
      if (!paste) {
        this.send("Input.dispatchKeyEvent", {
          type: "rawKeyDown",
          key: event.key,
          code: event.code,
          windowsVirtualKeyCode: event.keyCode,
          nativeVirtualKeyCode: event.keyCode,
          modifiers: modifiers(event),
          isSystemKey: event.altKey,
        });
      }
      if (event.key.length !== 1 || (!paste && !altGraph && (event.ctrlKey || event.metaKey || event.altKey))) {
        event.preventDefault();
      }
    };
    this.keyboard.onkeyup = (event) => {
      if (!((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "v")) {
        this.send("Input.dispatchKeyEvent", {
          type: "keyUp",
          key: event.key,
          code: event.code,
          windowsVirtualKeyCode: event.keyCode,
          nativeVirtualKeyCode: event.keyCode,
          modifiers: modifiers(event),
        });
      }
      if (event.key.length !== 1) event.preventDefault();
    };
    this.keyboard.onbeforeinput = (event) => {
      if (!event.isComposing && !event.inputType.includes("Composition") && event.data) {
        this.send("Input.insertText", { text: event.data });
      }
    };
    this.keyboard.oncompositionend = (event) => {
      if (event.data) this.send("Input.insertText", { text: event.data });
    };
    this.keyboard.oninput = () => (this.keyboard.value = "");
    this.keyboard.onpaste = (event) => {
      event.preventDefault();
      const text = event.clipboardData?.getData("text");
      if (text) this.send("Input.insertText", { text });
    };
  }

  fail(error) {
    this.status.textContent = t("common.failed");
    this.empty.hidden = false;
    this.empty.textContent = error.message || String(error);
  }
}

new RemoteBrowser().start();
