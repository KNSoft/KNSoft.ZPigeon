import { apiUrl } from "./client-context.mjs";
import { t } from "./i18n.mjs";

const kinds = { history: 3, downloads: 4, bookmarks: 5, settings: 6, extensions: 7, cookies: 8, passwords: 9 };
const ENCRYPTED = 4;
const APP_BOUND = 8;

const browserName = (record) => (record.browser === 1 ? "Google Chrome" : "Microsoft Edge");

function fileTime(value) {
  if (!value || value === "0") return "—";
  try {
    return new Date(Number(BigInt(value) / 10000n - 11644473600000n)).toLocaleString();
  } catch {
    return "—";
  }
}

function cell(row, value) {
  const item = row.insertCell();
  item.textContent = value || "—";
  item.title = item.textContent;
}

function cookieAttributes(record) {
  const data = record.data,
    values = [];
  if (data.flags & 1) values.push("Secure");
  if (data.flags & 2) values.push("HttpOnly");
  if (data.sameSite !== 0xffffffff)
    values.push(["None", "Lax", "Strict"][data.sameSite] || `SameSite ${data.sameSite}`);
  return values.join(" ");
}

function secretValue(record) {
  if (record.data.flags & APP_BOUND) return "（受应用绑定加密保护）";
  if (record.data.flags & ENCRYPTED) return "（已加密）";
  return record.detail;
}

function downloadDetail(record) {
  const data = record.data;
  return `已接收 ${data.receivedBytes} / ${data.totalBytes} 字节；结束时间 ${fileTime(data.endTime)}`;
}

