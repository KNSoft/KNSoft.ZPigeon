import { t } from "./i18n.mjs";

const actions = { create: 1, delete: 2, enable: 3, disable: 4, lock: 20, encrypt: 30, decrypt: 31,
  pause: 32, resume: 33, unlock: 34 };
const paths = { volumes: "bitlocker/volumes", protectors: "bitlocker/protectors" };
const conversionKeys = ["decrypted", "encrypted", "encrypting", "decrypting", "encryptionPaused", "decryptionPaused"];
const encryptionKeys = ["none", "aes128Diffuser", "aes256Diffuser", "aes128", "aes256", "hardware", "xts128", "xts256"];
const protectorKeys = ["unknown", "tpm", "externalKey", "recoveryPassword", "tpmPin", "tpmStartupKey",
  "tpmPinStartupKey", "publicKey", "passphrase", "tpmCertificate", "cng", "clearKey"];

const fileTime = (value) => {
  const ticks = BigInt(value);
  return ticks ? new Date(Number((ticks - 116444736000000000n) / 10000n)).toLocaleString() : "—";
};

const recoveryPassword = () => {
  const values = crypto.getRandomValues(new Uint32Array(8));
  return Array.from(values, (value) => String(((value % 65535) + 1) * 11).padStart(6, "0")).join("-");
};

