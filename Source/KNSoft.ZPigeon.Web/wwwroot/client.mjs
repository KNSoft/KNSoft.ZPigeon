import { Terminal } from "./vendor/xterm/xterm.mjs";
import { FitAddon } from "./vendor/xterm/addon-fit.mjs";
import { bindTerminalInteractions, exportTerminal, pasteTerminal } from "./terminal-interactions.mjs";
import { CertificateInstaller } from "./certificate-install.mjs";
import { RegistryEditor } from "./registry-editor.mjs";
import { FileManager, ProcessManager, ServiceManager, WindowManager } from "./management.mjs";
import {
  CertificateManager,
  EventViewer,
  FirewallManager,
  HardwareManager,
  InputMethodManager,
  PackageManager,
  PowerManager,
  SoftwareManager,
  SystemInformationManager,
  TaskManager,
  UpdateManager,
  UserManager,
  WslManager,
  WlanManager,
} from "./administration.mjs";
import { ExecutionManager, RemoteDesktopManager } from "./execution.mjs";
import { PortForwardManager } from "./port-forward.mjs";
import { AclEditor, RemoteFilePicker } from "./remote-dialogs.mjs";
import { BrowserManager } from "./browser.mjs";
import { WmiManager } from "./wmi.mjs";
import { AudioManager } from "./audio.mjs";
import { ClipboardManager } from "./clipboard.mjs";
import { HexEditor } from "./hex-editor.mjs";
import { CredentialManager } from "./credentials.mjs";
import { FirmwareManager } from "./firmware.mjs";
import { enableManagedTables } from "./table.mjs";
import { HardwareInformationManager } from "./hardware-info.mjs";
import { VideoManager } from "./video.mjs";
import { NetworkShareManager } from "./network-shares.mjs";
import { NetworkAdapterManager, NetworkRouteManager, NetworkEndpointManager } from "./network-status.mjs";
import { ProxyVpnManager } from "./proxy-vpn.mjs";
import { installDialogBehavior } from "./dialogs.mjs";
import { RtcDirect } from "./rtc.mjs";
import { SerialManager } from "./serial.mjs";
import { RecordingManager } from "./recording.mjs";
import { PortableDeviceManager } from "./portable-device.mjs";
import {
  BluetoothManager,
  FontManager,
  KeyboardManager,
  LocationManager,
  PageFileManager,
} from "./platform-management.mjs";
import { SandboxManager } from "./sandbox.mjs";
import { ShadowCopyManager } from "./shadow-copy.mjs";
import { BitLockerManager } from "./bitlocker.mjs";
import { WinObjManager } from "./winobj.mjs";
import { ClientStatusManager, ZPigeonConnectionManager } from "./zpigeon.mjs";
import { language, localize, observeLocalization, t, translateSource } from "./i18n.mjs";
import { apiUrl, clientId, postJson } from "./client-context.mjs";

// Browser-native dialogs are outside the DOM localization observer.
const nativeAlert = window.alert.bind(window),
  nativeConfirm = window.confirm.bind(window),
  nativePrompt = window.prompt.bind(window),
  localizeDialog = (value) => translateSource(language, String(value));
window.alert = (message) => nativeAlert(localizeDialog(message));
window.confirm = (message) => nativeConfirm(localizeDialog(message));
window.prompt = (message, defaultValue) =>
  defaultValue === undefined
    ? nativePrompt(localizeDialog(message))
    : nativePrompt(localizeDialog(message), defaultValue);
installDialogBehavior();
document.querySelector("#clientTitle").textContent = `KNSoft ZPigeon · Client ${clientId}`;
const $ = (id) => document.getElementById(id),
  encoder = new TextEncoder(),
  sessions = new Map();
const shellSelect = $("shellSelect"),
  newShell = $("newShell"),
  emptyNew = $("emptyNew"),
  scriptButton = $("scriptButton"),
  tabs = $("tabs"),
  terminals = $("terminals"),
  empty = $("empty"),
  closeShell = $("closeShell"),
  tabMenu = $("tabMenu"),
  renameDialog = $("renameDialog"),
  pasteDialog = $("pasteDialog"),
  scriptDialog = $("scriptDialog");
