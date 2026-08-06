import { t } from "./i18n.mjs";
import { apiUrl } from "./client-context.mjs";

const SPEED_LABELS = ["≤ 2 Mbps", "2–10 Mbps", "10–50 Mbps", "50–200 Mbps", "> 200 Mbps"];
const LATENCY_LABELS = ["> 300 ms", "150–300 ms", "80–150 ms", "40–80 ms", "≤ 40 ms"];

export class ZPigeonConnectionManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.active = false;
    this.loading = false;
    this.value = null;
    root.innerHTML = /* HTML */ ` <div class="zpigeon-page">
      <section class="zpigeon-card">
        <h2>${t("connection.current")}</h2>
        <div class="zpigeon-summary">
          <div><span>${t("common.status")}</span><strong data-role="state">${t("common.disconnected")}</strong></div>
          <div><span>${t("connection.protocol")}</span><strong data-role="transport">—</strong></div>
          <div><span>${t("connection.sent")}</span><strong data-role="sent">—</strong></div>
          <div><span>${t("connection.received")}</span><strong data-role="received">—</strong></div>
          <div><span>${t("connection.compression")}</span><strong data-role="compression">—</strong></div>
          <div><span>${t("connection.requests")}</span><strong data-role="requests">—</strong></div>
          <div><span>${t("connection.sendBacklog")}</span><strong data-role="send-backlog">—</strong></div>
          <div><span>${t("connection.sendQueueDelay")}</span><strong data-role="send-delay">—</strong></div>
          <div><span>${t("connection.rejectedSends")}</span><strong data-role="rejected-sends">—</strong></div>
        </div>
      </section>
      <section class="zpigeon-card">
        <div class="zpigeon-card-title">
          <h2>${t("connection.policy")}</h2>
          <div class="connection-actions">
            <button data-action="probe">${t("connection.detect")}</button>
            <label><input data-role="automatic" type="checkbox" />${t("connection.automatic")}</label>
          </div>
        </div>
        <p class="muted">${t("connection.passive")}</p>
        <div class="performance-control">
          <div class="performance-heading">
            <strong>${t("connection.speed")}</strong><span data-role="speed-effective"></span>
          </div>
          <div class="performance-track">
            <div class="performance-marker" data-role="speed-marker" hidden><span></span></div>
            <input data-role="speed" type="range" min="0" max="4" step="1" />
            <div class="performance-ticks">${SPEED_LABELS.map((value) => `<span>${value}</span>`).join("")}</div>
          </div>
          <div class="performance-details">
            <span data-role="speed-current">${t("connection.waitTraffic")}</span>
            <span data-role="directions"></span>
          </div>
        </div>
        <div class="performance-control">
          <div class="performance-heading">
            <strong>${t("connection.latency")}</strong><span data-role="latency-effective"></span>
          </div>
          <div class="performance-track">
            <div class="performance-marker" data-role="latency-marker" hidden><span></span></div>
            <input data-role="latency" type="range" min="0" max="4" step="1" />
            <div class="performance-ticks">${LATENCY_LABELS.map((value) => `<span>${value}</span>`).join("")}</div>
          </div>
          <div class="performance-details">
            <span data-role="latency-current">${t("connection.waitProbe")}</span>
            <span>${t("connection.latencyHint")}</span>
          </div>
        </div>
      </section>
    </div>`;
    this.automatic = root.querySelector("[data-role=automatic]");
    this.speed = root.querySelector("[data-role=speed]");
    this.latency = root.querySelector("[data-role=latency]");
    this.probeButton = root.querySelector("[data-action=probe]");
    this.automatic.onchange = () => this.save();
    this.speed.oninput = () => this.renderSelections();
    this.latency.oninput = () => this.renderSelections();
    this.speed.onchange = () => this.save();
    this.latency.onchange = () => this.save();
    this.probeButton.onclick = () => this.probe();
  }

  activate() {
    this.active = true;
    this.refresh();
  }
  deactivate() {
    this.active = false;
  }

  async refresh() {
    if (!this.active || this.loading) return;
    this.loading = true;
    try {
      const response = await fetch(apiUrl("/api/zpigeon/connection"));
      if (!response.ok) throw new Error(await response.text());
      this.value = await response.json();
      this.render();
    } catch (error) {
      this.notify(error);
    } finally {
      this.loading = false;
    }
  }

  async probe() {
    this.probeButton.disabled = true;
    this.probeButton.textContent = t("connection.detecting");
    try {
      this.value = await this.call("/api/zpigeon/connection/probe", {});
      this.render();
    } catch (error) {
      this.notify(error);
    } finally {
      this.probeButton.disabled = !this.value?.connected;
      this.probeButton.textContent = t("connection.detect");
    }
  }

  async save() {
    if (!this.value) return;
    try {
      this.value = await this.call("/api/zpigeon/connection", {
        automatic: this.automatic.checked,
        speedClass: Number(this.speed.value),
        latencyClass: Number(this.latency.value),
      });
      this.render();
    } catch (error) {
      this.notify(error);
      this.render();
    }
  }

  render() {
    const value = this.value;
    if (!value) return;
    this.automatic.checked = value.automatic;
    this.speed.disabled = this.latency.disabled = value.automatic;
    this.probeButton.disabled = !value.connected;
    this.speed.value = value.automatic ? value.effectiveSpeedClass : value.manualSpeedClass;
    this.latency.value = value.automatic ? value.effectiveLatencyClass : value.manualLatencyClass;
    this.renderSelections();
    this.text("state", value.connected ? t("common.connected") : t("common.disconnected"));
    this.text(
      "transport",
      value.connected
        ? { 1: "QUIC", 2: "TCP", 3: "UDP" }[value.transport] ||
            t("connection.transportType", { value: value.transport })
        : "—",
    );
    this.text("sent", value.connected ? this.bytes(value.sentBytes) : "—");
    this.text("received", value.connected ? this.bytes(value.receivedBytes) : "—");
    this.text(
      "requests",
      value.connected
        ? t("connection.requestSummary", {
            success: value.completedRequests.toLocaleString(),
            failed: value.failedRequests.toLocaleString(),
          })
        : "—",
    );
    this.text("compression", value.connected ? t("connection.adaptiveCompression") : "—");
    this.text(
      "send-backlog",
      value.connected
        ? t("connection.sendBacklogSummary", {
            current: this.bytes(value.outstandingSendBytes),
            maximum: this.bytes(value.maximumOutstandingSendBytes),
          })
        : "—",
    );
    this.text(
      "send-delay",
      value.connected ? `${value.maximumSendQueueDelayMilliseconds.toLocaleString()} ms` : "—",
    );
    this.text(
      "rejected-sends",
      value.connected ? value.rejectedSends.toLocaleString() : "—",
    );
    this.marker(
      "speed-marker",
      value.speedMbps,
      this.speedPosition,
      value.speedMbps == null ? "" : `${value.speedMbps.toFixed(1)} Mbps`,
    );
    this.marker(
      "latency-marker",
      value.roundTripMilliseconds,
      this.latencyPosition,
      value.roundTripMilliseconds == null ? "" : `${value.roundTripMilliseconds} ms`,
    );
    this.text(
      "speed-current",
      value.speedMbps == null
        ? t("connection.waitTraffic")
        : t("connection.currentSpeed", { value: value.speedMbps.toFixed(1) }) +
            (value.sampleAgeMilliseconds == null
              ? ""
              : ` · ${t("connection.sampleAgo", {
                  value: this.age(value.sampleAgeMilliseconds),
                })}`),
    );
    this.text(
      "directions",
      `Server → Client ${this.rate(value.sentMbps)} · Client → Server ${this.rate(value.receivedMbps)}`,
    );
    this.text(
      "latency-current",
      value.roundTripMilliseconds == null
        ? t("connection.waitProbe")
        : t("connection.currentLatency", { value: value.roundTripMilliseconds }),
    );
  }

  renderSelections() {
    const tier = this.automatic.checked ? t("connection.autoTier") : t("connection.manualTier");
    this.text("speed-effective", `${tier} · ${SPEED_LABELS[Number(this.speed.value)]}`);
    this.text("latency-effective", `${tier} · ${LATENCY_LABELS[Number(this.latency.value)]}`);
  }

  marker(role, value, position, label) {
    const marker = this.root.querySelector(`[data-role=${role}]`);
    marker.hidden = value == null;
    if (value == null) return;
    const percent = position(value);
    marker.style.left = `${percent}%`;
    marker.dataset.state = percent >= 75 ? "good" : percent >= 50 ? "fair" : "poor";
    marker.querySelector("span").textContent = label;
  }

  speedPosition(value) {
    return (value <= 2 ? 0 : value <= 10 ? 1 : value <= 50 ? 2 : value <= 200 ? 3 : 4) * 25;
  }
  latencyPosition(value) {
    return (value > 300 ? 0 : value > 150 ? 1 : value > 80 ? 2 : value > 40 ? 3 : 4) * 25;
  }
  text(role, value) {
    this.root.querySelector(`[data-role=${role}]`).textContent = value;
  }
  rate(value) {
    return value == null ? t("connection.waitSample") : `${value.toFixed(1)} Mbps`;
  }
  age(milliseconds) {
    const seconds = Math.floor(milliseconds / 1000);
    return seconds < 60
      ? `${seconds} s`
      : seconds < 3600
        ? `${Math.floor(seconds / 60)} min`
        : `${Math.floor(seconds / 3600)} h`;
  }
  bytes(value) {
    const units = ["B", "KiB", "MiB", "GiB", "TiB"];
    let number = Number(value),
      unit = 0;
    while (number >= 1024 && unit < units.length - 1) {
      number /= 1024;
      unit++;
    }
    return `${number.toFixed(unit ? 1 : 0)} ${units[unit]}`;
  }
}

