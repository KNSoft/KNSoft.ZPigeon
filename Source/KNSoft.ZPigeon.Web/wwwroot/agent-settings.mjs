import { localize, t } from "./i18n.mjs";

export class AgentSettings {
  constructor(root, { get, post, put, remove, notify }) {
    this.root = root;
    this.get = get;
    this.post = post;
    this.put = put;
    this.remove = remove;
    this.notify = notify;
    const template = document.createElement("template");
    template.innerHTML = settingsDialogs;
    this.modelDialog = template.content.querySelector("#agentModelDialog");
    this.modelForm = this.modelDialog.querySelector("form");
    this.profileDialog = template.content.querySelector("#agentProfileDialog");
    this.profileForm = this.profileDialog.querySelector("form");
    localize(template.content);
    document.body.append(template.content);
    this.models = [];
    this.tools = [];
    this.providers = [];
    this.catalogModels = [];
    this.modelEditingId = null;
    this.profileDocuments = [];
    this.selectedDocument = 0;
    this.bindEvents();
  }

  bindEvents() {
    this.root.querySelector('[data-action="models"]').onclick = () => this.openModels();
    this.root.querySelector('[data-action="profile"]').onclick = () => this.openProfile();
    const modelList = this.modelDialog.querySelector('[data-role="model-list"]');
    modelList.onchange = () => this.selectModel(modelList.value || null).catch((error) => this.notify(error));
    this.modelDialog.querySelector('[data-action="new-model"]').onclick = () =>
      this.selectModel(null).catch((error) => this.notify(error));
    this.modelDialog.querySelector('[data-action="delete-model"]').onclick = () => this.deleteModel();
    this.modelDialog.querySelector('[data-action="test-model"]').onclick = () => this.testModel();
    this.modelDialog.querySelector('[data-action="reveal-credential"]').onclick = () =>
      this.toggleCredential();
    this.modelDialog.querySelector('[data-action="copy-credential"]').onclick = () =>
      this.copyCredential();
    this.modelForm.elements.provider.onchange = () =>
      this.providerChanged(true).catch((error) => this.notify(error));
    this.modelForm.elements.modelId.onchange = () => this.catalogModelChanged();
    this.modelForm.elements.authentication.onchange = () => this.updateAuthentication();
    this.modelForm.onsubmit = (event) => {
      event.preventDefault();
      event.submitter?.value === "cancel" ? this.modelDialog.close() : this.saveModel();
    };

    this.profileDialog.querySelector('[data-action="add-document"]').onclick = () => this.addDocument();
    this.profileDialog.querySelector('[data-action="delete-document"]').onclick = () => this.deleteDocument();
    this.profileForm.elements.documentName.oninput = () => {
      this.profileDocuments[this.selectedDocument].name = this.profileForm.elements.documentName.value;
      this.renderDocumentList();
    };
    this.profileForm.elements.documentContent.oninput = () => {
      this.profileDocuments[this.selectedDocument].content = this.profileForm.elements.documentContent.value;
    };
    for (const input of this.profileForm.querySelectorAll('[name="toolMode"]'))
      input.onchange = () => this.changeToolSelection();
    this.profileForm.onsubmit = (event) => {
      event.preventDefault();
      event.submitter?.value === "cancel" ? this.profileDialog.close() : this.saveProfile();
    };
  }

  async loadConfiguration() {
    [this.models, this.tools, this.providers] = await Promise.all([
      this.get("/api/agent/models"),
      this.get("/api/agent/tools"),
      this.get("/api/agent/catalog/providers"),
    ]);
  }

  async openModels() {
    try {
      await this.loadConfiguration();
      await this.renderModelList(this.models[0]?.id ?? null);
      this.modelDialog.showModal();
    } catch (error) {
      this.notify(error);
    }
  }

