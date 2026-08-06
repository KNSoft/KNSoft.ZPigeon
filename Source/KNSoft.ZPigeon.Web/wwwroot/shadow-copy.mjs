import { t } from "./i18n.mjs";

const actions = { create: 1, delete: 2, enable: 3, disable: 4, activate: 21, configure: 23 };
const paths = {
  protection: "system-protection",
  restorePoints: "restore-points",
  shadowCopies: "shadow-copies",
};

const formatBytes = (value) => {
  let size = Number(value);
  if (!Number.isFinite(size) || size < 0) return "—";
  const units = ["B", "KiB", "MiB", "GiB", "TiB", "PiB"];
  let unit = 0;
  while (size >= 1024 && unit < units.length - 1) {
    size /= 1024;
    unit++;
  }
  return `${size.toLocaleString(undefined, { maximumFractionDigits: unit ? 2 : 0 })} ${units[unit]}`;
};

const fileTime = (value) => {
  const ticks = BigInt(value || 0);
  return ticks ? new Date(Number((ticks - 116444736000000000n) / 10000n)).toLocaleString() : "—";
};

const placeMenu = (menu, event) => {
  menu.hidden = false;
  const box = menu.getBoundingClientRect();
  menu.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - box.width - 6))}px`;
  menu.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - box.height - 6))}px`;
};

