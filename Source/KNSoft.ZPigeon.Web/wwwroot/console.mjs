import { getTheme, setTheme } from "./theme.mjs";
import { installDialogBehavior } from "./dialogs.mjs";
import { localize, observeLocalization, t } from "./i18n.mjs";

const $ = (id) => document.getElementById(id),
  transports = { 1: "QUIC", 2: "TCP / TLS", 3: "UDP / DTLS" };
let clients = [],
  groups = [],
  selectedGroup = null,
  refreshing = false,
  toastTimer,
  buildConfig,
  buildJob,
  buildEvents,
  buildStarting = false,
  buildCancelRequested = false,
  mcpPolicy;

const buildModuleGroups = [
  ["console.coreFeatures", [[44, "module.clientStatus"]]],
  ["nav.system", [[1, "module.system"], [26, "module.systemAdministration"], [7, "module.registry"],
    [9, "module.users"], [13, "module.wmi"], [22, "module.updates"], [28, "module.certificates"],
    [30, "module.credentials"], [6, "module.events"], [34, "module.pageFiles"], [39, "module.appContainer"],
    [40, "module.winobj"], [41, "module.wsl"]]],
  ["nav.network", [[11, "module.forwards"], [43, "module.proxyVpn"], [32, "module.networkShares"],
    [33, "module.networkStatus"], [27, "module.wlan"], [24, "module.firewall"]]],
  ["nav.storage", [[4, "module.files"], [46, "module.bitlocker"], [45, "module.shadowCopies"],
    [19, "module.portableDevices"], [29, "module.clipboard"]]],
  ["nav.tasks", [[2, "module.processes"], [8, "module.windows"], [3, "module.services"],
    [23, "module.tasks"], [42, "module.uiAutomation"]], true],
  ["nav.hardware", [[21, "module.hardware"], [35, "module.bluetooth"], [36, "module.keyboard"],
    [37, "module.location"], [31, "module.firmware"], [15, "module.video"], [14, "module.audio"],
    [17, "module.serial"], [25, "module.power"]]],
  ["nav.software", [[20, "console.softwareModule"], [38, "module.fonts"], [12, "module.browsers"]]],
  ["nav.remote", [[5, "module.terminal"], [10, "module.execution"], [16, "module.rtc"], [18, "module.recording"]]],
];
const buildModuleDescriptionKeys = {
  system: "console.buildModuleDescription.system",
  process: "console.buildModuleDescription.process",
  service: "console.buildModuleDescription.service",
  file: "console.buildModuleDescription.file",
  terminal: "console.buildModuleDescription.terminal",
  eventLog: "console.buildModuleDescription.eventLog",
  registry: "console.buildModuleDescription.registry",
  window: "console.buildModuleDescription.window",
  user: "console.buildModuleDescription.user",
  execution: "console.buildModuleDescription.execution",
  tunnel: "console.buildModuleDescription.tunnel",
  browser: "console.buildModuleDescription.browser",
  wmi: "console.buildModuleDescription.wmi",
  audio: "console.buildModuleDescription.audio",
  video: "console.buildModuleDescription.video",
  rtc: "console.buildModuleDescription.rtc",
  serial: "console.buildModuleDescription.serial",
  recording: "console.buildModuleDescription.recording",
  portableDevice: "console.buildModuleDescription.portableDevice",
  software: "console.buildModuleDescription.software",
  hardware: "console.buildModuleDescription.hardware",
  update: "console.buildModuleDescription.update",
  task: "console.buildModuleDescription.task",
  firewall: "console.buildModuleDescription.firewall",
  power: "console.buildModuleDescription.power",
  systemAdministration: "console.buildModuleDescription.systemAdministration",
  wlan: "console.buildModuleDescription.wlan",
  certificate: "console.buildModuleDescription.certificate",
  clipboard: "console.buildModuleDescription.clipboard",
  credential: "console.buildModuleDescription.credential",
  firmware: "console.buildModuleDescription.firmware",
  networkShare: "console.buildModuleDescription.networkShare",
  networkStatus: "console.buildModuleDescription.networkStatus",
  pageFile: "console.buildModuleDescription.pageFile",
  bluetooth: "console.buildModuleDescription.bluetooth",
  keyboard: "console.buildModuleDescription.keyboard",
  location: "console.buildModuleDescription.location",
  font: "console.buildModuleDescription.font",
  appContainer: "console.buildModuleDescription.appContainer",
  winobj: "console.buildModuleDescription.winobj",
  wsl: "console.buildModuleDescription.wsl",
  uiAutomation: "console.buildModuleDescription.uiAutomation",
  proxyVpn: "console.buildModuleDescription.proxyVpn",
  clientStatus: "console.buildModuleDescription.clientStatus",
  shadowCopy: "console.buildModuleDescription.shadowCopy",
  bitlocker: "console.buildModuleDescription.bitlocker",
};
const buildExecutionLabels = {
  runtimeDiscovery: "console.extensionRuntimes", shellExecute: "console.extensionShellExecute",
  alternateIdentity: "console.extensionIdentity", appContainer: "console.extensionAppContainer",
  customToken: "console.extensionCustomToken", staging: "console.extensionStaging",
};
const buildExecutionDescriptions = {
  runtimeDiscovery: "console.buildExecutionDescription.runtimeDiscovery",
  shellExecute: "console.buildExecutionDescription.shellExecute",
  alternateIdentity: "console.buildExecutionDescription.alternateIdentity",
  appContainer: "console.buildExecutionDescription.appContainer",
  customToken: "console.buildExecutionDescription.customToken",
  staging: "console.buildExecutionDescription.staging",
};
const buildTransportDescriptions = {
  1: "console.buildTransportDescription.quic",
  2: "console.buildTransportDescription.tcp",
  3: "console.buildTransportDescription.udp",
};