enableManagedTables();
const categories = {
    zpigeon: [
      ["connection", "module.connection"],
      ["clientStatus", "module.clientStatus"],
    ],
    system: [
      ["system", "module.system"],
      ["wsl", "module.wsl"],
      ["pageFiles", "module.pageFiles"],
      ["sandbox", "module.sandbox"],
      ["winobj", "module.winobj"],
      ["registry", "module.registry"],
      ["users", "module.users"],
      ["wmi", "module.wmi"],
      ["updates", "module.updates"],
      ["certificates", "module.certificates"],
      ["credentials", "module.credentials"],
      ["events", "module.events"],
    ],
    network: [
      ["forwards", "module.forwards"],
      ["proxyVpn", "module.proxyVpn"],
      ["networkShares", "module.networkShares"],
      ["networkAdapters", "module.networkAdapters"],
      ["networkRoutes", "module.networkRoutes"],
      ["networkEndpoints", "module.networkEndpoints"],
      ["wlan", "module.wlan"],
      ["firewall", "module.firewall"],
    ],
    storage: [
      ["files", "module.files"],
      ["bitlocker", "module.bitlocker"],
      ["shadowCopies", "module.shadowCopies"],
      ["portableDevices", "module.portableDevices"],
      ["clipboard", "module.clipboard"],
    ],
    tasks: [
      ["processes", "module.processes"],
      ["windows", "module.windows"],
      ["services", "module.services"],
      ["tasks", "module.tasks"],
    ],
    hardware: [
      ["hardwareInfo", "module.hardwareInfo"],
      ["hardware", "module.hardware"],
      ["bluetooth", "module.bluetooth"],
      ["keyboard", "module.keyboard"],
      ["location", "module.location"],
      ["firmware", "module.firmware"],
      ["video", "module.video"],
      ["audio", "module.audio"],
      ["serial", "module.serial"],
      ["power", "module.power"],
    ],
    software: [
      ["software", "module.software"],
      ["inputMethods", "module.inputMethods"],
      ["packages", "module.packages"],
      ["fonts", "module.fonts"],
      ["browsers", "module.browsers"],
    ],
    remote: [
      ["terminal", "module.terminal"],
      ["execution", "module.execution"],
      ["remoteDesktop", "module.remoteDesktop"],
    ],
  },
  views = Object.values(categories).flat(),
  viewCategory = new Map(
    Object.entries(categories).flatMap(([category, items]) => items.map(([view]) => [view, category])),
  );
let active = null,
  selectedShell = null,
  availableShells = null,
  shellsPromise = null,
  clientConnected = false,
  shellsLoaded = false,
  shellsLoading = false,
  sessionsRestored = false,
  currentView = views.some(([view]) => view === location.hash.slice(1)) ? location.hash.slice(1) : "terminal",
  fitFrame = 0,
  contextSession = null,
  renameSession = null,
  pendingPaste = null,
  toastTimer = 0;
const filePicker = new RemoteFilePicker({ call, notify }),
  aclEditor = new AclEditor({ call, notify }),
  hexEditor = new HexEditor({ notify }),
  certificateInstaller = new CertificateInstaller({ call, notify }),
  rtc = new RtcDirect(notify);
const recording = new RecordingManager({ call, notify });
const registry = new RegistryEditor($("registryEditor"), { call, notify, aclEditor, hexEditor });
const files = new FileManager($("fileManager"), {
    call,
    notify,
    aclEditor,
    hexEditor,
    certificateInstaller,
    getTerminalShells,
    openTerminal: openFolderTerminal,
    revealProcess: (processId) => {
      showView("processes");
      processes.revealProcess(processId);
    },
    revealServices: (names) => {
      showView("services");
      services.revealServices(names);
    },
    revealFile: (path) => {
      showView("files");
      files.reveal(path);
    },
    openExecution: async (values) => {
      showView("execution");
      await execution.openRun(values);
    },
  }),
  portableDevices = new PortableDeviceManager($("portableDeviceManager"), { call, notify }),
  clipboard = new ClipboardManager($("clipboardManager"), { call, notify }),
  services = new ServiceManager($("serviceManager"), { call, notify, filePicker }),
  processes = new ProcessManager($("processManager"), {
    call,
    notify,
    hexEditor,
    isConnected: () => clientConnected,
    revealFile: (path) => {
      showView("files");
      files.reveal(path);
    },
    revealServices: (names) => {
      showView("services");
      services.revealServices(names);
    },
  }),
  windows = new WindowManager($("windowManager"), {
    call,
    notify,
    rtc,
    recording,
    revealProcess: (processId) => {
      showView("processes");
      processes.revealProcess(processId);
    },
  }),
  audio = new AudioManager($("audioManager"), { call, notify, rtc, recording }),
  video = new VideoManager($("videoManager"), { call, notify, rtc, recording }),
  serial = new SerialManager($("serialManager"), { call, notify });