export class ShadowCopyManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.active = "protection";
    this.records = new Map();
    this.loaded = new Set();
    this.loading = new Set();
    this.epoch = 0;
    root.classList.add("shadow-copy-manager");
    root.innerHTML = /* HTML */ `<div class="property-tabs">
        <button class="active" data-tab="protection">${t("shadow.protection")}</button
        ><button data-tab="restorePoints">${t("shadow.restorePoints")}</button
        ><button data-tab="shadowCopies">${t("shadow.copies")}</button>
      </div>
      <section class="shadow-copy-panel" data-panel="protection">
        <div class="manager-toolbar">
          <span class="spacer"></span><button data-action="restore-create">${t("shadow.createRestorePoint")}</button
          ><button data-action="refresh">${t("common.refresh")}</button>
        </div>
        <div class="manager-table">
          <table>
            <thead><tr>
              <th>${t("shadow.drive")}</th><th>${t("common.status")}</th><th>${t("shadow.maximum")}</th
              ><th>${t("shadow.used")}</th><th>${t("shadow.allocated")}</th><th>${t("shadow.storageVolume")}</th>
            </tr></thead>
            <tbody></tbody>
          </table>
          <div class="manager-empty">${t("common.disconnected")}</div>
        </div>
      </section>
      <section class="shadow-copy-panel" data-panel="restorePoints" hidden>
        <div class="manager-toolbar">
          <span class="spacer"></span><button data-action="create">${t("shadow.createRestorePoint")}</button
          ><button data-action="refresh">${t("common.refresh")}</button>
        </div>
        <div class="manager-table">
          <table>
            <thead><tr>
              <th>${t("shadow.sequence")}</th><th>${t("common.description")}</th
              ><th>${t("shadow.created")}</th><th>${t("common.type")}</th>
            </tr></thead>
            <tbody></tbody>
          </table>
          <div class="manager-empty">${t("common.disconnected")}</div>
        </div>
      </section>
      <section class="shadow-copy-panel" data-panel="shadowCopies" hidden>
        <div class="manager-toolbar">
          <span class="spacer"></span><button data-action="create">${t("shadow.createCopy")}</button
          ><button data-action="refresh">${t("common.refresh")}</button>
        </div>
        <div class="manager-table">
          <table>
            <thead><tr>
              <th>${t("shadow.sourceVolume")}</th><th>${t("shadow.created")}</th
              ><th>${t("shadow.device")}</th><th>${t("shadow.attributes")}</th><th>ID</th>
            </tr></thead>
            <tbody></tbody>
          </table>
          <div class="manager-empty">${t("common.disconnected")}</div>
        </div>
      </section>
      <div class="context-menu administration-menu" data-role="menu" hidden></div>
      <dialog data-role="protection-editor">
        <form>
          <h2>${t("shadow.configureProtection")}</h2>
          <div class="dialog-summary" data-field="volume"></div>
          <label><input type="checkbox" data-field="enabled" />${t("shadow.enableProtection")}</label>
          <label>${t("shadow.maximumGiB")}
            <input data-field="maximum" type="number" min="0.01" step="0.01" required />
          </label>
          <div class="dialog-actions"><button type="button" data-action="cancel">${t("common.cancel")}</button
            ><button>${t("common.apply")}</button></div>
        </form>
      </dialog>
      <dialog data-role="restore-editor">
        <form>
          <h2>${t("shadow.createRestorePoint")}</h2>
          <label>${t("common.description")}<input data-field="description" maxlength="255" required /></label>
          <div class="dialog-actions"><button type="button" data-action="cancel">${t("common.cancel")}</button
            ><button>${t("common.create")}</button></div>
        </form>
      </dialog>
      <dialog data-role="copy-editor">
        <form>
          <h2>${t("shadow.createCopy")}</h2>
          <label>${t("shadow.sourceVolume")}<select data-field="volume" required></select></label>
          <p class="muted">${t("shadow.copyHint")}</p>
          <div class="dialog-actions"><button type="button" data-action="cancel">${t("common.cancel")}</button
            ><button>${t("common.create")}</button></div>
        </form>
      </dialog>`;
    this.menu = root.querySelector("[data-role=menu]");
    this.protectionDialog = root.querySelector("[data-role=protection-editor]");
    this.restoreDialog = root.querySelector("[data-role=restore-editor]");
    this.copyDialog = root.querySelector("[data-role=copy-editor]");
    for (const tab of root.querySelectorAll("[data-tab]")) tab.onclick = () => this.select(tab.dataset.tab);
    for (const panel of root.querySelectorAll("[data-panel]")) {
      panel.querySelector("[data-action=refresh]").onclick = () => this.load(panel.dataset.panel, true);
      panel.querySelector("tbody").oncontextmenu = (event) => this.openMenu(event, panel.dataset.panel);
    }
    root.querySelector("[data-panel=protection] [data-action=restore-create]").onclick = () => this.openRestore();
    root.querySelector("[data-panel=restorePoints] [data-action=create]").onclick = () => this.openRestore();
    root.querySelector("[data-panel=shadowCopies] [data-action=create]").onclick = () => this.openCopy();
    for (const dialog of [this.protectionDialog, this.restoreDialog, this.copyDialog])
      dialog.querySelector("[data-action=cancel]").onclick = () => dialog.close();
    this.protectionDialog.querySelector("form").onsubmit = (event) => this.saveProtection(event);
    this.restoreDialog.querySelector("form").onsubmit = (event) => this.createRestorePoint(event);
    this.copyDialog.querySelector("form").onsubmit = (event) => this.createShadowCopy(event);
    document.addEventListener("pointerdown", (event) => {
      if (!this.menu.hidden && !this.menu.contains(event.target)) this.menu.hidden = true;
    });
  }

  activate(connected) {
    this.connected = connected;
    if (connected) this.load(this.active);
    else this.setEmpty(this.active, t("common.disconnected"));
  }

  disconnect() {
    this.connected = false;
    this.epoch++;
    this.loaded.clear();
    this.loading.clear();
    this.records.clear();
    for (const panel of Object.keys(paths)) {
      this.render(panel);
      this.setEmpty(panel, t("common.disconnected"));
    }
  }

  select(panel) {
    this.active = panel;
    for (const tab of this.root.querySelectorAll("[data-tab]"))
      tab.classList.toggle("active", tab.dataset.tab === panel);
    for (const item of this.root.querySelectorAll("[data-panel]")) item.hidden = item.dataset.panel !== panel;
    if (this.connected) this.load(panel);
  }

  async load(panel, force = false) {
    if (!this.connected || this.loading.has(panel) || (this.loaded.has(panel) && !force)) return;
    const epoch = this.epoch;
    this.loading.add(panel);
    this.setEmpty(panel, t("common.fetching"));
    try {
      const records = await this.call(`/api/${paths[panel]}`);
      if (!this.connected || epoch !== this.epoch) return;
      this.records.set(panel, records);
      this.loaded.add(panel);
      this.render(panel);
    } catch (error) {
      if (epoch !== this.epoch) return;
      this.records.delete(panel);
      this.loaded.delete(panel);
      this.render(panel);
      this.setEmpty(panel, error.message);
      this.notify(error);
    } finally {
      if (epoch === this.epoch) this.loading.delete(panel);
    }
  }

  render(panel) {
    const body = this.root.querySelector(`[data-panel=${panel}] tbody`),
      empty = this.root.querySelector(`[data-panel=${panel}] .manager-empty`),
      records = this.records.get(panel) || [];
    body.replaceChildren(...records.map((record) => this.createRow(panel, record)));
    empty.hidden = records.length !== 0;
    if (!records.length && this.loaded.has(panel)) empty.textContent = t(`shadow.empty.${panel}`);
  }

  createRow(panel, record) {
    const detail = String(record.detail || "").split("\n");
    let values;
    if (panel === "protection") {
      values = [
        `${record.identity} ${record.name === record.identity ? "" : record.name}`.trim(),
        record.state ? t("common.enabled") : t("common.disabled"),
        record.state ? formatBytes(record.value) : "—",
        record.state ? formatBytes(detail[0]) : "—",
        record.state ? formatBytes(detail[1]) : "—",
        detail[2] || "—",
      ];
    } else if (panel === "restorePoints") {
      const types = { 0: t("shadow.type.applicationInstall"), 1: t("shadow.type.applicationUninstall"),
        10: t("shadow.type.deviceDriver"), 12: t("shadow.type.modifySettings"), 13: t("shadow.type.cancelled"),
      };
      values = [record.identity, record.name, fileTime(record.value), types[record.state] || record.state];
    } else {
      const attributes = [
        record.flags & 1 ? t("shadow.attribute.clientAccessible") : "",
        record.flags & 2 ? t("shadow.attribute.persistent") : "",
        record.flags & 4 ? t("shadow.attribute.noAutoRelease") : "",
        record.flags & 8 ? t("shadow.attribute.noWriters") : "",
        record.flags & 16 ? t("shadow.attribute.exposed") : "",
        record.flags & 32 ? t("shadow.attribute.hardware") : "",
      ].filter(Boolean);
      values = [record.name, fileTime(record.value), record.description, attributes.join(", ") || "—", record.identity];
    }
    const row = document.createElement("tr");
    row.record = record;
    for (const value of values) {
      const cell = row.insertCell();
      cell.textContent = value ?? "";
      cell.title = cell.textContent;
    }
    if (panel === "protection") row.ondblclick = () => this.configureProtection(record);
    return row;
  }

  setEmpty(panel, value) {
    const empty = this.root.querySelector(`[data-panel=${panel}] .manager-empty`);
    empty.hidden = false;
    empty.textContent = value;
  }

  openMenu(event, panel) {
    const row = event.target.closest("tr");
    if (!row) return;
    event.preventDefault();
    const record = row.record;
    this.menu.replaceChildren();
    if (panel === "protection") {
      this.addMenu(t("shadow.configure"), () => this.configureProtection(record));
    } else if (panel === "restorePoints") {
      this.addMenu(t("shadow.restore"), () => this.restore(record), true);
      this.addMenu(t("common.delete"), () => this.deleteRestorePoint(record), true);
    } else {
      this.addMenu(t("common.delete"), () => this.deleteShadowCopy(record), true);
    }
    placeMenu(this.menu, event);
  }

  addMenu(title, handler, danger = false) {
    const button = document.createElement("button");
    button.textContent = title;
    button.classList.toggle("danger", danger);
    button.onclick = () => {
      this.menu.hidden = true;
      handler();
    };
    this.menu.append(button);
  }

  configureProtection(record) {
    this.protectionTarget = record;
    this.protectionDialog.querySelector("[data-field=volume]").textContent = `${record.identity} · ${record.name}`;
    this.protectionDialog.querySelector("[data-field=enabled]").checked = record.state !== 0;
    const maximum = this.protectionDialog.querySelector("[data-field=maximum]");
    maximum.value = record.value ? Math.max(0.01, Number(record.value) / 1073741824).toFixed(2) : "5.00";
    this.protectionDialog.showModal();
    maximum.select();
  }

  async saveProtection(event) {
    event.preventDefault();
    const enabled = this.protectionDialog.querySelector("[data-field=enabled]").checked,
      maximum = Number(this.protectionDialog.querySelector("[data-field=maximum]").value),
      record = this.protectionTarget;
    if (!enabled && record.state && !confirm(t("shadow.confirmDisable", { volume: record.identity }))) return;
    if (enabled && (!Number.isFinite(maximum) || maximum <= 0)) return;
    const bytes = enabled ? BigInt(Math.round(maximum * 1024)) * 1048576n : null;
    try {
      await this.call("/api/system-protection/control", {
        action: enabled ? (record.state ? actions.configure : actions.enable) : actions.disable,
        identity: record.identity,
        argument: enabled ? String(bytes) : null,
      });
      this.protectionDialog.close();
      this.notify(t("shadow.protectionUpdated"));
      await this.load("protection", true);
    } catch (error) {
      this.notify(error);
    }
  }

  openRestore() {
    if (!this.connected) return;
    this.restoreDialog.querySelector("form").reset();
    this.restoreDialog.showModal();
    this.restoreDialog.querySelector("[data-field=description]").focus();
  }

  async createRestorePoint(event) {
    event.preventDefault();
    const description = this.restoreDialog.querySelector("[data-field=description]").value.trim();
    if (!description) return;
    try {
      await this.call("/api/restore-points/control", { action: actions.create, identity: description });
      this.restoreDialog.close();
      this.notify(t("shadow.restorePointCreated"));
      await this.load("restorePoints", true);
    } catch (error) {
      this.notify(error);
    }
  }

  async deleteRestorePoint(record) {
    if (!confirm(t("shadow.confirmDeleteRestore", { name: record.name }))) return;
    await this.run("restore-points", { action: actions.delete, identity: record.identity }, "restorePoints",
      t("shadow.restorePointDeleted"));
  }

  async restore(record) {
    if (!confirm(t("shadow.confirmRestore", { name: record.name }))) return;
    try {
      await this.call("/api/restore-points/control", { action: actions.activate, identity: record.identity });
      this.notify(t("shadow.restoreStarted"));
    } catch (error) {
      this.notify(error);
    }
  }

  async openCopy() {
    if (!this.connected) return;
    if (!this.loaded.has("protection")) await this.load("protection");
    const select = this.copyDialog.querySelector("[data-field=volume]");
    select.replaceChildren(
      ...(this.records.get("protection") || []).map(
        (record) => new Option(`${record.identity} · ${record.name}`, record.identity),
      ),
    );
    if (!select.options.length) {
      this.notify(t("shadow.noFixedVolume"));
      return;
    }
    this.copyDialog.showModal();
    select.focus();
  }

  async createShadowCopy(event) {
    event.preventDefault();
    const volume = this.copyDialog.querySelector("[data-field=volume]").value;
    try {
      await this.call("/api/shadow-copies/control", { action: actions.create, identity: volume });
      this.copyDialog.close();
      this.notify(t("shadow.copyCreated"));
      await this.load("shadowCopies", true);
    } catch (error) {
      this.notify(error);
    }
  }

  async deleteShadowCopy(record) {
    if (!confirm(t("shadow.confirmDeleteCopy", { id: record.identity }))) return;
    await this.run("shadow-copies", { action: actions.delete, identity: record.identity }, "shadowCopies",
      t("shadow.copyDeleted"));
  }

  async run(path, request, panel, message) {
    try {
      await this.call(`/api/${path}/control`, request);
      this.notify(message);
      await this.load(panel, true);
    } catch (error) {
      this.notify(error);
    }
  }
}