function notify(message) {
  $("toast").textContent = message?.message ?? String(message);
  $("toast").hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => ($("toast").hidden = true), 6000);
}

async function request(url, method = "GET", body) {
  const response = await fetch(url, {
    method,
    headers: body === undefined ? undefined : { "content-type": "application/json" },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  if (!response.ok) {
    const text = await response.text();
    let error;
    try { error = JSON.parse(text); } catch {}
    throw new Error(error?.message || text || `HTTP ${response.status}`);
  }
  return response;
}

async function json(url, method = "GET", body) {
  const response = await request(url, method, body);
  return response.status === 204 ? null : response.json();
}

installDialogBehavior();
localize();
observeLocalization();

function element(tag, className, text) {
  const value = document.createElement(tag);
  if (className) value.className = className;
  if (text !== undefined) value.textContent = text;
  return value;
}

function bytes(value) {
  if (value < 1024) return `${value} B`;
  if (value < 1048576) return `${(value / 1024).toFixed(1)} KiB`;
  if (value < 1073741824) return `${(value / 1048576).toFixed(1)} MiB`;
  return `${(value / 1073741824).toFixed(1)} GiB`;
}

function groupOptionInputs(group) {
  return [...$("buildModules").querySelectorAll("input[data-build-group-option]")]
    .filter((input) => input.dataset.buildGroupOption === group);
}

function executionOptionInputs() {
  return [...$("buildModules").querySelectorAll("input[data-build-execution]")];
}

function featureOptionInputs() {
  return [...$("buildModules").querySelectorAll("input[data-build-module], input[data-build-execution]")];
}

function updateBuildGroupChecks() {
  for (const input of $("buildModules").querySelectorAll("input[data-build-group-toggle]")) {
    const options = groupOptionInputs(input.dataset.buildGroupToggle),
      selected = options.filter((option) => option.checked).length;
    input.checked = options.length !== 0 && selected === options.length;
    input.indeterminate = selected !== 0 && selected !== options.length;
  }
  for (const input of $("buildModules").querySelectorAll("input[data-build-subgroup-toggle]")) {
    const options = [...$("buildModules").querySelectorAll("input[data-build-subgroup-option]")]
      .filter((option) => option.dataset.buildSubgroupOption === input.dataset.buildSubgroupToggle),
      selected = options.filter((option) => option.checked).length;
    input.checked = options.length !== 0 && selected === options.length;
    input.indeterminate = selected !== 0 && selected !== options.length;
  }
}

function updateBuildSelectAll() {
  const options = featureOptionInputs(), selected = options.filter((option) => option.checked).length;
  $("buildSelectAll").checked = options.length !== 0 && selected === options.length;
  $("buildSelectAll").indeterminate = selected !== 0 && selected !== options.length;
}

function updateMinimumOs(selected) {
  let minimum;
  if (selected.includes(1) || $("buildTcpTls13").checked)
    minimum = t("console.minimumOsWindows11");
  else if (selected.includes(2))
    minimum = t("console.minimumOsWindows10");
  else if (selected.includes(3))
    minimum = t("console.minimumOsWindows10Udp");
  else
    minimum = t("console.minimumOsSelectTransport");
  $("buildMinimumOs").textContent = minimum;
  $("buildMinimumOs").title = t("console.minimumOsHint");
}

function allGroups() {
  return [...new Set([...groups, ...clients.map((client) => client.group).filter(Boolean)])].sort();
}

function renderNavigation() {
  const values = allGroups();
  $("groupNames").replaceChildren(...values.map((group) => new Option(group, group)));
  $("groupNav").replaceChildren(...["", ...values].map((group) => {
    const button = element("button", selectedGroup === group ? "active" : "");
    button.append(element("span", "", group || t("console.ungrouped")),
      element("span", "nav-count", clients.filter((client) => client.group === group).length));
    button.onclick = () => {
      selectedGroup = group;
      showView("connections");
      render();
    };
    return button;
  }));
  $("groupCount").textContent = values.length;
}

function clientCard(client) {
  const performance = client.performance,
    card = element("article", "client-card"),
    title = element("div", "client-card-title"),
    link = element("a", "", `Client ${client.id} · ${client.fingerprint.slice(0, 8)}`),
    details = element("dl", "client-details"),
    quality = element("span", "client-quality"),
    bars = element("span", "quality"),
    footer = element("div", "client-footer"),
    select = document.createElement("select"),
    control = element("a", "control-link", `${t("console.openPanel")} →`);
  link.href = control.href = `/client.html?client=${client.id}`;
  title.append(link, element("span", "client-badge", transports[client.transport] ?? String(client.transport)));
  bars.dataset.level = performance.level;
  bars.setAttribute("aria-hidden", "true");
  for (let i = 0; i < 5; i++) bars.append(document.createElement("i"));
  quality.append(bars, element("span", "", performance.roundTripMilliseconds === null ?
    t("console.probing") : `${performance.roundTripMilliseconds} ms`));
  for (const [label, value] of [
    [t("home.address"), client.address],
    [t("console.quality"), quality],
    [t("console.transfer"), `↑ ${bytes(performance.sentBytes)} · ↓ ${bytes(performance.receivedBytes)}`],
    [t("console.requests"), `${performance.pendingRequests} / ${performance.failedRequests}`],
    [t("home.fingerprint"), element("span", "client-fingerprint", client.fingerprint)],
  ]) {
    const row = document.createElement("div"), valueElement = document.createElement("dd");
    valueElement.append(value instanceof Node ? value : document.createTextNode(value));
    row.append(element("dt", "", label), valueElement);
    details.append(row);
  }
  select.setAttribute("aria-label", t("console.clientGroup", { id: client.id }));
  select.append(new Option(t("console.ungrouped"), ""), ...allGroups().map((group) => new Option(group, group)));
  select.value = client.group;
  select.onchange = async () => {
    select.disabled = true;
    try {
      await json(`/api/console/clients/${client.fingerprint}/group`, "PUT", { group: select.value });
      client.group = select.value;
      select.blur();
      render();
    } catch (error) {
      select.value = client.group;
      notify(error.message);
    } finally { select.disabled = false; }
  };
  select.onblur = () => render();
  footer.append(select, control);
  card.append(title, details, footer);
  return card;
}

function render() {
  renderNavigation();
  $("connectionHeading").textContent = selectedGroup === null ? t("console.connections") :
    selectedGroup || t("console.ungrouped");
  $("onlineCount").textContent = clients.length;
  const measured = clients.map((client) => client.performance.roundTripMilliseconds).filter((value) => value !== null);
  $("averageRtt").textContent = measured.length ?
    `${Math.round(measured.reduce((sum, value) => sum + value, 0) / measured.length)} ms` : "—";
  $("traffic").textContent = clients.length ? bytes(clients.reduce((sum, client) =>
    sum + client.performance.sentBytes + client.performance.receivedBytes, 0)) : "—";
  const query = $("search").value.trim().toLowerCase(),
    visible = clients.filter((client) => (selectedGroup === null || client.group === selectedGroup) &&
      `${client.id} ${client.address} ${client.fingerprint} ${client.group}`.toLowerCase().includes(query));
  $("resultCount").textContent = t("console.resultCount", { count: visible.length });
  if ($("clients").contains(document.activeElement)) return;
  if (!visible.length) {
    $("clients").replaceChildren(element("div", "console-empty",
      t(clients.length ? "console.noMatches" : "home.empty")));
    return;
  }
  const grouped = new Map();
  for (const client of visible) {
    if (!grouped.has(client.group)) grouped.set(client.group, []);
    grouped.get(client.group).push(client);
  }
  $("clients").replaceChildren(...[...grouped].sort(([a], [b]) => a.localeCompare(b)).map(([group, values]) => {
    const section = element("section", "connection-group"), grid = element("div", "client-grid");
    grid.append(...values.map(clientCard));
    section.append(element("h2", "", `${group || t("console.ungrouped")} · ${values.length}`), grid);
    return section;
  }));
}

async function refresh() {
  if (refreshing) return;
  refreshing = true;
  try {
    [clients, groups] = await Promise.all([json("/api/clients"), json("/api/console/groups")]);
    $("consoleStatus").textContent = t("console.serverReady");
    render();
  } catch (error) {
    $("consoleStatus").textContent = t("console.serverUnavailable");
    notify(error.message);
  } finally { refreshing = false; }
}

function renderMcpPolicy() {
  if (!mcpPolicy) return;
  $("mcpEnabled").checked = mcpPolicy.enabled;
  $("mcpEndpoint").value = mcpPolicy.endpoint;
  $("mcpAccessMode").value = mcpPolicy.readOnlyOnly ? "readonly" : "all";
  $("mcpMaxConcurrent").value = mcpPolicy.maxConcurrentCalls;
  $("mcpMaxCallsPerMinute").value = mcpPolicy.maxCallsPerMinute;
  $("mcpCallTimeout").value = mcpPolicy.callTimeoutSeconds;
  $("mcpMaxResultSize").value = Math.max(1, Math.ceil(mcpPolicy.maxResultSizeBytes / 1048576));
  $("mcpAllToolsWarning").hidden = mcpPolicy.readOnlyOnly;
}

async function loadMcpPolicy() {
  try {
    mcpPolicy = await json("/api/console/mcp");
    renderMcpPolicy();
  } catch (error) { notify(error.message); }
}

function updateMcpWarning() {
  $("mcpAllToolsWarning").hidden = $("mcpAccessMode").value !== "all";
}

function buildCheckbox(value, text, checked, title, dataset = {}) {
  const label = element("label", "build-check"), input = document.createElement("input");
  input.type = "checkbox";
  input.value = value;
  input.checked = checked;
  if (title) label.title = title;
  for (const [name, data] of Object.entries(dataset)) input.dataset[name] = data;
  label.append(input, element("span", "", text));
  return label;
}

function selectedBuildModules() {
  return [...$("buildModules").querySelectorAll("input[data-build-module]:checked")]
    .map((input) => Number(input.value));
}

function selectedBuildExecutionOptions() {
  return executionOptionInputs().filter((input) => input.checked).map((input) => input.value);
}

function selectedBuildTransports() {
  const selected = [...$("buildTransports").querySelectorAll("input:checked")].map((input) => Number(input.value)),
    preferred = Number($("buildPreferredTransport").value);
  return selected.includes(preferred) ? [preferred, ...selected.filter((id) => id !== preferred)] : selected;
}

function buildActive() {
  return buildStarting || (buildJob && !["completed", "failed", "canceled"].includes(buildJob.state));
}

function updateBuildSelection() {
  if (!buildConfig) return;
  const modules = selectedBuildModules(), selected = selectedBuildTransports(),
    previous = $("buildPreferredTransport").value;
  $("buildPreferredTransport").replaceChildren(...selected.map((id) =>
    new Option(buildConfig.transports.find((transport) => transport.id === id).name, id)));
  if (selected.includes(Number(previous))) $("buildPreferredTransport").value = previous;
  if (!selected.includes(2)) $("buildTcpTls13").checked = false;
  $("buildTransportOrder").textContent = selected.length ? t("console.transportOrder", {
    order: selectedBuildTransports().map((id) => {
      const transport = buildConfig.transports.find((value) => value.id === id);
      return `${transports[id]} (${buildConfig.host}:${transport.port})`;
    }).join(" → "),
  }) : t("console.selectTransport");
  $("buildSelectedCount").textContent = t("console.selectedModules", {
    count: modules.length, total: buildConfig.modules.length,
  });
  const executionEnabled = modules.includes(10);
  for (const input of executionOptionInputs())
    input.disabled = !executionEnabled || buildActive();
  if (!buildConfig.supportsTcpTls13) $("buildTcpTls13").checked = false;
  $("buildTcpTls13").disabled = !selected.includes(2) || !buildConfig.supportsTcpTls13 || buildActive();
  updateMinimumOs(selected);
  const runtimeFiles = [];
  if (modules.includes(20))
    runtimeFiles.push("Microsoft.Management.Deployment.dll", "Microsoft.Management.Deployment.winmd");
  $("buildRuntimeFiles").textContent = runtimeFiles.join(" · ") || t("console.noRuntimeFiles");
  $("buildRuntimeHint").hidden = !runtimeFiles.length && !modules.includes(16);
  const instanceId = $("buildInstance").value;
  $("buildInstanceId").textContent = instanceId || t("console.newInstanceId");
  $("buildKeyName").textContent = `KNSoft.ZPigeon.Client.${buildConfig.deploymentId}.${instanceId || "<InstanceId>"}`;
  $("buildDownload").disabled = !buildConfig.available || !selected.length || !!buildActive();
  const executionExtensions = document.querySelector(".build-execution-extensions"),
    executionHeading = $("buildModules").querySelector("input[data-build-subgroup-toggle='execution']");
  executionExtensions?.classList.toggle("disabled", !executionEnabled);
  if (executionHeading) executionHeading.disabled = !executionEnabled || buildActive();
  updateBuildGroupChecks();
  updateBuildSelectAll();
  if (!buildActive()) {
    $("buildSize").textContent = "—";
    $("buildCache").textContent = t("console.cachePending");
    $("buildResultDownload").hidden = true;
  }
}

function renderBuildCatalog() {
  $("buildTransports").replaceChildren(...buildConfig.transports.map((transport) =>
    buildCheckbox(transport.id, `${transport.name} · ${transport.port}`, transport.id === 2,
                  t(buildTransportDescriptions[transport.id]))));
  const availableIds = new Set(buildConfig.modules.map((module) => module.id));
  $("buildModules").replaceChildren(...buildModuleGroups.map(([key, entries, includeExecution]) => {
    const section = element("section", "build-module-group"),
      heading = element("label", "build-group-heading"),
      headingInput = document.createElement("input"),
      options = element("div", "build-module-options");
    headingInput.type = "checkbox";
    heading.title = t(key);
    headingInput.dataset.buildGroupToggle = key;
    headingInput.onchange = () => {
      for (const input of groupOptionInputs(key)) input.checked = headingInput.checked;
      if (key === "nav.tasks" && headingInput.checked) {
        const execution = $("buildModules").querySelector("input[data-build-module][value='10']");
        if (execution) execution.checked = true;
      }
      updateBuildSelection();
    };
    heading.append(headingInput, element("span", "", t(key)));
    options.append(...entries.filter(([id]) => availableIds.has(id)).map(([id, labelKey]) => {
      const module = buildConfig.modules.find((value) => value.id === id);
      return buildCheckbox(id,
                          t(labelKey),
                          true,
                          t(buildModuleDescriptionKeys[module.key] || labelKey),
                          { buildModule: "", buildGroupOption: key });
    }));
    section.append(heading, options);
    if (includeExecution) {
      const extension = element("div", "build-execution-extensions"),
        extensionHeading = element("label", "build-group-heading"),
        extensionHeadingInput = document.createElement("input"),
        executionOptions = element("div", "build-options");
      extensionHeadingInput.type = "checkbox";
      extensionHeading.title = t("console.executionExtensions");
      extensionHeadingInput.dataset.buildSubgroupToggle = "execution";
      extensionHeadingInput.onchange = () => {
        if (extensionHeadingInput.checked) {
          const execution = $("buildModules").querySelector("input[data-build-module][value='10']");
          if (execution) execution.checked = true;
        }
        for (const input of $("buildModules").querySelectorAll("input[data-build-subgroup-option]"))
          if (input.dataset.buildSubgroupOption === "execution") input.checked = extensionHeadingInput.checked;
        updateBuildSelection();
      };
      extensionHeading.append(extensionHeadingInput,
                              element("span", "", t("console.executionExtensions")));
      executionOptions.id = "buildExecutionOptions";
      executionOptions.append(...buildConfig.executionOptions.map((option) =>
        buildCheckbox(option.id,
                      t(buildExecutionLabels[option.id]),
                      true,
                      t(buildExecutionDescriptions[option.id]),
                      {
                        buildExecution: "",
                        buildGroupOption: key,
                        buildSubgroupOption: "execution",
                      })));
      extension.append(extensionHeading, executionOptions);
      section.append(extension);
    }
    return section;
  }));
  updateBuildSelection();
}

async function loadBuild() {
  try {
    const firstLoad = !buildConfig, previousInstance = $("buildInstance").value;
    buildConfig = await json("/api/clients/build");
    $("buildHost").value = buildConfig.host;
    $("buildServerName").value = buildConfig.serverName;
    $("buildCertificate").textContent = buildConfig.rootCertificateSha256;
    $("buildInstance").replaceChildren(new Option(t("console.newInstance"), ""),
      ...buildConfig.instances.map((instance) => new Option(t("console.updateInstance", {
        id: instance.instanceId.slice(0, 8), group: instance.group || t("console.ungrouped"),
      }), instance.instanceId)));
    if (buildConfig.instances.some((instance) => instance.instanceId === previousInstance))
      $("buildInstance").value = previousInstance;
    $("buildError").hidden = buildConfig.available;
    $("buildError").textContent = buildConfig.error || "";
    if (firstLoad) renderBuildCatalog();
    else if (!buildJob) updateBuildSelection();
  } catch (error) { notify(error.message); }
}

function setBuildFormBusy(busy) {
  for (const input of $("buildForm").querySelectorAll("input:not([readonly]), select, button[type=button]"))
    input.disabled = busy;
  $("buildCancel").hidden = !busy;
  $("buildCancel").disabled = buildCancelRequested;
  updateBuildSelection();
}

function renderBuildJob(snapshot) {
  buildJob = snapshot;
  const terminal = ["completed", "failed", "canceled"].includes(snapshot.state),
    output = $("buildOutput"), followOutput = output.scrollHeight - output.scrollTop - output.clientHeight < 80;
  $("buildLogSection").hidden = false;
  $("buildLogSection").classList.toggle("failed", snapshot.state === "failed");
  output.textContent = snapshot.log;
  if (followOutput) output.scrollTop = output.scrollHeight;
  $("buildState").textContent = t({
    queued: "console.buildQueued", building: "console.buildRunning", completed: "console.buildCompleted",
    failed: "console.buildFailed", canceled: "console.buildCanceled",
  }[snapshot.state] || "console.buildRunning");
  $("buildError").hidden = !snapshot.error;
  $("buildError").textContent = snapshot.error || "";
  setBuildFormBusy(!terminal);
  $("buildSize").textContent = snapshot.executableSize ?
    `${bytes(snapshot.executableSize)} (${snapshot.executableSize.toLocaleString()} B)` : "—";
  $("buildInstanceId").textContent = snapshot.instanceId;
  $("buildKeyName").textContent = snapshot.keyName;
  $("buildCache").textContent = ["queued", "preparing"].includes(snapshot.state) ? t("console.cachePending") :
    t(snapshot.cacheHit ? "console.cacheHit" : "console.cacheMiss");
  if (snapshot.runtimeFiles) {
    $("buildRuntimeFiles").replaceChildren(...snapshot.runtimeFiles.flatMap((name, index) => {
      const link = snapshot.state === "completed" ? element("a", "control-link", name) : document.createTextNode(name);
      if (snapshot.state === "completed")
        link.href = `/api/clients/build/jobs/${snapshot.id}/runtime/${encodeURIComponent(name)}`;
      return index ? [document.createTextNode(" · "), link] : [link];
    }));
    if (!snapshot.runtimeFiles.length) $("buildRuntimeFiles").textContent = t("console.noRuntimeFiles");
  }
  $("buildResultDownload").hidden = snapshot.state !== "completed";
  $("buildResultDownload").href = `/api/clients/build/jobs/${snapshot.id}/download`;
  if (terminal) {
    buildEvents?.close();
    buildEvents = null;
    loadBuild();
    refresh();
  }
}

function watchBuild(snapshot) {
  buildEvents?.close();
  renderBuildJob(snapshot);
  if (["completed", "failed", "canceled"].includes(snapshot.state)) return;
  buildEvents = new EventSource(`/api/clients/build/jobs/${snapshot.id}/events`);
  buildEvents.addEventListener("build", (event) => renderBuildJob(JSON.parse(event.data)));
  buildEvents.onerror = () => { $("buildState").textContent = t("console.buildReconnecting"); };
}

function showView(name) {
  if (!["connections", "build", "settings"].includes(name)) name = "connections";
  for (const view of ["connections", "build", "settings"]) $(`${view}View`).hidden = view !== name;
  for (const button of document.querySelectorAll("[data-view]"))
    button.classList.toggle("active",
      button.dataset.view === name && (name !== "connections" || selectedGroup === null));
  if (location.hash !== `#${name}`) history.replaceState(null, "", `#${name}`);
  if (name === "build") loadBuild();
}

for (const button of document.querySelectorAll("[data-view]")) button.onclick = () => {
  selectedGroup = null;
  showView(button.dataset.view);
  render();
};
window.addEventListener("hashchange", () => showView(location.hash.slice(1)));
$("search").oninput = render;
$("refresh").onclick = refresh;
$("addGroup").onclick = () => {
  $("groupForm").reset();
  $("groupDialog").showModal();
};
$("groupDialog").querySelector('[data-action="cancel"]').onclick = () => $("groupDialog").close();
$("groupForm").onsubmit = async (event) => {
  event.preventDefault();
  const group = $("groupName").value.trim();
  if (!group) return;
  try {
    await json("/api/console/groups", "POST", { group });
    $("groupDialog").close();
    await refresh();
  } catch (error) { notify(error.message); }
};
async function startBuild() {
  if (buildActive()) return;
  buildStarting = true;
  buildCancelRequested = false;
  buildJob = null;
  buildEvents?.close();
  $("buildResultDownload").hidden = true;
  $("buildError").hidden = true;
  $("buildLogSection").hidden = false;
  $("buildOutput").textContent = "";
  $("buildState").textContent = t("console.buildQueued");
  $("buildLogSection").scrollIntoView({ behavior: "smooth", block: "start" });
  setBuildFormBusy(true);
  try {
    const snapshot = await json("/api/clients/build/jobs", "POST", {
      group: $("buildGroup").value.trim(),
      instanceId: $("buildInstance").value || null,
      modules: selectedBuildModules(),
      transports: selectedBuildTransports(),
      executionOptions: selectedBuildModules().includes(10) ? selectedBuildExecutionOptions() : [],
      requireTcpTls13: $("buildTcpTls13").checked,
    });
    buildStarting = false;
    watchBuild(snapshot);
  } catch (error) {
    buildStarting = false;
    setBuildFormBusy(false);
    $("buildError").hidden = false;
    $("buildError").textContent = error.message;
    $("buildState").textContent = t("console.buildFailed");
    notify(error.message);
  }
}
$("buildDownload").onclick = startBuild;
$("buildForm").onsubmit = (event) => {
  event.preventDefault();
  startBuild();
};
$("buildCancel").onclick = async () => {
  if (!buildJob || !buildActive()) return;
  buildCancelRequested = true;
  $("buildCancel").disabled = true;
  try {
    await request(`/api/clients/build/jobs/${buildJob.id}/cancel`, "POST");
    renderBuildJob(await json(`/api/clients/build/jobs/${buildJob.id}`));
  } catch (error) {
    buildCancelRequested = false;
    $("buildCancel").disabled = false;
    notify(error.message);
  }
};
$("buildModules").onchange = $("buildTransports").onchange = $("buildPreferredTransport").onchange =
  $("buildTcpTls13").onchange = updateBuildSelection;
$("buildInstance").onchange = () => {
  const instance = buildConfig.instances.find((value) => value.instanceId === $("buildInstance").value);
  if (instance) $("buildGroup").value = instance.group;
  updateBuildSelection();
};
$("buildGroup").oninput = updateBuildSelection;
$("buildSelectAll").onchange = () => {
  for (const input of featureOptionInputs()) input.checked = $("buildSelectAll").checked;
  updateBuildSelection();
};
$("mcpAccessMode").onchange = updateMcpWarning;
$("mcpCopyEndpoint").onclick = async () => {
  try {
    await navigator.clipboard.writeText($("mcpEndpoint").value);
    notify(t("console.mcpEndpointCopied"));
  } catch (error) { notify(error.message); }
};
$("mcpForm").onsubmit = async (event) => {
  event.preventDefault();
  if (!event.currentTarget.checkValidity()) return;
  $("mcpSave").disabled = true;
  $("mcpSaveStatus").textContent = "";
  try {
    mcpPolicy = await json("/api/console/mcp", "PUT", {
      enabled: $("mcpEnabled").checked,
      readOnlyOnly: $("mcpAccessMode").value === "readonly",
      maxConcurrentCalls: Number($("mcpMaxConcurrent").value),
      maxCallsPerMinute: Number($("mcpMaxCallsPerMinute").value),
      callTimeoutSeconds: Number($("mcpCallTimeout").value),
      maxResultSizeBytes: Number($("mcpMaxResultSize").value) * 1048576,
    });
    renderMcpPolicy();
    $("mcpSaveStatus").textContent = t("console.mcpSaved");
  } catch (error) {
    notify(error.message);
  } finally { $("mcpSave").disabled = false; }
};
const preference = getTheme();
$("theme").value = preference.theme;
$("accent").value = preference.accent;
$("theme").onchange = $("accent").onchange = () => setTheme($("theme").value, $("accent").value);
window.addEventListener("themechange", () => {
  const current = getTheme();
  $("theme").value = current.theme;
  $("accent").value = current.accent;
});
showView(location.hash.slice(1));
await refresh();
await loadMcpPolicy();
const events = new EventSource("/api/clients/events");
events.addEventListener("clients", (event) => {
  const snapshot = JSON.parse(event.data);
  clients = snapshot.clients;
  groups = snapshot.groups;
  $("consoleStatus").textContent = t("console.serverReady");
  render();
});
events.onerror = () => { $("consoleStatus").textContent = t("console.reconnecting"); };