const control = (action, identity, argument = null, secret = null) => ({ action, identity, argument, secret });
const revealRegistry = (root, path) => {
    showView("registry");
    registry.reveal(root, path);
  },
  revealService = (name) => {
    showView("services");
    services.revealService(name);
  };
const users = new UserManager($("userManager"), {
  call,
  notify,
  path: "users",
  columns: [
    { title: t("common.username"), value: (r) => r.identity },
    { title: t("common.fullName"), value: (r) => r.name },
    { title: t("common.description"), value: (r) => r.description },
    { title: t("common.homeDirectory"), value: (r) => r.detail },
    {
      title: t("common.status"),
      value: (r) =>
        r.flags & 0x80000000 ? t("common.unknown") : r.flags & 2 ? t("common.disabled") : t("common.enabled"),
    },
  ],
  actions: [
    { title: t("common.enable"), request: async (r) => control(3, r.identity) },
    { title: t("common.disable"), request: async (r) => control(4, r.identity) },
    { title: t("common.rename"), request: async (r) => users.rename(r) },
    {
      title: t("common.setPassword"),
      request: async (r) => {
        users.edit(r);
        return null;
      },
    },
    {
      title: t("common.delete"),
      danger: true,
      request: async (r) =>
        confirm(t("common.confirmDeleteUser", { name: r.identity })) ? control(2, r.identity) : null,
    },
  ],
});
const software = new SoftwareManager($("softwareManager"), { call, notify, revealRegistry });
const inputMethods = new InputMethodManager($("inputMethodManager"), { call, notify });
const packages = new PackageManager($("packageManager"), { call, notify });
const hardware = new HardwareManager($("hardwareManager"), { call, notify, revealRegistry, revealService });
const certificates = new CertificateManager($("certificateManager"), {
  call,
  notify,
  installer: certificateInstaller,
});
const credentials = new CredentialManager($("credentialManager"), { call, notify });
const firmware = new FirmwareManager($("firmwareManager"), { call, notify, hexEditor });
const networkShares = new NetworkShareManager($("networkShareManager"), { call, notify, filePicker, aclEditor });
const proxyVpn = new ProxyVpnManager($("proxyVpnManager"), { call, notify });
const networkAdapters = new NetworkAdapterManager($("networkAdapterManager"), { call, notify });
const networkRoutes = new NetworkRouteManager($("networkRouteManager"), { call, notify });
const networkEndpoints = new NetworkEndpointManager($("networkEndpointManager"), {
  call,
  notify,
  revealProcess: (processId) => {
    showView("processes");
    processes.revealProcess(processId);
  },
});
const pageFiles = new PageFileManager($("pageFileManager"), { call, notify });
const shadowCopies = new ShadowCopyManager($("shadowCopyManager"), { call, notify });
const bitlocker = new BitLockerManager($("bitLockerManager"), { call, notify });
const sandbox = new SandboxManager($("sandboxManager"), { call, notify });
const winobj = new WinObjManager($("winobjManager"), { call, notify });
const bluetooth = new BluetoothManager($("bluetoothManager"), { call, notify });
const keyboard = new KeyboardManager($("keyboardManager"), { call, notify });
const locationManager = new LocationManager($("locationManager"), { call, notify });
const fonts = new FontManager($("fontManager"), { call, notify, filePicker });
const browsers = new BrowserManager($("browserManager"), { call, notify });
const wmi = new WmiManager($("wmiManager"), { call, notify });
const updates = new UpdateManager($("updateManager"), { call, notify });
const tasks = new TaskManager($("taskManager"), { call, notify });
const events = new EventViewer($("eventViewer"), { call, notify });
const firewall = new FirewallManager($("firewallManager"), {
  call,
  notify,
  revealEvents: (channel) => {
    showView("events");
    events.openChannel(channel);
  },
});
const system = new SystemInformationManager($("systemManager"), { call, notify, filePicker }),
  wslManager = new WslManager($("wslManager"), {
    call,
    notify,
    openFiles: async (path) => {
      showView("files", "push");
      await files.open(path);
    },
  }),
  hardwareInfo = new HardwareInformationManager($("hardwareInfoManager"), { call, notify }),
  wlan = new WlanManager($("wlanManager"), { call, notify });