  async renderModelList(selectedId = this.modelEditingId) {
    const select = this.modelDialog.querySelector('[data-role="model-list"]');
    select.replaceChildren(
      new Option(t("agent.newModel"), ""),
      ...this.models.map((model) => new Option(model.name, model.id)),
    );
    if (selectedId && this.models.some((model) => model.id === selectedId)) select.value = selectedId;
    await this.selectModel(select.value || null);
  }

  async selectModel(id) {
    this.modelEditingId = id;
    this.modelDialog.querySelector('[data-action="delete-model"]').disabled = !id;
    let model = null;
    try {
      if (id) model = await this.get(`/api/agent/models/${id}`);
    } catch (error) {
      this.notify(error);
      return;
    }
    const form = this.modelForm;
    form.elements.name.value = model?.name ?? "";
    this.fillProviders(model?.provider ?? "openai");
    form.elements.protocol.value = String(model?.protocol ?? 1);
    form.elements.baseUrl.value = model?.baseUrl ?? "https://api.openai.com/v1";
    form.elements.authentication.value = String(model?.authentication ?? 1);
    form.elements.credential.value = model?.credential ?? "";
    form.elements.credential.type = "password";
    this.modelDialog.querySelector('[data-action="reveal-credential"]').textContent = t("agent.reveal");
    form.elements.modelId.value = model?.modelId ?? "";
    form.elements.contextWindow.value = String(model?.contextWindow ?? 128000);
    form.elements.maximumOutputTokens.value = String(model?.maximumOutputTokens ?? 16384);
    form.elements.reasoning.value = String(model?.reasoning ?? 0);
    form.elements.requestTimeoutSeconds.value = String(model?.requestTimeoutSeconds ?? 60);
    form.elements.advancedJson.value = model?.advancedJson ?? "{}";
    this.modelDialog.querySelector('[data-role="test-result"]').hidden = true;
    await this.providerChanged(false);
    this.updateAuthentication();
    this.catalogModelChanged(false);
  }

  fillProviders(provider) {
    const select = this.modelForm.elements.provider,
      known = this.providers.some((value) => value.id === provider);
    select.replaceChildren(
      ...this.providers.map((value) => new Option(`${value.name} (${value.id})`, value.id)),
      new Option(t("agent.customProvider"), "__custom"),
    );
    select.value = known ? provider : "__custom";
    this.modelForm.elements.customProvider.hidden = known;
    this.modelForm.elements.customProvider.value = known ? "" : provider;
    this.modelForm.elements.customProvider.placeholder = t("agent.providerId");
  }

  async providerChanged(applyDefaults) {
    const select = this.modelForm.elements.provider,
      custom = select.value === "__custom",
      customInput = this.modelForm.elements.customProvider,
      provider = this.providers.find((value) => value.id === select.value);
    customInput.hidden = !custom;
    customInput.required = custom;
    if (applyDefaults && provider) {
      if (provider.api) this.modelForm.elements.baseUrl.value = provider.api;
      if (provider.protocol) this.modelForm.elements.protocol.value = String(provider.protocol);
    }
    this.catalogModels = provider
      ? await this.get(`/api/agent/catalog/providers/${encodeURIComponent(provider.id)}/models`)
      : [];
    this.modelDialog.querySelector("#agentCatalogModels").replaceChildren(
      ...this.catalogModels.map((model) => {
        const option = document.createElement("option");
        option.value = model.id;
        option.label = model.name;
        return option;
      }),
    );
    if (applyDefaults && this.catalogModels.length) {
      this.modelForm.elements.modelId.value = this.catalogModels[0].id;
      this.catalogModelChanged(true);
    } else {
      this.catalogModelChanged(false);
    }
  }

  catalogModelChanged(apply = true) {
    const model = this.catalogModels.find((value) => value.id === this.modelForm.elements.modelId.value),
      description = this.modelDialog.querySelector('[data-role="model-description"]');
    if (!model) {
      description.textContent = "";
      return;
    }
    if (apply) {
      this.modelForm.elements.contextWindow.value = String(model.contextWindow);
      this.modelForm.elements.maximumOutputTokens.value = String(model.maximumOutputTokens);
      if (!model.reasoning) this.modelForm.elements.reasoning.value = "0";
    }
    description.textContent = t("agent.catalogModelDescription", {
      description: model.description ?? "",
      input: model.inputModalities.join(", "),
      output: model.outputModalities.join(", "),
    });
  }

