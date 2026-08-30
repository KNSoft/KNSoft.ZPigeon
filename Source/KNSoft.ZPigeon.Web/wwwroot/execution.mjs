import { CaptureFrameDecoder, configureCaptureEncoding, captureEncodingOptions } from "./capture-frames.mjs";
import { apiUrl } from "./client-context.mjs";
import { t } from "./i18n.mjs";

const CURRENT_SESSION = 4294967295,
  CLIENT_SESSION = 1,
  ACTIVE_SESSION = 2;

export class ExecutionManager {
  constructor(host, { call, notify, filePicker, openTerminal }) {
    this.host = host;
    this.call = call;
    this.notify = notify;
    this.filePicker = filePicker;
    this.openTerminal = openTerminal;
    this.connected = false;
    this.timer = 0;
    this.profilesLoaded = false;
    this.environment = null;
    this.sessions = [];
    this.templates = [];
    this.terminalTouched = false;
    this.currentAccountName = "";
    this.render();
  }
  render() {
    this.host.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <button data-action="run">${t("execution.run")}</button
        ><button data-action="refresh">${t("common.refresh")}</button
        ><span class="spacer"></span><span data-role="status"></span>
      </div>
      <div class="execution-body">
        <section class="execution-jobs">
          <h2>${t("execution.jobs")}</h2>
          <div class="manager-table">
            <table>
              <thead>
                <tr>
                  <th>${t("execution.program")}</th>
                  <th>PID</th>
                  <th>${t("execution.session")}</th>
                  <th>${t("execution.identity")}</th>
                  <th>${t("common.status")}</th>
                  <th>${t("execution.exitCode")}</th>
                  <th>${t("execution.startTime")}</th>
                </tr>
              </thead>
              <tbody></tbody>
            </table>
            <div class="manager-empty" data-role="empty">${t("execution.noJobs")}</div>
          </div>
        </section>
      </div>
      <dialog class="execution-run-dialog" data-role="run-dialog">
        <form data-role="form">
          <h2>${t("execution.run")}</h2>
          <label>${t("execution.mode")}<select data-field="mode"></select></label
          ><label
            >${t("execution.program")}
            <div class="execution-program">
              <input data-field="file" required spellcheck="false" /><button type="button" data-action="pick-program"
                >${t("common.browse")}</button
              ><button type="button" data-action="upload-program">${t("common.upload")}</button
              ><input data-field="upload" type="file" hidden /></div></label
          ><label>${t("execution.arguments")}<input data-field="arguments" spellcheck="false" /></label
          ><label
            >${t("execution.workingDirectory")}
            <div class="execution-directory">
              <input
                data-field="directory"
                spellcheck="false"
                placeholder="${t("execution.workingDirectoryPlaceholder")}"
              /><button type="button" data-action="pick-directory"
                >${t("common.browse")}</button
              >
            </div></label
          ><div class="execution-options">
            <label class="property-choice"
              ><input data-field="terminal" type="checkbox" />${t("execution.inTerminal")}</label
            ><label class="property-choice"
              ><input data-field="hidden" type="checkbox" checked />${t("execution.hidden")}</label
            >
          </div>
          <label
            >${t("execution.identity")}<select data-field="identity">
              <option value="1">${t("common.currentAccountPlain")}</option>
              <option value="2">${t("execution.identity.interactive")}</option>
              <option value="3">${t("execution.identity.administrator")}</option>
              <option value="4">SYSTEM</option>
              <option value="5">TrustedInstaller</option>
              <option value="6">${t("execution.identity.otherUser")}</option>
              <option value="7">AppContainer</option>
              <option value="8" disabled>Custom Token</option>
            </select></label
          ><label data-role="app-container" hidden
            >${t("execution.appContainerProfile")}<input
              data-field="appContainer"
              list="executionProfiles"
              autocomplete="off"
              placeholder="${t("execution.profilePlaceholder")}" /><datalist id="executionProfiles"></datalist></label
          ><label data-role="session"
            >${t("execution.session")}<select data-field="session">
              <option value="${CURRENT_SESSION}">${t("execution.clientSession")}</option>
            </select></label
          ><label data-role="username" hidden
            >${t("common.username")}<input data-field="username" autocomplete="username" /></label
          ><label data-role="password" hidden
            >${t("execution.password")}<input
              data-field="password"
              type="password"
              autocomplete="current-password" /></label
          ><fieldset data-role="custom-token" hidden>
            <legend>Custom Token</legend>
            <div class="execution-token-grid">
              <label>${t("execution.tokenUser")}<input data-field="token-user" spellcheck="false" /></label
              ><label>${t("execution.tokenOwner")}<input data-field="token-owner" spellcheck="false" /></label
              ><label>${t("execution.tokenPrimary")}<input data-field="token-primary" spellcheck="false" /></label
              ><label
                >${t("execution.authenticationId")}<input
                  data-field="token-authentication"
                  spellcheck="false" /></label
              ><label
                >${t("execution.integrity")}<select data-field="token-integrity">
                  <option value="4096">${t("execution.integrity.untrusted")}</option>
                  <option value="8192">${t("execution.integrity.low")}</option>
                  <option value="12288">${t("execution.integrity.medium")}</option>
                  <option value="16384" selected>${t("execution.integrity.system")}</option>
                  <option value="20480">${t("execution.integrity.protected")}</option>
                </select></label
              >
            </div>
            <label
              >${t("execution.tokenGroups")}<textarea
                data-field="token-groups"
                spellcheck="false"></textarea></label
            ><label
              >${t("execution.tokenPrivileges")}<textarea
                data-field="token-privileges"
                spellcheck="false"></textarea></label
            ><div class="execution-options">
              <label class="property-choice"
                ><input data-field="token-ui-access" type="checkbox" />${t("execution.uiAccess")}</label
              ><label class="property-choice"
                ><input data-field="token-logon-sid" type="checkbox" checked />${t("execution.addLogonSid")}</label
              ><button type="button" data-action="super-token">${t("execution.superTokenPreset")}</button>
            </div>
          </fieldset>
          <div class="dialog-actions">
            <button type="button" data-action="cancel">${t("common.cancel")}</button
            ><button type="submit">${t("execution.run")}</button>
          </div>
        </form>
      </dialog>
      <div class="context-menu execution-menu" data-role="menu" hidden>
        <button data-action="terminate" class="danger">${t("execution.terminate")}</button>
      </div>`;
    this.form = this.host.querySelector("[data-role=form]");
    this.body = this.host.querySelector("tbody");
    this.empty = this.host.querySelector("[data-role=empty]");
    this.status = this.host.querySelector("[data-role=status]");
    this.menu = this.host.querySelector("[data-role=menu]");
    this.dialog = this.host.querySelector("[data-role=run-dialog]");
    this.identity = this.field("identity");
    this.session = this.field("session");
    this.profile = this.field("appContainer");
    this.upload = this.field("upload");
    this.mode = this.field("mode");
    this.terminal = this.field("terminal");
    this.hidden = this.field("hidden");
    this.addPathPicker();
    this.host.querySelector("[data-action=run]").onclick = () => this.openRun();
    this.host.querySelector("[data-action=refresh]").onclick = () => this.refresh();
    this.host.querySelector("[data-action=pick-program]").onclick = () => this.pickProgram();
    this.host.querySelector("[data-action=upload-program]").onclick = () => this.upload.click();
    this.form.onsubmit = (e) => {
      e.preventDefault();
      this.start();
    };
    this.identity.onchange = () => this.sync();
    this.mode.onchange = () => this.selectTemplate();
    for (const name of ["file", "arguments", "directory"])
      this.field(name).addEventListener("input", () => this.markCustom());
    this.field("file").addEventListener("change", () => this.inspectProgram());
    this.terminal.onchange = () => {
      this.terminalTouched = true;
      this.syncTerminal();
    };
    this.upload.onchange = () => {
      if (this.upload.files[0]) {
        this.field("file").value = this.upload.files[0].name;
        this.markCustom();
      }
    };
    this.host.querySelector("[data-action=cancel]").onclick = () => this.dialog.close();
    this.host.querySelector("[data-action=super-token]").onclick = () => this.setSuperToken();
    this.menu.querySelector("button").onclick = () => {
      const job = this.context;
      this.hideMenu();
      if (job) this.terminate(job);
    };
    document.addEventListener("pointerdown", (e) => {
      if (!this.menu.hidden && !this.menu.contains(e.target)) this.hideMenu();
    });
  }
  field(name) {
    return this.host.querySelector(`[data-field=${name}]`);
  }
  addPathPicker() {
    const button = this.host.querySelector("[data-action=pick-directory]");
    if (!this.filePicker) {
      button.disabled = true;
      return;
    }
    button.onclick = async () => {
      const input = this.field("directory"),
        value = await this.filePicker.open({ mode: "folder", initialPath: input.value.trim() });
      if (value) {
        input.value = value;
        this.markCustom();
      }
    };
  }
  async pickProgram() {
    if (!this.filePicker) return;
    const input = this.field("file"),
      value = await this.filePicker.open({ mode: "file", initialPath: pickerDirectory(input.value) });
    if (value) {
      input.value = value;
      this.upload.value = "";
      this.markCustom();
      await this.inspectProgram();
    }
  }
  async openRun(spec = null) {
    if (!this.connected) return;
    try {
      await Promise.all([this.loadEnvironment(), this.loadSessions()]);
    } catch (error) {
      this.notify(error);
      return;
    }
    this.upload.value = "";
    this.identity.value = "1";
    this.session.value = String(CURRENT_SESSION);
    this.hidden.checked = true;
    this.terminal.checked = false;
    this.terminalTouched = false;
    this.buildTemplates(spec);
    this.selectTemplate();
    this.sync();
    this.dialog.showModal();
    this.field("file").focus();
  }
  activate(connected) {
    this.connected = connected;
    if (!connected) {
      this.disconnect();
      return;
    }
    this.host.querySelector("[data-action=run]").disabled = false;
    this.refresh();
    clearInterval(this.timer);
    this.timer = setInterval(() => this.load(false), 2000);
  }
  deactivate() {
    clearInterval(this.timer);
    this.timer = 0;
  }
  disconnect() {
    this.connected = false;
    this.profilesLoaded = false;
    this.environment = null;
    this.environmentPromise = null;
    this.sessionsPromise = null;
    this.profiles = [];
    this.currentAccountName = "";
    this.deactivate();
    this.body.replaceChildren();
    this.profile.value = "";
    this.profile.placeholder = t("execution.profilePlaceholder");
    this.host.querySelector("#executionProfiles").replaceChildren();
    this.identity.options[0].textContent = t("common.currentAccountPlain");
    this.identity.options[7].disabled = true;
    this.empty.hidden = false;
    this.empty.textContent = t("common.clientDisconnected");
    this.status.textContent = "";
    this.host.querySelector("[data-action=run]").disabled = true;
    if (this.dialog.open) this.dialog.close();
  }
  async loadSessions() {
    if (!this.connected) return;
    if (this.sessionsPromise) return this.sessionsPromise;
    this.sessionsPromise = this.querySessions();
    try {
      return await this.sessionsPromise;
    } finally {
      this.sessionsPromise = null;
    }
  }
  async querySessions() {
    this.sessions = await this.call("/api/execution/sessions");
    const previous = this.session.value,
      active = this.sessions.find((s) => s.flags & ACTIVE_SESSION),
      client = this.sessions.find((s) => s.flags & CLIENT_SESSION),
      current = this.identity.options[0];
    this.currentAccountName = client?.userName || "";
    current.textContent = this.currentAccountName
      ? t("common.currentAccount", { username: this.currentAccountName })
      : t("common.currentAccountPlain");
    this.session.replaceChildren(
      new Option(t("execution.clientSession"), CURRENT_SESSION),
      ...this.sessions.map(
        (s) =>
          new Option(
            `${s.sessionId} · ${s.userName || s.stationName || t("common.notLoggedIn")}` +
              `${s.flags & CLIENT_SESSION ? t("common.clientSuffix") : ""}`,
            s.sessionId,
          ),
      ),
    );
    this.session.value = [...this.session.options].some((o) => o.value === previous)
      ? previous
      : String(active?.sessionId ?? CURRENT_SESSION);
    this.sync();
  }
  async loadEnvironment(force = false) {
    if (!this.connected) return null;
    if (this.environment && !force) return this.environment;
    if (this.environmentPromise) return this.environmentPromise;
    this.environmentPromise = this.call("/api/execution/environment").then((value) => {
      this.environment = value;
      this.identity.options[7].disabled = !(value.flags & 1);
      return value;
    });
    try {
      return await this.environmentPromise;
    } finally {
      this.environmentPromise = null;
    }
  }
  async loadProfiles() {
    if (this.profilesLoaded || !this.connected) return;
    this.profile.disabled = true;
    this.profile.value = "";
    this.profile.placeholder = t("common.fetching");
    try {
      const records = await this.call("/api/app-containers");
      if (!this.connected) return;
      this.profiles = records
        .filter((record) => record.kind === 48)
        .map((record) => ({
          sid: record.detail.split("\n", 1)[0],
          identity: record.identity,
          name: record.name || record.identity,
        }))
        .filter((profile) => profile.sid)
        .sort((a, b) => a.name.localeCompare(b.name));
      this.host
        .querySelector("#executionProfiles")
        .replaceChildren(
          ...this.profiles.map(
            (profile) =>
              new Option(
                profile.name === profile.identity ? profile.name : `${profile.name} — ${profile.identity}`,
                profile.sid,
              ),
          ),
        );
      this.profilesLoaded = true;
      this.profile.placeholder = this.profiles.length ? t("execution.profilePlaceholder") : t("execution.noProfiles");
    } catch (error) {
      if (this.connected) {
        this.profile.placeholder = error.message;
        this.notify(error);
      }
    } finally {
      this.profile.disabled = !this.connected;
    }
  }
  sync() {
    const identity = Number(this.identity.value),
      other = identity === 6,
      appContainer = identity === 7,
      customToken = identity === 8;
    this.host.querySelector("[data-role=session]").hidden = appContainer;
    this.host.querySelector("[data-role=app-container]").hidden = !appContainer;
    this.host.querySelector("[data-role=username]").hidden = this.host.querySelector("[data-role=password]").hidden =
      !other;
    this.host.querySelector("[data-role=custom-token]").hidden = !customToken;
    this.session.disabled = identity === 1 || appContainer;
    if (appContainer) {
      this.session.value = String(CURRENT_SESSION);
      this.loadProfiles();
    }
    if (customToken && !this.field("token-user").value) this.setSuperToken();
    this.syncTerminal();
  }
  async refresh(report = true) {
    if (!this.connected) return;
    try {
      await Promise.all([this.loadEnvironment(true), this.loadSessions()]);
      await this.load(report);
    } catch (e) {
      if (report) this.notify(e);
    }
  }
  async load(report = true) {
    if (!this.connected) return;
    try {
      const jobs = await this.call("/api/execution/jobs");
      this.body.replaceChildren();
      for (const job of jobs) {
        const row = document.createElement("tr");
        const values = [
          job.fileName,
          job.processId ?? "",
          job.sessionId === CURRENT_SESSION ? t("common.current") : job.sessionId,
          identityName(job.identity, this.currentAccountName),
          job.state === 1 ? t("common.running") : t("execution.exited"),
          job.exitCode ?? "",
          new Date(job.createTime).toLocaleString(),
        ];
        for (const value of values) {
          const cell = row.insertCell();
          cell.textContent = value;
        }
        row.oncontextmenu = (e) => this.openMenu(e, job);
        this.body.append(row);
      }
      this.empty.hidden = jobs.length !== 0;
      this.empty.textContent = t("execution.noJobs");
      this.status.textContent = t("execution.runningCount", { count: jobs.filter((j) => j.state === 1).length });
    } catch (e) {
      if (report) this.notify(e);
    }
  }
  async start() {
    const submit = this.form.querySelector("[type=submit]"),
      file = this.upload.files[0],
      identity = Number(this.identity.value),
      appContainer = identity === 7,
      profile = appContainer ? this.resolveProfile() : null,
      inTerminal = this.terminal.checked;
    if (appContainer && !profile) {
      this.notify(t("execution.invalidProfile"));
      return;
    }
    submit.disabled = true;
    submit.textContent = file ? t("execution.uploading") : t("execution.starting");
    let staging = null;
    try {
      let fileName = this.field("file").value.trim(),
        argumentsValue = this.field("arguments").value.trim(),
        cleanupPath = null,
        flags = this.hidden.checked ? 1 : 0;
      if (file) {
        const result = await this.call("/api/execution/staging", { name: file.name });
        staging = result.path;
        const response = await fetch(apiUrl(`/api/file/upload?path=${encodeURIComponent(staging)}&overwrite=false`), {
          method: "PUT",
          body: file,
        });
        if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
        fileName = staging;
        cleanupPath = staging;
        flags |= 2;
      }
      const start = {
        engine: 1,
        identity,
        sessionId: appContainer ? CURRENT_SESSION : Number(this.session.value),
        flags,
        fileName,
        arguments: argumentsValue || null,
        workingDirectory: this.field("directory").value.trim() || null,
        verb: null,
        userName: identity === 6 ? this.field("username").value.trim() || null : null,
        password: identity === 6 ? this.field("password").value : null,
        appContainerSid: profile?.sid ?? null,
        customToken: identity === 8 ? this.customToken() : null,
        cleanupPath,
      };
      if (inTerminal) {
        const info = await this.call("/api/terminal/run", {
          start,
          columns: 120,
          rows: 30,
        });
        this.dialog.close();
        this.openTerminal?.(info);
        staging = null;
        this.field("password").value = "";
        return;
      }
      const job = await this.call("/api/execution/start", start);
      staging = null;
      this.field("password").value = "";
      this.dialog.close();
      this.notify(job.processId ? t("execution.started", { pid: job.processId }) : t("execution.submitted"));
      this.watch(job.jobId);
      await this.load();
    } catch (e) {
      this.notify(e);
    } finally {
      if (staging)
        try {
          await this.call("/api/file/delete", { path: staging });
        } catch {}
      submit.disabled = false;
      submit.textContent = t("execution.run");
    }
  }
  buildTemplates(spec) {
    const path = spec?.path || "",
      suffix = spec?.suffix?.toLocaleLowerCase() || "",
      directory = spec?.workingDirectory || "",
      runtime = (kind) => this.environment?.runtimes.find((value) => value.kind === kind),
      addRuntime = (values, kind, label, argumentsValue, always = false) => {
        const value = runtime(kind);
        if (value || always)
          values.push({
            id: `${kind}`,
            label: value ? runtimeLabel(kind, label, value.image.version) : `${label} (${t("execution.notInstalled")})`,
            file: value?.path || "",
            arguments: argumentsValue,
            directory,
            image: value?.image,
            disabled: !value,
          });
      },
      quoted = quoteArgument(path),
      values = [];
    if (suffix === ".exe" || suffix === ".com")
      values.push({
        id: "direct",
        label: t("execution.direct"),
        file: path,
        arguments: "",
        directory,
        terminal: suffix === ".com" ? true : undefined,
      });
    else if (suffix === ".cmd" || suffix === ".bat")
      addRuntime(values, 1, t("execution.runtime.cmd"), `/D /Q /C call ${quoted}`);
    else if (suffix === ".ps1") {
      addRuntime(values, 3, t("execution.runtime.pwsh"), `-NoLogo -NoProfile -ExecutionPolicy Bypass -File ${quoted}`);
      addRuntime(
        values,
        2,
        t("execution.runtime.powershell"),
        `-NoLogo -NoProfile -ExecutionPolicy Bypass -File ${quoted}`,
      );
    } else if (suffix === ".js") {
      addRuntime(values, 7, "Node.js", quoted, true);
      addRuntime(values, 4, "CScript / JScript", `//NoLogo //B //E:JScript ${quoted}`);
      addRuntime(values, 5, "WScript / JScript", `//NoLogo //E:JScript ${quoted}`);
    } else if (suffix === ".mjs" || suffix === ".cjs") addRuntime(values, 7, "Node.js", quoted, true);
    else if (suffix === ".vbs") {
      addRuntime(values, 4, "CScript / VBScript", `//NoLogo //B //E:VBScript ${quoted}`);
      addRuntime(values, 5, "WScript / VBScript", `//NoLogo //E:VBScript ${quoted}`);
    } else if (suffix === ".wsf") {
      addRuntime(values, 4, "CScript", `//NoLogo //B ${quoted}`);
      addRuntime(values, 5, "WScript", `//NoLogo ${quoted}`);
    } else if (suffix === ".hta") addRuntime(values, 6, "MSHTA", quoted);
    else if (suffix === ".py") {
      addRuntime(values, 8, "Python", quoted);
      addRuntime(values, 9, "Python GUI", quoted);
    } else if (suffix === ".pyw") {
      addRuntime(values, 9, "Python GUI", quoted);
      addRuntime(values, 8, "Python", quoted);
    } else if (suffix === ".go") addRuntime(values, 10, "Go", `run ${quoted}`);
    else
      for (const value of this.environment?.runtimes || [])
        values.push({
          id: `${value.kind}`,
          label: runtimeLabel(value.kind, runtimeName(value.kind), value.image.version),
          file: value.path,
          arguments: value.kind === 2 || value.kind === 3 ? "-NoLogo -NoProfile -ExecutionPolicy Bypass" : "",
          directory: "",
          image: value.image,
        });
    values.push({ id: "custom", label: t("execution.custom"), file: path, arguments: "", directory });
    this.templates = values;
    this.mode.replaceChildren(...values.map((value) => new Option(value.label, value.id, false, false)));
    values.forEach((value, index) => (this.mode.options[index].disabled = value.disabled));
    this.mode.value = values.find((value) => !value.disabled)?.id || "custom";
  }
  selectTemplate() {
    const template = this.templates.find((value) => value.id === this.mode.value);
    if (!template || template.disabled) return;
    if (template.id !== "custom") {
      this.field("file").value = template.file;
      this.field("arguments").value = template.arguments;
      this.field("directory").value = template.directory;
    }
    this.terminalTouched = false;
    if (template.terminal !== undefined) this.terminal.checked = template.terminal;
    else if (template.image?.subsystem) this.terminal.checked = template.image.subsystem === 3;
    else this.inspectProgram();
    this.syncTerminal();
  }
  markCustom() {
    if (this.mode.value !== "custom") this.mode.value = "custom";
  }
  async inspectProgram() {
    const path = this.field("file").value.trim();
    if (this.terminalTouched || !path) return;
    try {
      const image = await this.call("/api/execution/image", { path });
      if (!this.terminalTouched && this.field("file").value.trim() === path && image.subsystem)
        this.terminal.checked = image.subsystem === 3;
      this.syncTerminal();
    } catch {}
  }
  syncTerminal() {
    if (this.terminal.checked) this.hidden.checked = true;
    this.hidden.disabled = this.terminal.checked;
  }
  setSuperToken() {
    const owner = "S-1-5-32-546";
    this.field("token-user").value = owner;
    this.field("token-owner").value = owner;
    this.field("token-primary").value = owner;
    this.field("token-authentication").value = "999";
    this.field("token-integrity").value = "16384";
    this.field("token-ui-access").checked = true;
    this.field("token-logon-sid").checked = true;
    this.field("token-groups").value = [
      "S-1-5-32-544,7",
      "S-1-5-18,7",
      "S-1-5-11,7",
      "S-1-1-0,7",
      "S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464,7",
    ].join("\n");
    this.field("token-privileges").value = Array.from({ length: 35 }, (_, index) => `${index + 2},3`).join("\n");
  }
  customToken() {
    return {
      authenticationId: integer(this.field("token-authentication").value),
      integrityRid: integer(this.field("token-integrity").value),
      userSid: this.field("token-user").value.trim(),
      ownerSid: this.field("token-owner").value.trim(),
      primaryGroupSid: this.field("token-primary").value.trim(),
      uiAccess: this.field("token-ui-access").checked,
      addLogonSid: this.field("token-logon-sid").checked,
      groups: list(this.field("token-groups").value, (sid, attributes) => ({ sid, attributes })),
      privileges: list(this.field("token-privileges").value, (luid, attributes) => ({
        luid: integer(luid),
        attributes,
      })),
    };
  }
  async watch(jobId) {
    for (;;) {
      await new Promise((resolve) => setTimeout(resolve, 1000));
      if (!this.connected) return;
      try {
        const jobs = await this.call("/api/execution/jobs"),
          job = jobs.find((value) => value.jobId === jobId);
        if (job?.state === 2) {
          this.notify(t("execution.completed", { code: job.exitCode }));
          if (this.host.offsetParent) this.load(false);
          return;
        }
      } catch {
        return;
      }
    }
  }
  resolveProfile() {
    const value = this.profile.value.trim().toLocaleLowerCase();
    return this.profiles?.find(
      (profile) =>
        profile.sid.toLocaleLowerCase() === value ||
        profile.identity.toLocaleLowerCase() === value ||
        profile.name.toLocaleLowerCase() === value ||
        `${profile.name} — ${profile.identity}`.toLocaleLowerCase() === value,
    );
  }
  openMenu(event, job) {
    event.preventDefault();
    this.context = job;
    this.menu.hidden = false;
    this.menu.querySelector("button").disabled = job.state !== 1;
    this.menu.style.left = `${event.clientX}px`;
    this.menu.style.top = `${event.clientY}px`;
  }
  hideMenu() {
    this.menu.hidden = true;
    this.context = null;
  }
  async terminate(job) {
    try {
      await this.call("/api/execution/terminate", { jobId: job.jobId });
      await this.load();
    } catch (e) {
      this.notify(e);
    }
  }
}