const power = new PowerManager($("powerManager"), { call, notify }),
  administration = [
    system,
    wslManager,
    pageFiles,
    shadowCopies,
    bitlocker,
    sandbox,
    winobj,
    hardwareInfo,
    users,
    software,
    inputMethods,
    packages,
    fonts,
    hardware,
    bluetooth,
    keyboard,
    locationManager,
    firmware,
    certificates,
    credentials,
    browsers,
    wmi,
    updates,
    events,
    tasks,
    firewall,
    wlan,
    networkShares,
    proxyVpn,
    networkAdapters,
    networkRoutes,
    networkEndpoints,
    power,
  ];
const execution = new ExecutionManager($("executionManager"), {
  call,
  notify,
  filePicker,
  openTerminal: (info) => {
    showView("terminal", "push");
    openSession(info, true);
  },
});
const remoteDesktop = new RemoteDesktopManager($("remoteDesktopManager"), { call, notify, rtc });
const forwards = new PortForwardManager($("portForwardManager"), { call, notify });
const connection = new ZPigeonConnectionManager($("connectionManager"), { call, notify });
const clientStatus = new ClientStatusManager($("clientStatusManager"), { call, notify });
files.disconnect();
portableDevices.disconnect();
clipboard.disconnect();
processes.disconnect();
windows.disconnect();
audio.disconnect();
video.disconnect();
serial.disconnect();
services.disconnect();
forwards.disconnect();
remoteDesktop.disconnect();
execution.disconnect();
clientStatus.disconnect();
for (const manager of administration) manager.disconnect();
localize(document);
observeLocalization();
function applyStatus(state) {
  const connected = state.clientConnected,
    disconnected = clientConnected && !connected;
  clientConnected = connected;
  $("server").textContent =
    state.state === 2 ? t("common.running") : t("common.stateValue", { value: state.state });
  $("client").textContent = connected ? t("common.connected") : t("common.disconnected");
  $("serverDot").className = `dot ${state.state === 2 ? "ok" : "off"}`;
  $("clientDot").className = `dot ${connected ? "ok" : "off"}`;
  const quality = state.quality || { level: 0 };
  $("quality").dataset.level = quality.level;
  $("quality").title =
    quality.roundTripMilliseconds == null
      ? quality.failedRequests
        ? t("quality.noSuccess", { level: quality.level, failed: quality.failedRequests })
        : t("quality.waitProbe")
      : t("quality.summary", { level: quality.level, latency: quality.roundTripMilliseconds }) +
        (quality.pendingRequests ? t("quality.pending", { value: quality.pendingRequests }) : "") +
        (quality.consecutiveFailures ? t("quality.failures", { value: quality.consecutiveFailures }) : "");
  if (currentView === "connection") connection.refresh();
  if (currentView === "clientStatus") clientStatus.activate(connected);
  if (disconnected) {
    selectedShell = null;
    availableShells = null;
    shellsPromise = null;
    shellsLoaded = false;
    shellSelect.replaceChildren(new Option(t("common.clientDisconnected")));
    files.disconnect();
    portableDevices.disconnect();
    clipboard.disconnect();
    processes.disconnect();
    windows.disconnect();
    audio.disconnect();
    video.disconnect();
    serial.disconnect();
    services.disconnect();
    forwards.disconnect();
    remoteDesktop.disconnect();
    execution.disconnect();
    clientStatus.disconnect();
    for (const manager of administration) manager.disconnect();
  }
  shellSelect.disabled = newShell.disabled = emptyNew.disabled = !connected || selectedShell === null;
  scriptButton.disabled = !connected;
  return connected;
}

async function refresh() {
  try {
    return applyStatus(await fetch(apiUrl("/api/status")).then((response) => response.json()));
  } catch {
    clientConnected = false;
    $("server").textContent = t("common.unavailable");
    $("client").textContent = t("common.unknown");
    $("serverDot").className = "dot off";
    $("clientDot").className = "dot";
    $("quality").dataset.level = 0;
    $("quality").title = t("quality.serverUnavailable");
    return false;
  }
}