export class BitLockerManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.active = "volumes";
    this.records = new Map();
    this.loaded = new Set();
    this.loading = new Set();
    this.epoch = 0;
    root.innerHTML = /* HTML */ `<div class="property-tabs">
        <button class="active" data-tab="volumes">${t("bitlocker.volumes")}</button
        ><button data-tab="protectors">${t("bitlocker.protectors")}</button>
      </div>
      <section data-panel="volumes">
        <div class="manager-toolbar"><span class="spacer"></span
          ><button data-action="refresh">${t("common.refresh")}</button></div>
        <div class="manager-table"><table><thead><tr>
          <th>${t("bitlocker.volume")}</th><th>${t("common.type")}</th><th>${t("bitlocker.conversion")}</th
          ><th>${t("bitlocker.progress")}</th><th>${t("bitlocker.protection")}</th
          ><th>${t("bitlocker.lock")}</th><th>${t("bitlocker.method")}</th
          ><th>${t("bitlocker.scope")}</th><th>${t("bitlocker.autoUnlock")}</th><th>${t("common.actions")}</th>
        </tr></thead><tbody></tbody></table><div class="manager-empty"></div></div>
      </section>
      <section data-panel="protectors" hidden>
        <div class="manager-toolbar"><span class="spacer"></span
          ><button data-action="add">${t("bitlocker.addRecoveryPassword")}</button
          ><button data-action="refresh">${t("common.refresh")}</button></div>
        <div class="manager-table"><table><thead><tr>
          <th>${t("bitlocker.volume")}</th><th>${t("common.type")}</th><th>${t("common.description")}</th
          ><th>${t("bitlocker.created")}</th><th>ID</th><th>${t("common.actions")}</th>
        </tr></thead><tbody></tbody></table><div class="manager-empty"></div></div>
      </section>
      <dialog data-role="encrypt"><form>
        <h2>${t("bitlocker.enableTitle")}</h2><div class="dialog-summary" data-field="volume"></div>
        <label>${t("bitlocker.method")}<select data-field="method">
          <option value="6">XTS-AES 128</option><option value="7">XTS-AES 256</option
          ><option value="3">AES 128</option><option value="4">AES 256</option>
        </select></label><label>${t("bitlocker.scope")}<select data-field="scope">
          <option value="256">${t("bitlocker.scope.used")}</option
          ><option value="0">${t("bitlocker.scope.full")}</option>
        </select></label><p class="muted">${t("bitlocker.encryptHint")}</p>
        <div class="dialog-actions"><button type="button" data-action="cancel">${t("common.cancel")}</button
          ><button>${t("bitlocker.encrypt")}</button></div>
      </form></dialog>
      <dialog data-role="recovery"><form>
        <h2>${t("bitlocker.addRecoveryPassword")}</h2>
        <label>${t("bitlocker.volume")}<select data-field="volume" required></select></label>
        <label>${t("common.name")}<input data-field="name" maxlength="256" /></label>
        <label>${t("bitlocker.recoveryPassword")}<input data-field="password" maxlength="55" required /></label>
        <p class="muted">${t("bitlocker.recoveryHint")}</p>
        <div class="dialog-actions"><button type="button" data-action="cancel">${t("common.cancel")}</button
          ><button>${t("common.create")}</button></div>
      </form></dialog>`;
    this.encryptDialog = root.querySelector("[data-role=encrypt]");
    this.recoveryDialog = root.querySelector("[data-role=recovery]");
    for (const tab of root.querySelectorAll("[data-tab]")) tab.onclick = () => this.select(tab.dataset.tab);
    for (const panel of root.querySelectorAll("[data-panel]"))
      panel.querySelector("[data-action=refresh]").onclick = () => this.load(panel.dataset.panel, true);
    root.querySelector("[data-panel=protectors] [data-action=add]").onclick = () => this.openRecovery();
    for (const dialog of [this.encryptDialog, this.recoveryDialog])
      dialog.querySelector("[data-action=cancel]").onclick = () => dialog.close();
    this.encryptDialog.querySelector("form").onsubmit = (event) => this.encrypt(event);
    this.recoveryDialog.querySelector("form").onsubmit = (event) => this.addRecovery(event);
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
    const records = this.records.get(panel) || [],
      body = this.root.querySelector(`[data-panel=${panel}] tbody`),
      empty = this.root.querySelector(`[data-panel=${panel}] .manager-empty`);
    body.replaceChildren(...records.map((record) => panel === "volumes" ? this.volumeRow(record) :
      this.protectorRow(record)));
    empty.hidden = records.length !== 0;
    if (!records.length && this.loaded.has(panel)) empty.textContent = t(`bitlocker.empty.${panel}`);
  }

  volumeRow(record) {
    const row = document.createElement("tr"),
      volumeType = record.flags & 3,
      protection = (record.flags & 0xc) >> 2,
      lock = (record.flags & 0x30) >> 4,
      method = (record.flags & 0xf00) >> 8,
      initialized = Boolean(record.flags & 0x1000),
      values = [
        record.name,
        t(`bitlocker.volumeType.${["system", "fixed", "removable"][volumeType] || "unknown"}`),
        t(`bitlocker.conversionState.${conversionKeys[record.state] || "unknown"}`),
        `${Number(record.value).toLocaleString()}%`,
        t(`bitlocker.protectionState.${["off", "on", "unknown"][protection] || "unknown"}`),
        t(`bitlocker.lockState.${["unlocked", "locked", "unknown"][lock] || "unknown"}`),
        t(`bitlocker.encryptionMethod.${encryptionKeys[method] || "unknown"}`),
        initialized ? t(`bitlocker.scope.${record.flags & 0x4000 ? "used" : "full"}`) : "—",
        volumeType ? t(record.flags & 0x2000 ? "common.enabled" : "common.disabled") : "—",
      ];
    for (const value of values) {
      const cell = row.insertCell();
      cell.textContent = value;
      cell.title = value;
    }
    const buttons = [];
    if (record.state === 0) buttons.push([t("bitlocker.encrypt"), () => this.openEncrypt(record)]);
    else if (record.state === 1) buttons.push([t("bitlocker.decrypt"), () => this.decrypt(record), true]);
    else if (record.state === 2 || record.state === 3)
      buttons.push([t("common.pause"), () => this.runVolume(record, actions.pause, null, null,
        t("bitlocker.conversionPaused"))]);
    else if (record.state === 4 || record.state === 5)
      buttons.push([t("common.resume"), () => this.runVolume(record, actions.resume, null, null,
        t("bitlocker.conversionResumed"))]);
    if (protection === 1)
      buttons.push([t("bitlocker.suspend"), () => this.suspend(record)]);
    else if (protection === 0 && record.state !== 0)
      buttons.push([t("bitlocker.resumeProtection"), () => this.runVolume(record, actions.enable, null, null,
        t("bitlocker.protectionResumed"))]);
    if (volumeType !== 0 && record.state !== 0)
      buttons.push(lock === 1 ? [t("bitlocker.unlock"), () => this.unlock(record)] :
        [t("bitlocker.lockVolume"), () => this.lock(record), true]);
    const actionCell = row.insertCell();
    for (const [label, handler, danger] of buttons) {
      const button = document.createElement("button");
      button.textContent = label;
      button.classList.toggle("danger", Boolean(danger));
      button.onclick = handler;
      actionCell.append(button);
    }
    return row;
  }

  protectorRow(record) {
    const row = document.createElement("tr");
    for (const value of [record.name, t(`bitlocker.protectorType.${protectorKeys[record.state] || "unknown"}`),
      record.description, fileTime(record.value), record.identity]) {
      const cell = row.insertCell();
      cell.textContent = value;
      cell.title = value;
    }
    const actionCell = row.insertCell();
    if (record.state !== 11) {
      const button = document.createElement("button");
      button.textContent = t("common.delete");
      button.className = "danger";
      button.onclick = () => this.deleteProtector(record);
      actionCell.append(button);
    }
    return row;
  }

  setEmpty(panel, value) {
    const empty = this.root.querySelector(`[data-panel=${panel}] .manager-empty`);
    empty.hidden = false;
    empty.textContent = value;
  }

  openEncrypt(record) {
    this.encryptTarget = record;
    this.encryptDialog.querySelector("[data-field=volume]").textContent = record.name;
    this.encryptDialog.showModal();
  }

  async encrypt(event) {
    event.preventDefault();
    const method = Number(this.encryptDialog.querySelector("[data-field=method]").value),
      scope = Number(this.encryptDialog.querySelector("[data-field=scope]").value);
    try {
      await this.call("/api/bitlocker/volumes/control", {
        action: actions.encrypt, identity: this.encryptTarget.identity, argument: String(method | scope),
      });
      this.encryptDialog.close();
      this.notify(t("bitlocker.encryptionStarted"));
      await this.load("volumes", true);
    } catch (error) {
      this.notify(error);
    }
  }

  async decrypt(record) {
    if (!confirm(t("bitlocker.confirmDecrypt", { volume: record.name }))) return;
    await this.runVolume(record, actions.decrypt, null, null, t("bitlocker.decryptionStarted"));
  }

  async suspend(record) {
    if (!confirm(t("bitlocker.confirmSuspend", { volume: record.name }))) return;
    await this.runVolume(record, actions.disable, (record.flags & 3) === 0 ? "1" : null, null,
      t("bitlocker.protectionSuspended"));
  }

  async lock(record) {
    if (!confirm(t("bitlocker.confirmLock", { volume: record.name }))) return;
    await this.runVolume(record, actions.lock, null, null, t("bitlocker.volumeLocked"));
  }

  async unlock(record) {
    const password = prompt(t("bitlocker.enterRecoveryPassword"));
    if (password === null) return;
    await this.runVolume(record, actions.unlock, null, password.trim(), t("bitlocker.volumeUnlocked"));
  }

  async runVolume(record, action, argument, secret, message) {
    try {
      await this.call("/api/bitlocker/volumes/control", { action, identity: record.identity, argument, secret });
      this.notify(message);
      await this.load("volumes", true);
    } catch (error) {
      this.notify(error);
    }
  }

  async openRecovery() {
    if (!this.loaded.has("volumes")) await this.load("volumes");
    const select = this.recoveryDialog.querySelector("[data-field=volume]");
    select.replaceChildren(...(this.records.get("volumes") || []).map((record) =>
      new Option(record.name, record.identity)));
    if (!select.options.length) {
      this.notify(t("bitlocker.empty.volumes"));
      return;
    }
    this.recoveryDialog.querySelector("form").reset();
    this.recoveryDialog.querySelector("[data-field=password]").value = recoveryPassword();
    this.recoveryDialog.showModal();
    this.recoveryDialog.querySelector("[data-field=password]").select();
  }

  async addRecovery(event) {
    event.preventDefault();
    const volume = this.recoveryDialog.querySelector("[data-field=volume]").value,
      name = this.recoveryDialog.querySelector("[data-field=name]").value.trim(),
      password = this.recoveryDialog.querySelector("[data-field=password]").value.trim();
    try {
      await this.call("/api/bitlocker/protectors/control", {
        action: actions.create, identity: volume, argument: name || null, secret: password,
      });
      this.recoveryDialog.close();
      this.notify(t("bitlocker.recoveryAdded"));
      await this.load("protectors", true);
      await this.load("volumes", true);
    } catch (error) {
      this.notify(error);
    }
  }

  async deleteProtector(record) {
    if (!confirm(t("bitlocker.confirmDeleteProtector", { id: record.identity }))) return;
    try {
      await this.call("/api/bitlocker/protectors/control", {
        action: actions.delete, identity: record.detail, argument: record.identity,
      });
      this.notify(t("bitlocker.protectorDeleted"));
      await this.load("protectors", true);
      await this.load("volumes", true);
    } catch (error) {
      this.notify(error);
    }
  }
}
