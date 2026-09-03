import { CaptureFrameDecoder, configureCaptureEncoding, captureEncodingOptions } from "./capture-frames.mjs";
import { apiUrl, postBinary, postData } from "./client-context.mjs";
import { contextMenuItems, fileAssociation } from "./file-associations.mjs";
import { FilePreview } from "./file-previews.mjs";
import { t } from "./i18n.mjs";
import { revealTableRow } from "./table.mjs";
const DIRECTORY = 0x10;
const collator = new Intl.Collator(undefined, { numeric: true, sensitivity: "base" });
const compareFileRecords = (left, right) =>
  Number(!!(right.attributes & DIRECTORY)) - Number(!!(left.attributes & DIRECTORY)) ||
  collator.compare(left.name, right.name);
const FILE_ATTRIBUTES = [
  [0x1, "只读"],
  [0x2, "隐藏"],
  [0x4, "系统"],
  [0x20, "存档"],
  [0x100, "临时"],
  [0x1000, "脱机"],
  [0x2000, "不建立内容索引"],
];
const FILE_CONTEXT_LABELS = Object.freeze({
  "browse-folder": "common.open",
  download: "common.download",
  hash: "file.menu.hash",
  binary: "file.menu.binary",
  owners: "file.menu.owners",
  rename: "common.rename",
  delete: "common.delete",
  security: "file.menu.security",
  info: "common.properties",
});