async function loadShells() {
  if (!clientConnected || currentView !== "terminal" || shellsLoaded || shellsLoading) return;
  shellsLoading = true;
  shellSelect.replaceChildren(new Option(t("terminal.loadingShells")));
  try {
    const values = await getTerminalShells();
    const previous = selectedShell?.id;
    selectedShell = values.find((value) => value.id === previous) || values[0] || null;
    shellSelect.replaceChildren(...values.map((value) => new Option(value.name, value.id)));
    const scriptOptions = [];
    for (const value of values) {
      if (value.id === 1) {
        scriptOptions.push(
          new Option(t("terminal.batchCmd"), "1|.cmd"),
          new Option(t("terminal.batchBat"), "1|.bat"),
        );
      } else scriptOptions.push(new Option(value.name, value.id + "|.ps1"));
    }
    scriptOptions.push(
      new Option(t("terminal.vbscriptConsole"), "8|.vbs"),
      new Option(t("terminal.vbscriptWindow"), "16|.vbs"),
      new Option(t("terminal.jscriptConsole"), "8|.js"),
      new Option(t("terminal.jscriptWindow"), "16|.js"),
      new Option(t("terminal.windowsScriptFileConsole"), "8|.wsf"),
      new Option(t("terminal.windowsScriptFileWindow"), "16|.wsf"),
      new Option(t("terminal.hta"), "32|.hta"),
    );
    $("scriptType").replaceChildren(...scriptOptions);
    selectShell(selectedShell);
    shellsLoaded = true;
  } catch {
    selectedShell = null;
    shellSelect.replaceChildren(
      new Option(clientConnected ? t("terminal.shellsFailed") : t("common.clientDisconnected")),
    );
    shellSelect.disabled = true;
  } finally {
    shellsLoading = false;
  }
}

function getTerminalShells() {
  if (!clientConnected) return Promise.reject(new Error(t("common.clientDisconnected")));
  if (availableShells) return Promise.resolve(availableShells);
  if (!shellsPromise)
    shellsPromise = fetch(apiUrl("/api/terminal/shells"))
      .then(async (response) => {
        if (!response.ok) throw new Error(await response.text());
        availableShells = (await response.json()).map((value) => ({
          ...value,
          name: value.id === 1 ? t("execution.runtime.cmd") : value.name,
        }));
        return availableShells;
      })
      .finally(() => (shellsPromise = null));
  return shellsPromise;
}

function selectShell(value) {
  selectedShell = value;
  shellSelect.value = value?.id ?? "";
  newShell.title = value ? `${t("common.create")} ${value.name}` : t("terminal.new");
}

function notify(value) {
  const toast = $("toast");
  toast.textContent = value instanceof Error ? value.message : String(value);
  toast.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (toast.hidden = true), 4000);
}

function activate(id) {
  active = id;
  for (const [key, session] of sessions) {
    const selected = key === id;
    session.tab.classList.toggle("active", selected);
    session.panel.classList.toggle("active", selected);
  }
  empty.hidden = sessions.size !== 0;
  closeShell.disabled = id === null;
  scheduleFit();
}

function scheduleFit() {
  if (fitFrame || active === null) return;
  fitFrame = requestAnimationFrame(() => {
    fitFrame = 0;
    const session = sessions.get(active);
    if (!session || !session.panel.classList.contains("active")) return;
    session.fit.fit();
    session.terminal.focus();
    resize(session);
  });
}

function resize(session) {
  if (
    session.id !== active ||
    !session.panel.classList.contains("active") ||
    session.socket.readyState !== WebSocket.OPEN
  )
    return;
  const { cols, rows } = session.terminal;
  if (cols === session.cols && rows === session.rows) return;
  session.cols = cols;
  session.rows = rows;
  session.socket.send(JSON.stringify({ Type: "resize", Columns: cols, Rows: rows }));
}