const CLIENT_STATUS_GROUPS = ["process", "startup", "session", "security", "resources"],
  CLIENT_STATUS_BYTES = new Set([
    "workingSet",
    "peakWorkingSet",
    "privateBytes",
    "readBytes",
    "writeBytes",
    "otherBytes",
  ]),
  CLIENT_STATUS_TIMES = new Set(["kernelTime", "userTime"]);

export class ClientStatusManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <input data-role="filter" placeholder="${t("common.filter")}" /><span class="spacer"></span
        ><button data-action="refresh">${t("common.refresh")}</button>
      </div>
      <div class="client-status-page">
        <div class="client-status-cards" data-role="cards"></div>
        <section class="zpigeon-card client-environment">
          <h2>${t("clientStatus.environment")}</h2>
          <div class="manager-table">
            <table>
              <thead><tr><th>${t("common.name")}</th><th>${t("common.value")}</th></tr></thead>
              <tbody></tbody>
            </table>
            <div class="manager-empty">${t("common.clientDisconnected")}</div>
          </div>
        </section>
      </div>`;
    this.cards = root.querySelector("[data-role=cards]");
    this.body = root.querySelector("tbody");
    this.empty = root.querySelector(".manager-empty");
    this.filter = root.querySelector("[data-role=filter]");
    this.groupLists = new Map();
    this.filter.oninput = () => this.renderEnvironment();
    root.querySelector("[data-action=refresh]").onclick = () => this.load(true);
  }
  activate(connected) {
    const activated = !this.active;
    this.active = true;
    this.connected = connected;
    if (!connected) return;
    if (!this.loaded) this.load();
    else if (activated) this.schedule();
  }
  deactivate() {
    this.active = false;
    clearTimeout(this.timer);
  }
  disconnect() {
    this.deactivate();
    this.connected = this.loaded = false;
    this.request = (this.request || 0) + 1;
    this.records = [];
    this.cards.replaceChildren();
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = t("common.clientDisconnected");
  }
  async load(force = false, resourcesOnly = false) {
    if (!this.connected || this.loading || (this.loaded && !force)) return;
    clearTimeout(this.timer);
    const request = (this.request || 0) + 1;
    this.request = request;
    this.loading = true;
    if (!resourcesOnly) {
      this.empty.hidden = false;
      this.empty.textContent = t("common.fetching");
    }
    try {
      const records = await this.call("/api/client-status");
      if (request !== this.request) return;
      this.records = records;
      this.loaded = true;
      if (resourcesOnly && this.groupLists.size) this.renderGroup("resources");
      else this.render();
    } catch (error) {
      if (request !== this.request) return;
      if (!resourcesOnly) this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      if (request === this.request) {
        this.loading = false;
        this.schedule();
      }
    }
  }
  schedule() {
    clearTimeout(this.timer);
    if (this.active && this.connected && this.loaded)
      this.timer = setTimeout(() => this.load(true, true), 3000);
  }
  render() {
    this.groupLists.clear();
    this.cards.replaceChildren(
      ...CLIENT_STATUS_GROUPS.map((group) => {
        const section = document.createElement("section"),
          title = document.createElement("h2"),
          list = document.createElement("dl");
        section.className = "zpigeon-card";
        list.className = "details-grid";
        title.textContent = t(`clientStatus.group.${group}`);
        this.groupLists.set(group, list);
        section.append(title, list);
        return section;
      }),
    );
    for (const group of CLIENT_STATUS_GROUPS) this.renderGroup(group);
    this.renderEnvironment();
  }
  renderGroup(group) {
    const list = this.groupLists.get(group);
    if (!list) return;
    list.replaceChildren();
    for (const record of this.records.filter((record) => record.kind === 65 && record.description === group)) {
      const term = document.createElement("dt"),
        detail = document.createElement("dd");
      term.textContent = t(`clientStatus.${record.identity}`);
      detail.textContent = this.value(record);
      detail.title = detail.textContent;
      list.append(term, detail);
    }
  }
  renderEnvironment() {
    const query = this.filter.value.toLocaleLowerCase(),
      values = (this.records || [])
        .filter(
          (record) =>
            record.kind === 66 &&
            (!query || `${record.identity} ${record.detail}`.toLocaleLowerCase().includes(query)),
        )
        .sort((left, right) => left.identity.localeCompare(right.identity));
    this.body.replaceChildren(
      ...values.map((record) => {
        const row = document.createElement("tr"),
          name = row.insertCell(),
          value = row.insertCell();
        name.textContent = name.title = record.identity;
        value.textContent = value.title = record.detail;
        name.dataset.copyable = value.dataset.copyable = "";
        return row;
      }),
    );
    this.empty.hidden = values.length !== 0;
    if (this.connected && this.loaded && !values.length) this.empty.textContent = t("common.noItems");
  }
  value(record) {
    if (record.identity === "startTime") {
      const ticks = BigInt(record.value);
      return new Date(Number((ticks - 116444736000000000n) / 10000n)).toLocaleString();
    }
    if (CLIENT_STATUS_BYTES.has(record.identity)) return this.bytes(record.value);
    if (CLIENT_STATUS_TIMES.has(record.identity)) return this.duration(Number(record.value) / 10000000);
    if (["interactive", "administrator"].includes(record.identity))
      return t(Number(record.value) ? "common.yes" : "common.no");
    if (record.identity === "machine") {
      const value = Number(record.value),
        native = value & 0xffff,
        process = value >>> 16 || native,
        name = (machine) =>
          ({ 0x014c: "x86", 0x8664: "x64", 0xaa64: "ARM64" })[machine] ||
          t("clientStatus.architecture.unknown", {
            value: machine.toString(16).padStart(4, "0").toUpperCase(),
          });
      return `${name(process)} / ${name(native)}`;
    }
    if (record.identity === "parentProcessId") {
      const path = this.records.find((value) => value.identity === "parentImagePath")?.detail;
      return path ? `${record.value} (${path.split(/[\\/]/).pop()})` : record.value;
    }
    if (record.identity === "integrityLevel") {
      const key = ({
        0: "untrusted",
        4096: "low",
        8192: "medium",
        8448: "mediumPlus",
        12288: "high",
        16384: "system",
        20480: "protected",
      })[record.value];
      return key ? t(`clientStatus.integrity.${key}`) : record.value;
    }
    if (record.identity === "priorityClass") {
      const key = ({
        32: "normal",
        64: "idle",
        128: "high",
        256: "realtime",
        16384: "belowNormal",
        32768: "aboveNormal",
      })[record.value];
      return key ? t(`clientStatus.priority.${key}`) : record.value;
    }
    return record.detail || record.value || "—";
  }
  duration(seconds) {
    return seconds < 60
      ? t("clientStatus.duration.seconds", { value: seconds.toFixed(2) })
      : t("clientStatus.duration.minutes", {
          minutes: Math.floor(seconds / 60),
          seconds: (seconds % 60).toFixed(1),
        });
  }
  bytes(value) {
    const units = ["B", "KiB", "MiB", "GiB", "TiB"];
    let number = Number(value),
      unit = 0;
    while (number >= 1024 && unit < units.length - 1) {
      number /= 1024;
      unit++;
    }
    return `${number.toFixed(unit ? 1 : 0)} ${units[unit]}`;
  }
}
