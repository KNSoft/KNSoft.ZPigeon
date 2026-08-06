export class WinObjManager {
  constructor(host, { call, notify }) {
    this.host = host;
    this.call = call;
    this.notify = notify;
    host.innerHTML = /* HTML */ `<div class="manager-toolbar">
        <input data-role="filter" placeholder="筛选当前目录" /><input
          data-role="path"
          class="winobj-path"
          value="\\"
          readonly
        /><span data-role="summary" class="status"></span><button data-action="refresh">刷新</button>
      </div>
      <div class="winobj-body">
        <aside class="administration-tree winobj-tree"><ul data-role="tree"></ul></aside>
        <div class="manager-table winobj-table">
          <table>
            <thead>
              <tr>
                <th>名称</th>
                <th>类型</th>
                <th>符号链接目标</th>
              </tr>
            </thead>
            <tbody></tbody>
          </table>
          <div class="manager-empty">进入页面后读取对象命名空间</div>
        </div>
      </div>
      <div class="context-menu" data-role="menu" hidden>
        <button data-action="copy">复制完整路径</button><button data-action="properties">属性</button>
        <hr />
        <button data-action="refresh-item">刷新</button>
      </div>
      <dialog data-role="properties">
        <form method="dialog">
          <h2>对象属性</h2>
          <dl class="details-grid" data-role="details"></dl>
          <div class="dialog-actions"><button value="close">关闭</button></div>
        </form>
      </dialog>`;
    this.tree = host.querySelector("[data-role=tree]");
    this.body = host.querySelector("tbody");
    this.empty = host.querySelector(".manager-empty");
    this.filter = host.querySelector("[data-role=filter]");
    this.menu = host.querySelector("[data-role=menu]");
    this.root = this.node("\\", "对象命名空间");
    this.tree.append(this.root.item);
    this.filter.oninput = () => this.render();
    host.querySelector("[data-action=refresh]").onclick = () => this.load(this.selected || this.root, true);
    host.querySelector("[data-action=copy]").onclick = async () => {
      this.menu.hidden = true;
      if (this.contextRecord) await navigator.clipboard.writeText(this.contextRecord.identity);
    };
    host.querySelector("[data-action=properties]").onclick = () => {
      this.menu.hidden = true;
      this.properties(this.contextRecord);
    };
    host.querySelector("[data-action=refresh-item]").onclick = () => {
      this.menu.hidden = true;
      this.load(this.selected || this.root, true);
    };
    addEventListener("pointerdown", (event) => {
      if (!this.menu.contains(event.target)) this.menu.hidden = true;
    });
  }
  node(path, name) {
    const item = document.createElement("li"),
      row = document.createElement("div"),
      arrow = document.createElement("button"),
      label = document.createElement("button"),
      children = document.createElement("ul"),
      node = { path, item, row, arrow, label, children, loaded: false, expanded: false };
    item._node = node;
    row.className = "administration-node-row";
    arrow.className = "administration-arrow";
    label.className = "administration-node-label";
    arrow.textContent = "▸";
    label.textContent = name;
    children.hidden = true;
    row.append(arrow, label);
    item.append(row, children);
    arrow.onclick = (event) => {
      event.stopPropagation();
      this.toggle(node);
    };
    label.onclick = () => this.select(node, true);
    return node;
  }
  activate(connected) {
    this.connected = connected;
    if (connected && !this.root.loaded) this.select(this.root, true);
  }
  disconnect() {
    this.connected = false;
    this.request = (this.request || 0) + 1;
    this.records = [];
    this.selected = null;
    this.root.loaded = false;
    this.root.children.replaceChildren();
    this.root.children.hidden = true;
    this.root.arrow.disabled = false;
    this.root.arrow.textContent = "▸";
    this.body.replaceChildren();
    this.empty.hidden = false;
    this.empty.textContent = "Client 未连接";
  }
  async toggle(node) {
    if (node.expanded) {
      node.expanded = false;
      node.children.hidden = true;
      node.arrow.textContent = "▸";
      return;
    }
    const loaded = this.selected === node ? await this.load(node) : await this.select(node, false);
    if (!loaded || node.arrow.disabled) return;
    node.expanded = true;
    node.children.hidden = false;
    node.arrow.textContent = "▾";
  }
  async select(node, expand) {
    this.selected?.row.classList.remove("selected");
    this.selected = node;
    node.row.classList.add("selected");
    this.records = [];
    this.body.replaceChildren();
    this.host.querySelector("[data-role=path]").value = node.path;
    this.host.querySelector("[data-role=summary]").textContent = "";
    this.empty.hidden = false;
    this.empty.textContent = "正在读取对象目录…";
    const loaded = await this.load(node);
    if (loaded && expand && !node.arrow.disabled) {
      node.expanded = true;
      node.children.hidden = false;
      node.arrow.textContent = "▾";
    }
    return loaded;
  }
  async load(node, refresh = false) {
    if (!this.connected) return false;
    if (node.loaded && !refresh) {
      if (this.selected === node) {
        this.records = node.records;
        this.render();
      }
      return true;
    }
    if (this.selected === node && refresh) {
      this.records = [];
      this.body.replaceChildren();
      this.host.querySelector("[data-role=summary]").textContent = "";
      this.empty.hidden = false;
      this.empty.textContent = "正在读取对象目录…";
    }
    if (node.loading) return node.loading;
    const request = this.request || 0;
    node.loading = (async () => {
      try {
        const records = await this.call("/api/winobj", { path: node.path });
        if (!this.connected || request !== (this.request || 0)) return false;
        records.sort((a, b) => (b.kind === 51) - (a.kind === 51) || a.name.localeCompare(b.name));
        node.records = records;
        node.loaded = true;
        node.children.replaceChildren();
        const directories = records.filter((value) => value.kind === 51);
        for (const record of directories)
          node.children.append(this.node(record.identity, record.name).item);
        node.arrow.disabled = directories.length === 0;
        node.arrow.textContent = directories.length === 0 ? "" : "▸";
        if (node.arrow.disabled) {
          node.expanded = false;
          node.children.hidden = true;
        }
        if (this.selected === node) {
          this.records = records;
          this.render();
        }
        return true;
      } catch (error) {
        if (!this.connected || request !== (this.request || 0)) return false;
        if (this.selected === node) {
          this.records = [];
          this.body.replaceChildren();
          this.host.querySelector("[data-role=summary]").textContent = "";
          this.empty.hidden = false;
          this.empty.textContent = error.message;
        }
        this.notify(error);
        return false;
      } finally {
        node.loading = null;
      }
    })();
    return node.loading;
  }
  render() {
    const filter = this.filter.value.toLocaleLowerCase(),
      records = (this.records || []).filter(
        (record) =>
          !filter || `${record.name} ${record.description} ${record.detail}`.toLocaleLowerCase().includes(filter),
      );
    this.body.replaceChildren();
    for (const record of records) {
      const row = document.createElement("tr");
      for (const value of [record.name, record.description, record.detail || ""]) {
        const cell = document.createElement("td");
        cell.textContent = value;
        cell.title = value;
        row.append(cell);
      }
      row.ondblclick = () => {
        if (record.kind === 51) {
          const child = [...this.selected.children.children]
            .map((item) => item._node)
            .find((node) => node.path === record.identity);
          if (child) this.select(child, true);
        } else this.properties(record);
      };
      row.oncontextmenu = (event) => this.context(event, record);
      this.body.append(row);
    }
    this.empty.hidden = records.length !== 0;
    this.empty.textContent = "此目录没有对象";
    this.host.querySelector("[data-role=summary]").textContent =
      `${records.length} / ${(this.records || []).length} 个对象`;
  }
  context(event, record) {
    event.preventDefault();
    this.contextRecord = record;
    this.menu.hidden = false;
    const rect = this.menu.getBoundingClientRect();
    this.menu.style.left = `${Math.max(6, Math.min(event.clientX, innerWidth - rect.width - 6))}px`;
    this.menu.style.top = `${Math.max(6, Math.min(event.clientY, innerHeight - rect.height - 6))}px`;
  }
  properties(record) {
    if (!record) return;
    const values = [
        ["名称", record.name],
        ["类型", record.description],
        ["完整路径", record.identity],
        ["符号链接目标", record.detail || "—"],
      ],
      details = this.host.querySelector("[data-role=details]");
    details.replaceChildren(
      ...values.flatMap(([name, value]) => {
        const term = document.createElement("dt"),
          description = document.createElement("dd");
        term.textContent = name;
        description.textContent = value;
        return [term, description];
      }),
    );
    this.host.querySelector("[data-role=properties]").showModal();
  }
}