async function restoreSessions() {
  if (sessionsRestored) return;
  sessionsRestored = true;
  try {
    const values = await fetch(apiUrl("/api/terminal/sessions")).then(async (response) => {
      if (!response.ok) throw new Error(await response.text());
      return response.json();
    });
    for (const value of values) openSession(value, false);
    if (values.length) activate(values.at(-1).id);
  } catch (error) {
    sessionsRestored = false;
    notify(error);
  }
}

async function createShell() {
  if (!selectedShell) return;
  await createTerminal(selectedShell);
}

async function openFolderTerminal(shell, workingDirectory) {
  selectShell(shell);
  showView("terminal", "push");
  await createTerminal(shell, workingDirectory);
}

async function createTerminal(shell, workingDirectory = null) {
  try {
    const current = sessions.get(active)?.terminal,
      info = await call("/api/terminal/session", {
        shell: shell.id,
        columns: current?.cols || 120,
        rows: current?.rows || 30,
        workingDirectory,
      });
    openSession(info, true);
  } catch (error) {
    notify(error);
  }
}

function openScriptDialog() {
  if (!clientConnected) return;
  scriptDialog.returnValue = "";
  scriptDialog.showModal();
  $("scriptText").focus();
}

async function createScript() {
  const [shell, extension] = $("scriptType").value.split("|"),
    current = sessions.get(active)?.terminal;
  try {
    const info = await call("/api/terminal/script", {
      shell: Number(shell),
      extension,
      script: $("scriptText").value,
      columns: current?.cols || 120,
      rows: current?.rows || 30,
    });
    scriptDialog.close();
    openSession(info, true);
  } catch (error) {
    notify(error);
  }
}

function openSession(info, select) {
  if (sessions.has(info.id)) return;
  const id = info.id,
    shell = info.shell,
    title = info.title,
    panel = document.createElement("div"),
    host = document.createElement("div"),
    tab = document.createElement("button");
  panel.className = "terminal-panel";
  host.className = "terminal-host";
  panel.append(host);
  terminals.append(panel);
  tab.className = "tab";
  tabs.append(tab);
  const terminal = new Terminal({
      cursorBlink: true,
      scrollback: 5000,
      scrollOnUserInput: true,
      fontFamily: "Cascadia Mono,Consolas,monospace",
      fontSize: 14,
      theme: {
        background: "#0b0f15",
        foreground: "#d9e1ee",
        cursor: "#b7c9e5",
        selectionBackground: "#34517a88",
      },
    }),
    fit = new FitAddon();
  terminal.loadAddon(fit);
  terminal.open(host);
  const scheme = location.protocol === "https:" ? "wss" : "ws",
    socket = new WebSocket(
      apiUrl(
        `${scheme}://${location.host}/api/terminal?session=${id}&columns=${terminal.cols}&rows=${terminal.rows}`,
      ),
    ),
    send = (data) => {
      if (socket.readyState === WebSocket.OPEN) socket.send(encoder.encode(data));
    },
    session = {
      id,
      shell,
      title,
      tab,
      panel,
      host,
      terminal,
      fit,
      socket,
      send,
      cols: terminal.cols,
      rows: terminal.rows,
      ended: false,
    };
  sessions.set(id, session);
  tab.textContent = title;
  bindTerminalInteractions(session, (text) => requestPaste(session, text), notify);
  socket.binaryType = "arraybuffer";
  socket.onmessage = (event) => receiveTerminal(session, event);
  socket.onopen = () => {
    terminal.focus();
    scheduleFit();
  };
  socket.onclose = (event) => {
    if (!session.ended) showEnd(session, "WebSocket", event.code, event.reason || webSocketName(event.code));
  };
  terminal.onData(send);
  terminal.onBinary((data) => {
    if (socket.readyState === WebSocket.OPEN) socket.send(Uint8Array.from(data, (c) => c.charCodeAt(0)));
  });
  tab.onclick = () => activate(id);
  tab.oncontextmenu = (event) => openTabMenu(session, event);
  if (select) activate(id);
  else {
    empty.hidden = true;
    closeShell.disabled = active === null;
  }
}