  updateAuthentication() {
    const authentication = Number(this.modelForm.elements.authentication.value),
      none = authentication === 3,
      row = this.modelDialog.querySelector('[data-role="credential-row"]'),
      label = this.modelDialog.querySelector('[data-role="credential-label"]');
    row.hidden = none;
    this.modelForm.elements.credential.required = !none;
    label.textContent = t(
      authentication === 1
        ? "agent.auth.apiKey"
        : authentication === 4
          ? "agent.auth.oauth"
          : "agent.auth.bearer",
    );
  }

  modelRequest() {
    const form = this.modelForm;
    return {
      id: this.modelEditingId,
      name: form.elements.name.value.trim(),
      provider:
        form.elements.provider.value === "__custom"
          ? form.elements.customProvider.value.trim()
          : form.elements.provider.value,
      protocol: Number(form.elements.protocol.value),
      baseUrl: form.elements.baseUrl.value.trim(),
      authentication: Number(form.elements.authentication.value),
      credential: form.elements.credential.value,
      modelId: form.elements.modelId.value.trim(),
      contextWindow: Number(form.elements.contextWindow.value),
      maximumOutputTokens: Number(form.elements.maximumOutputTokens.value),
      reasoning: Number(form.elements.reasoning.value),
      requestTimeoutSeconds: Number(form.elements.requestTimeoutSeconds.value),
      advancedJson: form.elements.advancedJson.value.trim() || "{}",
    };
  }

  async saveModel() {
    if (!this.modelForm.reportValidity()) return;
    try {
      const body = this.modelRequest(),
        value = this.modelEditingId
          ? await this.put(`/api/agent/models/${this.modelEditingId}`, body)
          : await this.post("/api/agent/models", body);
      this.models = await this.get("/api/agent/models");
      await this.renderModelList(value.id);
    } catch (error) {
      this.notify(error);
    }
  }

  async testModel() {
    if (!this.modelForm.reportValidity()) return;
    const result = this.modelDialog.querySelector('[data-role="test-result"]'),
      button = this.modelDialog.querySelector('[data-action="test-model"]');
    button.disabled = true;
    result.hidden = false;
    result.className = "agent-test-result";
    result.textContent = t("agent.testingConnection");
    try {
      const value = await this.post("/api/agent/models/test", this.modelRequest());
      result.classList.add(value.success ? "success" : "failed");
      result.textContent = value.success
        ? t("agent.connectionSucceeded", { reply: value.message || "OK" })
        : value.message;
    } catch (error) {
      result.classList.add("failed");
      result.textContent = error.message;
    } finally {
      button.disabled = false;
    }
  }

  async deleteModel() {
    const model = this.models.find((value) => value.id === this.modelEditingId);
    if (!model || !confirm(t("agent.confirmDeleteModel", { name: model.name }))) return;
    try {
      await this.remove(`/api/agent/models/${model.id}`);
      this.models = await this.get("/api/agent/models");
      await this.renderModelList(this.models[0]?.id ?? null);
    } catch (error) {
      this.notify(error);
    }
  }

  toggleCredential() {
    const input = this.modelForm.elements.credential,
      button = this.modelDialog.querySelector('[data-action="reveal-credential"]');
    input.type = input.type === "password" ? "text" : "password";
    button.textContent = t(input.type === "password" ? "agent.reveal" : "agent.hide");
  }

  async copyCredential() {
    try {
      await navigator.clipboard.writeText(this.modelForm.elements.credential.value);
    } catch (error) {
      this.notify(error);
    }
  }