const identityName = (value, userName) =>
  [
    "",
    userName ? t("common.currentAccount", { username: userName }) : t("common.currentAccountPlain"),
    t("execution.identity.interactive"),
    t("execution.identity.administrator"),
    "SYSTEM",
    "TrustedInstaller",
    t("execution.identity.otherUser"),
    "AppContainer",
    "Custom Token",
  ][value] ?? value;
const runtimeName = (kind) =>
  ({
    1: t("execution.runtime.cmd"),
    2: t("execution.runtime.powershell"),
    3: t("execution.runtime.pwsh"),
    4: "CScript",
    5: "WScript",
    6: "MSHTA",
    7: "Node.js",
    8: "Python",
    9: "Python GUI",
    10: "Go",
  })[kind] || kind;
const version = (value) => {
  const parts = String(value || "0.0").split(".");
  while (parts.length > 2 && parts.at(-1) === "0") parts.pop();
  return parts.every((part) => part === "0") ? t("execution.versionUnknown") : parts.join(".");
};
const runtimeLabel = (kind, label, value) =>
  [1, 4, 5, 6].includes(kind) ? label : `${label} ${version(value)}`;
const quoteArgument = (value) => `"${value.replace(/(\\*)"/g, "$1$1\\\"").replace(/(\\+)$/, "$1$1")}"`;
const integer = (value) => {
  const result = Number(value.trim());
  if (!Number.isSafeInteger(result) || result < 0) throw new Error(t("execution.invalidToken"));
  return result;
};
const list = (value, create) =>
  value
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line) => {
      const separator = line.lastIndexOf(",");
      if (separator < 1) throw new Error(t("execution.invalidToken"));
      return create(line.slice(0, separator).trim(), integer(line.slice(separator + 1)));
    });