function requestPaste(session, text) {
  if (!text || session.ended || session.socket.readyState !== WebSocket.OPEN) return;
  const bytes = encoder.encode(text).byteLength,
    multiline = /[\r\n]/.test(text);
  if (!multiline && bytes <= 5120) {
    pasteTerminal(session.terminal, text);
    return;
  }
  if (pendingPaste) {
    notify(t("terminal.pastePending"));
    return;
  }
  const lines = text.split(/\r\n|\r|\n/).length,
    limit = 32768;
  pendingPaste = { id: session.id, text };
  $("pasteSummary").textContent = t("terminal.pasteSummary", {
    lines,
    bytes: bytes.toLocaleString(),
    truncated: text.length > limit ? t("terminal.previewTruncated") : "",
  });
  $("pastePreview").textContent = text.length > limit ? `${text.slice(0, limit)}\n…` : text;
  pasteDialog.returnValue = "";
  pasteDialog.showModal();
}

function openTabMenu(session, event) {
  event.preventDefault();
  contextSession = session.id;
  tabMenu.hidden = false;
  tabMenu.style.left = `${event.clientX}px`;
  tabMenu.style.top = `${event.clientY}px`;
  const box = tabMenu.getBoundingClientRect();
  tabMenu.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - box.width - 6))}px`;
  tabMenu.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - box.height - 6))}px`;
  tabMenu.querySelector("button").focus();
}

function hideTabMenu() {
  tabMenu.hidden = true;
  contextSession = null;
}

function receiveTerminal(session, event) {
  if (typeof event.data !== "string") {
    session.terminal.write(new Uint8Array(event.data));
    return;
  }
  try {
    const message = JSON.parse(event.data);
    if (message.Type === "closed") {
      showEnd(session, message.Source, message.Code, message.Name);
      return;
    }
  } catch {}
  session.socket.close(1007);
}

function showEnd(session, source, code, name) {
  if (session.ended) return;
  session.ended = true;
  const value =
    source === "ProcessExit" || source === "WebSocket"
      ? code
      : `0x${code.toString(16).padStart(8, "0").toUpperCase()}`;
  session.terminal.writeln(
    `\r\n\x1b[90m[${t("terminal.sessionEnded", {
      source,
      value,
      name: name ? ` · ${name}` : "",
    })}]\x1b[0m`,
  );
}

function webSocketName(code) {
  return (
    {
      1000: "NORMAL_CLOSURE",
      1001: "GOING_AWAY",
      1002: "PROTOCOL_ERROR",
      1006: "ABNORMAL_CLOSURE",
      1007: "INVALID_PAYLOAD",
      1008: "POLICY_VIOLATION",
      1009: "MESSAGE_TOO_BIG",
      1011: "INTERNAL_ERROR",
    }[code] || null
  );
}

async function closeSession(id) {
  const session = sessions.get(id);
  if (!session) return;
  const ids = [...sessions.keys()],
    index = ids.indexOf(id),
    next = ids[index + 1] ?? ids[index - 1] ?? null;
  try {
    await call("/api/terminal/session/close", { id });
  } catch (error) {
    notify(error);
    return;
  }
  if (pendingPaste?.id === id) {
    pendingPaste = null;
    if (pasteDialog.open) pasteDialog.close("cancel");
  }
  if (renameSession === id) {
    renameSession = null;
    if (renameDialog.open) renameDialog.close("cancel");
  }
  session.ended = true;
  session.socket.close();
  session.terminal.dispose();
  session.tab.remove();
  session.panel.remove();
  sessions.delete(id);
  activate(active === id ? next : active);
}

function closeActive() {
  closeSession(active);
}