  async openProfile() {
    try {
      await this.loadConfiguration();
      const profile = await this.get("/api/agent/profile"),
        form = this.profileForm;
      const selected = new Set(profile.toolNames),
        reads = this.tools.filter((tool) => tool.readOnly);
      form.elements.toolMode.value = selected.size === reads.length &&
        reads.every((tool) => selected.has(tool.name)) ? "read" :
        selected.size === this.tools.length && this.tools.every((tool) => selected.has(tool.name)) ? "all" : "custom";
      this.renderTools(selected);
      this.updateToolSelection();
      this.profileDialog.querySelector('[data-role="tool-details"]').open = false;
      this.profileDocuments = [
        { key: "systemPrompt", name: t("agent.systemPrompt"), content: profile.systemPrompt },
        { key: "agentsMd", name: "AGENTS.md", content: profile.agentsMd },
        { key: "toolsMd", name: "TOOLS.md", content: profile.toolsMd },
        { key: "memoryMd", name: "MEMORY.md", content: profile.memoryMd },
        ...profile.documents.map((document) => ({ ...document })),
      ];
      this.selectDocument(0);
      this.profileDialog.showModal();
    } catch (error) {
      this.notify(error);
    }
  }

  renderTools(selected) {
    const nodes = this.tools.map((tool) => {
      const label = document.createElement("label"),
        input = document.createElement("input"),
        text = document.createElement("span"),
        name = document.createElement("strong"),
        description = document.createElement("small");
      input.type = "checkbox";
      input.value = tool.name;
      input.checked = selected.has(tool.name);
      input.onchange = () => this.updateToolSummary();
      name.textContent = tool.name +
        (tool.sensitive ? t("agent.sensitiveSuffix") : tool.destructive ? t("agent.destructiveSuffix") : "");
      description.textContent = tool.description;
      text.append(name, description);
      label.append(input, text);
      return label;
    });
    this.profileDialog.querySelector('[data-role="tools"]').replaceChildren(...nodes);
  }

  presetToolNames() {
    const mode = this.profileForm.elements.toolMode.value;
    return new Set(this.tools.filter((tool) => mode === "all" || tool.readOnly).map((tool) => tool.name));
  }

  changeToolSelection() {
    const custom = this.profileForm.elements.toolMode.value === "custom";
    if (custom)
      this.profileDialog.querySelector('[data-role="tool-details"]').open = true;
    if (!custom) {
      const selected = this.presetToolNames();
      for (const input of this.profileDialog.querySelectorAll('[data-role="tools"] input'))
        input.checked = selected.has(input.value);
    }
    this.updateToolSelection();
  }

  updateToolSelection() {
    const custom = this.profileForm.elements.toolMode.value === "custom";
    for (const input of this.profileDialog.querySelectorAll('[data-role="tools"] input'))
      input.disabled = !custom;
    this.updateToolSummary();
  }

  updateToolSummary() {
    this.profileDialog.querySelector('[data-role="tool-summary"]').textContent = t("agent.toolDetails", {
      selected: this.profileDialog.querySelectorAll('[data-role="tools"] input:checked').length,
      total: this.tools.length,
    });
  }

  renderDocumentList() {
    this.profileDialog.querySelector('[data-role="documents"]').replaceChildren(
      ...this.profileDocuments.map((entry, index) => {
        const button = document.createElement("button");
        button.type = "button";
        button.textContent = entry.name || t("agent.documentName");
        button.title = button.textContent;
        button.setAttribute("role", "option");
        button.setAttribute("aria-selected", String(index === this.selectedDocument));
        button.onclick = () => this.selectDocument(index);
        return button;
      }),
    );
    this.profileDialog.querySelector('[data-action="add-document"]').disabled =
      this.profileDocuments.filter((document) => !document.key).length >= 16;
  }

  selectDocument(index) {
    this.selectedDocument = index;
    const document = this.profileDocuments[index], form = this.profileForm;
    form.elements.documentName.value = document.name;
    form.elements.documentName.readOnly = !!document.key;
    form.elements.documentContent.value = document.content;
    form.elements.documentContent.maxLength = document.key === "systemPrompt" ? 65536 : 262144;
    this.profileDialog.querySelector('[data-action="delete-document"]').disabled = !!document.key;
    this.renderDocumentList();
  }