export class FileManager {
  constructor(
    host,
    {
      call,
      notify,
      aclEditor,
      hexEditor,
      certificateInstaller,
      getTerminalShells,
      openTerminal,
      revealProcess,
      revealServices,
      revealFile,
      openExecution,
    },
  ) {
    this.host = host;
    this.call = call;
    this.notify = notify;
    this.aclEditor = aclEditor;
    this.hexEditor = hexEditor;
    this.certificateInstaller = certificateInstaller;
    this.getTerminalShells = getTerminalShells;
    this.openTerminal = openTerminal;
    this.revealProcess = revealProcess;
    this.revealServices = revealServices;
    this.revealFile = revealFile;
    this.openExecution = openExecution;
    this.preview = new FilePreview(notify);
    this.path = "";
    this.records = [];
    this.nodes = new Map();
    this.request = 0;
    this.menuRequest = 0;
    this.enumerationId = null;
    host.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <button data-action="up">向上</button
        ><input data-role="path" placeholder="输入远端路径" spellcheck="false" /><button data-action="go">转到</button
        ><button data-action="refresh">刷新</button><button data-action="upload">上传</button
        ><button data-action="url-download">从网络下载…</button
        ><input data-role="upload" type="file" hidden />
      </div>
      <div class="file-body">
        <aside class="file-tree" tabindex="0"><ul data-role="tree"></ul></aside>
        <div class="manager-table file-list" tabindex="0">
          <table>
            <thead>
              <tr>
                <th class="file-icon" aria-label="${t("file.icon")}"></th>
                <th>名称</th>
                <th>修改日期</th>
                <th>类型</th>
                <th>大小</th>
              </tr>
            </thead>
            <tbody></tbody>
          </table>
          <button data-action="more" hidden>加载下一页</button>
          <div class="manager-empty">进入页面后读取远端目录</div>
        </div>
      </div>
      <div class="context-menu file-menu" data-role="menu" hidden></div>
      <dialog data-role="hash">
        <form method="dialog">
          <h2 data-role="hash-title">计算 Hash</h2>
          <p data-role="hash-status" class="status"></p>
          <textarea data-role="hash-value" class="hash-value" readonly></textarea>
          <div class="dialog-actions">
            <button type="button" data-action="copy-hash" disabled>复制</button><button value="close">关闭</button>
          </div>
        </form>
      </dialog>
      <dialog data-role="details" class="file-details">
        <form method="dialog">
          <h2 data-role="details-title"></h2>
          <dl class="details-grid" data-role="details-body"></dl>
          <fieldset data-role="attributes-box">
            <legend>属性</legend>
            <div class="file-attributes" data-role="attributes"></div>
          </fieldset>
          <section data-role="volume" hidden>
            <label>卷标<input data-role="volume-label" maxlength="32" /></label>
            <div class="dialog-actions">
              <button type="button" data-action="format-volume" class="danger">格式化…</button>
            </div>
          </section>
          <div class="dialog-actions">
            <button type="button" data-action="edit-security">权限…</button><span class="spacer"></span
            ><button value="cancel">取消</button><button type="button" data-action="save-properties">确定</button>
          </div>
        </form>
      </dialog>
      <dialog data-role="format">
        <form method="dialog">
          <h2>格式化卷</h2>
          <p data-role="format-target" class="danger"></p>
          <label
            >文件系统<select data-role="format-fs">
              <option>NTFS</option>
              <option>ReFS</option>
              <option>exFAT</option>
              <option>FAT32</option>
            </select></label
          ><label>卷标<input data-role="format-label" maxlength="32" /></label
          ><label class="file-attribute"><input data-role="format-quick" type="checkbox" checked />快速格式化</label
          ><label>输入目标盘符以确认<input data-role="format-confirm" autocomplete="off" required /></label>
          <div class="dialog-actions">
            <button value="cancel">取消</button><button value="format" class="danger">格式化</button>
          </div>
        </form>
      </dialog>
      <dialog data-role="rename">
        <form method="dialog">
          <h2>重命名</h2>
          <input data-role="rename-input" class="dialog-input" required autocomplete="off" />
          <div class="dialog-actions">
            <button value="cancel" formnovalidate>取消</button><button value="rename">重命名</button>
          </div>
        </form>
      </dialog>
      <dialog data-role="url-download" class="file-download-dialog">
        <form>
          <h2>从网络下载</h2>
          <p class="status">文件由 Client 直接下载到当前目录，不经过管理端中转。</p>
          <label>URL<input class="dialog-input" data-field="url" type="url" maxlength="2048" required /></label
          ><label
            >文件名<input class="dialog-input" data-field="name" maxlength="255" autocomplete="off" required /></label
          ><label
            >下载引擎<select data-field="engine">
              <option value="1">BITS（可恢复，适合弱网）</option>
              <option value="2">WinHTTP（立即直连）</option>
            </select></label
          ><label class="file-attribute"
            ><input data-field="overwrite" type="checkbox" />替换已有同名文件</label
          >
          <p class="status">保存到：<span data-role="download-directory"></span></p>
          <div class="manager-table file-download-jobs">
            <table>
              <thead>
                <tr><th>文件</th><th>引擎</th><th>状态</th><th>进度</th><th>操作</th></tr>
              </thead>
              <tbody></tbody>
            </table>
            <div class="manager-empty">暂无下载作业</div>
          </div>
          <div class="dialog-actions">
            <button type="button" data-action="refresh-downloads">刷新</button><span class="spacer"></span
            ><button type="button" data-action="close-download">关闭</button><button data-action="start-download"
              >下载</button
            >
          </div>
        </form>
      </dialog>`;
    const searchButton = document.createElement("button");
    searchButton.dataset.action = "search";
    searchButton.textContent = "搜索";
    this.action("upload").before(searchButton);
    this.searchDialog = document.createElement("dialog");
    this.searchDialog.innerHTML = /* HTML */ `<form>
      <h2>搜索文件</h2>
      <label
        >方式<select data-role="search-mode">
          <option value="1">where.exe（原始输出）</option>
          <option value="2">Windows 索引服务</option>
          <option value="3">通常搜索（递归）</option>
        </select></label
      ><label>名称或模式<input data-role="search-query" required placeholder="例如 *.log 或 ZPigeon" /></label>
      <p data-role="search-status" class="status"></p>
      <pre data-role="search-output"></pre>
      <div class="dialog-actions">
        <button type="button" data-action="close-search">关闭</button><button type="submit">搜索</button>
      </div>
    </form>`;
    host.append(this.searchDialog);
    this.ownersDialog = document.createElement("dialog");
    this.ownersDialog.className = "file-owners";
    this.ownersDialog.innerHTML = /* HTML */ `<form method="dialog">
      <h2 data-role="owners-title">解除占用</h2>
      <p class="status" data-role="owners-status"></p>
      <div class="manager-table">
        <table>
          <thead>
            <tr>
              <th>PID</th>
              <th>进程</th>
              <th>路径</th>
              <th>命令行</th>
              <th>关联服务</th>
            </tr>
          </thead>
          <tbody data-role="owners-body"></tbody>
        </table>
        <div class="manager-empty" data-role="owners-empty">没有进程占用此项目</div>
      </div>
      <div class="dialog-actions">
        <button type="button" data-action="terminate-all" class="danger">结束所有进程</button
        ><button type="button" data-action="close-all" class="danger">解除全部占用</button
        ><button type="button" data-action="refresh-owners">刷新</button><span class="spacer"></span
        ><button value="close">关闭</button>
      </div>
      <div class="context-menu" data-role="owner-menu" hidden>
        <button type="button" data-action="terminate-owner" class="danger">结束进程</button
        ><button type="button" data-action="close-owner" class="danger">解除占用</button>
        <hr />
        <button type="button" data-action="owner-process">转到进程</button
        ><button type="button" data-action="owner-service">转到服务</button
        ><button type="button" data-action="owner-file">转到文件</button>
      </div>
    </form>`;
    host.append(this.ownersDialog);
    this.input = host.querySelector("[data-role=path]");
    this.tree = host.querySelector("[data-role=tree]");
    this.list = host.querySelector(".file-list");
    this.body = host.querySelector("tbody");
    this.empty = host.querySelector(".manager-empty");
    this.more = this.action("more");
    this.menu = host.querySelector("[data-role=menu]");
    this.detailsDialog = host.querySelector("[data-role=details]");
    this.formatDialog = host.querySelector("[data-role=format]");
    this.hashDialog = host.querySelector("[data-role=hash]");
    this.uploadInput = host.querySelector("[data-role=upload]");
    this.renameDialog = host.querySelector("[data-role=rename]");
    this.renameInput = host.querySelector("[data-role=rename-input]");
    this.downloadDialog = host.querySelector("[data-role=url-download]");
    this.downloadBody = this.downloadDialog.querySelector("tbody");
    this.downloadEmpty = this.downloadDialog.querySelector(".manager-empty");
    this.downloadStates = new Map();
    this.ownerMenu = this.ownersDialog.querySelector("[data-role=owner-menu]");
    this.action("go").onclick = () => this.open(this.input.value);
    this.action("refresh").onclick = () => this.open(this.path, this.currentNode, true);
    this.action("up").onclick = () => this.up();
    this.action("search").onclick = () => this.showSearch();
    this.action("upload").onclick = () => this.uploadInput.click();
    this.action("url-download").onclick = () => this.showUrlDownload();
    this.action("save-properties").onclick = () => this.saveProperties();
    this.action("edit-security").onclick = () =>
      this.security(this.property?.path, !!(this.property?.directory || this.property?.volume));
    this.action("format-volume").onclick = () => this.showFormat();
    this.action("copy-hash").onclick = () =>
      navigator.clipboard.writeText(this.host.querySelector("[data-role=hash-value]").value);
    this.more.onclick = () => this.load(this.currentNode, false, true);
    this.uploadInput.onchange = () => this.upload();
    this.input.onkeydown = (event) => {
      if (event.key === "Enter") this.open(this.input.value);
    };
    this.renameDialog.onclose = () => this.rename();
    this.downloadDialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.startUrlDownload();
    };
    this.action("refresh-downloads", this.downloadDialog).onclick = () => this.loadUrlDownloads(true);
    this.action("close-download", this.downloadDialog).onclick = () => this.downloadDialog.close();
    this.downloadDialog.onclose = () => {
      clearInterval(this.downloadTimer);
      this.downloadTimer = 0;
    };
    this.formatDialog.onclose = () => this.format();
    this.searchDialog.querySelector("form").onsubmit = (event) => {
      event.preventDefault();
      this.search();
    };
    this.action("close-search", this.searchDialog).onclick = () => this.searchDialog.close();
    this.action("terminate-all", this.ownersDialog).onclick = () =>
      this.controlOwners(
        1,
        this.owners.map((item) => item.processId),
      );
    this.action("close-all", this.ownersDialog).onclick = () =>
      this.controlOwners(
        2,
        this.owners.map((item) => item.processId),
      );
    this.action("refresh-owners", this.ownersDialog).onclick = () => this.loadOwners();
    this.action("terminate-owner", this.ownerMenu).onclick = () => this.controlOwners(1, [this.owner.processId]);
    this.action("close-owner", this.ownerMenu).onclick = () => this.controlOwners(2, [this.owner.processId]);
    this.action("owner-process", this.ownerMenu).onclick = () => {
      this.ownersDialog.close();
      this.revealProcess?.(this.owner.processId);
    };
    this.action("owner-service", this.ownerMenu).onclick = () => {
      this.ownersDialog.close();
      this.revealServices?.(this.owner.serviceNames);
    };
    this.action("owner-file", this.ownerMenu).onclick = () => {
      this.ownersDialog.close();
      this.revealFile?.(this.owner.imagePath);
    };
    this.list.onclick = (event) => {
      if (!event.target.closest("tbody tr")) this.clearSelection();
    };
    host.addEventListener("pointerdown", (event) => {
      if (!event.target.closest(".file-menu")) this.hideMenus();
      if (!event.target.closest("[data-role=owner-menu]")) this.ownerMenu.hidden = true;
    });
    host.addEventListener("keydown", (event) => this.onKey(event));
    addEventListener("blur", () => this.hideMenus());
  }
  action(name, root = this.host) {
    return root.querySelector(`[data-action="${name}"]`);
  }
  activate(connected) {
    if (connected && !this.loaded) this.loadDrives();
  }
  clearSelection() {
    this.body.querySelector(".selected")?.classList.remove("selected");
    this.selected = null;
  }
  disconnect() {
    this.loaded = false;
    this.path = "";
    this.input.value = "";
    this.records = [];
    this.enumerationId = null;
    this.request++;
    this.clearSelection();
    this.currentNode = null;
    this.nodes.clear();
    this.tree.replaceChildren();
    this.body.replaceChildren();
    this.more.hidden = true;
    this.hideMenus();
    clearInterval(this.downloadTimer);
    this.downloadTimer = 0;
    this.downloadStates.clear();
    this.downloadBody.replaceChildren();
    if (this.downloadDialog.open) this.downloadDialog.close();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  async loadDrives() {
    if (this.driveLoading) return this.driveLoading;
    const request = ++this.request;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取驱动器…";
    this.driveLoading = (async () => {
      try {
        const page = await this.call("/api/files", { path: "", enumerationId: null });
        if (request !== this.request) return;
        this.nodes.clear();
        this.tree.replaceChildren();
        for (const item of page.records) {
          const node = { name: item.name, path: item.name, item, hasChildren: true, loaded: false, expanded: false };
          node.element = this.renderNode(node);
          this.tree.append(node.element);
          this.nodes.set(item.name.toLocaleLowerCase(), node);
        }
        this.loaded = true;
        this.body.replaceChildren();
        this.empty.textContent = page.records.length ? "选择驱动器以查看内容" : "未找到驱动器";
      } catch (error) {
        if (request === this.request) this.empty.textContent = error.message;
      }
    })();
    try {
      return await this.driveLoading;
    } finally {
      this.driveLoading = null;
    }
  }
  async open(path, node = null, refresh = false) {
    path = normalizePath(path);
    if (!path) return;
    if (!node || normalizePath(node.path) !== path)
      node = this.nodes.get(path.toLocaleLowerCase()) || this.addRoot(path);
    this.selectNode(node);
    this.path = path;
    this.input.value = path;
    this.records = [];
    this.enumerationId = null;
    this.more.hidden = true;
    this.clearSelection();
    await this.load(node, refresh);
  }
  async reveal(filePath) {
    if (!this.loaded) await this.loadDrives();
    filePath = normalizePath(filePath);
    const directory = parent(filePath);
    if (!directory) return;
    const name = filePath.slice(directory.length).replace(/^\\/, "");
    await this.open(directory);
    let index = this.records.findIndex((item) => item.name.toLocaleLowerCase() === name.toLocaleLowerCase());
    while (index < 0 && this.enumerationId) {
      await this.load(this.currentNode, false, true);
      index = this.records.findIndex((item) => item.name.toLocaleLowerCase() === name.toLocaleLowerCase());
    }
    if (index < 0) {
      this.notify(`未在目录中找到 ${name}`);
      return;
    }
    const item = this.records[index],
      row = [...this.body.rows].find((value) => value.cells[1]?.textContent === item.name);
    this.select(row, item);
    revealTableRow(row);
  }
  async load(node, refresh, append = false) {
    const request = ++this.request;
    if (!append) this.body.replaceChildren();
    this.more.disabled = true;
    this.empty.textContent = "正在读取…";
    this.empty.hidden = false;
    try {
      const page = await this.call("/api/files", {
        path: append ? null : this.path,
        enumerationId: append ? this.enumerationId : null,
      });
      if (request !== this.request) return;
      this.records = (append ? this.records.concat(page.records) : page.records).sort(compareFileRecords);
      this.enumerationId = page.enumerationId;
      this.render();
      const directories = page.records.filter((item) => item.attributes & DIRECTORY);
      if (append) this.appendChildren(node, directories);
      else if (refresh || !node.loaded) this.setChildren(node, directories);
      this.more.hidden = !this.enumerationId;
      this.loaded = true;
    } catch (error) {
      if (request !== this.request) return;
      this.empty.textContent = error.message;
    } finally {
      if (request === this.request) this.more.disabled = false;
    }
  }
  render() {
    this.body.replaceChildren();
    for (const item of this.records) {
      const row = document.createElement("tr"),
        directory = !!(item.attributes & DIRECTORY),
        association = directory ? null : fileAssociation(item.name);
      row.innerHTML = "<td></td><td></td><td></td><td></td><td></td>";
      row.children[0].className = "file-icon";
      row.children[0].textContent = directory ? "📁" : association?.icon || "";
      row.children[1].textContent = item.name;
      row.children[2].textContent = date(item.lastWriteTime);
      row.children[3].textContent = directory ? t("file.type.folder") : association ? t(association.typeKey) : "";
      fileSize(row.children[4], item);
      row.onclick = () => this.select(row, item);
      row.ondblclick = () => {
        if (item.attributes & DIRECTORY)
          this.open(join(this.path, item.name), this.nodes.get(join(this.path, item.name).toLocaleLowerCase()));
      };
      row.oncontextmenu = (event) => this.context(event, row, item);
      this.body.append(row);
    }
    this.empty.hidden = this.records.length !== 0;
    this.empty.textContent = "此文件夹为空";
  }
  select(row, item) {
    this.body.querySelector(".selected")?.classList.remove("selected");
    row.classList.add("selected");
    this.selected = item;
  }
  addRoot(path) {
    const node = { name: path, path, hasChildren: false, loaded: false, expanded: false };
    node.element = this.renderNode(node);
    this.tree.append(node.element);
    this.nodes.set(path.toLocaleLowerCase(), node);
    return node;
  }
  renderNode(node) {
    const li = document.createElement("li"),
      row = document.createElement("div"),
      arrow = document.createElement("button"),
      label = document.createElement("button"),
      children = document.createElement("ul");
    row.className = "file-node-row";
    arrow.className = "file-arrow";
    arrow.textContent = node.hasChildren ? "▸" : "";
    arrow.disabled = !node.hasChildren;
    arrow.tabIndex = -1;
    label.className = "file-node-label";
    label.textContent = node.name;
    label.title = node.path;
    children.hidden = true;
    row.append(arrow, label);
    li.append(row, children);
    node.row = row;
    node.arrow = arrow;
    node.label = label;
    node.children = children;
    arrow.onclick = (event) => {
      event.stopPropagation();
      this.toggle(node);
    };
    label.onclick = () => this.open(node.path, node);
    row.oncontextmenu = (event) => this.treeContext(event, node);
    return li;
  }
  setChildren(node, records) {
    for (const child of node.children.children) {
      const childNode = this.nodes.get(child.dataset.path);
      if (childNode) this.removeNode(childNode);
    }
    node.children.replaceChildren();
    for (const item of records) {
      const path = join(node.path, item.name),
        child = { name: item.name, path, item, hasChildren: item.hasChildren, loaded: false, expanded: false };
      child.element = this.renderNode(child);
      child.element.dataset.path = path.toLocaleLowerCase();
      node.children.append(child.element);
      this.nodes.set(path.toLocaleLowerCase(), child);
    }
    node.hasChildren = records.length !== 0;
    node.loaded = true;
    node.expanded = node.hasChildren;
    node.children.hidden = !node.expanded;
    node.arrow.disabled = !node.hasChildren;
    node.arrow.textContent = node.hasChildren ? "▾" : "";
  }
  appendChildren(node, records) {
    for (const item of records) {
      const path = join(node.path, item.name),
        key = path.toLocaleLowerCase();
      if (this.nodes.has(key)) continue;
      const child = { name: item.name, path, item, hasChildren: item.hasChildren, loaded: false, expanded: false };
      child.element = this.renderNode(child);
      child.element.dataset.path = key;
      node.children.append(child.element);
      this.nodes.set(key, child);
    }
    if (node.children.childElementCount !== 0) {
      node.hasChildren = true;
      node.expanded = true;
      node.children.hidden = false;
      node.arrow.disabled = false;
      node.arrow.textContent = "▾";
    }
  }
  removeNode(node) {
    for (const child of node.children.children) {
      const childNode = this.nodes.get(child.dataset.path);
      if (childNode) this.removeNode(childNode);
    }
    this.nodes.delete(node.path.toLocaleLowerCase());
  }
  selectNode(node) {
    this.currentNode?.row.classList.remove("selected");
    this.currentNode = node;
    node.row.classList.add("selected");
  }
  toggle(node) {
    if (!node.loaded) return this.open(node.path, node);
    node.expanded = !node.expanded;
    node.children.hidden = !node.expanded;
    node.arrow.textContent = node.expanded ? "▾" : "▸";
  }
  context(event, row, item) {
    event.preventDefault();
    this.select(row, item);
    const path = join(this.path, item.name);
    if (item.attributes & DIRECTORY)
      this.folderContext(event, {
        path,
        name: item.name,
        item,
        node: this.nodes.get(path.toLocaleLowerCase()),
      });
    else {
      this.menuRequest++;
      this.contextTarget = { path, name: item.name, item };
      this.renderContextMenu(
        [
          fileAssociation(item.name)?.contextMenu || [],
          ["download", { type: "hash" }, "binary"],
          ["owners", "rename", "delete"],
          ["security", "info"],
        ],
        event,
      );
    }
  }
  treeContext(event, node) {
    event.preventDefault();
    event.stopPropagation();
    this.selectNode(node);
    this.folderContext(event, { path: node.path, name: node.name, item: node.item, node });
  }
  folderContext(event, target) {
    const request = ++this.menuRequest,
      point = { clientX: event.clientX, clientY: event.clientY };
    this.contextTarget = target;
    this.renderFolderMenu(target, this.getTerminalShells ? null : [], point);
    this.getTerminalShells?.()
      .then((shells) => {
        if (request === this.menuRequest && !this.menu.hidden) this.renderFolderMenu(target, shells, point);
      })
      .catch((error) => {
        if (request === this.menuRequest && !this.menu.hidden) this.renderFolderMenu(target, [], point);
        this.notify(error);
      });
  }
  renderFolderMenu(target, shells, point) {
    this.renderContextMenu(
      [
        ["browse-folder", shells === null || shells.length ? { type: "shell", shells } : null],
        parent(target.path) === null ? [] : ["rename", "delete"],
        ["security", "info"],
      ],
      point,
    );
  }
  renderContextMenu(groups, event) {
    this.menu.replaceChildren();
    for (const group of groups) {
      const items = group.filter(Boolean);
      if (!items.length) continue;
      if (this.menu.childElementCount) this.menu.append(document.createElement("hr"));
      for (const item of items) this.menu.append(this.contextMenuItem(item));
    }
    this.menu.classList.toggle("open-left", event.clientX > innerWidth - 330);
    this.menu.hidden = false;
    const rect = this.menu.getBoundingClientRect();
    this.menu.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - rect.width - 6))}px`;
    this.menu.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - rect.height - 6))}px`;
  }
  contextMenuItem(item) {
    if (item.type === "hash")
      return this.submenu(
        t(FILE_CONTEXT_LABELS.hash),
        [
          [1, "CRC32"],
          [2, "MD5"],
          [3, "SHA-1"],
          [4, "SHA-256"],
        ].map(([algorithm, name]) => [name, () => this.hash(algorithm, name)]),
      );
    if (item.type === "shell")
      return this.submenu(
        t("file.menu.openInShell"),
        item.shells === null
          ? [[t("terminal.loadingShells"), null]]
          : item.shells.map((shell) => [shell.name, () => this.openFolderShell(shell)]),
      );
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = t(contextMenuItems[item] || FILE_CONTEXT_LABELS[item]);
    button.classList.toggle("danger", item === "delete");
    button.onclick = () => (this.contextTarget.item?.attributes & DIRECTORY || this.contextTarget.node ?
      this.invokeFolder(item) : this.invoke(item));
    return button;
  }
  submenu(label, items) {
    const root = document.createElement("div"),
      button = document.createElement("button"),
      menu = document.createElement("div");
    root.className = "file-submenu";
    button.type = "button";
    button.textContent = label;
    menu.className = "context-menu file-submenu-menu";
    for (const [name, action] of items) {
      const child = document.createElement("button");
      child.type = "button";
      child.textContent = name;
      child.disabled = action === null;
      child.onclick = action;
      menu.append(child);
    }
    root.append(button, menu);
    return root;
  }
  hideMenus() {
    this.menu.hidden = true;
    this.menuRequest++;
  }
  invoke(action) {
    this.menu.hidden = true;
    if (action === "preview-text") this.preview.text(this.selectedPath(), this.selected.name);
    else if (action === "view-structured")
      this.preview.structured(this.selectedPath(), this.selected.name, this.selected.size);
    else if (action === "preview-image") this.preview.image(this.selectedPath(), this.selected.name);
    else if (action === "preview-pdf") this.preview.pdf(this.selectedPath(), this.selected.name);
    else if (action === "preview-video") this.preview.media(this.selectedPath(), this.selected.name, true);
    else if (action === "preview-audio") this.preview.media(this.selectedPath(), this.selected.name, false);
    else if (action === "preview-font") this.preview.fontPreview(this.selectedPath(), this.selected.name);
    else if (action === "browse-archive") this.preview.archive(this.selectedPath(), this.selected.name, this.call);
    else if (action === "run") this.run();
    else if (action === "install-inf") this.installInf();
    else if (action === "import-reg") this.importRegistry();
    else if (action === "install-package") this.installPackage();
    else if (action === "install-font") this.installFont();
    else if (action === "view-target") this.shortcutTarget(false);
    else if (action === "open-target") this.shortcutTarget(true);
    else if (action === "view-certificate") this.viewCertificate();
    else if (action === "install-certificate")
      this.certificateInstaller.installFile(this.selectedPath(), this.selected.name);
    else if (action === "rename") this.showRename();
    else if (action === "security")
      this.security(this.selected && join(this.path, this.selected.name), !!(this.selected?.attributes & DIRECTORY));
    else if (action === "owners") this.showOwners(join(this.path, this.selected.name));
    else this[action]?.();
  }
  selectedPath() {
    return join(this.path, this.selected.name);
  }
  invokeFolder(action) {
    this.menu.hidden = true;
    const target = this.contextTarget;
    if (action === "browse-folder") this.open(target.path, target.node);
    else if (action === "rename") this.showRename(target.node);
    else if (action === "delete") this.delete(target);
    else if (action === "security") this.security(target.path, true);
    else if (action === "info") this.info(target.path, target.name, target.item);
  }
  openFolderShell(shell) {
    this.menu.hidden = true;
    this.openTerminal?.(shell, this.contextTarget.path);
  }
  up() {
    const value = parent(this.path);
    if (value) this.open(value);
  }
  async info(
    path = this.selected && join(this.path, this.selected.name),
    title = this.selected?.name,
    value = this.selected,
  ) {
    if (!path) return;
    try {
      const root = /^[A-Za-z]:\\$/.test(path),
        body = this.host.querySelector("[data-role=details-body]"),
        attributesBox = this.host.querySelector("[data-role=attributes-box]"),
        volumeBox = this.host.querySelector("[data-role=volume]");
      if (root) {
        const value = await this.call("/api/file/volume", { path });
        this.property = { path, volume: true, label: value.label };
        body.replaceChildren(
          ...details([
            ["路径", path],
            ["类型", "本地磁盘"],
            ["文件系统", value.fileSystem],
            ["容量", bytes(value.totalBytes)],
            ["可用空间", bytes(value.freeBytes)],
            ["已用空间", bytes(value.totalBytes - value.freeBytes)],
            ["卷序列号", value.serialNumber.toString(16).padStart(8, "0").toUpperCase()],
          ]),
        );
        this.host.querySelector("[data-role=volume-label]").value = value.label;
        attributesBox.hidden = true;
        volumeBox.hidden = false;
      } else {
        value ||= await this.call("/api/file/info", { path });
        const directory = !!(value.attributes & DIRECTORY),
          fields = [
            ["路径", path],
            ["类型", directory ? "文件夹" : "文件"],
            ["大小", directory ? "" : bytes(value.size)],
            ["创建时间", date(value.creationTime)],
            ["修改时间", date(value.lastWriteTime)],
            ["访问时间", date(value.lastAccessTime)],
          ],
          attributes = directory ? FILE_ATTRIBUTES.filter(([flag]) => flag === 0x2) : FILE_ATTRIBUTES;
        this.property = { path, directory };
        body.replaceChildren(...details(fields));
        this.host.querySelector("[data-role=attributes]").replaceChildren(
          ...attributes.map(([flag, name]) => {
            const label = document.createElement("label"),
              input = document.createElement("input");
            label.className = "file-attribute";
            input.type = "checkbox";
            input.value = flag;
            input.checked = !!(value.attributes & flag);
            label.append(input, name);
            return label;
          }),
        );
        attributesBox.hidden = false;
        volumeBox.hidden = true;
      }
      this.host.querySelector("[data-role=details-title]").textContent = title;
      this.detailsDialog.showModal();
    } catch (e) {
      this.notify(e);
    }
  }
  async saveProperties() {
    if (!this.property) return;
    try {
      if (this.property.volume) {
        const label = this.host.querySelector("[data-role=volume-label]").value;
        if (label !== this.property.label)
          await this.call("/api/file/volume/label", { path: this.property.path, label });
      } else {
        let attributes = 0;
        for (const input of this.host.querySelectorAll("[data-role=attributes] input:checked"))
          attributes |= Number(input.value);
        await this.call("/api/file/attributes", { path: this.property.path, attributes });
      }
      this.detailsDialog.close();
      await this.open(this.path, this.currentNode, true);
    } catch (e) {
      this.notify(e);
    }
  }
  security(path = this.selected && join(this.path, this.selected.name), container = false) {
    if (!path || !this.aclEditor) return;
    this.aclEditor.open({
      title: `${path} 的权限`,
      objectType: "file",
      container,
      load: () => this.call("/api/file/security", { path }),
      save: (sddl, daclProtected) => this.call("/api/file/security/set", { path, sddl, daclProtected }),
    });
  }
  showFormat() {
    const path = this.property?.path;
    if (!this.property?.volume) return;
    this.host.querySelector("[data-role=format-target]").textContent =
      `将永久删除 ${path} 上的全部数据。此操作无法撤销。`;
    this.host.querySelector("[data-role=format-label]").value =
      this.host.querySelector("[data-role=volume-label]").value;
    this.host.querySelector("[data-role=format-confirm]").value = "";
    this.formatDialog.returnValue = "";
    this.formatDialog.showModal();
  }
  async format() {
    if (this.formatDialog.returnValue !== "format" || !this.property?.volume) return;
    const path = this.property.path,
      confirmValue = this.host.querySelector("[data-role=format-confirm]").value.trim().toUpperCase();
    if (confirmValue !== path.slice(0, 2).toUpperCase()) {
      this.notify(`确认内容必须为 ${path.slice(0, 2)}`);
      return;
    }
    try {
      const job = await this.call("/api/file/volume/format", {
        path,
        fileSystem: this.host.querySelector("[data-role=format-fs]").value,
        label: this.host.querySelector("[data-role=format-label]").value,
        quick: this.host.querySelector("[data-role=format-quick]").checked,
      });
      this.detailsDialog.close();
      this.notify(`格式化任务已启动${job.processId ? `，PID ${job.processId}` : ""}`);
    } catch (e) {
      this.notify(e);
    }
  }
  showSearch() {
    this.searchDialog.querySelector("[data-role=search-status]").textContent = `范围：${this.path}`;
    this.searchDialog.querySelector("[data-role=search-output]").textContent = "";
    this.searchDialog.showModal();
    this.searchDialog.querySelector("[data-role=search-query]").focus();
  }
  async search() {
    const query = this.searchDialog.querySelector("[data-role=search-query]").value.trim(),
      mode = Number(this.searchDialog.querySelector("[data-role=search-mode]").value),
      status = this.searchDialog.querySelector("[data-role=search-status]"),
      output = this.searchDialog.querySelector("[data-role=search-output]"),
      submit = this.searchDialog.querySelector("[type=submit]");
    if (!query) return;
    submit.disabled = true;
    status.textContent = `正在搜索 ${this.path}…`;
    output.textContent = "";
    try {
      const result = await this.call("/api/file/search", { path: this.path, query, mode });
      output.textContent = result.text || "(没有结果)";
      status.textContent =
        result.status.type === 0
          ? "搜索完成"
          : `搜索结束 · 类型 ${result.status.type} · 0x${result.status.code.toString(16).padStart(8, "0").toUpperCase()}`;
    } catch (e) {
      status.textContent = e.message;
    } finally {
      submit.disabled = false;
    }
  }
  async hash(algorithm, name) {
    if (!this.selected) return;
    this.menu.hidden = true;
    const path = join(this.path, this.selected.name),
      status = this.host.querySelector("[data-role=hash-status]"),
      output = this.host.querySelector("[data-role=hash-value]"),
      copy = this.action("copy-hash");
    this.host.querySelector("[data-role=hash-title]").textContent = `${name} · ${this.selected.name}`;
    status.textContent = "正在计算…";
    output.value = "";
    copy.disabled = true;
    this.hashDialog.showModal();
    try {
      const value = await this.call("/api/file/hash", { path, algorithm });
      status.textContent = "计算完成";
      output.value = value.value;
      copy.disabled = false;
    } catch (e) {
      status.textContent = e.message;
    }
  }
  showOwners(path) {
    if (!path) return;
    this.ownerPath = path;
    this.owners = [];
    this.ownersDialog.querySelector("[data-role=owners-title]").textContent = `解除占用 · ${path}`;
    this.ownersDialog.showModal();
    this.loadOwners();
  }
  async loadOwners() {
    const status = this.ownersDialog.querySelector("[data-role=owners-status]"),
      body = this.ownersDialog.querySelector("[data-role=owners-body]"),
      empty = this.ownersDialog.querySelector("[data-role=owners-empty]");
    status.textContent = "正在查询占用进程…";
    body.replaceChildren();
    empty.textContent = "没有进程占用此项目";
    empty.hidden = true;
    this.owner = null;
    try {
      this.owners = await this.call("/api/file/owners", { path: this.ownerPath });
      for (const item of this.owners) {
        const row = document.createElement("tr");
        row.innerHTML = "<td></td><td></td><td></td><td></td><td></td>";
        row.children[0].textContent = item.processId;
        row.children[1].textContent = item.imageName || "(未知进程)";
        ownerField(row.children[2], item.imagePath, item.imagePathStatus);
        ownerField(row.children[3], item.commandLine, item.commandLineStatus);
        row.children[4].textContent = item.serviceNames.join("、");
        row.onclick = () => {
          body.querySelector(".selected")?.classList.remove("selected");
          row.classList.add("selected");
          this.owner = item;
        };
        row.oncontextmenu = (event) => this.ownerContext(event, row, item);
        body.append(row);
      }
      empty.hidden = this.owners.length !== 0;
      status.textContent = this.owners.length ? `${this.owners.length} 个占用进程` : "未发现占用进程";
    } catch (e) {
      status.textContent = e.message;
      empty.hidden = false;
      empty.textContent = e.message;
    }
  }
  ownerContext(event, row, item) {
    event.preventDefault();
    row.click();
    this.action("owner-service", this.ownerMenu).disabled = item.serviceNames.length === 0;
    this.action("owner-file", this.ownerMenu).disabled = item.imagePathStatus < 0 || !item.imagePath;
    this.ownerMenu.hidden = false;
    const rect = this.ownerMenu.getBoundingClientRect();
    this.ownerMenu.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - rect.width - 6))}px`;
    this.ownerMenu.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - rect.height - 6))}px`;
  }
  async controlOwners(control, processIds) {
    this.ownerMenu.hidden = true;
    if (processIds.length === 0) return;
    const action = control === 1 ? "结束进程" : "强制关闭句柄";
    if (!confirm(`确定${action}？此操作可能导致数据丢失或目标程序异常。`)) return;
    if (control === 2 && !confirm("强制关闭其他进程的文件句柄存在崩溃、数据损坏及句柄复用风险。是否仍要继续？")) return;
    try {
      const results = await this.call("/api/file/owners/control", { path: this.ownerPath, control, processIds }),
        failed = results.filter((item) => item.status < 0);
      if (failed.length)
        this.notify(failed.map((item) => `PID ${item.processId}: ${ntstatus(item.status)}`).join("\n"));
      else
        this.notify(
          control === 1
            ? "进程控制完成"
            : `已关闭 ${results.reduce((sum, item) => sum + item.affectedHandleCount, 0)} 个句柄`,
        );
      await this.loadOwners();
    } catch (e) {
      this.notify(e);
    }
  }
  download() {
    if (this.selected && !(this.selected.attributes & DIRECTORY)) {
      const link = document.createElement("a");
      link.href = apiUrl(`/api/file/download?path=${encodeURIComponent(join(this.path, this.selected.name))}`);
      link.download = this.selected.name;
      link.click();
    }
  }
  binary() {
    if (!this.selected || this.selected.attributes & DIRECTORY || !this.hexEditor) return;
    const item = this.selected,
      path = join(this.path, item.name);
    this.hexEditor.open({
      title: `二进制编辑 - ${item.name}`,
      size: item.size,
      read: (offset, length) => postBinary("/api/file/range", { path, offset, length }),
      write: (offset, data) => postData("/api/file/range/write", { path, offset }, data),
    });
  }
  run() {
    this.openExecution?.({
      path: this.selectedPath(),
      suffix: extension(this.selected.name),
      workingDirectory: this.path,
    });
  }
  async installInf() {
    if (!confirm(t("file.confirm.installInf", { name: this.selected.name }))) return;
    await this.startExecution({
      engine: 2,
      flags: 0,
      fileName: this.selectedPath(),
      verb: "install",
    });
  }
  async importRegistry() {
    if (!confirm(t("file.confirm.importRegistry", { name: this.selected.name }))) return;
    await this.startExecution({
      engine: 1,
      flags: 1,
      fileName: "reg.exe",
      arguments: `import ${quoteArgument(this.selectedPath())}`,
      workingDirectory: this.path,
    });
  }
  async installPackage() {
    if (!confirm(t("file.confirm.installPackage", { name: this.selected.name }))) return;
    try {
      await this.call("/api/packages/install-existing", {
        path: this.selectedPath(),
        name: this.selected.name,
      });
      this.notify(t("file.install.submitted"));
    } catch (error) {
      this.notify(error);
    }
  }
  async installFont() {
    if (!confirm(t("file.confirm.installFont", { name: this.selected.name }))) return;
    try {
      await this.call("/api/fonts/control", {
        action: 8,
        identity: "user",
        argument: this.selectedPath(),
      });
      this.notify(t("file.install.fontComplete"));
    } catch (error) {
      this.notify(error);
    }
  }
  async shortcutTarget(open) {
    try {
      const value = await this.call("/api/file/shortcut", { path: this.selectedPath() });
      if (open) await this.call("/api/file/open", { path: value.target, hidden: false });
      else this.preview.target(this.selected.name, value.target);
    } catch (error) {
      this.notify(error);
    }
  }
  async viewCertificate() {
    try {
      this.preview.certificate(
        this.selected.name,
        await this.call("/api/file/certificate", { path: this.selectedPath() }),
      );
    } catch (error) {
      this.notify(error);
    }
  }
  async startExecution(values) {
    try {
      const job = await this.call("/api/execution/start", {
        engine: values.engine,
        identity: 1,
        sessionId: 0xffffffff,
        flags: values.flags,
        fileName: values.fileName,
        arguments: values.arguments || null,
        workingDirectory: values.workingDirectory || null,
        verb: values.verb || null,
        userName: null,
        password: null,
        appContainerSid: null,
        cleanupPath: null,
      });
      this.notify(job.processId ? t("file.execution.started", { pid: job.processId }) : t("file.execution.submitted"));
    } catch (error) {
      this.notify(error);
    }
  }
  async upload() {
    const file = this.uploadInput.files[0];
    this.uploadInput.value = "";
    if (!file) return;
    const path = join(this.path, file.name),
      exists = this.records.some((item) => item.name.toLocaleLowerCase() === file.name.toLocaleLowerCase());
    if (exists && !confirm(`“${file.name}”已存在，是否替换？`)) return;
    try {
      const response = await fetch(apiUrl(`/api/file/upload?path=${encodeURIComponent(path)}&overwrite=${exists}`), {
        method: "PUT",
        body: file,
      });
      if (!response.ok) throw new Error((await response.text()) || `HTTP ${response.status}`);
      await this.open(this.path, this.currentNode, true);
    } catch (e) {
      this.notify(e);
    }
  }
  showUrlDownload() {
    this.downloadDialog.querySelector("[data-role=download-directory]").textContent = this.path;
    this.downloadDialog.querySelector("[data-field=url]").value = "";
    this.downloadDialog.querySelector("[data-field=name]").value = "";
    this.downloadDialog.querySelector("[data-field=overwrite]").checked = false;
    this.downloadDialog.showModal();
    this.downloadDialog.querySelector("[data-field=url]").focus();
    this.loadUrlDownloads(true);
    clearInterval(this.downloadTimer);
    this.downloadTimer = setInterval(() => this.loadUrlDownloads(), 1000);
  }
  async startUrlDownload() {
    const submit = this.action("start-download", this.downloadDialog),
      url = this.downloadDialog.querySelector("[data-field=url]").value.trim(),
      name = this.downloadDialog.querySelector("[data-field=name]").value.trim(),
      engine = Number(this.downloadDialog.querySelector("[data-field=engine]").value),
      overwrite = this.downloadDialog.querySelector("[data-field=overwrite]").checked;
    submit.disabled = true;
    try {
      await this.call("/api/file/url-download", { directory: this.path, url, name, engine, overwrite });
      this.notify("下载作业已提交");
      await this.loadUrlDownloads(true);
    } catch (error) {
      this.notify(error);
    } finally {
      submit.disabled = false;
    }
  }
  async loadUrlDownloads(report = false) {
    if (!this.downloadDialog.open || this.downloadsLoading) return;
    this.downloadsLoading = true;
    try {
      const jobs = await this.call("/api/file/url-downloads"),
        completed = jobs.filter(
          (job) => this.downloadStates.has(job.id) && this.downloadStates.get(job.id) < 4 && job.state >= 4,
        );
      this.downloadBody.replaceChildren(
        ...jobs.map((job) => {
          const row = document.createElement("tr"),
            fileName = job.path.replace(/^.*[\\/]/, ""),
            status =
              job.state === 1
                ? "排队中"
                : job.state === 2
                  ? "正在下载"
                  : job.state === 3
                    ? "等待网络恢复"
                    : job.state === 4
                      ? "已完成"
                      : job.state === 5
                        ? "失败"
                        : "已取消";
          for (const value of [
            fileName,
            job.engine === 1 ? "BITS" : "WinHTTP",
            status,
            downloadProgress(job),
          ])
            row.insertCell().textContent = value;
          row.cells[0].title = `${job.path}\n${job.url}`;
          row.cells[2].title = job.errorText || (job.result ? hex(job.result) : "");
          const action = row.insertCell();
          if (job.state < 4) {
            const cancel = document.createElement("button");
            cancel.type = "button";
            cancel.textContent = "取消";
            cancel.onclick = () => this.cancelUrlDownload(job, fileName);
            action.append(cancel);
          }
          this.downloadStates.set(job.id, job.state);
          return row;
        }),
      );
      this.downloadEmpty.hidden = jobs.length !== 0;
      this.downloadEmpty.textContent = "暂无下载作业";
      if (completed.some((job) => job.state === 4)) await this.open(this.path, this.currentNode, true);
    } catch (error) {
      if (report) this.notify(error);
    } finally {
      this.downloadsLoading = false;
    }
  }
  async cancelUrlDownload(job, name) {
    if (!confirm(`确定取消下载“${name}”？`)) return;
    try {
      await this.call("/api/file/url-download/cancel", { id: job.id });
      await this.loadUrlDownloads(true);
    } catch (error) {
      this.notify(error);
    }
  }
  showRename(node = null) {
    const item = node ? { name: node.name } : this.selected;
    if (!item) return;
    this.renameTarget = node ? { item, path: parent(node.path), node } : { item, path: this.path };
    this.renameInput.value = item.name;
    this.renameDialog.returnValue = "";
    this.renameDialog.showModal();
    this.renameInput.select();
  }
  async rename() {
    const target = this.renameTarget,
      name = this.renameInput.value.trim();
    this.renameTarget = null;
    if (this.renameDialog.returnValue !== "rename" || !target || !name || name === target.item.name) return;
    if (/[\\/]/.test(name)) {
      this.notify("名称不能包含路径分隔符");
      return;
    }
    try {
      await this.call("/api/file/rename", {
        path: join(target.path, target.item.name),
        newPath: join(target.path, name),
      });
      await this.open(
        target.path,
        target.node ? this.nodes.get(target.path.toLocaleLowerCase()) : this.currentNode,
        true,
      );
    } catch (e) {
      this.notify(e);
    }
  }
  async delete(target = null) {
    const item = target?.item || (target?.node ? { name: target.name, attributes: DIRECTORY } : this.selected),
      path = target?.path || (item && join(this.path, item.name));
    if (
      !item ||
      !confirm(`确定永久删除“${item.name}”吗？${item.attributes & DIRECTORY ? "非空文件夹不会被递归删除。" : ""}`)
    )
      return;
    try {
      await this.call("/api/file/delete", { path });
      const directory = parent(path);
      await this.open(directory, this.nodes.get(directory.toLocaleLowerCase()), true);
    } catch (e) {
      this.notify(e);
    }
  }
  onKey(event) {
    if (event.target.closest("dialog") || !this.selected) return;
    if (event.key === "F2") {
      event.preventDefault();
      this.showRename();
    } else if (event.key === "Delete") {
      event.preventDefault();
      this.delete();
    } else if (event.key === "Enter" && this.selected.attributes & DIRECTORY) {
      event.preventDefault();
      this.open(
        join(this.path, this.selected.name),
        this.nodes.get(join(this.path, this.selected.name).toLocaleLowerCase()),
      );
    }
  }
}

export class ProcessManager {
  constructor(host, { call, notify, hexEditor, isConnected, revealFile, revealServices }) {
    this.host = host;
    this.call = call;
    this.notify = notify;
    this.hexEditor = hexEditor;
    this.isConnected = isConnected;
    this.revealFile = revealFile;
    this.revealServices = revealServices;
    this.active = false;
    this.refreshing = null;
    this.interval = 3000;
    host.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选进程" /><span data-role="summary" class="status"></span
        ><span class="spacer"></span
        ><label class="process-refresh"
          >刷新频率
          <select data-role="interval">
            <option value="0">手动</option>
            <option value="1000">1 秒</option>
            <option value="2000">2 秒</option>
            <option value="3000" selected>3 秒</option>
            <option value="5000">5 秒</option>
            <option value="10000">10 秒</option>
          </select></label
        ><button data-action="refresh" hidden>刷新</button>
      </div>
      <div class="manager-table">
        <table class="process-table">
          <thead>
            <tr>
              <th>名称</th>
              <th>PID</th>
              <th>状态</th>
              <th>用户名</th>
              <th>CPU</th>
              <th title="工作集 / 专用提交">内存（工作集/专用）</th>
              <th>路径</th>
              <th>平台</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty">进入页面后读取远端进程</div>
      </div>
      <div class="context-menu process-menu" data-role="menu" hidden>
        <button data-action="terminate" class="danger">结束进程</button
        ><button data-action="terminate-tree" class="danger">结束进程树</button>
        <hr />
        <button data-action="suspend">暂停</button><button data-action="resume">恢复</button
        ><button data-action="efficiency">效能模式</button>
        <div class="process-submenu">
          <button data-role="priority-root">设置优先级</button>
          <div class="context-menu process-priority-menu">
            <button data-priority="4">实时</button><button data-priority="3">高</button
            ><button data-priority="6">高于正常</button><button data-priority="2">正常</button
            ><button data-priority="5">低于正常</button><button data-priority="1">低</button>
          </div>
        </div>
        <button data-action="virtualization">UAC 虚拟化</button>
        <hr />
        <button data-action="dump">创建内存转储文件</button><button data-action="modules">DLL 列表</button
        ><button data-action="handles">${t("process.handleTable")}</button
        ><button data-action="memory">编辑内存</button><button data-action="file">转到文件</button
        ><button data-action="service">转到服务</button>
        <hr />
        <button data-action="details">属性</button>
      </div>
      <dialog data-role="details">
        <form method="dialog">
          <h2 data-role="details-title"></h2>
          <dl class="details-grid" data-role="details-body"></dl>
          <div class="dialog-actions"><button value="close">关闭</button></div>
        </form>
      </dialog>
      <dialog data-role="memory">
        <form method="dialog">
          <h2>编辑进程内存</h2>
          <label
            >起始地址<input
              data-role="memory-address"
              class="dialog-input"
              placeholder="0x0000000000000000"
              required /></label
          ><label
            >长度（字节）<input
              data-role="memory-length"
              class="dialog-input"
              type="number"
              min="1"
              value="65536"
              required
          /></label>
          <p class="status">仅访问当前已提交内存区域，不会自动更改页面保护。</p>
          <div class="dialog-actions">
            <button value="cancel" formnovalidate>取消</button><button value="default">打开</button>
          </div>
        </form>
      </dialog>
      <dialog data-role="dump" class="process-dump">
        <h2>创建内存转储文件</h2>
        <fieldset data-role="dump-options">
          <legend>MiniDump 内容</legend>
          <label><input type="checkbox" value="32" checked />已卸载模块</label
          ><label><input type="checkbox" value="4096" checked />线程信息</label
          ><label><input type="checkbox" value="4" />句柄数据</label
          ><label><input type="checkbox" value="2048" />完整内存信息</label
          ><label><input type="checkbox" value="262144" />Token 信息</label
          ><label><input type="checkbox" value="524288" />模块头</label
          ><label><input type="checkbox" value="2" />完整内存</label
          ><label><input type="checkbox" value="512" />私有读写内存</label>
        </fieldset>
        <p data-role="dump-status" class="status">选择要包含的内容。</p>
        <progress data-role="dump-progress" hidden></progress>
        <div class="dialog-actions">
          <button data-action="dump-cancel">取消</button><button data-action="dump-start">创建并下载</button>
        </div>
      </dialog>`;
    this.body = host.querySelector("tbody");
    this.empty = host.querySelector(".manager-empty");
    this.filter = host.querySelector("[data-role=filter]");
    this.menu = host.querySelector("[data-role=menu]");
    this.memoryDialog = host.querySelector("[data-role=memory]");
    this.dumpDialog = host.querySelector("[data-role=dump]");
    this.filter.oninput = () => this.render();
    this.action("refresh").onclick = () => this.refresh();
    host.querySelector("[data-role=interval]").onchange = (event) => {
      this.interval = Number(event.target.value);
      this.action("refresh").hidden = this.interval !== 0;
      this.update(true);
    };
    for (const button of this.menu.querySelectorAll("[data-action]"))
      button.onclick = () => this.invoke(button.dataset.action);
    for (const button of this.menu.querySelectorAll("[data-priority]"))
      button.onclick = () => {
        this.hideMenu();
        this.control(6, Number(button.dataset.priority));
      };
    this.memoryDialog.onclose = () => {
      if (this.memoryDialog.returnValue === "default") this.openMemory();
    };
    this.action("dump-start").onclick = () => this.createDump();
    this.action("dump-cancel").onclick = () => {
      this.dumpAbort?.abort();
      this.dumpDialog.close();
    };
    this.tick = () => this.update();
    document.addEventListener("visibilitychange", this.tick);
    addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.hideMenu();
    });
    addEventListener("blur", () => this.hideMenu());
    this.vmmapButton = document.createElement("button");
    this.vmmapButton.textContent = "VMMap";
    this.action("memory", this.menu).before(this.vmmapButton);
    this.vmmapButton.onclick = () => {
      this.hideMenu();
      this.openMemoryMap();
    };
    this.vmmapDialog = document.createElement("dialog");
    this.vmmapDialog.className = "vmmap-dialog";
    this.vmmapDialog.innerHTML = /* HTML */ `<h2 data-role="vmmap-title">VMMap</h2>
      <div class="vmmap-summary" data-role="vmmap-summary"></div>
      <div class="manager-toolbar">
        <select data-role="vmmap-filter">
          <option value="">全部类型</option>
          <option>映像</option>
          <option>映射文件</option>
          <option>页文件映射</option>
          <option>专用内存</option>
          <option>AWE</option>
          <option>物理内存</option>
          <option>软件 Enclave</option>
          <option>占位符</option>
          <option>保留</option></select
        ><label><input data-role="vmmap-free" type="checkbox" />显示空闲</label
        ><span data-role="vmmap-status" class="status"></span><span class="spacer"></span
        ><button data-action="vmmap-export">导出 CSV</button><button data-action="vmmap-refresh">刷新</button>
      </div>
      <div class="manager-table">
        <table>
          <thead>
            <tr>
              <th>基址</th>
              <th>大小</th>
              <th>已提交</th>
              <th>工作集</th>
              <th>专用工作集</th>
              <th>共享工作集</th>
              <th>状态</th>
              <th>类型</th>
              <th>优先级</th>
              <th>保护</th>
              <th>区域属性</th>
              <th>映射文件</th>
            </tr>
          </thead>
          <tbody data-role="vmmap-body"></tbody>
        </table>
        <div class="manager-empty" data-role="vmmap-empty"></div>
      </div>
      <div class="dialog-actions"><button data-action="vmmap-close">关闭</button></div>`;
    host.append(this.vmmapDialog);
    this.action("vmmap-refresh", this.vmmapDialog).onclick = () => this.loadMemoryMap();
    this.action("vmmap-export", this.vmmapDialog).onclick = () => this.exportMemoryMap();
    this.action("vmmap-close", this.vmmapDialog).onclick = () => this.vmmapDialog.close();
    this.vmmapDialog.onclose = () => this.closeMemoryMap();
    this.vmmapDialog.querySelector("[data-role=vmmap-filter]").onchange = () => this.renderMemoryMap();
    this.vmmapDialog.querySelector("[data-role=vmmap-free]").onchange = () => this.renderMemoryMap();
    this.moduleDialog = document.createElement("dialog");
    this.moduleDialog.className = "vmmap-dialog";
    this.moduleDialog.innerHTML = /* HTML */ `<h2 data-role="module-title">DLL 列表</h2>
      <div class="manager-toolbar">
        <input data-role="module-filter" placeholder="筛选 DLL" /><span data-role="module-summary" class="status"></span
        ><span class="spacer"></span><button data-action="module-refresh">刷新</button>
      </div>
      <div class="manager-table">
        <table>
          <thead>
            <tr>
              <th>名称</th>
              <th>类型</th>
              <th>基址</th>
              <th>大小</th>
              <th>入口点</th>
              <th>加载原因</th>
              <th>加载时间</th>
              <th>路径</th>
            </tr>
          </thead>
          <tbody data-role="module-body"></tbody>
        </table>
        <div class="manager-empty" data-role="module-empty"></div>
      </div>
      <div class="context-menu" data-role="module-menu" hidden><button data-action="module-file">转到文件</button></div>
      <div class="dialog-actions"><button data-action="module-close">关闭</button></div>`;
    host.append(this.moduleDialog);
    this.moduleBody = this.moduleDialog.querySelector("[data-role=module-body]");
    this.moduleMenu = this.moduleDialog.querySelector("[data-role=module-menu]");
    this.moduleDialog.querySelector("[data-role=module-filter]").oninput = () => this.renderModules();
    this.action("module-refresh", this.moduleDialog).onclick = () => this.loadModules();
    this.action("module-close", this.moduleDialog).onclick = () => this.moduleDialog.close();
    this.action("module-file", this.moduleMenu).onclick = () => {
      this.moduleMenu.hidden = true;
      if (this.selectedModule?.path) {
        this.moduleDialog.close();
        this.revealFile?.(this.selectedModule.path);
      }
    };
    this.moduleDialog.onpointerdown = (event) => {
      if (!this.moduleMenu.contains(event.target)) this.moduleMenu.hidden = true;
    };
    this.handleDialog = document.createElement("dialog");
    this.handleDialog.className = "vmmap-dialog";
    this.handleDialog.innerHTML = /* HTML */ `<h2 data-role="handle-title">${t("process.handleTable")}</h2>
      <div class="manager-toolbar">
        <input data-role="handle-filter" placeholder="${t("process.filterHandles")}" /><span
          data-role="handle-summary"
          class="status"></span
        ><span class="spacer"></span><button data-action="handle-refresh">${t("common.refresh")}</button>
      </div>
      <div class="manager-table">
        <table>
          <thead>
            <tr>
              <th>${t("process.handleValue")}</th>
              <th>${t("process.handleType")}</th>
              <th>${t("process.objectName")}</th>
            </tr>
          </thead>
          <tbody data-role="handle-body"></tbody>
        </table>
        <div class="manager-empty" data-role="handle-empty"></div>
      </div>
      <div class="dialog-actions"><button data-action="handle-close">${t("common.close")}</button></div>`;
    host.append(this.handleDialog);
    this.handleBody = this.handleDialog.querySelector("[data-role=handle-body]");
    this.handleDialog.querySelector("[data-role=handle-filter]").oninput = () => this.renderHandles();
    this.action("handle-refresh", this.handleDialog).onclick = () => this.loadHandles();
    this.action("handle-close", this.handleDialog).onclick = () => this.handleDialog.close();
  }
  action(name, root = this.host) {
    return root.querySelector(`[data-action="${name}"]`);
  }
  identity(item = this.selected) {
    return item && { processId: item.processId, createTime: item.createTime };
  }
  setActive(value) {
    this.active = value;
    this.update(true);
  }
  async revealProcess(processId) {
    this.loadSystem();
    if (this.refreshing || !this.records?.length) await this.refresh();
    const item = this.records?.find((value) => value.processId === processId);
    this.filter.value = "";
    this.selected = item || null;
    this.render();
    if (item) revealTableRow(this.body.querySelector(".selected"));
    else this.notify(`未找到 PID ${processId}`);
  }
  eligible() {
    return this.active && this.isConnected() && document.visibilityState === "visible";
  }
  update(immediate = false) {
    clearTimeout(this.timer);
    this.timer = null;
    if (!this.eligible()) return;
    if (this.processorCount === undefined && !this.loadingSystem) this.loadSystem();
    if (immediate) this.refresh();
    else if (this.interval) this.timer = setTimeout(() => this.refresh(), this.interval);
  }
  loadSystem() {
    if (this.processorCount !== undefined) return Promise.resolve(this.processorCount !== null);
    if (!this.loadingSystem)
      this.loadingSystem = this.call("/api/system")
        .then((info) => {
          this.processorCount = info.processorCount;
          return true;
        })
        .catch((error) => {
          this.processorCount = null;
          this.notify(error);
          return false;
        })
        .finally(() => (this.loadingSystem = null));
    return this.loadingSystem;
  }
  clearSelection() {
    this.body.querySelector(".selected")?.classList.remove("selected");
    this.selected = null;
  }
  disconnect() {
    clearTimeout(this.timer);
    this.timer = null;
    this.records = [];
    this.clearSelection();
    this.sampleTime = 0;
    this.processorCount = undefined;
    this.hideMenu();
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  refresh() {
    if (this.refreshing) return this.refreshing;
    if (!this.eligible()) return Promise.resolve(false);
    this.refreshing = (async () => {
      try {
        const now = performance.now(),
          records = await this.call("/api/processes");
        if (!this.eligible()) return false;
        const elapsed = this.sampleTime ? now - this.sampleTime : 0,
          previous = new Map((this.records || []).map((item) => [`${item.processId}:${item.createTime}`, item]));
        for (const item of records) {
          const old = previous.get(`${item.processId}:${item.createTime}`),
            delta = old
              ? Number(BigInt(item.userTime) + BigInt(item.kernelTime) - BigInt(old.userTime) - BigInt(old.kernelTime))
              : 0;
          item.cpu = elapsed && this.processorCount ?
            Math.max(0, delta / (elapsed * 10000 * this.processorCount)) * 100 :
            null;
        }
        this.sampleTime = now;
        this.records = records.sort(
          (a, b) => collator.compare(a.imageName || "", b.imageName || "") || a.processId - b.processId,
        );
        this.render();
        return true;
      } catch (e) {
        this.empty.hidden = false;
        this.empty.textContent = e.message;
        return false;
      } finally {
        this.refreshing = null;
        this.update();
      }
    })();
    return this.refreshing;
  }
  render() {
    const filter = this.filter.value.toLocaleLowerCase(),
      selected = this.selected;
    let found = false;
    this.body.replaceChildren();
    for (const item of this.records || []) {
      if (
        filter &&
        !`${item.imageName} ${item.processId} ${item.userName} ${item.imagePath}`.toLocaleLowerCase().includes(filter)
      )
        continue;
      const row = document.createElement("tr"),
        values = [
          item.imageName || "System Idle Process",
          item.processId,
          processState(item),
          item.userName || "—",
          item.wslIdentity || item.cpu === null ? "—" : `${item.cpu.toFixed(1)}%`,
          item.wslIdentity ? "—" : `${bytes(item.workingSetBytes)} / ${bytes(item.privateBytes)}`,
          item.imagePath || "—",
          item.wslIdentity ? `WSL · ${item.wslDistribution}` : processPlatform(item.machineType),
        ];
      for (const value of values) {
        const cell = document.createElement("td");
        cell.textContent = value;
        cell.title = value;
        row.append(cell);
      }
      row.onclick = () => this.select(row, item);
      row.ondblclick = () => this.details();
      row.oncontextmenu = (event) => this.context(event, row, item);
      if (item.processId === selected?.processId && item.createTime === selected.createTime) {
        this.select(row, item);
        found = true;
      }
      this.body.append(row);
    }
    if (selected && !found) this.clearSelection();
    this.empty.hidden = this.body.children.length !== 0;
    this.empty.textContent = "没有匹配的进程";
    this.host.querySelector("[data-role=summary]").textContent = `${(this.records || []).length} 个进程`;
  }
  select(row, item) {
    this.body.querySelector(".selected")?.classList.remove("selected");
    row.classList.add("selected");
    this.selected = item;
  }
  context(event, row, item) {
    event.preventDefault();
    this.select(row, item);
    const system = item.processId === 0,
      wsl = !!item.wslIdentity,
      suspended = wsl ? "Tt".includes(item.wslState) : !!(item.flags & 1),
      efficiency = !!(item.flags & 2),
      virtualized = !!(item.flags & 8),
      priority = this.menu.querySelector("[data-role=priority-root]");
    for (const action of [
      "terminate",
      "terminate-tree",
      "suspend",
      "resume",
      "efficiency",
      "dump",
      "memory",
      "details",
    ])
      this.action(action, this.menu).disabled =
        system || (wsl && !["terminate", "suspend", "resume", "details"].includes(action));
    this.vmmapButton.disabled = system || wsl;
    this.action("modules", this.menu).disabled = wsl || item.processId === 0 || item.processId === 4;
    this.action("handles", this.menu).disabled = wsl || item.processId === 0;
    this.action("suspend", this.menu).disabled ||= suspended;
    this.action("resume", this.menu).disabled ||= !suspended;
    this.action("efficiency", this.menu).textContent = `${efficiency ? "✓ " : ""}效能模式`;
    priority.disabled = system || wsl;
    priority.parentElement.classList.toggle("disabled", system || wsl);
    this.action("virtualization", this.menu).disabled = system || wsl || !(item.flags & 4);
    this.action("virtualization", this.menu).textContent = `${virtualized ? "✓ " : ""}UAC 虚拟化`;
    this.action("file", this.menu).disabled = wsl || !item.imagePath;
    this.action("service", this.menu).disabled = wsl || !item.serviceNames?.length;
    for (const button of this.menu.querySelectorAll("[data-priority]")) {
      const priority = Number(button.dataset.priority);
      button.textContent = `${priority === item.priorityClass ? "✓ " : ""}${priorityName(priority)}`;
    }
    this.menu.hidden = false;
    this.menu.classList.toggle("open-left", event.clientX > innerWidth - 340);
    const rect = this.menu.getBoundingClientRect();
    this.menu.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - rect.width - 6))}px`;
    this.menu.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - rect.height - 6))}px`;
  }
  hideMenu() {
    this.menu.hidden = true;
  }
  invoke(action) {
    const item = this.selected;
    this.hideMenu();
    if (!item) return;
    if (action === "details") this.details();
    else if (action === "terminate")
      this.confirmControl(1, `确定结束进程“${item.imageName}” (PID ${item.processId})？未保存的数据将丢失。`);
    else if (action === "terminate-tree")
      this.confirmControl(2, `确定结束“${item.imageName}”及其所有子进程？未保存的数据将丢失。`);
    else if (action === "suspend") this.control(3);
    else if (action === "resume") this.control(4);
    else if (action === "efficiency") this.control(5, item.flags & 2 ? 0 : 1);
    else if (action === "virtualization") this.control(7, item.flags & 8 ? 0 : 1);
    else if (action === "dump") this.openDump();
    else if (action === "modules") this.openModules();
    else if (action === "handles") this.openHandles();
    else if (action === "memory") this.openMemory();
    else if (action === "file") this.revealFile?.(item.imagePath);
    else if (action === "service") this.revealServices?.(item.serviceNames);
  }
  confirmControl(control, message) {
    if (confirm(message)) this.control(control);
  }
  async control(control, value = 0) {
    const item = this.selected;
    if (!item) return;
    try {
      if (item.wslIdentity) {
        const action = { 1: 7, 3: 4, 4: 3 }[control];
        if (!action) return;
        await this.call("/api/wsl/process/control", { action, identity: item.wslIdentity });
      } else await this.call("/api/process/control", { ...this.identity(item), control, value });
      await this.refresh();
    } catch (e) {
      this.notify(e);
    }
  }
  async details() {
    if (!this.selected || this.selected.processId === 0) return;
    try {
      const p = this.selected.wslIdentity
          ? this.selected
          : await this.call("/api/process/info", this.identity()),
        dialog = this.host.querySelector("[data-role=details]"),
        fields = p.wslIdentity
          ? [
              ["进程 ID", p.processId],
              ["父进程 ID", p.parentProcessId],
              ["状态", processState(p)],
              ["用户", p.userName],
              ["平台", `WSL · ${p.wslDistribution}`],
              ["运行时间", t("process.uptimeSeconds", { value: p.wslElapsedSeconds })],
              ["命令", p.imagePath || "—"],
            ]
          : [
          ["进程 ID", p.processId],
          ["父进程 ID", p.parentProcessId],
          ["状态", processState(p)],
          ["用户名", p.userName || "—"],
          ["平台", processPlatform(p.machineType)],
          ["优先级", priorityName(p.priorityClass)],
          ["会话", p.sessionId],
          ["创建时间", date(p.createTime)],
          ["线程", p.threadCount],
          ["句柄", p.handleCount],
          ["工作集", bytes(p.workingSetBytes)],
          ["专用提交", bytes(p.privateBytes)],
          ["用户 CPU 时间", duration(p.userTime)],
          ["内核 CPU 时间", duration(p.kernelTime)],
          ["映像路径", detailString(p.imagePathStatus, p.imagePath)],
          ["命令行", detailString(p.commandLineStatus, p.commandLine)],
            ];
      this.host.querySelector("[data-role=details-title]").textContent = p.imageName || `PID ${p.processId}`;
      this.host.querySelector("[data-role=details-body]").replaceChildren(...details(fields));
      dialog.showModal();
    } catch (e) {
      this.notify(e);
    }
  }
  openMemoryMap() {
    const item = this.selected;
    if (!item || item.processId === 0) return;
    this.vmmapProcess = item;
    this.vmmapDialog.querySelector("[data-role=vmmap-title]").textContent =
      `${item.imageName} (PID ${item.processId}) — VMMap`;
    this.vmmapDialog.showModal();
    this.loadMemoryMap();
  }
  async loadMemoryMap() {
    const item = this.vmmapProcess;
    if (!item) return;
    await this.closeMemoryMap();
    const status = this.vmmapDialog.querySelector("[data-role=vmmap-status]"),
      body = this.vmmapDialog.querySelector("[data-role=vmmap-body]"),
      empty = this.vmmapDialog.querySelector("[data-role=vmmap-empty]");
    status.textContent = "正在分析虚拟地址空间…";
    body.replaceChildren();
    empty.hidden = true;
    try {
      this.memoryMap = await this.call("/api/process/memory/map", this.identity(item));
      this.memoryRegions = new Map();
      this.expandedAllocations = new Set();
      this.renderMemoryMap();
    } catch (error) {
      this.memoryMap = null;
      empty.hidden = false;
      empty.textContent = error.message;
      status.textContent = "读取失败";
      this.notify(error);
    }
  }
  async closeMemoryMap() {
    const id = this.memoryMap?.snapshotId;
    this.memoryMap = null;
    this.memoryRegions = null;
    this.expandedAllocations = null;
    if (id && this.isConnected())
      try {
        await this.call("/api/process/memory/map/close", { snapshotId: id });
      } catch {}
  }
  async toggleMemoryAllocation(index) {
    if (this.expandedAllocations.has(index)) {
      this.expandedAllocations.delete(index);
      this.renderMemoryMap();
      return;
    }
    if (!this.memoryRegions.has(index)) {
      const status = this.vmmapDialog.querySelector("[data-role=vmmap-status]");
      status.textContent = "正在读取分配区域…";
      try {
        this.memoryRegions.set(
          index,
          await this.call("/api/process/memory/map/regions", {
            snapshotId: this.memoryMap.snapshotId,
            allocationIndex: index,
          }),
        );
      } catch (error) {
        this.notify(error);
        status.textContent = "读取失败";
        return;
      }
    }
    this.expandedAllocations.add(index);
    this.renderMemoryMap();
  }
  memoryMapRow(values, className) {
    const row = document.createElement("tr");
    if (className) row.className = className;
    for (const value of values) {
      const cell = document.createElement("td");
      cell.textContent = value;
      cell.title = value;
      row.append(cell);
    }
    return row;
  }
  renderMemoryMap() {
    const allocations = this.memoryMap?.allocations || [],
      filter = this.vmmapDialog.querySelector("[data-role=vmmap-filter]").value,
      showFree = this.vmmapDialog.querySelector("[data-role=vmmap-free]").checked,
      body = this.vmmapDialog.querySelector("[data-role=vmmap-body]"),
      empty = this.vmmapDialog.querySelector("[data-role=vmmap-empty]"),
      summary = this.vmmapDialog.querySelector("[data-role=vmmap-summary]"),
      groups = new Map();
    body.replaceChildren();
    let visible = 0;
    allocations.forEach((allocation, index) => {
      const type = memoryType(allocation),
        free = type === "空闲";
      if (!groups.has(type)) groups.set(type, { size: 0n, commit: 0n, working: 0n, private: 0n, shared: 0n, count: 0 });
      const group = groups.get(type);
      group.size += BigInt(allocation.regionSize);
      group.commit += BigInt(allocation.commitSize);
      group.working += BigInt(allocation.workingSetBytes);
      group.private += BigInt(allocation.privateWorkingSetBytes);
      group.shared += BigInt(allocation.sharedWorkingSetBytes);
      group.count++;
      if ((free && !showFree) || (filter && type !== filter)) return;
      visible++;
      const expanded = this.expandedAllocations?.has(index),
        mapped = allocation.type === 0x1000000 || allocation.type === 0x40000,
        working =
          allocation.workingSetStatus < 0 ? ntstatus(allocation.workingSetStatus) : bytes(allocation.workingSetBytes),
        attributes = free
          ? "—"
          : allocation.regionStatus < 0
            ? ntstatus(allocation.regionStatus)
            : memoryRegionFlags(allocation.regionType),
        path = !mapped
          ? "—"
          : allocation.mappedPathStatus < 0
            ? ntstatus(allocation.mappedPathStatus)
            : allocation.mappedPath || "—",
        row = this.memoryMapRow(
          [
            `${expanded ? "▾" : "▸"} 0x${BigInt(allocation.allocationBase)
              .toString(16)
              .toUpperCase()
              .padStart(16, "0")}`,
            bytes(allocation.regionSize),
            bytes(allocation.commitSize),
            working,
            allocation.workingSetStatus < 0 ? "—" : bytes(allocation.privateWorkingSetBytes),
            allocation.workingSetStatus < 0 ? "—" : bytes(allocation.sharedWorkingSetBytes),
            free ? "空闲" : "分配",
            type,
            allocation.priority,
            memoryProtection(allocation.allocationProtect),
            attributes,
            path,
          ],
          "vmmap-allocation",
        );
      row.onclick = () => this.toggleMemoryAllocation(index);
      body.append(row);
      if (expanded)
        for (const region of this.memoryRegions.get(index) || []) {
          const committed = region.state === 0x1000,
            child = this.memoryMapRow(
              [
                `    0x${BigInt(region.baseAddress).toString(16).toUpperCase().padStart(16, "0")}`,
                bytes(region.regionSize),
                bytes(region.commitSize),
                !committed
                  ? "—"
                  : region.workingSetStatus < 0
                    ? ntstatus(region.workingSetStatus)
                    : bytes(region.workingSetBytes),
                !committed || region.workingSetStatus < 0 ? "—" : bytes(region.privateWorkingSetBytes),
                !committed || region.workingSetStatus < 0 ? "—" : bytes(region.sharedWorkingSetBytes),
                memoryState(region.state),
                type,
                !committed || region.workingSetStatus < 0 ? "—" : region.priority,
                memoryProtection(region.protect),
                "—",
                "—",
              ],
              "vmmap-region",
            );
          body.append(child);
        }
    });
    summary.replaceChildren(
      ...[...groups]
        .filter(([name]) => name !== "空闲")
        .map(([name, value]) => {
          const card = document.createElement("div");
          card.innerHTML = "<strong></strong><span></span><small></small>";
          card.children[0].textContent = name;
          card.children[1].textContent = `提交 ${bytes(value.commit)} · 工作集 ${bytes(value.working)}`;
          card.children[2].textContent =
            `专用 ${bytes(value.private)} · 共享 ${bytes(value.shared)} · ` +
            `${value.count} 个分配 / ${bytes(value.size)}`;
          return card;
        }),
    );
    empty.hidden = visible !== 0;
    empty.textContent = "没有匹配的内存分配";
    this.vmmapDialog.querySelector("[data-role=vmmap-status]").textContent =
      `${visible} / ${allocations.length} 个分配`;
  }
  async exportMemoryMap() {
    if (!this.memoryMap) return;
    const button = this.action("vmmap-export", this.vmmapDialog);
    button.disabled = true;
    button.textContent = "正在读取…";
    try {
      const records = [];
      for (let index = 0; index < this.memoryMap.allocations.length; index++) {
        let regions = this.memoryRegions.get(index);
        if (!regions) {
          regions = await this.call("/api/process/memory/map/regions", {
            snapshotId: this.memoryMap.snapshotId,
            allocationIndex: index,
          });
          this.memoryRegions.set(index, regions);
        }
        const allocation = this.memoryMap.allocations[index];
        for (const region of regions)
          records.push({
            ...region,
            allocationBase: allocation.allocationBase,
            type: allocation.type,
            regionType: allocation.regionType,
            mappedPath: allocation.mappedPath,
          });
      }
      const columns = [
          ["基址", (r) => `0x${BigInt(r.baseAddress).toString(16).toUpperCase()}`],
          ["分配基址", (r) => `0x${BigInt(r.allocationBase).toString(16).toUpperCase()}`],
          ["大小", (r) => r.regionSize],
          ["已提交", (r) => r.commitSize],
          ["工作集", (r) => r.workingSetBytes],
          ["专用工作集", (r) => r.privateWorkingSetBytes],
          ["共享工作集", (r) => r.sharedWorkingSetBytes],
          ["可共享工作集", (r) => r.shareableWorkingSetBytes],
          ["锁定工作集", (r) => r.lockedWorkingSetBytes],
          ["原始共享页", (r) => r.sharedOriginalBytes],
          ["状态", (r) => memoryState(r.state)],
          ["类型", (r) => memoryType(r)],
          ["优先级", (r) => r.priority],
          ["保护", (r) => memoryProtection(r.protect)],
          ["区域属性", (r) => memoryRegionFlags(r.regionType)],
          ["映射文件", (r) => r.mappedPath],
        ],
        quote = (value) => `"${String(value ?? "").replaceAll('"', '""')}"`,
        text = [
          columns.map(([name]) => quote(name)).join(","),
          ...records.map((record) => columns.map(([, get]) => quote(get(record))).join(",")),
        ].join("\r\n"),
        url = URL.createObjectURL(new Blob(["\ufeff", text], { type: "text/csv;charset=utf-8" })),
        link = document.createElement("a");
      link.href = url;
      link.download = `${this.vmmapProcess?.imageName || "process"}-${this.vmmapProcess?.processId || 0}-vmmap.csv`;
      link.click();
      URL.revokeObjectURL(url);
    } catch (error) {
      this.notify(error);
    } finally {
      button.disabled = false;
      button.textContent = "导出 CSV";
    }
  }
  openModules() {
    const item = this.selected;
    if (!item || item.processId === 0 || item.processId === 4) return;
    this.moduleProcess = item;
    this.moduleDialog.querySelector("[data-role=module-title]").textContent =
      `${item.imageName} (PID ${item.processId}) — DLL 列表`;
    this.moduleDialog.showModal();
    this.loadModules();
  }
  async loadModules() {
    const item = this.moduleProcess;
    if (!item) return;
    const empty = this.moduleDialog.querySelector("[data-role=module-empty]"),
      summary = this.moduleDialog.querySelector("[data-role=module-summary]");
    this.modules = null;
    this.moduleBody.replaceChildren();
    empty.hidden = false;
    empty.textContent = "正在读取 Loader 模块…";
    summary.textContent = "";
    try {
      this.modules = await this.call("/api/process/modules", this.identity(item));
      this.renderModules();
    } catch (error) {
      empty.textContent = error.message;
      this.notify(error);
    }
  }
  renderModules() {
    if (!this.modules) return;
    const query = this.moduleDialog.querySelector("[data-role=module-filter]").value.toLocaleLowerCase(),
      width = this.modules.machineBits === 32 ? 8 : 16,
      records = this.modules.modules.filter(
        (module) =>
          !query ||
          `${module.path} ${module.mainImage ? t("common.mainImage") : "DLL"}`.toLocaleLowerCase().includes(query),
      ),
      empty = this.moduleDialog.querySelector("[data-role=module-empty]");
    this.moduleBody.replaceChildren(
      ...records.map((module) => {
        const row = document.createElement("tr"),
          name = module.path.split(/[\\/]/).pop() || "(无名称)",
          values = [
            name,
            module.mainImage ? t("common.mainImage") : "DLL",
            `0x${BigInt(module.baseAddress).toString(16).toUpperCase().padStart(width, "0")}`,
            bytes(module.sizeOfImage),
            `0x${BigInt(module.entryPoint).toString(16).toUpperCase().padStart(width, "0")}`,
            moduleLoadReason(module.loadReason),
            module.loadTime ? date(module.loadTime) : "—",
            module.path || "—",
          ];
        for (const value of values) {
          const cell = document.createElement("td");
          cell.textContent = value;
          cell.title = value;
          row.append(cell);
        }
        row.ondblclick = () => {
          if (module.path) {
            this.moduleDialog.close();
            this.revealFile?.(module.path);
          }
        };
        row.oncontextmenu = (event) => this.moduleContext(event, row, module);
        return row;
      }),
    );
    empty.hidden = records.length !== 0;
    empty.textContent = "没有匹配的 DLL";
    const platform = processPlatform(this.modules.machineType);
    this.moduleDialog.querySelector("[data-role=module-summary]").textContent =
      `${platform} · ${this.modules.machineBits} 位 · ${this.modules.modules.length} 个模块`;
  }
  moduleContext(event, row, module) {
    event.preventDefault();
    this.moduleBody.querySelector(".selected")?.classList.remove("selected");
    row.classList.add("selected");
    this.selectedModule = module;
    this.action("module-file", this.moduleMenu).disabled = !module.path;
    this.moduleMenu.hidden = false;
    const rect = this.moduleMenu.getBoundingClientRect();
    this.moduleMenu.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - rect.width - 6))}px`;
    this.moduleMenu.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - rect.height - 6))}px`;
  }
  openHandles() {
    const item = this.selected;
    if (!item || item.processId === 0) return;
    this.handleProcess = item;
    this.handleDialog.querySelector("[data-role=handle-title]").textContent = t("process.handleTableTitle", {
      name: item.imageName,
      pid: item.processId,
    });
    this.handleDialog.showModal();
    this.loadHandles();
  }
  async loadHandles() {
    const item = this.handleProcess;
    if (!item) return;
    const empty = this.handleDialog.querySelector("[data-role=handle-empty]"),
      summary = this.handleDialog.querySelector("[data-role=handle-summary]");
    this.handles = null;
    this.handleBody.replaceChildren();
    empty.hidden = false;
    empty.textContent = t("process.loadingHandles");
    summary.textContent = "";
    try {
      this.handles = await this.call("/api/process/handles", this.identity(item));
      this.renderHandles();
    } catch (error) {
      empty.textContent = error.message;
      this.notify(error);
    }
  }
  renderHandles() {
    if (!this.handles) return;
    const query = this.handleDialog.querySelector("[data-role=handle-filter]").value.toLocaleLowerCase(),
      records = this.handles.filter((handle) => {
        const value = `0x${BigInt(handle.handleValue).toString(16).toUpperCase()}`;
        return !query || `${value} ${handle.typeName} ${handle.objectName}`.toLocaleLowerCase().includes(query);
      }),
      empty = this.handleDialog.querySelector("[data-role=handle-empty]");
    this.handleBody.replaceChildren(
      ...records.map((handle) => {
        const row = document.createElement("tr"),
          values = [
            `0x${BigInt(handle.handleValue).toString(16).toUpperCase()}`,
            handle.typeName || "—",
            handle.objectName || "—",
          ];
        for (const value of values) {
          const cell = document.createElement("td");
          cell.textContent = value;
          cell.title = value;
          row.append(cell);
        }
        return row;
      }),
    );
    empty.hidden = records.length !== 0;
    empty.textContent = t("process.noMatchingHandles");
    this.handleDialog.querySelector("[data-role=handle-summary]").textContent = t("process.handleCount", {
      value: this.handles.length,
    });
  }
  async openMemory() {
    const item = this.selected;
    if (!item) return;
    try {
      const info = await this.call("/api/process/info", this.identity(item));
      if (info.imageBaseStatus < 0)
        throw new Error(
          `无法读取主模块基址，NTSTATUS: 0x${(info.imageBaseStatus >>> 0).toString(16).toUpperCase().padStart(8, "0")}`,
        );
      const address = BigInt(info.imageBase),
        length = 1024,
        identity = this.identity(item);
      this.hexEditor.open({
        title: `${item.imageName} (PID ${item.processId}) — 0x${address.toString(16).toUpperCase()}`,
        size: length,
        read: async (offset, count) => {
          const result = await postBinary("/api/process/memory/read", {
            ...identity,
            address: (address + BigInt(offset)).toString(),
            length: count,
          });
          return { size: length.toString(), data: result.data };
        },
        write: (offset, data) =>
          postData(
            "/api/process/memory/write",
            { ...identity, address: (address + BigInt(offset)).toString() },
            data,
          ),
      });
    } catch (error) {
      this.notify(error);
    }
  }
  openDump() {
    this.dumpDialog.querySelector("[data-role=dump-options]").disabled = false;
    this.action("dump-start").disabled = false;
    this.dumpDialog.querySelector("[data-role=dump-status]").textContent = "选择要包含的内容。";
    this.dumpDialog.querySelector("[data-role=dump-progress]").hidden = true;
    this.dumpDialog.showModal();
  }
  async createDump() {
    const item = this.selected;
    if (!item) return;
    const options = this.dumpDialog.querySelector("[data-role=dump-options]"),
      status = this.dumpDialog.querySelector("[data-role=dump-status]"),
      progress = this.dumpDialog.querySelector("[data-role=dump-progress]"),
      dumpType = [...options.querySelectorAll("input:checked")].reduce(
        (value, input) => value | Number(input.value),
        0,
      ),
      controller = new AbortController();
    this.dumpAbort = controller;
    options.disabled = this.action("dump-start").disabled = true;
    progress.hidden = false;
    progress.removeAttribute("value");
    status.textContent = "正在创建远端转储…";
    try {
      const response = await fetch(apiUrl("/api/process/dump"), {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ ...this.identity(item), dumpType }),
        signal: controller.signal,
      });
      if (!response.ok) throw new Error(await response.text());
      const total = Number(response.headers.get("content-length")) || 0,
        reader = response.body.getReader(),
        chunks = [];
      let received = 0;
      status.textContent = "正在下载…";
      if (total) {
        progress.max = total;
        progress.value = 0;
      }
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(value);
        received += value.byteLength;
        if (total) {
          progress.value = received;
          status.textContent = `正在下载… ${Math.floor((received / total) * 100)}%`;
        }
      }
      const blob = new Blob(chunks, { type: "application/octet-stream" }),
        link = document.createElement("a");
      link.href = URL.createObjectURL(blob);
      link.download = `${item.imageName || "process"}-${item.processId}.dmp`;
      link.click();
      setTimeout(() => URL.revokeObjectURL(link.href), 0);
      status.textContent = `已下载 ${bytes(received)}`;
      progress.hidden = true;
    } catch (e) {
      if (e.name !== "AbortError") {
        status.textContent = e.message;
        this.notify(e);
      }
    } finally {
      this.dumpAbort = null;
      options.disabled = this.action("dump-start").disabled = false;
    }
  }
}