const pickerDirectory = (value) => {
  value = value.trim().replace(/\//g, "\\");
  const index = value.lastIndexOf("\\");
  return index < 0 ? "" : value.slice(0, index) || value;
};

export class RemoteDesktopManager {
  constructor(host, { call, notify, rtc }) {
    this.host = host;
    this.call = call;
    this.notify = notify;
    this.rtc = rtc;
    this.connected = false;
    this.pressedKeys = new Map();
    this.pressedButtons = 0;
    host.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <strong>远程桌面</strong><span class="spacer"></span><button data-action="refresh">刷新配置</button>
      </div>
      <div class="tools remote-desktop">
        <section class="card">
          <h2>被控端配置</h2>
          <label class="property-choice"><input type="checkbox" data-field="enabled" />允许远程桌面连接</label
          ><label>监听端口<input type="number" data-field="port" min="1" max="65535" required /></label>
          <p class="property-note">监听端口修改后需要重新启动远程桌面服务或系统。</p>
          <button data-action="save">保存配置</button>
        </section>
        <section class="card">
          <h2>RDP（TCP/UDP）</h2>
          <p class="muted">创建临时入口，目标系统仍使用 Windows NLA 登录。</p>
          <button data-action="create">创建 RDP 入口</button>
          <div data-role="lease" hidden>
            <p><code data-role="address"></code></p>
            <p data-role="state" class="muted"></p>
            <div class="dialog-actions">
              <button data-action="copy">复制地址</button><button data-action="download">下载 .rdp</button>
            </div>
          </div>
        </section>
        <section class="card remote-control-card">
          <header>
            <h2>Web 交互式远控</h2>
            <span class="spacer"></span
            ><label><input data-field="captureCursor" type="checkbox" checked />远端鼠标</label
            ><label
              >编码<select data-field="captureEncoding">
                <option value="auto">自动</option>
                <option value="image">PNG / JPEG</option>
                <option value="h264">H.264</option>
                <option value="h265">H.265</option>
              </select></label
            ><label
              >帧率<select data-field="frameRate">
                <option>6</option>
                <option selected>12</option>
                <option>24</option>
                <option>30</option>
              </select></label
            ><label
              >尺寸<select data-field="maxDimension">
                <option>960</option>
                <option selected>1280</option>
                <option>1920</option>
                <option value="7680">原始</option>
              </select></label
            ><label
              >图像质量<input data-field="imageQuality" type="range" min="1" max="100" value="85" /><output
                >85</output
              ></label
            ><button data-action="desktop-refresh">截屏</button><button data-action="desktop-toggle">▶</button>
          </header>
          <div class="remote-control-view" tabindex="0">
            <span data-role="desktop-status">点击“开始远控”后显示远程桌面</span
            ><img data-role="desktop-image" alt="远程桌面" hidden /><canvas data-role="desktop-canvas" hidden></canvas>
            <button class="remote-control-start" data-action="desktop-start">开始远控</button>
          </div>
          <footer>
            <button data-action="clipboard-send">发送本地剪贴板</button
            ><button data-action="clipboard-copy">复制远端剪贴板</button><button data-action="fullscreen">全屏</button
            ><span class="muted">点击画面后可使用鼠标和键盘；浏览器保留的系统快捷键无法下发。</span>
          </footer>
        </section>
      </div>`;
    this.create = host.querySelector("[data-action=create]");
    this.enabled = host.querySelector("[data-field=enabled]");
    this.port = host.querySelector("[data-field=port]");
    this.save = host.querySelector("[data-action=save]");
    this.lease = host.querySelector("[data-role=lease]");
    this.view = host.querySelector(".remote-control-view");
    this.canvas = host.querySelector("[data-role=desktop-canvas]");
    this.image = host.querySelector("[data-role=desktop-image]");
    this.desktopStatus = host.querySelector("[data-role=desktop-status]");
    this.toggle = host.querySelector("[data-action=desktop-toggle]");
    this.startButton = host.querySelector("[data-action=desktop-start]");
    this.decoder = new CaptureFrameDecoder(
      this.canvas,
      this.desktopStatus,
      (socket) => this.socket === socket,
      (sequence, keyframe, socket) => this.acknowledgeFrame(sequence, keyframe, socket),
      (codecs, width, height, socket) => this.reportVideoCodecs(codecs, width, height, socket),
    );
    this.create.onclick = () => this.open();
    this.save.onclick = () => this.saveConfiguration();
    host.querySelector("[data-action=refresh]").onclick = () => this.loadConfiguration();
    host.querySelector("[data-action=copy]").onclick = () => navigator.clipboard.writeText(this.address);
    host.querySelector("[data-action=download]").onclick = () => this.download();
    host.querySelector("[data-action=desktop-refresh]").onclick = () => this.capture();
    this.toggle.onclick = () => (this.socket ? this.stopStream() : this.startStream());
    this.startButton.onclick = () => this.startStream();
    host.querySelector("[data-action=clipboard-send]").onclick = () => this.sendLocalClipboard();
    host.querySelector("[data-action=clipboard-copy]").onclick = () => this.copyRemoteClipboard();
    host.querySelector("[data-action=fullscreen]").onclick = () => this.view.requestFullscreen();
    const quality = host.querySelector("[data-field=imageQuality]"),
      output = quality.nextElementSibling;
    quality.oninput = () => (output.value = quality.value);
    quality.onchange = () => this.restartStream();
    this.encoding = host.querySelector("[data-field=captureEncoding]");
    configureCaptureEncoding(this.encoding);
    for (const field of ["captureCursor", "captureEncoding", "frameRate", "maxDimension"])
      host.querySelector(`[data-field=${field}]`).onchange = () => this.restartStream();
    for (const surface of [this.image, this.canvas]) {
      surface.onpointerdown = (event) => this.pointerDown(event);
      surface.onpointerup = (event) => this.pointerUp(event);
      surface.onpointercancel = () => this.releaseInput();
      surface.onpointermove = (event) => this.queuePointer(event);
      surface.oncontextmenu = (event) => event.preventDefault();
      surface.onwheel = (event) => this.queueWheel(event);
    }
    this.view.onkeydown = (event) => this.key(event, false);
    this.view.onkeyup = (event) => this.key(event, true);
    this.view.onpaste = (event) => this.paste(event);
    window.addEventListener("blur", () => this.releaseInput());
    document.addEventListener("visibilitychange", () => {
      if (document.hidden) this.releaseInput();
    });
  }
  activate(connected) {
    const reconnected = connected && !this.connected;
    this.connected = connected;
    this.create.disabled = this.save.disabled = this.toggle.disabled = this.startButton.disabled = !connected;
    if (connected) {
      if (reconnected) {
        this.image.hidden = this.canvas.hidden = true;
        this.startButton.hidden = false;
        this.desktopStatus.hidden = false;
        this.desktopStatus.textContent = "点击“开始远控”后显示远程桌面";
      }
      this.ensureMonitorPicker();
      this.loadConfiguration();
      this.loadMonitors();
    }
  }
  disconnect() {
    this.connected = false;
    clearTimeout(this.timer);
    this.stopStream(true);
    this.create.disabled = this.save.disabled = this.toggle.disabled = this.startButton.disabled = true;
    this.lease.hidden = true;
    this.image.hidden = this.canvas.hidden = true;
    this.startButton.hidden = false;
    this.desktopStatus.hidden = false;
    this.desktopStatus.textContent = "Client 未连接";
  }
  async open() {
    const port = Number(this.port.value);
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
      this.notify("远程桌面端口无效");
      return;
    }
    this.create.disabled = true;
    this.create.textContent = "正在创建…";
    try {
      const lease = await this.call("/api/remote/rdp", { port }),
        host = location.hostname.includes(":") ? `[${location.hostname}]` : location.hostname;
      this.address = `${host}:${lease.port}`;
      this.id = lease.id;
      this.lease.hidden = false;
      this.host.querySelector("[data-role=address]").textContent = this.address;
      this.render(lease);
      this.poll();
    } catch (error) {
      this.notify(error);
    } finally {
      this.create.disabled = !this.connected;
      this.create.textContent = "创建 RDP 入口";
    }
  }
  async loadConfiguration() {
    if (!this.connected) return;
    try {
      const records = await this.call("/api/system-details"),
        enabled = records.find((record) => record.identity === "remoteDesktopEnabled"),
        port = records.find((record) => record.identity === "remoteDesktopPort");
      this.enabled.checked = Number(enabled?.value) === 1;
      this.port.value = port?.value || 3389;
      this.configuration = { enabled: this.enabled.checked, port: Number(this.port.value) };
    } catch (error) {
      this.notify(error);
    }
  }
  async saveConfiguration() {
    const port = Number(this.port.value);
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
      this.notify("远程桌面端口无效");
      return;
    }
    this.save.disabled = true;
    try {
      if (this.enabled.checked !== this.configuration?.enabled)
        await this.call("/api/system-details/control", {
          action: 23,
          identity: "remoteDesktopEnabled",
          argument: this.enabled.checked ? "1" : "0",
        });
      if (port !== this.configuration?.port)
        await this.call("/api/system-details/control", {
          action: 23,
          identity: "remoteDesktopPort",
          argument: String(port),
        });
      this.notify("远程桌面配置已保存");
      await this.loadConfiguration();
    } catch (error) {
      this.notify(error);
      await this.loadConfiguration();
    } finally {
      this.save.disabled = !this.connected;
    }
  }
  render(lease) {
    const status = lease.status
        ? ` · ${statusType(lease.status.type)}: 0x${lease.status.code.toString(16).toUpperCase().padStart(8, "0")}`
        : "",
      idle = lease.idleExpires
        ? ` · 无连接时于 ${new Date(lease.idleExpires).toLocaleTimeString()} 自动关闭`
        : " · 活动连接中";
    this.host.querySelector("[data-role=state]").textContent = `${lease.state}${status}${idle}`;
  }
  async poll() {
    clearTimeout(this.timer);
    try {
      const lease = await this.call(`/api/remote/forward/${this.id}`);
      this.render(lease);
      if (["Waiting", "Connected"].includes(lease.state)) this.timer = setTimeout(() => this.poll(), 1000);
    } catch (error) {
      this.notify(error);
    }
  }
  download() {
    const content =
        `full address:s:${this.address}\r\n` +
        "prompt for credentials:i:1\r\n" +
        "authentication level:i:2\r\n" +
        "enablecredsspsupport:i:1\r\n",
      url = URL.createObjectURL(new Blob([content], { type: "application/x-rdp" })),
      link = document.createElement("a");
    link.href = url;
    link.download = "ZPigeon.rdp";
    link.click();
    URL.revokeObjectURL(url);
  }
  ensureMonitorPicker() {
    if (this.monitor) return;
    this.monitor = document.createElement("select");
    this.monitor.dataset.field = "monitor";
    this.monitor.append(new Option("主显示器", "4294967295"));
    const label = document.createElement("label");
    label.append("显示器", this.monitor);
    this.host.querySelector("[data-field=frameRate]").parentElement.before(label);
    this.monitor.onchange = () => this.capture();
  }
  async loadMonitors(captureAfter = false) {
    if (!this.connected) return;
    try {
      const selected = this.monitor.value,
        monitors = await this.call("/api/remote/desktop/monitors");
      this.monitor.replaceChildren(
        ...monitors.map(
          (m) =>
            new Option(
              `${m.primary ? t("common.primaryDisplay") : t("common.displayNumber", { value: m.index + 1 })}` +
                ` · ${m.device} · ` +
                `${m.right - m.left} × ${m.bottom - m.top}`,
              m.primary ? 4294967295 : m.index,
            ),
        ),
      );
      this.monitor.value = [...this.monitor.options].some((option) => option.value === selected)
        ? selected
        : "4294967295";
    } catch (error) {
      this.notify(error);
    } finally {
      if (captureAfter) await this.capture();
    }
  }
  options() {
    return {
      captureCursor: this.host.querySelector("[data-field=captureCursor]").checked,
      maxDimension: Number(this.host.querySelector("[data-field=maxDimension]").value),
      frameRate: Number(this.host.querySelector("[data-field=frameRate]").value),
      imageQuality: Number(this.host.querySelector("[data-field=imageQuality]").value),
      monitorIndex: Number(this.monitor?.value ?? 4294967295),
      ...captureEncodingOptions(this.encoding),
    };
  }
  async capture() {
    if (!this.connected) return;
    this.stopStream(true);
    this.startButton.hidden = true;
    this.desktopStatus.hidden = false;
    this.desktopStatus.textContent = "正在获取桌面图像…";
    this.image.hidden = this.canvas.hidden = true;
    try {
      const response = await fetch(apiUrl("/api/remote/desktop/image"), {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify(this.options()),
      });
      if (!response.ok) throw await responseError(response);
      if (this.imageUrl) URL.revokeObjectURL(this.imageUrl);
      this.imageUrl = URL.createObjectURL(await response.blob());
      this.image.onload = () => {
        this.desktopStatus.textContent = `${this.image.naturalWidth} × ${this.image.naturalHeight}`;
      };
      this.image.src = this.imageUrl;
      this.image.hidden = false;
    } catch (error) {
      this.startButton.hidden = false;
      this.desktopStatus.textContent = error.message;
      this.notify(error);
    }
  }
  async startStream() {
    if (!this.connected || this.socket || this.starting) return;
    this.starting = true;
    this.startButton.hidden = true;
    this.toggle.disabled = true;
    let socket, direct;
    try {
      try {
        direct = await this.rtc.open(
          (data) => this.decoder.receive(data, socket),
          (text) => (this.desktopStatus.textContent = text),
        );
      } catch (error) {
        this.desktopStatus.textContent = "P2P 加速失败，继续使用 Server 中转";
        console.warn(error);
      }
      const query = new URLSearchParams({
          handle: 0,
          processId: 0,
          threadId: 0,
          desktop: true,
          ...this.options(),
          directStreamId: direct?.id || 0,
        }),
        url = new URL(`/api/remote/desktop?${query}`, location.href);
      url.protocol = location.protocol === "https:" ? "wss:" : "ws:";
      socket = this.socket = new WebSocket(apiUrl(url));
      this.direct = direct;
      this.decoder.reset();
      socket.binaryType = "arraybuffer";
      socket.onopen = () => {
        this.toggle.textContent = "■";
        this.desktopStatus.textContent = direct ? "P2P 实时画面已连接" : "Server 中转实时画面已连接";
      };
      if (!direct) socket.onmessage = (event) => this.decoder.receive(new Uint8Array(event.data), socket);
      socket.onclose = (event) => {
        direct?.close();
        if (this.socket !== socket) return;
        this.clearInput();
        this.socket = this.direct = null;
        this.toggle.textContent = "▶";
        this.startButton.hidden = false;
        if (event.code !== 1000) {
          const message = event.reason || `WebSocket: ${event.code}`;
          this.desktopStatus.textContent = message;
          this.notify(message);
        }
      };
      socket.onerror = () => {
        if (this.socket === socket) this.desktopStatus.textContent = "远控连接失败";
      };
    } finally {
      this.starting = false;
      this.toggle.disabled = !this.connected;
      if (!this.socket) this.startButton.hidden = false;
    }
  }
  restartStream() {
    if (!this.socket) return;
    this.stopStream(true);
    this.startStream();
  }
  stopStream(silent = false) {
    const socket = this.socket,
      direct = this.direct;
    this.releaseInput(socket);
    this.socket = this.direct = null;
    if (socket && socket.readyState < 2) socket.close(1000);
    direct?.close();
    this.decoder.reset();
    this.toggle.textContent = "▶";
    this.startButton.hidden = false;
    if (!silent && this.canvas.width) this.desktopStatus.textContent = `${this.canvas.width} × ${this.canvas.height}`;
  }
  send(data, socket = this.socket) {
    if (socket?.readyState === WebSocket.OPEN) socket.send(data);
  }
  acknowledgeFrame(sequence, keyframe, socket) {
    const data = new ArrayBuffer(6),
      view = new DataView(data);
    view.setUint8(0, 4);
    view.setUint8(1, keyframe ? 1 : 0);
    view.setUint32(2, sequence, true);
    this.send(data, socket);
  }
  reportVideoCodecs(codecs, width, height, socket) {
    const data = new ArrayBuffer(10),
      view = new DataView(data);
    view.setUint8(0, 6);
    view.setUint8(1, codecs);
    view.setUint32(2, width, true);
    view.setUint32(6, height, true);
    this.send(data, socket);
  }
  pointerDown(event) {
    event.preventDefault();
    this.view.focus({ preventScroll: true });
    event.currentTarget.setPointerCapture(event.pointerId);
    this.pressedButtons |= mouseButton(event.button);
    this.sendPointer(event.clientX, event.clientY, buttonFlag(event.button, true));
  }
  pointerUp(event) {
    event.preventDefault();
    this.pressedButtons &= ~mouseButton(event.button);
    this.sendPointer(event.clientX, event.clientY, buttonFlag(event.button, false));
    if (!this.pressedButtons && event.currentTarget.hasPointerCapture(event.pointerId))
      event.currentTarget.releasePointerCapture(event.pointerId);
  }
  queuePointer(event) {
    this.pointerPosition = { x: event.clientX, y: event.clientY };
    if (this.pointerFrame) return;
    this.pointerFrame = requestAnimationFrame(() => {
      this.pointerFrame = 0;
      const point = this.pointerPosition;
      this.sendPointer(point.x, point.y, 1);
    });
  }
  queueWheel(event) {
    event.preventDefault();
    this.pointerPosition = { x: event.clientX, y: event.clientY };
    if (event.shiftKey) this.horizontalWheel = (this.horizontalWheel || 0) - event.deltaY;
    else this.verticalWheel = (this.verticalWheel || 0) - event.deltaY;
    if (this.wheelFrame) return;
    this.wheelFrame = requestAnimationFrame(() => {
      this.wheelFrame = 0;
      const point = this.pointerPosition;
      if (this.verticalWheel) this.sendPointer(point.x, point.y, 128, this.verticalWheel);
      if (this.horizontalWheel) this.sendPointer(point.x, point.y, 256, this.horizontalWheel);
      this.verticalWheel = this.horizontalWheel = 0;
    });
  }
  sendPointer(clientX, clientY, flags, wheel = 0, socket = this.socket) {
    if (!flags || socket?.readyState !== WebSocket.OPEN) return;
    const rect = (this.canvas.hidden ? this.image : this.canvas).getBoundingClientRect();
    if (!rect.width || !rect.height) return;
    const x = Math.max(0, Math.min(65535, Math.round(((clientX - rect.left) * 65535) / rect.width))),
      y = Math.max(0, Math.min(65535, Math.round(((clientY - rect.top) * 65535) / rect.height))),
      data = new ArrayBuffer(9),
      view = new DataView(data);
    view.setUint8(0, 1);
    view.setUint16(1, flags | 1, true);
    view.setUint16(3, x, true);
    view.setUint16(5, y, true);
    view.setInt16(7, Math.max(-32768, Math.min(32767, Math.round(wheel))), true);
    this.send(data, socket);
  }
  key(event, up) {
    if (event.ctrlKey && event.code === "KeyV") {
      event.preventDefault();
      return;
    }
    const key = scanCode(event.code);
    if (!key || this.socket?.readyState !== WebSocket.OPEN) return;
    event.preventDefault();
    if (up) this.pressedKeys.delete(event.code);
    else this.pressedKeys.set(event.code, key);
    this.sendKey(key.code, up, key.extended);
  }
  releaseInput(socket = this.socket) {
    if (socket?.readyState === WebSocket.OPEN) {
      for (const key of this.pressedKeys.values()) this.sendKey(key.code, true, key.extended, socket);
      const point = this.pointerPosition;
      if (point && this.pressedButtons) {
        let flags = 0;
        if (this.pressedButtons & 1) flags |= 4;
        if (this.pressedButtons & 2) flags |= 16;
        if (this.pressedButtons & 4) flags |= 64;
        this.sendPointer(point.x, point.y, flags, 0, socket);
      }
    }
    this.clearInput();
  }
  clearInput() {
    this.pressedKeys.clear();
    this.pressedButtons = 0;
    if (this.pointerFrame) cancelAnimationFrame(this.pointerFrame);
    if (this.wheelFrame) cancelAnimationFrame(this.wheelFrame);
    this.pointerFrame = this.wheelFrame = 0;
    this.verticalWheel = this.horizontalWheel = 0;
  }
  paste(event) {
    const text = event.clipboardData?.getData("text/plain");
    if (text === undefined || this.socket?.readyState !== WebSocket.OPEN) return;
    event.preventDefault();
    this.sendClipboard(text);
    if (!event.ctrlKey) this.sendKey(0x1d, false);
    this.sendKey(0x2f, false);
    this.sendKey(0x2f, true);
    if (!event.ctrlKey) this.sendKey(0x1d, true);
  }
  sendKey(code, up, extended = false, socket = this.socket) {
    const data = new ArrayBuffer(5),
      view = new DataView(data);
    view.setUint8(0, 2);
    view.setUint16(1, (up ? 1 : 0) | (extended ? 2 : 0), true);
    view.setUint16(3, code, true);
    this.send(data, socket);
  }
  sendClipboard(text) {
    if (text.length > 0x80000) {
      this.notify("剪贴板文本超过 1 MiB");
      return false;
    }
    const data = new ArrayBuffer(1 + text.length * 2),
      view = new DataView(data);
    view.setUint8(0, 3);
    for (let i = 0; i < text.length; i++) view.setUint16(1 + i * 2, text.charCodeAt(i), true);
    this.send(data);
    return true;
  }
  async sendLocalClipboard() {
    try {
      const text = await navigator.clipboard.readText();
      if (this.socket?.readyState === WebSocket.OPEN) {
        if (!this.sendClipboard(text)) return;
      } else await this.call("/api/clipboard/control", { action: 23, identity: "unicode", argument: text });
      this.notify("本地剪贴板已发送");
    } catch (error) {
      this.notify(error);
    }
  }
  async copyRemoteClipboard() {
    try {
      const records = await this.call("/api/clipboard"),
        text = records.find((record) => record.kind === 24 && record.state === 13)?.detail;
      if (text === undefined) throw new Error("远端剪贴板没有 Unicode 文本");
      await navigator.clipboard.writeText(text);
      this.notify("远端剪贴板已复制到本机");
    } catch (error) {
      this.notify(error);
    }
  }
}

function buttonFlag(button, down) {
  return button === 0 ? (down ? 2 : 4) : button === 1 ? (down ? 32 : 64) : button === 2 ? (down ? 8 : 16) : 0;
}
function mouseButton(button) {
  return button === 0 ? 1 : button === 2 ? 2 : button === 1 ? 4 : 0;
}
function scanCode(code) {
  const fixed = {
      Escape: 0x01,
      Backspace: 0x0e,
      Tab: 0x0f,
      Enter: 0x1c,
      ControlLeft: 0x1d,
      ShiftLeft: 0x2a,
      ShiftRight: 0x36,
      AltLeft: 0x38,
      Space: 0x39,
      CapsLock: 0x3a,
      NumLock: 0x45,
      ScrollLock: 0x46,
      NumpadMultiply: 0x37,
      NumpadSubtract: 0x4a,
      NumpadAdd: 0x4e,
      NumpadDecimal: 0x53,
      NumpadDivide: 0x35,
      NumpadEnter: 0x1c,
      ControlRight: 0x1d,
      AltRight: 0x38,
      Home: 0x47,
      ArrowUp: 0x48,
      PageUp: 0x49,
      ArrowLeft: 0x4b,
      ArrowRight: 0x4d,
      End: 0x4f,
      ArrowDown: 0x50,
      PageDown: 0x51,
      Insert: 0x52,
      Delete: 0x53,
      MetaLeft: 0x5b,
      MetaRight: 0x5c,
      ContextMenu: 0x5d,
      Semicolon: 0x27,
      Equal: 0x0d,
      Comma: 0x33,
      Minus: 0x0c,
      Period: 0x34,
      Slash: 0x35,
      Backquote: 0x29,
      BracketLeft: 0x1a,
      Backslash: 0x2b,
      BracketRight: 0x1b,
      Quote: 0x28,
    },
    letters = "QWERTYUIOPASDFGHJKLZXCVBNM",
    letterCodes = [
      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
      0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32,
    ];
  let value = fixed[code];
  if (code.startsWith("Key")) value = letterCodes[letters.indexOf(code.slice(3))];
  else if (code.startsWith("Digit")) value = code === "Digit0" ? 0x0b : Number(code.slice(5)) + 1;
  else if (code.startsWith("F") && Number(code.slice(1)) >= 1 && Number(code.slice(1)) <= 10)
    value = 0x3a + Number(code.slice(1));
  else if (code === "F11") value = 0x57;
  else if (code === "F12") value = 0x58;
  else if (code.startsWith("Numpad") && /^Numpad[0-9]$/.test(code))
    value = [0x52, 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49][Number(code.slice(6))];
  if (!value) return null;
  return {
    code: value,
    extended: [
      "ControlRight",
      "AltRight",
      "NumpadDivide",
      "NumpadEnter",
      "Home",
      "ArrowUp",
      "PageUp",
      "ArrowLeft",
      "ArrowRight",
      "End",
      "ArrowDown",
      "PageDown",
      "Insert",
      "Delete",
      "MetaLeft",
      "MetaRight",
      "ContextMenu",
    ].includes(code),
  };
}
async function responseError(response) {
  const text = await response.text();
  try {
    const body = JSON.parse(text);
    return new Error(body.message || text || `HTTP ${response.status}`);
  } catch {
    return new Error(text || `HTTP ${response.status}`);
  }
}

const statusType = (value) =>
  ["Success", "NTSTATUS", "Win32", "Winsock", "HRESULT", "Security", "QUIC", "ProcessExit", "ConfigurationManager"][
    value
  ] ?? `Type ${value}`;