  addDocument() {
    if (this.profileDocuments.filter((document) => !document.key).length >= 16) return;
    const names = new Set(this.profileDocuments.map((document) => document.name.toLowerCase()));
    let name = "NOTES.md", suffix = 2;
    while (names.has(name.toLowerCase())) name = `NOTES-${suffix++}.md`;
    this.profileDocuments.push({ name, content: "" });
    this.selectDocument(this.profileDocuments.length - 1);
    this.profileForm.elements.documentName.focus();
    this.profileForm.elements.documentName.select();
  }

  deleteDocument() {
    if (this.profileDocuments[this.selectedDocument].key) return;
    this.profileDocuments.splice(this.selectedDocument, 1);
    this.selectDocument(Math.min(this.selectedDocument, this.profileDocuments.length - 1));
  }

  profileRequest() {
    const request = {
      toolNames: [...this.profileDialog.querySelectorAll('[data-role="tools"] input:checked')].map(
        (input) => input.value,
      ),
      documents: [],
    };
    const names = new Set();
    for (const document of this.profileDocuments) {
      const name = document.name.trim();
      if (!name || names.has(name.toLowerCase())) throw new Error(t("agent.uniqueDocumentName"));
      names.add(name.toLowerCase());
      if (document.key) request[document.key] = document.content;
      else request.documents.push({ name, content: document.content });
    }
    return request;
  }

  async saveProfile() {
    if (!this.profileForm.reportValidity()) return;
    try {
      await this.put("/api/agent/profile", this.profileRequest());
      this.profileDialog.close();
    } catch (error) {
      this.notify(error);
    }
  }

}