export class ServiceManager {
  constructor(host, { call, notify, filePicker }) {
    this.host = host;
    this.call = call;
    this.notify = notify;
    this.filePicker = filePicker;
    this.modified = new Set();
    host.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选服务" /><span class="spacer"></span
        ><label><input data-role="user-services" type="checkbox" checked />${t("service.userMode")}</label
        ><label><input data-role="drivers" type="checkbox" checked />${t("service.drivers")}</label>
      </div>
      <div class="manager-table">
        <table class="service-table">
          <thead>
            <tr>
              <th>服务名称</th>
              <th>PID</th>
              <th>显示名称</th>
              <th>描述</th>
              <th>类型</th>
              <th>状态</th>
              <th>启动类型</th>
              <th>登录为</th>
            </tr>
          </thead>
          <tbody></tbody>
        </table>
        <div class="manager-empty">进入页面后读取远端服务</div>
      </div>
      <div class="context-menu service-menu" data-role="menu" hidden>
        <button data-action="start">启动</button><button data-action="stop">停止</button
        ><button data-action="pause">暂停</button><button data-action="continue">恢复</button
        ><button data-action="restart">重新启动</button>
        <hr />
        <button data-action="refresh">刷新</button>
        <hr />
        <button data-action="details">属性</button>
      </div>
      <dialog data-role="details" class="service-properties">
        <div class="service-properties-title">
          <strong data-role="details-title"></strong><button data-action="close" title="关闭">×</button>
        </div>
        <nav class="property-tabs">
          <button data-tab="general" class="active">常规</button><button data-tab="logon">登录</button
          ><button data-tab="recovery">恢复</button><button data-tab="dependencies">依存关系</button>
        </nav>
        <section data-panel="general" class="property-panel service-general">
          <label>服务名称<input data-field="serviceName" readonly /></label
          ><label>显示名称<input data-field="displayName" data-config="general" required /></label
          ><label>描述<textarea data-field="description" data-config="general" rows="3"></textarea></label
          ><label>可执行文件的路径<input data-field="binaryPathName" data-config="general" required /></label
          ><label data-role="dll-row">服务 DLL<input data-field="serviceDll" readonly /></label>
          <div class="service-grid">
            <label>启动类型<select data-field="startType" data-config="general"></select></label
            ><label>类型<input data-field="serviceType" readonly /></label
            ><label>进程 ID<input data-field="processId" readonly /></label>
          </div>
          <label>加载顺序组<input data-field="loadOrderGroup" data-config="general" /></label>
          <div class="service-status">
            <span>服务状态：<strong data-field="status"></strong></span><button data-control="1">启动</button
            ><button data-control="2">停止</button><button data-control="3">暂停</button
            ><button data-control="4">恢复</button>
          </div>
          <label>启动参数<input data-field="argument" /></label>
        </section>
        <section data-panel="logon" class="property-panel" hidden>
          <label class="property-choice"
            ><input
              type="radio"
              name="service-account"
              data-field="localSystem"
              data-config="account"
            />本地系统账户</label
          ><label class="property-choice"
            ><input type="radio" name="service-account" data-field="thisAccount" data-config="account" />此账户</label
          ><label>账户名称<input data-field="startName" data-config="account" /></label>
          <div class="service-grid">
            <label
              >密码<input
                type="password"
                data-field="password"
                data-config="account"
                autocomplete="new-password" /></label
            ><label
              >确认密码<input
                type="password"
                data-field="passwordConfirm"
                data-config="account"
                autocomplete="new-password"
            /></label>
          </div>
          <p class="property-note">留空密码表示不修改现有密码；系统不会读取或显示当前密码。</p>
        </section>
        <section data-panel="recovery" class="property-panel" hidden>
          <p data-role="recovery-unavailable" class="property-note" hidden>此服务不支持恢复操作。</p>
          <div data-role="recovery-options">
            <div class="service-grid">
              <label>第一次失败<select data-field="firstFailureAction" data-config="recovery"></select></label
              ><label>第二次失败<select data-field="secondFailureAction" data-config="recovery"></select></label
              ><label>第三次失败<select data-field="thirdFailureAction" data-config="recovery"></select></label
              ><label>后续失败<select data-field="subsequentFailureAction" data-config="recovery"></select></label
              ><label
                >在此时间后重置失败计数（天）<input
                  type="number"
                  min="0"
                  step="any"
                  data-field="resetPeriodDays"
                  data-config="recovery" /></label
              ><label
                >在此时间后重新启动服务（分钟）<input
                  type="number"
                  min="0"
                  step="any"
                  data-field="restartDelayMinutes"
                  data-config="recovery" /></label
              ><label
                >在此时间后重新启动计算机（分钟）<input
                  type="number"
                  min="0"
                  step="any"
                  data-field="rebootDelayMinutes"
                  data-config="recovery" /></label
              ><label
                >错误控制<select data-field="errorControl" data-config="recovery">
                  <option value="0">忽略</option>
                  <option value="1">正常</option>
                  <option value="2">严重</option>
                  <option value="3">关键</option>
                </select></label
              >
            </div>
            <label>运行程序<input data-field="recoveryCommand" data-config="recovery" /></label
            ><label
              >重新启动计算机前发送的消息<textarea
                data-field="rebootMessage"
                data-config="recovery"
                rows="3"
              ></textarea></label
            ><label class="property-choice"
              ><input
                type="checkbox"
                data-field="failureActionsOnNonCrashFailures"
                data-config="recovery"
              />对非崩溃失败也启用操作</label
            >
            <p data-role="recovery-extra" class="property-note" hidden>
              此服务配置了四项以上的恢复操作；保存后将统一为“后续失败”操作。
            </p>
          </div>
        </section>
        <section data-panel="dependencies" class="property-panel" hidden>
          <h3>此服务依赖以下系统组件</h3>
          <ul data-role="dependencies"></ul>
          <h3>以下服务依赖此服务</h3>
          <ul data-role="dependents"></ul>
        </section>
        <div class="dialog-actions">
          <button data-action="ok">确定</button><button data-action="cancel">取消</button
          ><button data-action="apply" disabled>应用</button>
        </div>
      </dialog>`;
    this.body = host.querySelector("tbody");
    this.empty = host.querySelector(".manager-empty");
    this.filter = host.querySelector("[data-role=filter]");
    this.userServices = host.querySelector("[data-role=user-services]");
    this.drivers = host.querySelector("[data-role=drivers]");
    this.menu = host.querySelector("[data-role=menu]");
    this.dialog = host.querySelector("[data-role=details]");
    this.dialog.onclose = () => {
      this.dialog.querySelector("[data-field=password]").value = this.dialog.querySelector(
        "[data-field=passwordConfirm]",
      ).value = "";
    };
    this.filter.oninput = () => this.render();
    for (const filter of [this.userServices, this.drivers])
      filter.onchange = () => {
        if (!this.userServices.checked && !this.drivers.checked) filter.checked = true;
        this.render();
      };
    for (const button of host.querySelectorAll(".service-menu [data-action]"))
      button.onclick = () => this.invoke(button.dataset.action);
    for (const button of this.dialog.querySelectorAll("[data-tab]"))
      button.onclick = () => this.tab(button.dataset.tab);
    for (const button of this.dialog.querySelectorAll("[data-control]"))
      button.onclick = () => this.control(Number(button.dataset.control));
    for (const field of this.dialog.querySelectorAll("[data-config]"))
      field.oninput = () => {
        field.dataset.changed = "1";
        this.dirty(field.dataset.config);
        this.updatePropertyOptions();
      };
    for (const name of ["binaryPathName", "recoveryCommand"]) this.addPathPicker(name);
    this.action("close", this.dialog).onclick = () => this.dialog.close();
    this.action("cancel", this.dialog).onclick = () => this.dialog.close();
    this.action("apply", this.dialog).onclick = () => this.apply();
    this.action("ok", this.dialog).onclick = async () => {
      if (await this.apply()) this.dialog.close();
    };
    addEventListener("click", (event) => {
      if (!this.menu.contains(event.target)) this.menu.hidden = true;
    });
    addEventListener("blur", () => (this.menu.hidden = true));
  }
  action(name, root = this.host) {
    return root.querySelector(`[data-action="${name}"]`);
  }
  addPathPicker(name) {
    if (!this.filePicker) return;
    const field = this.dialog.querySelector(`[data-field="${name}"]`),
      button = document.createElement("button"),
      wrapper = document.createElement("span");
    wrapper.className = "remote-path-field";
    button.type = "button";
    button.textContent = "选择…";
    button.onclick = async () => {
      const value = await this.filePicker.open({ mode: "file", initialPath: parent(commandPath(field.value)) || "" });
      if (!value) return;
      field.value = value.includes(" ") ? `"${value}"` : value;
      field.dispatchEvent(new Event("input"));
    };
    field.replaceWith(wrapper);
    wrapper.append(field, button);
  }
  activate(connected) {
    if (connected && !this.loaded) this.load();
  }
  clearSelection() {
    this.selected = null;
    this.updateActions();
  }
  disconnect() {
    this.loaded = false;
    this.records = [];
    this.menu.hidden = true;
    this.dialog.close();
    this.clearSelection();
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  async load() {
    if (this.loading) return this.loading;
    this.empty.hidden = false;
    this.empty.textContent = "正在读取服务…";
    this.loading = (async () => {
      try {
        this.records = await this.call("/api/services");
        this.loaded = true;
        this.render();
      } catch (e) {
        this.empty.hidden = false;
        this.empty.textContent = e.message;
        this.notify(e);
      }
    })();
    try {
      return await this.loading;
    } finally {
      this.loading = null;
    }
  }
  render() {
    const filter = this.filter.value.toLocaleLowerCase(),
      selected = this.selected;
    let found = false;
    this.body.replaceChildren();
    for (const item of this.records || []) {
      const driver = (item.serviceType & 0xf) !== 0;
      if (driver ? !this.drivers.checked : !this.userServices.checked) continue;
      if (
        filter &&
        !`${item.serviceName} ${item.processId} ${item.displayName} ${item.description} ${item.startName}`
          .toLocaleLowerCase()
          .includes(filter)
      )
        continue;
      const row = document.createElement("tr"),
        values = [
          item.serviceName,
          item.processId || "",
          item.displayName,
          item.description,
          serviceType(item.serviceType),
          state(item.currentState),
          startType(item.startType),
          item.startName,
        ];
      for (const value of values) {
        const cell = document.createElement("td");
        cell.textContent = value;
        cell.title = value;
        row.append(cell);
      }
      row.onclick = () => this.select(row, item);
      row.ondblclick = () => this.details();
      row.oncontextmenu = (event) => this.context(event, row, item);
      if (item.serviceName === selected?.serviceName) {
        this.select(row, item);
        found = true;
      }
      this.body.append(row);
    }
    if (selected && !found) this.clearSelection();
    this.empty.hidden = this.body.children.length !== 0;
    this.empty.textContent = "没有匹配的服务";
  }
  async revealProcess(processId) {
    if (!this.loaded) await this.load();
    const item = this.records.find((value) => value.processId === processId);
    this.reveal(item, `未找到 PID ${processId} 对应的服务`);
  }
  async revealServices(serviceNames) {
    if (!this.loaded) await this.load();
    const names = new Set(serviceNames.map((value) => value.toLocaleLowerCase())),
      item = this.records.find((value) => names.has(value.serviceName.toLocaleLowerCase()));
    this.reveal(item, "未找到关联服务");
  }
  async revealService(serviceName) {
    if (!this.loaded) await this.load();
    const item = this.records.find(
      (value) => value.serviceName.toLocaleLowerCase() === serviceName.toLocaleLowerCase(),
    );
    this.reveal(item, `未找到服务 ${serviceName}`);
  }
  reveal(item, missing) {
    this.filter.value = "";
    this.userServices.checked = this.drivers.checked = true;
    this.selected = item || null;
    this.render();
    if (item) revealTableRow(this.body.querySelector(".selected"));
    else this.notify(missing);
  }
  select(row, item) {
    this.body.querySelector(".selected")?.classList.remove("selected");
    row.classList.add("selected");
    this.selected = item;
    this.updateActions();
  }
  updateActions() {
    const s = this.selected,
      accepted = s?.controlsAccepted || 0,
      stopped = s?.currentState === 1,
      running = s?.currentState === 4,
      paused = s?.currentState === 7,
      disabled = s?.startType === 4,
      values = {
        details: !s,
        start: !stopped || disabled,
        stop: !s || stopped || !(accepted & 1),
        pause: !running || !(accepted & 2),
        continue: !paused || !(accepted & 2),
        restart: !s || (!running && !paused) || disabled || !(accepted & 1),
      };
    for (const [name, value] of Object.entries(values)) this.action(name, this.menu).disabled = value;
  }
  context(event, row, item) {
    event.preventDefault();
    this.select(row, item);
    this.menu.hidden = false;
    const rect = this.menu.getBoundingClientRect();
    this.menu.style.left = `${Math.min(event.clientX, innerWidth - rect.width - 6)}px`;
    this.menu.style.top = `${Math.min(event.clientY, innerHeight - rect.height - 6)}px`;
  }
  invoke(action) {
    this.menu.hidden = true;
    if (action === "refresh") this.load();
    else if (action === "details") this.details();
    else this.control({ start: 1, stop: 2, pause: 3, continue: 4, restart: 5 }[action]);
  }
  async details() {
    if (!this.selected) return;
    try {
      this.info = await this.call("/api/service/info", { serviceName: this.selected.serviceName });
      const driver = (this.info.serviceType & 0xf) !== 0;
      for (const name of ["logon", "recovery"]) this.dialog.querySelector(`[data-tab="${name}"]`).hidden = driver;
      this.fillProperties();
      if (!this.dialog.open) this.dialog.showModal();
    } catch (e) {
      this.notify(e);
    }
  }
  fillProperties() {
    const s = this.info,
      field = (name) => this.dialog.querySelector(`[data-field="${name}"]`),
      driver = (s.serviceType & 0xf) !== 0,
      instance = (s.serviceType & 0x80) !== 0,
      duration = (value, unit) => Math.round((value / unit) * 1000) / 1000;
    this.dialog.querySelector("[data-role=details-title]").textContent = `${s.displayName || s.serviceName} 的属性`;
    field("serviceName").value = s.serviceName;
    field("displayName").value = s.displayName;
    field("description").value = s.description;
    field("binaryPathName").value = s.binaryPathName;
    field("serviceDll").value = s.serviceDll;
    this.dialog.querySelector("[data-role=dll-row]").hidden = !s.serviceDll;
    field("loadOrderGroup").value = s.loadOrderGroup;
    field("serviceType").value = serviceType(s.serviceType);
    field("processId").value = s.processId || "—";
    field("status").textContent = state(s.currentState);
    field("argument").value = "";
    const start = field("startType"),
      startOptions = driver
        ? [
            [0, "引导"],
            [1, "系统"],
            [2, "自动"],
            [3, "手动"],
            [4, "已禁用"],
          ]
        : [
            [2, "自动"],
            ["2d", "自动（延迟启动）"],
            [3, "手动"],
            [4, "已禁用"],
          ];
    start.replaceChildren(...startOptions.map(([value, text]) => new Option(text, value)));
    start.value = s.startType === 2 && s.delayedAutoStart ? "2d" : String(s.startType);
    field("localSystem").checked = s.startName.toLocaleLowerCase() === "localsystem";
    field("thisAccount").checked = !field("localSystem").checked;
    field("startName").value = s.startName;
    field("password").value = "";
    field("passwordConfirm").value = "";
    this.dialog.querySelector("[data-panel=logon] .property-note").textContent = driver
      ? "驱动程序不使用服务登录账户。"
      : "留空密码表示不修改现有密码；系统不会读取或显示当前密码。";
    const actionOptions = [
      [0, "不执行操作"],
      [1, "重新启动服务"],
      [2, "重新启动计算机"],
      [3, "运行程序"],
      [4, "仅重新启动此服务"],
    ];
    for (const name of ["firstFailureAction", "secondFailureAction", "thirdFailureAction", "subsequentFailureAction"])
      field(name).replaceChildren(...actionOptions.map(([value, text]) => new Option(text, value)));
    field("firstFailureAction").value = String(s.firstFailureAction);
    field("secondFailureAction").value = String(s.secondFailureAction);
    field("thirdFailureAction").value = String(s.thirdFailureAction);
    field("subsequentFailureAction").value = String(s.subsequentFailureAction);
    field("resetPeriodDays").value = Math.floor(s.resetPeriodSeconds / 86400);
    field("restartDelayMinutes").value = duration(s.restartDelayMilliseconds, 60000);
    field("rebootDelayMinutes").value = duration(s.rebootDelayMilliseconds, 60000);
    field("errorControl").value = String(s.errorControl);
    field("recoveryCommand").value = s.recoveryCommand;
    field("rebootMessage").value = s.rebootMessage;
    field("failureActionsOnNonCrashFailures").checked = s.failureActionsOnNonCrashFailures;
    this.dialog.querySelector("[data-role=recovery-unavailable]").hidden = s.recoverySupported;
    this.dialog.querySelector("[data-role=recovery-options]").hidden = !s.recoverySupported;
    this.dialog.querySelector("[data-role=recovery-extra]").hidden = s.recoveryActionCount <= 4;
    for (const input of this.dialog.querySelectorAll("[data-config]")) {
      input.disabled = instance || (driver && input.dataset.config === "account");
      delete input.dataset.changed;
    }
    this.dependencies("dependencies", s.dependencies);
    this.dependencies("dependents", s.dependents);
    this.tab("general");
    this.updatePropertyControls();
    this.updatePropertyOptions();
    this.dirty();
  }
  dependencies(role, names) {
    const list = this.dialog.querySelector(`[data-role="${role}"]`);
    list.classList.toggle("empty", names.length === 0);
    if (names.length === 0) {
      list.textContent = "无";
      return;
    }
    list.replaceChildren(
      ...names.map((name) => {
        const item = document.createElement("li"),
          service = this.records.find((value) => value.serviceName.toLocaleLowerCase() === name.toLocaleLowerCase());
        item.textContent = service && service.displayName !== name ? `${service.displayName} (${name})` : name;
        return item;
      }),
    );
  }
  tab(name) {
    for (const button of this.dialog.querySelectorAll("[data-tab]"))
      button.classList.toggle("active", button.dataset.tab === name);
    for (const panel of this.dialog.querySelectorAll("[data-panel]")) panel.hidden = panel.dataset.panel !== name;
  }
  dirty(group) {
    if (group === undefined) this.modified = new Set();
    else if (group) this.modified.add(group);
    this.action("apply", this.dialog).disabled = this.modified.size === 0 || (this.info.serviceType & 0x80) !== 0;
  }
  updatePropertyOptions() {
    const field = (name) => this.dialog.querySelector(`[data-field="${name}"]`),
      instance = (this.info.serviceType & 0x80) !== 0,
      driver = (this.info.serviceType & 0xf) !== 0,
      account = field("thisAccount").checked && !instance && !driver;
    for (const name of ["startName", "password", "passwordConfirm"]) field(name).disabled = !account;
    const actions = ["firstFailureAction", "secondFailureAction", "thirdFailureAction", "subsequentFailureAction"].map(
        (name) => Number(field(name).value),
      ),
      recovery = this.info.recoverySupported && !instance;
    field("restartDelayMinutes").disabled = !recovery || !actions.some((value) => value === 1 || value === 4);
    field("rebootDelayMinutes").disabled = !recovery || !actions.includes(2);
    field("rebootMessage").disabled = !recovery || !actions.includes(2);
    field("recoveryCommand").disabled = !recovery || !actions.includes(3);
  }
  updatePropertyControls() {
    const s = this.info,
      accepted = s.controlsAccepted || 0,
      buttons = this.dialog.querySelectorAll("[data-control]");
    for (const button of buttons)
      button.disabled =
        button.dataset.control == 1
          ? s.currentState !== 1 || s.startType === 4
          : button.dataset.control == 2
            ? s.currentState === 1 || !(accepted & 1)
            : button.dataset.control == 3
              ? s.currentState !== 4 || !(accepted & 2)
              : s.currentState !== 7 || !(accepted & 2);
  }
  async control(control) {
    const s = this.selected;
    if (!s || !control) return;
    const verbs = { 1: "启动", 2: "停止", 3: "暂停", 4: "恢复", 5: "重新启动" },
      danger = control === 2 || control === 5;
    if (danger && !confirm(`确定${verbs[control]}服务“${s.displayName || s.serviceName}”？`)) return;
    try {
      const argument =
        control === 1 && this.dialog.open ? this.dialog.querySelector("[data-field=argument]").value : null;
      await this.call("/api/service/control", { serviceName: s.serviceName, control, argument });
      await this.load();
      if (this.dialog.open) await this.details();
    } catch (e) {
      this.notify(e);
    }
  }
  async apply() {
    if (this.modified.size === 0) return true;
    const field = (name) => this.dialog.querySelector(`[data-field="${name}"]`),
      pending = new Set(this.modified);
    try {
      if (pending.has("general")) {
        const displayName = field("displayName").value.trim(),
          binaryPathName = field("binaryPathName").value.trim(),
          start = field("startType").value;
        if (!displayName || !binaryPathName) {
          this.notify("显示名称和可执行文件路径不能为空");
          return false;
        }
        await this.call("/api/service/configure", {
          serviceName: this.info.serviceName,
          startType: Number.parseInt(start, 10),
          delayedAutoStart: start === "2d",
          displayName,
          description: field("description").value,
          binaryPathName,
          loadOrderGroup: field("loadOrderGroup").value,
        });
        this.modified.delete("general");
      }
      if (pending.has("recovery")) {
        const scale = (name, factor, original) => {
          if (!field(name).dataset.changed) return original;
          const value = Number(field(name).value) * factor;
          if (!Number.isInteger(value) || value < 0 || value > 0xffffffff)
            throw new Error(`${field(name).parentElement.firstChild.textContent.trim()}无效`);
          return value;
        };
        await this.call("/api/service/configure-recovery", {
          serviceName: this.info.serviceName,
          errorControl: Number(field("errorControl").value),
          failureActionsOnNonCrashFailures: field("failureActionsOnNonCrashFailures").checked,
          resetPeriodSeconds: scale("resetPeriodDays", 86400, this.info.resetPeriodSeconds),
          restartDelayMilliseconds: scale("restartDelayMinutes", 60000, this.info.restartDelayMilliseconds),
          rebootDelayMilliseconds: scale("rebootDelayMinutes", 60000, this.info.rebootDelayMilliseconds),
          firstFailureAction: Number(field("firstFailureAction").value),
          secondFailureAction: Number(field("secondFailureAction").value),
          thirdFailureAction: Number(field("thirdFailureAction").value),
          subsequentFailureAction: Number(field("subsequentFailureAction").value),
          rebootMessage: field("rebootMessage").value,
          command: field("recoveryCommand").value,
        });
        this.modified.delete("recovery");
      }
      if (pending.has("account")) {
        const password = field("password").value;
        if (password !== field("passwordConfirm").value) {
          this.notify("两次输入的密码不一致");
          return false;
        }
        const startName = field("localSystem").checked ? "LocalSystem" : field("startName").value.trim();
        if (!startName) {
          this.notify("账户名称不能为空");
          return false;
        }
        await this.call("/api/service/configure-account", {
          serviceName: this.info.serviceName,
          startName,
          password: password || null,
        });
        this.modified.delete("account");
      }
      field("password").value = field("passwordConfirm").value = "";
      this.dirty();
      await this.load();
      await this.details();
      return true;
    } catch (e) {
      field("password").value = field("passwordConfirm").value = "";
      this.dirty(null);
      this.notify(e);
      return false;
    }
  }
}

export class WindowManager {
  constructor(host, { call, notify, rtc, recording, revealProcess }) {
    this.host = host;
    this.call = call;
    this.notify = notify;
    this.rtc = rtc;
    this.recording = recording;
    this.revealProcess = revealProcess;
    this.request = 0;
    host.classList.add("window-manager");
    host.innerHTML = /* HTML */ `<nav class="property-tabs">
        <button data-window-tab="windows" class="active">窗口视图</button
        ><button data-window-tab="uia">UI 自动化</button>
      </nav>
      <div class="manager-toolbar" data-window-panel="windows">
        <input data-role="filter" placeholder="筛选窗口" /><span data-role="summary" class="status"></span
        ><span class="spacer"></span><button data-action="recordings">录制任务</button
        ><button data-action="refresh">刷新</button>
      </div>
      <div class="window-tree" data-window-panel="windows">
        <ul data-role="tree"></ul>
        <div class="manager-empty">进入页面后读取远端窗口</div>
      </div>
      <div class="manager-toolbar" data-window-panel="uia" hidden>
        <input data-role="uia-filter" placeholder="筛选已加载的 UI 元素" /><span
          data-role="uia-summary"
          class="status"></span
        ><span class="spacer"></span><button data-action="uia-refresh">刷新</button>
      </div>
      <div class="window-uia-workspace" data-window-panel="uia" hidden>
        <div class="window-tree window-uia-tree">
          <ul data-role="uia-tree"></ul>
          <div class="manager-empty" data-role="uia-empty">展开节点时按需读取其直接子元素</div>
        </div>
        <aside class="window-uia-details">
          <div class="manager-empty" data-role="uia-properties-empty">选择左侧节点以查看 UIA 属性</div>
          <div data-role="uia-properties"></div>
        </aside>
      </div>
      <div class="context-menu window-menu" data-role="menu" hidden>
        <button data-control="8">高亮</button><button data-control="6">切换到窗口</button>
        <hr />
        <button data-control="1">显示</button><button data-control="2">隐藏</button
        ><button data-control="3">最小化</button><button data-control="4">最大化</button
        ><button data-control="5">还原</button>
        <hr />
        <button data-control="9">启用</button><button data-control="10">禁用</button
        ><button data-control="11">置顶</button><button data-control="12">取消置顶</button>
        <hr />
        <button data-action="record">录制</button><button data-control="7" class="danger">关闭</button>
        <hr />
        <button data-action="refresh">刷新</button><button data-action="details">属性</button>
      </div>
      <dialog class="window-properties" data-role="details">
        <form method="dialog">
          <h2 data-role="details-title"></h2>
          <div class="window-property-grid">
            <label class="window-caption">标题<input data-field="caption" maxlength="512" /></label
            ><label>左<input data-field="left" type="number" required /></label
            ><label>上<input data-field="top" type="number" required /></label
            ><label>右<input data-field="right" type="number" required /></label
            ><label>下<input data-field="bottom" type="number" required /></label
            ><label>样式<input data-field="style" required /></label
            ><label>扩展样式<input data-field="exStyle" required /></label>
          </div>
          <section class="window-capture">
            <header>
              <strong>窗口图像</strong><label><input data-field="captureCursor" type="checkbox" />鼠标</label
              ><label
                >编码<select data-field="captureEncoding">
                  <option value="auto">自动</option>
                  <option value="image">PNG / JPEG</option>
                  <option value="h264">H.264</option>
                  <option value="h265">H.265</option>
                </select></label
              ><label
                >帧率<select data-field="frameRate">
                  <option>3</option>
                  <option>6</option>
                  <option selected>12</option>
                  <option>24</option>
                  <option>30</option>
                </select></label
              ><label
                >尺寸<select data-field="maxDimension">
                  <option>640</option>
                  <option>960</option>
                  <option selected>1280</option>
                  <option>1920</option>
                  <option value="7680">原始</option>
                </select></label
              ><label>图像质量<input data-field="imageQuality" type="number" min="1" max="100" value="85" /></label
              ><button data-action="capture" type="button">截屏</button
              ><button data-action="record" type="button">录制</button>
            </header>
            <div class="window-capture-view">
              <span data-role="image-status">正在获取图像…</span><img data-role="image" alt="窗口图像" hidden /><canvas
                data-role="canvas"
                hidden
              ></canvas
              ><button class="window-capture-play" data-action="play" type="button" hidden>▶</button>
            </div>
          </section>
          <dl class="details-grid" data-role="details-body"></dl>
          <div class="dialog-actions">
            <button value="cancel">取消</button><button data-action="apply" type="button">应用</button>
          </div>
        </form>
      </dialog>`;
    this.tree = host.querySelector("[data-role=tree]");
    this.uiaTree = host.querySelector("[data-role=uia-tree]");
    this.uiaEmpty = host.querySelector("[data-role=uia-empty]");
    this.uiaFilter = host.querySelector("[data-role=uia-filter]");
    this.uiaProperties = host.querySelector("[data-role=uia-properties]");
    this.uiaPropertiesEmpty = host.querySelector("[data-role=uia-properties-empty]");
    this.empty = host.querySelector(".manager-empty");
    this.filter = host.querySelector("[data-role=filter]");
    this.menu = host.querySelector("[data-role=menu]");
    const processButton = document.createElement("button");
    processButton.textContent = "转到进程";
    processButton.onclick = () => {
      this.menu.hidden = true;
      if (this.selected) this.revealProcess?.(this.selected.processId);
    };
    this.menu.insertBefore(processButton, this.action("refresh", this.menu));
    this.dialog = host.querySelector("[data-role=details]");
    this.image = this.dialog.querySelector("[data-role=image]");
    this.canvas = this.dialog.querySelector("[data-role=canvas]");
    this.play = this.action("play", this.dialog);
    this.imageStatus = this.dialog.querySelector("[data-role=image-status]");
    this.encoding = this.dialog.querySelector("[data-field=captureEncoding]");
    configureCaptureEncoding(this.encoding);
    this.decoder = new CaptureFrameDecoder(
      this.canvas,
      this.imageStatus,
      (socket) => this.socket === socket,
      (sequence, keyframe, socket) => this.acknowledgeFrame(sequence, keyframe, socket),
      (codecs, width, height, socket) => this.reportVideoCodecs(codecs, width, height, socket),
    );
    this.filter.oninput = () => this.render();
    this.uiaFilter.oninput = () => this.filterUia();
    for (const tab of host.querySelectorAll("[data-window-tab]"))
      tab.onclick = () => this.showMode(tab.dataset.windowTab);
    this.action("refresh").onclick = () => this.load();
    this.action("uia-refresh").onclick = () => this.loadUia(true);
    this.action("recordings").onclick = () => this.recording.show();
    this.action("refresh", this.menu).onclick = () => {
      this.menu.hidden = true;
      this.load();
    };
    this.action("details", this.menu).onclick = () => {
      this.menu.hidden = true;
      this.details();
    };
    this.action("record", this.menu).onclick = () => {
      this.menu.hidden = true;
      if (this.selected) this.recording.window(this.selected.handle);
    };
    this.action("capture", this.dialog).onclick = () => this.capture();
    this.action("record", this.dialog).onclick = () => this.recording.window(this.info.handle);
    this.play.onclick = () => (this.socket ? this.stopStream() : this.startStream());
    this.action("apply", this.dialog).onclick = () => this.apply();
    this.dialog.addEventListener("close", () => this.clearImage());
    for (const button of this.menu.querySelectorAll("[data-control]"))
      button.onclick = () => {
        this.menu.hidden = true;
        this.control(Number(button.dataset.control));
      };
    addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.menu.hidden = true;
    });
    addEventListener("blur", () => (this.menu.hidden = true));
  }
  action(name, root = this.host) {
    return root.querySelector(`[data-action="${name}"]`);
  }
  showMode(mode) {
    for (const tab of this.host.querySelectorAll("[data-window-tab]"))
      tab.classList.toggle("active", tab.dataset.windowTab === mode);
    for (const panel of this.host.querySelectorAll("[data-window-panel]"))
      panel.hidden = panel.dataset.windowPanel !== mode;
    if (mode === "uia" && this.connected && !this.uiaLoaded) this.loadUia();
  }
  activate(connected) {
    this.connected = connected;
    if (connected && !this.loaded) this.load();
  }
  disconnect() {
    this.request++;
    this.uiaPropertiesRequest = (this.uiaPropertiesRequest || 0) + 1;
    this.loaded = false;
    this.uiaLoaded = false;
    this.connected = false;
    this.records = [];
    this.selected = null;
    this.menu.hidden = true;
    if (this.dialog.open) this.dialog.close();
    else this.clearImage();
    this.tree.replaceChildren();
    this.uiaTree.replaceChildren();
    this.uiaProperties.replaceChildren();
    this.uiaPropertiesEmpty.hidden = false;
    this.uiaPropertiesEmpty.textContent = "Client 未连接";
    this.uiaEmpty.hidden = false;
    this.uiaEmpty.textContent = "Client 未连接";
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
    this.host.querySelector("[data-role=summary]").textContent = "";
    this.host.querySelector("[data-role=uia-summary]").textContent = "";
  }
  async loadUia(force = false) {
    if (!this.connected || (this.uiaLoading && !force)) return;
    const request = (this.uiaRequest = (this.uiaRequest || 0) + 1);
    this.uiaLoading = true;
    this.uiaLoaded = false;
    this.uiaTree.replaceChildren();
    this.uiaEmpty.hidden = false;
    this.uiaEmpty.textContent = "正在读取 UI 自动化根级元素…";
    try {
      const records = await this.call("/api/uia/children", { identity: "root" });
      if (request !== this.uiaRequest) return;
      this.uiaTree.replaceChildren(...records.map((record) => this.uiaNode(record)));
      this.uiaLoaded = true;
      this.uiaEmpty.hidden = records.length !== 0;
      this.uiaEmpty.textContent = "没有 UI 自动化根级元素";
      this.updateUiaSummary();
      const first = this.uiaTree.firstElementChild?.uiaNode;
      if (first) this.selectUia(first);
    } catch (error) {
      if (request !== this.uiaRequest) return;
      this.uiaEmpty.textContent = error.message;
      this.notify(error);
    } finally {
      if (request === this.uiaRequest) this.uiaLoading = false;
    }
  }
  uiaNode(record) {
    const item = document.createElement("li"),
      row = document.createElement("div"),
      arrow = document.createElement("button"),
      label = document.createElement("button"),
      children = document.createElement("ul"),
      details = (record.detail || "").split("\n"),
      node = { item, row, arrow, label, children, record, loaded: false };
    item.uiaNode = node;
    row.className = "window-node-row";
    arrow.className = "window-arrow";
    arrow.textContent = record.state ? "▸" : "";
    arrow.disabled = !record.state;
    arrow.tabIndex = -1;
    label.className = "window-node-label";
    label.textContent = `${record.name || `(${record.description || "未命名"})`}  [${record.description || "—"}]`;
    label.title = `${label.textContent}\nPID ${record.value}\nAutomationId: ${details[0] || "—"}\nClass: ${
      details[1] || "—"
    }\nFramework: ${details[2] || "—"}`;
    children.hidden = true;
    row.append(arrow, label);
    item.append(row, children);
    arrow.onclick = () => this.toggleUia(node);
    label.onclick = () => this.selectUia(node);
    return item;
  }
  async selectUia(node) {
    this.uiaTree.querySelector(".selected")?.classList.remove("selected");
    node.row.classList.add("selected");
    this.uiaProperties.replaceChildren();
    this.uiaPropertiesEmpty.hidden = false;
    this.uiaPropertiesEmpty.textContent = "正在读取 UIA 属性…";
    const request = (this.uiaPropertiesRequest = (this.uiaPropertiesRequest || 0) + 1);
    try {
      const records = await this.call("/api/uia/properties", { identity: node.record.identity });
      if (request !== this.uiaPropertiesRequest) return;
      const groups = [
        ["properties", t("common.properties")],
        ["patterns", t("uia.patternAvailability")],
      ];
      this.uiaProperties.replaceChildren(
        ...groups.map(([group, title]) => {
          const section = document.createElement("section"),
            heading = document.createElement("h3"),
            list = document.createElement("dl");
          heading.textContent = title;
          list.className = "details-grid";
          for (const record of records.filter((value) => value.description === group)) {
            const term = document.createElement("dt"),
              detail = document.createElement("dd");
            term.textContent = uiaPropertyName(record.identity);
            detail.textContent =
              record.identity === "controlType" && record.detail
                ? `${record.detail} · ${node.record.description || "—"}`
                : uiaPropertyValue(record);
            detail.title = detail.textContent;
            list.append(term, detail);
          }
          section.append(heading, list);
          return section;
        }),
      );
      this.uiaPropertiesEmpty.hidden = records.length !== 0;
      this.uiaPropertiesEmpty.textContent = "此节点没有可用的 UIA 属性";
    } catch (error) {
      if (request !== this.uiaPropertiesRequest) return;
      this.uiaPropertiesEmpty.textContent = error.message;
      this.notify(error);
    }
  }
  async toggleUia(node) {
    if (!node.children.hidden) {
      node.children.hidden = true;
      node.arrow.textContent = "▸";
      return;
    }
    if (!node.loaded) {
      node.arrow.disabled = true;
      node.arrow.textContent = "…";
      try {
        const records = await this.call("/api/uia/children", { identity: node.record.identity });
        node.children.replaceChildren(...records.map((record) => this.uiaNode(record)));
        node.loaded = true;
        if (!records.length) {
          node.arrow.textContent = "";
          node.arrow.disabled = true;
          node.record.state = 0;
          return;
        }
      } catch (error) {
        node.arrow.textContent = "▸";
        node.arrow.disabled = false;
        this.notify(error);
        return;
      }
    }
    node.children.hidden = false;
    node.arrow.textContent = "▾";
    node.arrow.disabled = false;
    this.filterUia();
    this.updateUiaSummary();
  }
  filterUia() {
    const query = this.uiaFilter.value.toLocaleLowerCase();
    for (const item of this.uiaTree.querySelectorAll("li")) {
      const node = item.uiaNode;
      item.hidden = !!query && !`${node.label.textContent} ${node.label.title}`.toLocaleLowerCase().includes(query);
    }
  }
  updateUiaSummary() {
    this.host.querySelector("[data-role=uia-summary]").textContent =
      `${this.uiaTree.querySelectorAll("li").length} 个已加载元素`;
  }
  async load() {
    const request = ++this.request;
    this.loaded = false;
    this.records = [];
    this.selected = null;
    this.tree.replaceChildren();
    this.host.querySelector("[data-role=summary]").textContent = "";
    this.empty.hidden = false;
    this.empty.textContent = "正在读取…";
    try {
      const records = await this.call("/api/windows");
      if (request !== this.request) return;
      this.records = records;
      this.loaded = true;
      this.render();
    } catch (e) {
      if (request !== this.request) return;
      this.empty.hidden = false;
      this.empty.textContent = e.message;
      this.notify(e);
    }
  }
  render() {
    const records = this.records || [],
      byHandle = new Map(records.map((item) => [item.handle, item])),
      children = new Map();
    for (const item of records) {
      const parent = byHandle.has(item.parentHandle) ? item.parentHandle : "";
      if (!children.has(parent)) children.set(parent, []);
      children.get(parent).push(item);
    }
    const filter = this.filter.value.toLocaleLowerCase(),
      matches = (item) =>
        !filter ||
        `${item.caption} ${item.className} ${item.processId} ${windowHandle(item.handle)}`
          .toLocaleLowerCase()
          .includes(filter) ||
        (children.get(item.handle) || []).some(matches);
    this.tree.replaceChildren(
      ...(children.get("") || []).filter(matches).map((item) => this.node(item, children, matches)),
    );
    this.empty.hidden = this.tree.childElementCount !== 0;
    this.empty.textContent = records.length ? "没有匹配的窗口" : "没有窗口";
    this.host.querySelector("[data-role=summary]").textContent = `${records.length} 个窗口`;
  }
  node(item, children, matches) {
    const li = document.createElement("li"),
      row = document.createElement("div"),
      arrow = document.createElement("button"),
      label = document.createElement("button"),
      list = document.createElement("ul"),
      values = (children.get(item.handle) || []).filter(matches);
    const expanded = !!(item.flags & 256);
    row.className = "window-node-row";
    arrow.className = "window-arrow";
    arrow.textContent = values.length ? (expanded ? "▾" : "▸") : "";
    arrow.disabled = values.length === 0;
    arrow.tabIndex = -1;
    label.className = "window-node-label";
    const caption = expanded ? "桌面" : item.caption || "(无标题)";
    label.textContent = `${windowHandle(item.handle)}  ${caption}  [${item.className || "—"}]`;
    label.title = label.textContent;
    list.hidden = !expanded;
    if (expanded) list.append(...values.map((child) => this.node(child, children, matches)));
    row.append(arrow, label);
    li.append(row, list);
    arrow.onclick = (event) => {
      event.stopPropagation();
      list.hidden = !list.hidden;
      arrow.textContent = list.hidden ? "▸" : "▾";
      if (!list.hidden && !list.childElementCount)
        list.append(...values.map((child) => this.node(child, children, matches)));
    };
    label.onclick = () => this.select(row, item);
    label.ondblclick = () => this.details();
    row.oncontextmenu = (event) => this.context(event, row, item);
    return li;
  }
  select(row, item) {
    this.tree.querySelector(".selected")?.classList.remove("selected");
    row.classList.add("selected");
    this.selected = item;
  }
  context(event, row, item) {
    event.preventDefault();
    this.select(row, item);
    const flags = item.flags;
    this.menu.querySelector('[data-control="1"]').disabled = !!(flags & 1);
    this.menu.querySelector('[data-control="2"]').disabled = !(flags & 1);
    this.menu.querySelector('[data-control="3"]').disabled = !!(flags & 8);
    this.menu.querySelector('[data-control="4"]').disabled = !!(flags & 16);
    this.menu.querySelector('[data-control="5"]').disabled = !(flags & 24);
    this.menu.querySelector('[data-control="9"]').disabled = !!(flags & 2);
    this.menu.querySelector('[data-control="10"]').disabled = !(flags & 2);
    this.menu.querySelector('[data-control="11"]').disabled = !!(flags & 128);
    this.menu.querySelector('[data-control="12"]').disabled = !(flags & 128);
    this.menu.hidden = false;
    const rect = this.menu.getBoundingClientRect();
    this.menu.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - rect.width - 6))}px`;
    this.menu.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - rect.height - 6))}px`;
  }
  identity(item = this.selected) {
    return item && { handle: item.handle, processId: item.processId, threadId: item.threadId };
  }
  async control(control) {
    const item = this.selected;
    if (!item) return;
    if (control === 7 && !confirm(`确定关闭窗口“${item.caption || item.className}”吗？未保存的数据可能丢失。`)) return;
    try {
      await this.call("/api/window/control", { ...this.identity(item), control });
      await this.load();
    } catch (e) {
      this.notify(e);
    }
  }
  async details() {
    if (!this.selected) return;
    try {
      const w = (this.info = await this.call("/api/window/info", this.identity())),
        flags = [];
      for (const [flag, name] of [
        [1, "可见"],
        [2, "已启用"],
        [4, "Unicode"],
        [8, "已最小化"],
        [16, "已最大化"],
        [32, "顶层窗口"],
        [64, "无响应"],
        [128, "置顶"],
      ])
        if (w.flags & flag) flags.push(name);
      const field = (name) => this.dialog.querySelector(`[data-field="${name}"]`),
        handle = (value) => (value === "0" ? "—" : windowHandle(value)),
        fields = [
          ["句柄", windowHandle(w.handle)],
          ["类名", w.className || "(空)"],
          ["进程 ID", w.processId],
          ["线程 ID", w.threadId],
          ["父窗口", handle(w.parentHandle)],
          ["所有者窗口", handle(w.ownerHandle)],
          ["前一窗口", handle(w.previousHandle)],
          ["后一窗口", handle(w.nextHandle)],
          ["第一个子窗口", handle(w.firstChildHandle)],
          ["第一个同级窗口", handle(w.firstSiblingHandle)],
          ["最后一个同级窗口", handle(w.lastSiblingHandle)],
          ["客户区", rect(w.clientLeft, w.clientTop, w.clientRight, w.clientBottom)],
          ["状态", flags.join("、") || "—"],
          ["边框", `${w.borderWidth} × ${w.borderHeight}`],
          ["显示器", w.monitorDevice || "—"],
          ["显示器区域", w.monitorDevice ? rect(w.monitorLeft, w.monitorTop, w.monitorRight, w.monitorBottom) : "—"],
          ["窗口状态", hex(w.windowStatus)],
          ["窗口类 Atom", hex(w.classAtom, 4)],
          ["创建者版本", hex(w.creatorVersion, 4)],
        ],
        title = w.caption || w.className || windowHandle(w.handle);
      field("caption").value = w.caption;
      for (const name of ["left", "top", "right", "bottom"])
        field(name).value = w[`window${name[0].toUpperCase()}${name.slice(1)}`];
      field("style").value = hex(w.style);
      field("exStyle").value = hex(w.exStyle);
      this.dialog.querySelector("[data-role=details-title]").textContent = title;
      this.dialog.querySelector("[data-role=details-title]").title = title;
      this.dialog.querySelector("[data-role=details-body]").replaceChildren(...details(fields));
      if (!this.dialog.open) this.dialog.showModal();
      this.capture();
    } catch (e) {
      this.notify(e);
    }
  }
  captureOptions() {
    const field = (name) => this.dialog.querySelector(`[data-field="${name}"]`),
      imageQuality = Number(field("imageQuality").value);
    if (!Number.isInteger(imageQuality) || imageQuality < 1 || imageQuality > 100)
      throw new Error("图像质量必须是 1 到 100 的整数");
    return {
      captureCursor: field("captureCursor").checked,
      maxDimension: Number(field("maxDimension").value),
      frameRate: Number(field("frameRate").value),
      imageQuality,
      ...captureEncodingOptions(this.encoding),
    };
  }
  clearImage() {
    this.captureRequest = (this.captureRequest || 0) + 1;
    this.stopStream(true);
    if (this.imageUrl) URL.revokeObjectURL(this.imageUrl);
    this.imageUrl = null;
    this.image.removeAttribute("src");
    this.image.hidden = true;
    this.canvas.hidden = true;
    this.play.hidden = true;
    this.imageStatus.hidden = false;
    this.imageStatus.textContent = "正在获取图像…";
  }
  async capture() {
    const w = this.info;
    if (!w) return;
    this.stopStream(true);
    const request = (this.captureRequest || 0) + 1;
    this.captureRequest = request;
    if (this.imageUrl) URL.revokeObjectURL(this.imageUrl);
    this.imageUrl = null;
    this.canvas.hidden = true;
    this.image.hidden = true;
    this.play.hidden = true;
    this.imageStatus.hidden = false;
    this.imageStatus.textContent = "正在获取图像…";
    try {
      const response = await fetch(apiUrl("/api/window/image"), {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ ...this.identity(w), ...this.captureOptions() }),
      });
      if (!response.ok) {
        const text = await response.text();
        let error;
        try {
          error = JSON.parse(text);
        } catch {}
        throw new Error(error?.message || text || `HTTP ${response.status}`);
      }
      const blob = await response.blob();
      if (request !== this.captureRequest || !this.dialog.open) return;
      this.imageUrl = URL.createObjectURL(blob);
      this.image.onload = () => {
        if (request !== this.captureRequest) return;
        this.imageStatus.textContent = `${this.image.naturalWidth} × ${this.image.naturalHeight}`;
        this.imageStatus.hidden = false;
        this.play.hidden = false;
      };
      this.image.src = this.imageUrl;
      this.image.hidden = false;
    } catch (e) {
      if (request !== this.captureRequest) return;
      this.imageStatus.textContent = e.message;
      this.notify(e);
    }
  }
  async startStream() {
    if (!this.info || this.socket || this.streamStarting) return;
    let options;
    try {
      options = this.captureOptions();
    } catch (e) {
      this.notify(e);
      return;
    }
    const request = (this.streamRequest = (this.streamRequest || 0) + 1);
    this.streamStarting = true;
    this.play.textContent = "■";
    this.play.classList.add("playing");
    let socket, direct;
    try {
      try {
        direct = await this.rtc.open(
          (data) => this.decoder.receive(data, socket),
          (text) => (this.imageStatus.textContent = text),
        );
      } catch (error) {
        this.imageStatus.textContent = "P2P 加速失败";
        console.warn(error);
      }
      if (request !== this.streamRequest) {
        direct?.close();
        return;
      }
      const query = new URLSearchParams({ ...this.identity(this.info), ...options, directStreamId: direct?.id || 0 }),
        url = new URL(`/api/window/stream?${query}`, location.href);
      url.protocol = location.protocol === "https:" ? "wss:" : "ws:";
      socket = this.socket = new WebSocket(apiUrl(url));
      this.streamDirect = direct;
      this.decoder.reset();
      socket.binaryType = "arraybuffer";
      socket.onopen = () => {
        if (direct) this.imageStatus.textContent = "P2P 实时画面已连接";
      };
      if (!direct) socket.onmessage = (event) => this.decoder.receive(new Uint8Array(event.data), socket);
      socket.onclose = (event) => {
        direct?.close();
        if (this.streamDirect === direct) this.streamDirect = null;
        if (this.socket !== socket) return;
        this.socket = null;
        this.play.textContent = "▶";
        this.play.classList.remove("playing");
        this.play.hidden = false;
        if (event.code !== 1000) {
          const message = event.reason || `WebSocket: ${event.code}`;
          this.imageStatus.textContent = message;
          this.notify(message);
        } else if (!this.canvas.hidden) this.imageStatus.textContent = `${this.canvas.width} × ${this.canvas.height}`;
      };
      socket.onerror = () => {
        if (this.socket === socket) this.imageStatus.textContent = "实时画面连接失败";
      };
    } finally {
      this.streamStarting = false;
    }
  }
  stopStream(silent = false) {
    this.streamRequest = (this.streamRequest || 0) + 1;
    const socket = this.socket,
      direct = this.streamDirect;
    this.socket = this.streamDirect = null;
    if (socket && socket.readyState < 2) socket.close(1000);
    else direct?.close();
    this.decoder.reset();
    this.play.textContent = "▶";
    this.play.classList.remove("playing");
    if (!silent) {
      this.play.hidden = false;
      if (!this.canvas.hidden) this.imageStatus.textContent = `${this.canvas.width} × ${this.canvas.height}`;
    }
  }
  acknowledgeFrame(sequence, keyframe, socket) {
    if (socket?.readyState !== WebSocket.OPEN) return;
    const data = new ArrayBuffer(6),
      view = new DataView(data);
    view.setUint8(0, 4);
    view.setUint8(1, keyframe ? 1 : 0);
    view.setUint32(2, sequence, true);
    socket.send(data);
  }
  reportVideoCodecs(codecs, width, height, socket) {
    if (socket?.readyState !== WebSocket.OPEN) return;
    const data = new ArrayBuffer(6),
      view = new DataView(data);
    view.setUint8(0, 6);
    view.setUint8(1, codecs);
    view.setUint16(2, width, true);
    view.setUint16(4, height, true);
    socket.send(data);
  }
  async apply() {
    const w = this.info;
    if (!w) return;
    const field = (name) => this.dialog.querySelector(`[data-field="${name}"]`),
      caption = field("caption").value,
      number = (name) => Number(field(name).value),
      left = number("left"),
      top = number("top"),
      right = number("right"),
      bottom = number("bottom"),
      style = number("style"),
      exStyle = number("exStyle");
    if (
      ![left, top, right, bottom].every(Number.isSafeInteger) ||
      right <= left ||
      bottom <= top ||
      ![style, exStyle].every((value) => Number.isInteger(value) && value >= 0 && value <= 0xffffffff)
    ) {
      this.notify("窗口区域或样式值无效");
      return;
    }
    let fields = 0;
    if (caption !== w.caption) fields |= 1;
    if (left !== w.windowLeft || top !== w.windowTop || right !== w.windowRight || bottom !== w.windowBottom)
      fields |= 2;
    if (style !== w.style) fields |= 4;
    if (exStyle !== w.exStyle) fields |= 8;
    if (!fields) {
      this.dialog.close();
      return;
    }
    try {
      await this.call("/api/window/update", {
        ...this.identity(),
        fields,
        caption,
        left,
        top,
        right,
        bottom,
        style,
        exStyle,
      });
      this.dialog.close();
      await this.load();
    } catch (e) {
      this.notify(e);
    }
  }
}

function join(path, name) {
  return `${path.replace(/[\\/]+$/, "")}\\${name}`;
}
function extension(name) {
  const index = name.lastIndexOf(".");
  return index < 0 ? "" : name.slice(index).toLocaleLowerCase();
}
function quoteArgument(value) {
  return `"${value}"`;
}
function normalizePath(value) {
  const path = value.trim().replace(/\//g, "\\").replace(/\\+$/, "");
  return /^[A-Za-z]:$/.test(path) ? `${path}\\` : path || "\\";
}
function parent(value) {
  const path = value.trim().replace(/\//g, "\\").replace(/\\+$/, "");
  if (/^[A-Za-z]:$/.test(path)) return null;
  if (path.startsWith("\\\\")) {
    const parts = path.slice(2).split("\\");
    if (parts.length <= 2) return null;
  }
  const index = path.lastIndexOf("\\");
  return index < 0 ? null : index === 2 ? path.slice(0, 3) : path.slice(0, index);
}
function commandPath(value) {
  const match = (value || "").trim().match(/^"([^"]+)"|^(\S+)/);
  return match?.[1] || match?.[2] || "";
}
function fileSize(cell, item) {
  if (!(item.attributes & DIRECTORY)) cell.textContent = bytes(item.size);
}
function details(fields) {
  return fields.flatMap(([name, value]) => {
    const dt = document.createElement("dt"),
      dd = document.createElement("dd");
    dt.textContent = name;
    dd.textContent = value;
    return [dt, dd];
  });
}
function bytes(value) {
  value = Number(value);
  if (value < 1024) return `${value} B`;
  const units = ["KB", "MB", "GB", "TB"];
  let i = -1;
  do {
    value /= 1024;
    i++;
  } while (value >= 1024 && i < units.length - 1);
  return `${value.toFixed(value < 10 ? 1 : 0)} ${units[i]}`;
}
function downloadProgress(job) {
  const transferred = BigInt(job.transferredBytes);
  if (job.totalBytes == null) return bytes(transferred);
  const total = BigInt(job.totalBytes);
  return `${bytes(transferred)} / ${bytes(total)}${total ? ` (${Number((transferred * 10000n) / total) / 100}%)` : ""}`;
}
function date(value) {
  return new Date(value).toLocaleString();
}
function duration(value) {
  return `${(Number(BigInt(value)) / 10000000).toFixed(3)} 秒`;
}
function ntstatus(value) {
  return `NTSTATUS: 0x${(value >>> 0).toString(16).padStart(8, "0").toUpperCase()}`;
}
function ownerField(cell, value, status) {
  cell.textContent = status >= 0 ? value || "(空)" : ntstatus(status);
  cell.title = cell.textContent;
}
function detailString(status, value) {
  return status >= 0
    ? value || "(空)"
    : `不可用（NTSTATUS 0x${(status >>> 0).toString(16).padStart(8, "0").toUpperCase()}）`;
}
function serviceType(value) {
  return (
    {
      1: "内核驱动",
      2: "文件系统驱动",
      4: "适配器驱动",
      8: "文件系统识别驱动",
      16: "独立进程",
      32: "共享进程",
      48: "Win32",
      80: "用户独立进程",
      96: "用户共享进程",
      208: "用户独立进程实例",
      224: "用户共享进程实例",
      240: "用户服务实例",
      272: "交互式独立进程",
      288: "交互式共享进程",
      528: "打包独立进程",
      544: "打包共享进程",
    }[value] || `0x${value.toString(16).padStart(8, "0").toUpperCase()}`
  );
}
function state(value) {
  return (
    { 1: "已停止", 2: "正在启动", 3: "正在停止", 4: "正在运行", 5: "即将继续", 6: "正在暂停", 7: "已暂停" }[value] ||
    `状态 ${value}`
  );
}
function startType(value) {
  return value === 0xff ? "—" : { 0: "引导", 1: "系统", 2: "自动", 3: "手动", 4: "已禁用" }[value] || `类型 ${value}`;
}
function windowHandle(value) {
  return value && value !== "0" ? `0x${BigInt(value).toString(16).toUpperCase()}` : "—";
}
function uiaPropertyName(value) {
  return value.replace(/^[a-z]/, (letter) => letter.toUpperCase()).replace(/[A-Z]/g, (letter) => ` ${letter}`).trim();
}
function uiaPropertyValue(record) {
  if (record.flags & 2) return t("common.notSupported");
  if (record.flags & 1) return t("common.unavailable");
  const value = record.detail;
  if (!value) return "—";
  if (value === "true" || value === "false") return t(value === "true" ? "common.yes" : "common.no");
  return value;
}
function rect(left, top, right, bottom) {
  return `${left}, ${top} — ${right}, ${bottom}（${right - left} × ${bottom - top}）`;
}
function hex(value, width = 8) {
  return `0x${Number(value).toString(16).padStart(width, "0").toUpperCase()}`;
}
function processState(item) {
  if (item.wslIdentity)
    return "Tt".includes(item.wslState)
      ? t("process.suspended")
      : item.wslState === "Z"
        ? t("process.zombie")
        : item.wslState === "D"
          ? t("process.uninterruptible")
          : item.wslState === "R"
            ? t("process.running")
            : t("process.waiting");
  return item.flags & 1 ? "已挂起" : item.flags & 2 ? "正在运行（效能模式）" : "正在运行";
}
function processPlatform(machine) {
  return (
    {
      0x14c: "x86",
      0x1c0: "ARM",
      0x1c4: "ARM",
      0x3a64: "x86 (CHPE)",
      0x8664: "x64",
      0xaa64: "ARM64",
      0xa641: "ARM64EC",
      0xa64e: "ARM64X",
    }[machine] || "—"
  );
}
function moduleLoadReason(value) {
  return (
    {
      0: "静态依赖",
      1: "静态转发依赖",
      2: "动态转发依赖",
      3: "延迟加载依赖",
      4: "动态加载",
      5: "映像加载",
      6: "数据加载",
      7: "Enclave 主模块",
      8: "Enclave 依赖",
      9: "补丁映像",
    }[value] || "未知"
  );
}
function priorityName(value) {
  return { 1: "低", 2: "正常", 3: "高", 4: "实时", 5: "低于正常", 6: "高于正常" }[value] || "未知";
}
function memoryState(value) {
  return { 0x1000: "已提交", 0x2000: "保留", 0x10000: "空闲" }[value] || hex(value);
}
function memoryType(region) {
  const flags = region.regionType;
  if (region.state === 0x10000) return "空闲";
  if (flags & 0x200) return "AWE";
  if (flags & 0x10) return "物理内存";
  if (flags & 0x40) return "软件 Enclave";
  if (flags & 0x100) return "占位符";
  if (flags & 0x8) return "页文件映射";
  if (region.state === 0x2000) return "保留";
  return { 0x1000000: "映像", 0x40000: "映射文件", 0x20000: "专用内存" }[region.type] || "其它";
}
function memoryRegionFlags(value) {
  const names = [];
  for (const [flag, name] of [
    [1, "专用"],
    [2, "数据文件"],
    [4, "映像"],
    [8, "页文件"],
    [16, "物理内存"],
    [32, "直接映射"],
    [64, "软件 Enclave"],
    [128, "64 KB 页"],
    [256, "占位符"],
    [512, "AWE"],
    [1024, "写入监视"],
    [2048, "大页"],
    [4096, "超大页"],
  ])
    if (value & flag) names.push(name);
  return names.join("、") || "—";
}
function memoryProtection(value) {
  if (!value) return "—";
  const base =
      {
        1: "无访问",
        2: "只读",
        4: "读写",
        8: "写时复制",
        16: "执行",
        32: "执行/只读",
        64: "执行/读写",
        128: "执行/写时复制",
      }[value & 0xff] || hex(value & 0xff),
    flags = [];
  if (value & 0x100) flags.push("Guard");
  if (value & 0x200) flags.push("NoCache");
  if (value & 0x400) flags.push("WriteCombine");
  return flags.length ? `${base} / ${flags.join(" / ")}` : base;
}