function showView(name, historyMode = "replace") {
  if (!viewCategory.has(name)) name = "terminal";
  if (name !== currentView) ({ execution, forwards, packages, updates, clientStatus })[currentView]?.deactivate?.();
  currentView = name;
  const category = viewCategory.get(name),
    moduleNav = $("moduleNav");
  for (const [view] of views) $(`${view}View`).hidden = name !== view;
  for (const button of document.querySelectorAll(".app-nav button"))
    button.classList.toggle("active", button.dataset.category === category);
  moduleNav.replaceChildren(
    ...categories[category].map(([view, title]) => {
      const button = document.createElement("button");
      button.textContent = t(title);
      button.classList.toggle("active", view === name);
      button.onclick = () => showView(view, "push");
      return button;
    }),
  );
  const url = `#${name}`;
  if (location.hash !== url) history[`${historyMode}State`](null, "", url);
  processes.setActive(name === "processes");
  connection[name === "connection" ? "activate" : "deactivate"]();
  if (name === "clientStatus") clientStatus.activate(clientConnected);
  if (name === "terminal") {
    scheduleFit();
    loadShells();
  } else if (name === "execution") execution.activate(clientConnected);
  else if (name === "remoteDesktop") remoteDesktop.activate(clientConnected);
  else if (name === "forwards") forwards.activate(clientConnected);
  else if (name === "files") files.activate(clientConnected);
  else if (name === "portableDevices") portableDevices.activate(clientConnected);
  else if (name === "clipboard") clipboard.activate(clientConnected);
  else if (name === "windows") windows.activate(clientConnected);
  else if (name === "audio") audio.activate(clientConnected);
  else if (name === "video") video.activate(clientConnected);
  else if (name === "serial") serial.activate(clientConnected);
  else if (name === "services") services.activate(clientConnected);
  else
    ({
      system,
      wsl: wslManager,
      pageFiles,
      bitlocker,
      shadowCopies,
      sandbox,
      winobj,
      hardwareInfo,
      users,
      software,
      inputMethods,
      packages,
      fonts,
      hardware,
      bluetooth,
      keyboard,
      location: locationManager,
      firmware,
      certificates,
      credentials,
      browsers,
      wmi,
      updates,
      events,
      tasks,
      firewall,
      wlan,
      networkShares,
      proxyVpn,
      networkAdapters,
      networkRoutes,
      networkEndpoints,
      power,
    })[name]?.activate(clientConnected);
}

async function call(url, body) {
  return postJson(url, body);
}
shellSelect.onchange = () =>
  selectShell({ id: Number(shellSelect.value), name: shellSelect.selectedOptions[0].text });
newShell.onclick = emptyNew.onclick = createShell;
scriptButton.onclick = openScriptDialog;
closeShell.onclick = closeActive;
scriptDialog.addEventListener("submit", (event) => {
  event.preventDefault();
  event.submitter?.value === "cancel" ? scriptDialog.close() : createScript();
});
$("renameTab").onclick = () => {
  const session = sessions.get(contextSession);
  hideTabMenu();
  if (!session) return;
  renameSession = session.id;
  $("renameInput").value = session.title;
  renameDialog.returnValue = "";
  renameDialog.showModal();
  $("renameInput").select();
};
$("exportTab").onclick = () => {
  const session = sessions.get(contextSession);
  hideTabMenu();
  if (session) exportTerminal(session);
};
$("closeTab").onclick = () => {
  const id = contextSession;
  hideTabMenu();
  closeSession(id);
};
renameDialog.onclose = async () => {
  const session = sessions.get(renameSession),
    title = $("renameInput").value.trim();
  if (renameDialog.returnValue === "rename" && session && title)
    try {
      await call("/api/terminal/session/rename", { id: session.id, title });
      session.title = title;
      session.tab.textContent = title;
    } catch (error) {
      notify(error);
    }
  renameSession = null;
  if (session) session.terminal.focus();
};
pasteDialog.onclose = () => {
  const paste = pendingPaste;
  pendingPaste = null;
  const session = paste && sessions.get(paste.id);
  if (
    pasteDialog.returnValue === "paste" &&
    session &&
    !session.ended &&
    session.socket.readyState === WebSocket.OPEN
  )
    pasteTerminal(session.terminal, paste.text);
  else if (session) session.terminal.focus();
};
document.addEventListener("pointerdown", (event) => {
  if (!tabMenu.hidden && !tabMenu.contains(event.target)) hideTabMenu();
});
document.addEventListener("keydown", (event) => {
  if (event.key === "Escape") hideTabMenu();
});
addEventListener("resize", hideTabMenu);
tabs.addEventListener("scroll", hideTabMenu);
for (const button of document.querySelectorAll(".app-nav button"))
  button.onclick = () => showView(categories[button.dataset.category][0][0], "push");
addEventListener("hashchange", () => showView(location.hash.slice(1)));
showView(currentView);
new ResizeObserver(scheduleFit).observe(terminals);
const connected = await refresh();
showView(currentView);
await restoreSessions();
if (connected) await loadShells();
const statusEvents = new EventSource(apiUrl("/api/status/events"));
statusEvents.onmessage = (event) => {
  const wasConnected = clientConnected,
    connected = applyStatus(JSON.parse(event.data));
  if (connected && !wasConnected) showView(currentView);
};