const settingsDialogs = `
    <dialog id="agentModelDialog" class="agent-settings-dialog">
      <form method="dialog" class="agent-settings-form">
        <header>
          <h2 data-i18n="agent.models">模型</h2>
          <select data-role="model-list" data-i18n-aria-label="agent.models" aria-label="模型"></select>
          <button type="button" data-action="new-model" data-i18n="common.create">新建</button
          ><button type="button" data-action="delete-model" class="danger" data-i18n="common.delete"
            >删除</button
          >
        </header>
        <div class="agent-settings-body">
          <label><span data-i18n="common.name">名称</span><input name="name" maxlength="128" required /></label>
          <label
            ><span data-i18n="agent.provider">Provider</span
            ><select name="provider" required></select>
            <input name="customProvider" maxlength="128" hidden />
          </label>
          <label
            ><span data-i18n="agent.protocol">接口协议</span
            ><select name="protocol">
              <option value="1">OpenAI Responses</option>
              <option value="2">OpenAI Chat Completions</option>
              <option value="3">Anthropic Messages</option>
            </select></label
          >
          <label
            ><span data-i18n="agent.baseUrl">Base URL</span
            ><input name="baseUrl" type="url" maxlength="2048" required /></label
          >
          <label
            ><span data-i18n="agent.authentication">认证</span
            ><select name="authentication">
              <option value="1" data-i18n="agent.auth.apiKey">API Key</option>
              <option value="2" data-i18n="agent.auth.bearer">Bearer Token</option>
              <option value="3" data-i18n="agent.auth.none">无认证</option>
              <option value="4" data-i18n="agent.auth.oauth">OAuth Access Token</option>
            </select></label
          >
          <label data-role="credential-row">
            <span data-role="credential-label">API Key</span>
            <span class="agent-secret-input"
              ><input name="credential" type="password" maxlength="32768" autocomplete="off" />
              <button type="button" data-action="reveal-credential" data-i18n="agent.reveal">显示</button
              ><button type="button" data-action="copy-credential" data-i18n="common.copy">复制</button></span
            >
          </label>
          <label
            ><span data-i18n="agent.modelId">模型 ID</span
            ><input name="modelId" list="agentCatalogModels" maxlength="256" required />
            <datalist id="agentCatalogModels"></datalist>
          </label>
          <label
            ><span data-i18n="agent.contextWindow">上下文窗口</span
            ><input name="contextWindow" type="number" min="1" max="100000000" required /></label
          >
          <label
            ><span data-i18n="agent.maximumOutput">最大输出 Token</span
            ><input name="maximumOutputTokens" type="number" min="1" max="10000000" required /></label
          >
          <label
            ><span data-i18n="agent.reasoning">Thinking / Reasoning</span
            ><select name="reasoning">
              <option value="0" data-i18n="common.none">无</option>
              <option value="1">minimal</option>
              <option value="2">low</option>
              <option value="3">medium</option>
              <option value="4">high</option>
              <option value="5">xhigh</option>
              <option value="6">max</option>
            </select></label
          >
          <label
            ><span data-i18n="agent.requestTimeout">请求超时（秒）</span
            ><input name="requestTimeoutSeconds" type="number" min="1" max="600" value="60" required /></label
          >
          <label class="agent-settings-wide"
            ><span data-i18n="agent.advancedJson">高级参数 JSON</span
            ><textarea name="advancedJson" rows="6" maxlength="65536">{}</textarea></label
          >
          <p class="agent-model-description muted" data-role="model-description"></p>
          <p class="muted" data-i18n="agent.secretStorage">凭据使用当前 Windows 账户加密后保存在本机。</p>
          <p data-role="test-result" class="agent-test-result" hidden></p>
        </div>
        <div class="dialog-actions">
          <span
            ><button type="button" data-action="test-model" data-i18n="agent.testConnection"
              >测试连接</button
            ></span
          >
          <span class="spacer"></span>
          <button value="cancel" formnovalidate data-i18n="common.cancel">取消</button
          ><button value="save" data-i18n="common.save">保存</button>
        </div>
      </form>
    </dialog>

    <dialog id="agentProfileDialog" class="agent-settings-dialog">
      <form method="dialog" class="agent-settings-form">
        <header><h2 data-i18n="agent.profile">Profile</h2></header>
        <div class="agent-profile-body">
          <p class="muted" data-i18n="agent.profileHint">
            所有会话共用这一份 Profile；修改后从下一轮消息开始生效。
          </p>
          <fieldset class="agent-tool-settings">
            <legend data-i18n="agent.tools">工具</legend>
            <div class="agent-tool-presets">
              <label><input type="radio"
                name="toolMode" value="read" checked /><span data-i18n="agent.readOnlyTools">只读</span></label>
              <label><input type="radio"
                name="toolMode" value="all" /><span data-i18n="agent.allTools">全部</span></label>
              <label><input type="radio"
                name="toolMode" value="custom" /><span data-i18n="agent.customTools">自定义</span></label>
            </div>
            <details data-role="tool-details">
              <summary data-role="tool-summary"></summary>
              <div class="agent-tool-list" data-role="tools"></div>
            </details>
          </fieldset>
          <section class="agent-document-editor">
            <aside class="agent-document-sidebar">
              <div class="agent-document-list" data-role="documents" role="listbox"
                data-i18n-aria-label="agent.profileDocuments" aria-label="提示词与 Markdown"></div>
              <button type="button" data-action="add-document" data-i18n="agent.addDocument">新建 Markdown</button>
            </aside>
            <div class="agent-document-content">
              <div class="agent-document-toolbar">
                <input name="documentName" maxlength="128" required
                  data-i18n-aria-label="agent.documentName" aria-label="文档名" />
                <button type="button" data-action="delete-document" class="danger" data-i18n="common.delete">删除</button>
              </div>
              <textarea name="documentContent" spellcheck="false"
                data-i18n-aria-label="agent.markdownContent" aria-label="Markdown 内容"></textarea>
            </div>
          </section>
        </div>
        <div class="dialog-actions">
          <button value="cancel" formnovalidate data-i18n="common.cancel">取消</button
          ><button value="save" data-i18n="common.save">保存</button>
        </div>
      </form>
    </dialog>
`;