export class BrowserManager {
  constructor(root, { call, notify }) {
    this.root = root;
    this.call = call;
    this.notify = notify;
    this.connected = false;
    this.cursorStack = ["0"];
    root.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <select data-role="browser"></select
        ><select data-role="profile"></select
        ><select data-role="kind">
          <option value="history">历史记录</option>
          <option value="downloads">下载记录</option>
          <option value="bookmarks">书签</option>
          <option value="settings">浏览器设置</option>
          <option value="extensions">扩展</option>
          <option value="cookies">Cookie</option><option value="passwords">密码</option></select
        ><label class="browser-reveal" hidden><input type="checkbox" data-role="reveal" /><span
            data-i18n="browser.passwordReveal"
            >显示明文</span
          ></label
        ><input data-role="filter" placeholder="筛选当前页" /><span class="spacer"></span
        ><button data-role="export">导出</button><button data-role="refresh">刷新</button>
      </div>
      <div class="browser-body">
        <aside>
          <div data-role="overview"></div>
          <p class="property-note">
            读取当前账户的浏览器数据。加密内容默认不以明文形式显示，浏览器运行时可能读取数据有误。
          </p>
          <hr />
          <h2 data-i18n="browserControl.title">远程浏览器</h2>
          <p class="property-note" data-i18n="browserControl.headlessNote">
            浏览器始终以 Headless 模式运行，被控端不显示窗口。
          </p>
          <label><span data-i18n="browserControl.profile">Profile</span
            ><select data-field="cdp-profile"></select></label
          ><button data-action="cdp-start" data-i18n="browserControl.start">启动远程浏览器</button>
          <div data-role="cdp-sessions"></div>
        </aside>
        <section class="browser-data">
          <div class="manager-table">
            <table>
              <thead></thead>
              <tbody></tbody>
            </table>
            <div data-role="document" class="browser-document" hidden></div>
            <div class="manager-empty">Client 未连接</div>
          </div>
          <footer>
            <span data-role="summary"></span><span class="spacer"></span
            ><button data-role="previous" disabled>上一页</button><button data-role="next" disabled>下一页</button>
          </footer>
        </section>
      </div>`;
    this.browser = root.querySelector("[data-role=browser]");
    this.profile = root.querySelector("[data-role=profile]");
    this.kind = root.querySelector("[data-role=kind]");
    this.reveal = root.querySelector("[data-role=reveal]");
    this.filter = root.querySelector("[data-role=filter]");
    this.overview = root.querySelector("[data-role=overview]");
    this.head = root.querySelector("thead");
    this.body = root.querySelector("tbody");
    this.document = root.querySelector("[data-role=document]");
    this.empty = root.querySelector(".manager-empty");
    this.summary = root.querySelector("[data-role=summary]");
    this.previous = root.querySelector("[data-role=previous]");
    this.next = root.querySelector("[data-role=next]");
    this.browser.onchange = () => {
      this.fillProfiles();
      this.resetQuery();
      this.syncCdp();
    };
    this.profile.onchange = this.kind.onchange = () => this.resetQuery();
    this.reveal.onchange = () => this.syncReveal();
    this.filter.oninput = () => this.render();
    root.querySelector("[data-role=refresh]").onclick = () => this.load(true);
    root.querySelector("[data-role=export]").onclick = () => this.export();
    this.previous.onclick = () => this.move(-1);
    this.next.onclick = () => this.move(1);
    root.querySelector("[data-action=cdp-start]").onclick = () => this.startCdp();
    this.syncReveal();
  }
  activate(connected) {
    this.connected = connected;
    if (!connected) {
      this.empty.hidden = false;
      this.empty.textContent = "Client 未连接";
      return;
    }
    if (!this.loaded) this.load().then(() => this.connected && this.loadCdp());
    else this.loadCdp();
  }
  disconnect() {
    this.connected = false;
    this.loaded = false;
    this.reveal.checked = false;
    this.records = [];
    this.page = null;
    this.documentSnapshotId = 0;
    this.browser.replaceChildren();
    this.profile.replaceChildren();
    this.overview.replaceChildren();
    this.body.replaceChildren();
    this.head.replaceChildren();
    this.document.replaceChildren();
    this.document.hidden = true;
    this.summary.textContent = "";
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  async load(force = false) {
    if (!this.connected || this.loading || (this.loaded && !force)) return;
    this.loading = true;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取已安装浏览器…";
    const browser = this.browser.value,
      profile = this.profile.value;
    try {
      const page = await this.call("/api/browsers");
      this.records = page.records;
      this.loaded = true;
      const browsers = this.records.filter((record) => record.kind === 1);
      this.browser.replaceChildren(...browsers.map((record) => new Option(browserName(record), String(record.browser))));
      if (browsers.some((record) => String(record.browser) === browser)) this.browser.value = browser;
      this.fillProfiles(profile);
      if (this.cdpBrowsers) this.syncCdp();
      this.renderOverview();
      await this.query("0");
    } catch (error) {
      this.records = [];
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      this.loading = false;
    }
  }
  fillProfiles(selected) {
    const values = this.records.filter((record) => record.kind === 2 && String(record.browser) === this.browser.value);
    this.profile.replaceChildren(...values.map((record) => new Option(record.identity, record.identity)));
    if (values.some((record) => record.identity === selected)) this.profile.value = selected;
    this.browser.disabled = !this.browser.options.length;
    this.profile.disabled = !values.length;
    this.kind.disabled = !values.length;
  }
  renderOverview() {
    const record = this.records.find((value) => value.kind === 1 && String(value.browser) === this.browser.value);
    this.overview.replaceChildren();
    if (!record) return;
    const title = document.createElement("h2"),
      list = document.createElement("dl");
    title.textContent = browserName(record);
    list.className = "details-grid";
    for (const [name, value] of [
      ["版本", record.detail || "未知"],
      ["程序", record.location],
      [
        "Profile 数量",
        String(this.records.filter((item) => item.kind === 2 && item.browser === record.browser).length),
      ],
    ]) {
      const dt = document.createElement("dt"),
        dd = document.createElement("dd");
      dt.textContent = name;
      dd.textContent = value;
      dd.title = value;
      list.append(dt, dd);
    }
    this.overview.append(title, list);
  }
  resetQuery() {
    this.reveal.checked = false;
    this.syncReveal();
    this.renderOverview();
    this.query("0");
  }
  syncReveal() {
    this.reveal.parentElement.hidden = this.kind.value !== "cookies" && this.kind.value !== "passwords";
    for (const cell of this.body.querySelectorAll("td[data-copy-mask]"))
      if (this.reveal.checked) cell.removeAttribute("data-copy-reveal");
      else cell.dataset.copyReveal = "hover";
  }
  documentKind() {
    return this.kind.value === "bookmarks" || this.kind.value === "settings";
  }
  async closeDocument() {
    const snapshotId = this.documentSnapshotId;
    this.documentSnapshotId = 0;
    if (snapshotId && this.connected)
      try {
        await this.call("/api/browser/document/close", { snapshotId });
      } catch {}
  }
  async query(cursor) {
    if (!this.connected || this.querying) return;
    if (!this.profile.value) {
      this.empty.hidden = false;
      this.empty.textContent = this.browser.value ? "没有可读取的 Profile" : "未发现 Chrome 或 Edge";
      return;
    }
    this.querying = true;
    this.page = null;
    this.body.replaceChildren();
    this.head.replaceChildren();
    this.body.parentElement.hidden = true;
    this.document.hidden = true;
    this.document.replaceChildren();
    this.summary.textContent = "";
    this.previous.disabled = this.next.disabled = true;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取浏览器数据…";
    try {
      if (this.documentKind()) {
        await this.closeDocument();
        this.page = await this.call("/api/browser/document/open", {
          browser: Number(this.browser.value),
          kind: kinds[this.kind.value],
          profile: this.profile.value,
        });
        this.documentSnapshotId = this.page.snapshotId;
        this.renderDocument();
      } else {
        await this.closeDocument();
        this.page = await this.call("/api/browser/query", {
          browser: Number(this.browser.value),
          kind: kinds[this.kind.value],
          profile: this.profile.value,
          cursor,
        });
        const existing = this.cursorStack.indexOf(cursor);
        if (existing >= 0) this.cursorStack.length = existing + 1;
        else this.cursorStack.push(cursor);
        this.render();
      }
    } catch (error) {
      this.empty.textContent = error.message;
      this.notify(error);
    } finally {
      this.querying = false;
    }
  }
  move(direction) {
    if (direction < 0 && this.cursorStack.length > 1) {
      this.cursorStack.pop();
      this.query(this.cursorStack.pop());
    } else if (direction > 0 && this.page?.nextCursor !== "0") this.query(this.page.nextCursor);
  }
  render() {
    if (!this.page) return;
    if (this.documentKind()) {
      this.filterDocument();
      return;
    }
    const query = this.filter.value.toLocaleLowerCase(),
      records = this.page.records.filter(
        (record) =>
          !query ||
          `${record.name} ${record.identity} ${record.location} ${record.detail} ${JSON.stringify(record.data)}`
            .toLocaleLowerCase()
            .includes(query),
      );
    this.document.hidden = true;
    this.body.parentElement.hidden = false;
    this.head.replaceChildren(this.header());
    this.body.replaceChildren(...records.map((record) => this.row(record)));
    this.empty.hidden = records.length !== 0;
    this.empty.textContent = "没有数据";
    this.summary.textContent = `当前页 ${records.length} 项`;
    this.previous.disabled = this.cursorStack.length <= 1;
    this.next.disabled = this.page.nextCursor === "0";
  }
  jsonToken(value, fallback = "") {
    if (!value) return fallback;
    try {
      const parsed = JSON.parse(value);
      return typeof parsed === "string" ? parsed : JSON.stringify(parsed);
    } catch {
      return value;
    }
  }
  renderDocument() {
    this.document.hidden = false;
    this.body.parentElement.hidden = true;
    this.document.replaceChildren();
    this.appendDocumentPage(this.document, this.page, 0, 1);
    this.empty.hidden = this.page.nodes.length !== 0;
    this.empty.textContent = "没有数据";
    this.summary.textContent = "按需展开浏览器数据";
    this.filterDocument();
  }
  appendDocumentPage(host, page, depth, parentNodeId) {
    for (const node of page.nodes) {
      const item = document.createElement("div"),
        row = document.createElement("div"),
        toggle = document.createElement("button"),
        name = document.createElement("span"),
        value = document.createElement("span"),
        children = document.createElement("div");
      item.className = "browser-document-node";
      row.className = "browser-document-row";
      row.style.paddingInlineStart = `${depth * 18}px`;
      toggle.className = "browser-document-toggle";
      toggle.textContent = node.hasChildren ? "▸" : "·";
      toggle.disabled = !node.hasChildren;
      name.className = "browser-document-name";
      name.textContent = node.name ? this.jsonToken(node.name) : `#${node.id}`;
      value.className = "browser-document-value";
      value.textContent = node.hasChildren ? (node.type === 1 ? "{…}" : "[…]") : this.jsonToken(node.value, "null");
      row.append(toggle, name, value);
      item.append(row, children);
      if (node.hasChildren)
        toggle.onclick = async () => {
          if (!children.hidden && children.childElementCount) {
            children.hidden = true;
            toggle.textContent = "▸";
            return;
          }
          if (!children.childElementCount) {
            toggle.disabled = true;
            try {
              const childPage = await this.call("/api/browser/document/node", {
                snapshotId: this.documentSnapshotId,
                nodeId: node.id,
                cursor: 0,
              });
              this.appendDocumentPage(children, childPage, depth + 1, node.id);
            } catch (error) {
              this.notify(error);
            } finally {
              toggle.disabled = false;
            }
          }
          children.hidden = false;
          toggle.textContent = "▾";
        };
      host.append(item);
    }
    if (page.nextCursor) {
      const more = document.createElement("button");
      more.className = "browser-document-more";
      more.textContent = "加载更多…";
      more.style.marginInlineStart = `${depth * 18}px`;
      more.onclick = async () => {
        more.disabled = true;
        try {
          const next = await this.call("/api/browser/document/node", {
            snapshotId: this.documentSnapshotId,
            nodeId: parentNodeId,
            cursor: page.nextCursor,
          });
          more.remove();
          this.appendDocumentPage(host, next, depth, parentNodeId);
        } catch (error) {
          more.disabled = false;
          this.notify(error);
        }
      };
      host.append(more);
    }
  }
  filterDocument() {
    if (!this.documentKind()) return;
    const query = this.filter.value.toLocaleLowerCase();
    for (const node of this.document.querySelectorAll(".browser-document-node"))
      node.hidden = !!query && !node.firstElementChild.textContent.toLocaleLowerCase().includes(query);
  }
  header() {
    const row = document.createElement("tr"),
      titles =
        this.kind.value === "history"
          ? ["标题", "地址", "最后访问", "访问次数"]
          : this.kind.value === "downloads"
            ? ["文件", "来源", "开始时间", "状态", "详细信息"]
            : this.kind.value === "cookies"
              ? ["域", "名称", "内容", "路径", "最后访问", "到期时间", "属性"]
              : this.kind.value === "passwords"
                ? ["来源", "用户名", "密码", "创建时间"]
                : ["名称", "标识", "位置"],
      sensitiveColumn = this.kind.value === "cookies" || this.kind.value === "passwords" ? 2 : -1;
    for (const [index, title] of titles.entries()) {
      const item = document.createElement("th");
      item.textContent = title;
      if (index === sensitiveColumn) item.dataset.tableUnsortable = "";
      row.append(item);
    }
    return row;
  }
  sensitiveCell(row, record) {
    const item = row.insertCell();
    let value = secretValue(record);
    value ||= "";
    item.textContent = this.reveal.checked ? value || "—" : "••••••••";
    item.dataset.copyable = "";
    item.dataset.copyValue = value;
    item.dataset.copyMask = "••••••••";
    if (!this.reveal.checked) item.dataset.copyReveal = "hover";
  }
  row(record) {
    const row = document.createElement("tr");
    if (this.kind.value === "passwords") {
      cell(row, record.identity);
      cell(row, record.name);
      this.sensitiveCell(row, record);
      cell(row, fileTime(record.data.creationTime));
      return row;
    }
    if (this.kind.value === "cookies") {
      cell(row, record.identity);
      cell(row, record.name);
      this.sensitiveCell(row, record);
      cell(row, record.location);
      cell(row, fileTime(record.data.lastAccessTime));
      cell(row, fileTime(record.data.expirationTime));
      cell(row, cookieAttributes(record));
      return row;
    }
    const values =
        this.kind.value === "history"
          ? [record.name, record.identity, fileTime(record.data.lastVisitTime), record.data.visitCount]
          : this.kind.value === "downloads"
            ? [record.name, record.identity, fileTime(record.data.startTime), String(record.data.state), downloadDetail(record)]
            : [record.name || record.identity, record.identity, record.location];
    for (const value of values) cell(row, value);
    return row;
  }
  async export() {
    if (!this.profile.value) return;
    const button = this.root.querySelector("[data-role=export]");
    button.disabled = true;
    button.textContent = "正在导出…";
    try {
      const query = new URLSearchParams({
          browser: this.browser.value,
          kind: kinds[this.kind.value],
          profile: this.profile.value,
        }),
        response = await fetch(apiUrl(`/api/browser/export?${query}`));
      if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
      const url = URL.createObjectURL(await response.blob()),
        link = document.createElement("a");
      link.href = url;
      const browser = this.browser.selectedOptions[0]?.text || "Browser";
      const kind = this.kind.selectedOptions[0]?.text || "Data";
      link.download = `ZPigeon-${browser}-${kind}.csv`;
      link.click();
      URL.revokeObjectURL(url);
    } catch (error) {
      this.notify(error);
    } finally {
      button.disabled = !this.connected || !this.profile.value;
      button.textContent = "导出";
    }
  }
  async loadCdp() {
    try {
      const discovery = await this.call("/api/remote/cdp/browsers");
      this.cdpBrowsers = discovery.browsers;
      this.syncCdp();
      await this.loadCdpSessions();
    } catch (error) {
      this.notify(error);
    }
  }
  syncCdp() {
    const browser = this.cdpBrowsers?.find(
        (item) => item.id === (this.browser.value === "2" ? "edge" : "chrome"),
      ),
      profile = this.root.querySelector("[data-field=cdp-profile]"),
      selected = profile.value,
      source = document.createElement("optgroup"),
      managed = document.createElement("optgroup");
    source.label = t("browserControl.existingProfiles");
    managed.label = t("browserControl.createdProfiles");
    source.append(
      ...(browser?.profiles ?? [])
        .filter((item) => item.kind === "source")
        .map((item) => new Option(item.name, `source:${item.name}`)),
    );
    managed.append(
      ...(browser?.profiles ?? [])
        .filter((item) => item.kind === "managed")
        .map((item) => new Option(item.name, `managed:${item.name}`)),
    );
    profile.replaceChildren(
      new Option(t("browserControl.temporaryProfile"), "temporary:"),
      new Option(t("browserControl.incognitoProfile"), "incognito:"),
      source,
      managed,
      new Option(t("browserControl.newProfile"), "new:"),
    );
    const values = [...profile.options].map((option) => option.value),
      preferred = `source:${this.profile.value}`;
    if (values.includes(selected)) profile.value = selected;
    else if (values.includes(preferred)) profile.value = preferred;
    this.root.querySelector("[data-action=cdp-start]").disabled = !browser;
  }
  async startCdp() {
    const button = this.root.querySelector("[data-action=cdp-start]"),
      browser = this.browser.value === "2" ? "edge" : "chrome",
      selection = this.root.querySelector("[data-field=cdp-profile]").value,
      separator = selection.indexOf(":"),
      kind = selection.slice(0, separator),
      selectedProfile = selection.slice(separator + 1);
    button.disabled = true;
    button.textContent = t("browserControl.starting");
    try {
      let mode = kind,
        profile = selectedProfile;
      if (kind === "source") {
        button.textContent = t("browserControl.inspectingProfile");
        const inspection = await this.call("/api/remote/cdp/profile/inspect", {
            browser,
            profile: selectedProfile,
          }),
          stamp = new Date().toISOString().replace(/[-:]/g, "").slice(0, 13),
          name = prompt(t("browserControl.cloneName"), `${selectedProfile.slice(0, 48)}-${stamp}`)?.trim();
        if (!name) return;
        const warning = t("browserControl.cloneWarning", {
          profile: selectedProfile,
          size: formatBytes(inspection.profileSize),
          free: formatBytes(inspection.availableSpace),
          running: inspection.browserRunning ? `\n${t("browserControl.runningWarning")}` : "",
        });
        if (inspection.profileSize > inspection.availableSpace) {
          throw new Error(t("browserControl.insufficientSpace"));
        }
        if (!confirm(warning)) return;
        button.textContent = t("browserControl.cloningProfile");
        await this.call("/api/remote/cdp/profile/clone", {
          browser,
          profile: selectedProfile,
          name,
        });
        mode = "managed";
        profile = name;
        await this.loadCdp();
        this.root.querySelector("[data-field=cdp-profile]").value = `managed:${name}`;
      } else if (kind === "new") {
        const name = prompt(t("browserControl.newProfileName"))?.trim();
        if (!name) return;
        button.textContent = t("browserControl.creatingProfile");
        await this.call("/api/remote/cdp/profile/create", { browser, name });
        mode = "managed";
        profile = name;
        await this.loadCdp();
        this.root.querySelector("[data-field=cdp-profile]").value = `managed:${name}`;
      }
      button.textContent = t("browserControl.starting");
      await this.call("/api/remote/cdp/start", {
        browser,
        mode,
        profile: mode === "managed" ? profile : null,
      });
      await this.loadCdpSessions();
    } catch (error) {
      this.notify(error);
    } finally {
      button.disabled = !this.cdpBrowsers?.some((item) => item.id === browser);
      button.textContent = t("browserControl.start");
    }
  }
  async loadCdpSessions() {
    const sessions = await this.call("/api/remote/cdp/sessions"),
      container = this.root.querySelector("[data-role=cdp-sessions]");
    container.replaceChildren();
    for (const session of sessions) {
      const row = document.createElement("p"),
        control = document.createElement("button"),
        devtools = document.createElement("button"),
        close = document.createElement("button");
      row.append(`${session.browser} · ${cdpProfileName(session)} · `);
      control.type = "button";
      control.textContent = t("browserControl.control");
      control.onclick = () => {
        const url = apiUrl("/browser-control.html");
        url.searchParams.set("session", session.id);
        window.open(url, "_blank", "noopener");
      };
      devtools.type = "button";
      devtools.textContent = "DevTools";
      devtools.onclick = async () => {
        try {
          const targets = await this.call("/api/remote/cdp/targets", { id: session.id }),
            target = targets[0];
          if (!target?.devtoolsFrontendUrl) throw new Error(t("browserControl.noDevToolsTarget"));
          window.open(cdpDevtoolsUrl(session, target), "_blank", "noopener");
        } catch (error) {
          this.notify(error);
        }
      };
      close.type = "button";
      close.textContent = t("common.close");
      close.onclick = async () => {
        try {
          await this.call("/api/remote/cdp/close", { id: session.id });
          await this.loadCdpSessions();
        } catch (error) {
          this.notify(error);
        }
      };
      row.append(control, devtools, close);
      container.append(row);
    }
  }
}

function formatBytes(value) {
  const units = ["B", "KiB", "MiB", "GiB", "TiB"];
  let size = Number(value),
    unit = 0;
  while (size >= 1024 && unit < units.length - 1) {
    size /= 1024;
    unit++;
  }
  return `${size.toLocaleString(undefined, { maximumFractionDigits: unit ? 2 : 0 })} ${units[unit]}`;
}

function cdpProfileName(session) {
  if (session.profileKind === "temporary") return t("browserControl.temporaryProfile");
  if (session.profileKind === "incognito") return t("browserControl.incognitoProfile");
  return session.profile;
}

function cdpDevtoolsUrl(session, target) {
  const host = location.hostname.includes(":") ? `[${location.hostname}]` : location.hostname,
    authority = `${host}:${session.forward.port}`,
    path = target.devtoolsFrontendUrl.replace("devtools://devtools/bundled/", "/devtools/"),
    url = new URL(path, `http://${authority}`);
  url.searchParams.set("ws", `${authority}/devtools/page/${target.id}`);
  return url;
}
