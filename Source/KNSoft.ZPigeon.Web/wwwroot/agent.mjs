import { apiUrl } from "./client-context.mjs";
import { language, t } from "./i18n.mjs";

const ItemKind = Object.freeze({ user: 1, assistant: 2, toolCall: 3, toolResult: 4, compaction: 5, error: 6 }),
  ItemState = Object.freeze({ completed: 1, queued: 2, running: 3, canceled: 4, failed: 5 }),
  Disposition = Object.freeze({ queue: 1, steer: 2 });

export class AgentManager {
  constructor(root, { get, post, put, remove, download, notify }) {
    this.root = root;
    this.get = get;
    this.post = post;
    this.put = put;
    this.remove = remove;
    this.download = download;
    this.notify = notify;
    this.sessionsRoot = root.querySelector('[data-role="sessions"]');
    this.searchInput = root.querySelector('[data-role="session-search"]');
    this.messagesRoot = root.querySelector('[data-role="messages"]');
    this.input = root.querySelector('[data-role="input"]');
    this.titleButton = root.querySelector('[data-action="rename-session"]');
    this.subtitle = root.querySelector('[data-role="session-subtitle"]');
    this.usageRoot = root.querySelector('[data-role="usage"]');
    this.contextLabel = root.querySelector('[data-role="context-label"]');
    this.tokenLabel = root.querySelector('[data-role="token-label"]');
    this.contextProgress = root.querySelector('[data-role="context"]');
    this.runState = root.querySelector('[data-role="run-state"]');
    this.sendButton = root.querySelector('[data-action="send"]');
    this.steerButton = root.querySelector('[data-action="steer"]');
    this.stopButton = root.querySelector('[data-action="stop"]');
    this.compactButton = root.querySelector('[data-action="compact"]');
    this.forkButton = root.querySelector('[data-action="fork-session"]');
    this.exportButton = root.querySelector('[data-action="export"]');
    this.deleteSessionButton = root.querySelector('[data-action="delete-session"]');
    this.modelDialog = document.querySelector("#agentModelDialog");
    this.modelForm = this.modelDialog.querySelector("form");
    this.profileDialog = document.querySelector("#agentProfileDialog");
    this.profileForm = this.profileDialog.querySelector("form");
    this.newSessionDialog = document.querySelector("#agentNewSessionDialog");
    this.newSessionForm = this.newSessionDialog.querySelector("form");
    this.renameDialog = document.querySelector("#agentRenameSessionDialog");
    this.renameForm = this.renameDialog.querySelector("form");
    this.models = [];
    this.tools = [];
    this.providers = [];
    this.catalogModels = [];
    this.sessions = [];
    this.session = null;
    this.selectedSessionId = null;
    this.modelEditingId = null;
    this.eventSource = null;
    this.connected = false;
    this.loaded = false;
    this.active = false;
    this.refreshing = false;
    this.refreshPending = false;
    this.searchTimer = null;
    this.profileDocuments = [];
    this.selectedDocument = 0;
    this.bindEvents();
    this.renderSessions();
    this.renderSession();
  }

  bindEvents() {
    this.root.querySelector('[data-action="new-session"]').onclick = () => this.openNewSession();
    this.root.querySelector('[data-action="models"]').onclick = () => this.openModels();
    this.profileButton = this.root.querySelector('[data-action="profile"]');
    this.profileButton.onclick = () => this.openProfile();
    this.titleButton.onclick = () => this.openRename();
    this.compactButton.onclick = () => this.compact();
    this.forkButton.onclick = () => this.fork();
    this.exportButton.onclick = () => this.exportSession();
    this.deleteSessionButton.onclick = () => this.deleteSession();
    this.steerButton.onclick = () => this.send(Disposition.steer);
    this.stopButton.onclick = () => this.stop();
    this.root.querySelector("form.agent-composer").onsubmit = (event) => {
      event.preventDefault();
      this.send(Disposition.queue);
    };
    this.input.oninput = () => this.resizeComposer();
    this.input.onkeydown = (event) => {
      if (event.key === "Enter" && !event.shiftKey && !event.ctrlKey && !event.altKey &&
          !event.metaKey && !event.isComposing) {
        event.preventDefault();
        this.send(Disposition.queue);
      }
    };
    this.searchInput.oninput = () => {
      clearTimeout(this.searchTimer);
      this.searchTimer = setTimeout(() => this.loadSessions().catch((error) => this.notify(error)), 250);
    };

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

    this.newSessionForm.onsubmit = (event) => {
      event.preventDefault();
      event.submitter?.value === "cancel" ? this.newSessionDialog.close() : this.createSession();
    };
    this.renameForm.onsubmit = (event) => {
      event.preventDefault();
      event.submitter?.value === "cancel" ? this.renameDialog.close() : this.renameSession();
    };
  }

  async activate(connected) {
    this.active = true;
    this.connected = connected;
    try {
      if (!this.loaded) await this.loadConfiguration();
      if (connected) await this.loadSessions(this.selectedSessionId);
    } catch (error) {
      this.notify(error);
    }
    this.updateState();
  }

  deactivate() {
    this.active = false;
    this.closeEvents();
  }

  disconnect() {
    this.connected = false;
    this.closeEvents();
    this.updateState();
  }

  async loadConfiguration() {
    [this.models, this.tools, this.providers] = await Promise.all([
      this.get("/api/agent/models"),
      this.get("/api/agent/tools"),
      this.get("/api/agent/catalog/providers"),
    ]);
    this.loaded = true;
  }

  async loadSessions(preferredId = this.selectedSessionId) {
    if (!this.connected) return;
    const query = this.searchInput.value.trim(),
      suffix = query ? `?query=${encodeURIComponent(query)}` : "";
    this.sessions = await this.get(`/api/agent/sessions${suffix}`);
    const selected = this.sessions.some((session) => session.id === preferredId)
      ? preferredId
      : this.sessions[0]?.id ?? null;
    this.renderSessions();
    if (selected !== this.selectedSessionId || (selected && !this.session)) await this.selectSession(selected);
    else if (!selected) {
      this.selectedSessionId = null;
      this.session = null;
      this.closeEvents();
      this.renderSession();
    }
  }

  async refreshSessionList() {
    if (!this.connected) return;
    const query = this.searchInput.value.trim(),
      suffix = query ? `?query=${encodeURIComponent(query)}` : "";
    this.sessions = await this.get(`/api/agent/sessions${suffix}`);
    this.renderSessions();
  }

  renderSessions() {
    const nodes = this.sessions.map((session) => {
      const button = document.createElement("button"),
        title = document.createElement("strong"),
        details = document.createElement("small");
      button.className = `agent-session-entry${session.id === this.selectedSessionId ? " selected" : ""}`;
      title.textContent = session.title;
      details.textContent = `${session.modelName} · ${this.formatDate(session.updatedAt)}`;
      button.append(title, details);
      button.onclick = () => this.selectSession(session.id);
      return button;
    });
    if (!nodes.length) {
      const empty = document.createElement("p");
      empty.className = "muted";
      empty.textContent = t(this.searchInput.value.trim() ? "agent.noSearchResults" : "agent.noSessions");
      nodes.push(empty);
    }
    this.sessionsRoot.replaceChildren(...nodes);
  }

  async selectSession(id) {
    this.selectedSessionId = id;
    this.session = null;
    this.closeEvents();
    this.renderSessions();
    this.renderSession();
    if (!id) return;
    try {
      await this.refreshSession();
      this.subscribe(id);
    } catch (error) {
      this.notify(error);
    }
  }

  async refreshSession() {
    if (!this.selectedSessionId || this.refreshing) {
      this.refreshPending = !!this.selectedSessionId;
      return;
    }
    this.refreshing = true;
    const id = this.selectedSessionId;
    try {
      const value = await this.get(`/api/agent/sessions/${id}`);
      if (id !== this.selectedSessionId) return;
      const nearBottom =
        this.messagesRoot.scrollHeight - this.messagesRoot.scrollTop - this.messagesRoot.clientHeight < 80;
      this.session = value;
      this.renderSession();
      if (nearBottom) this.messagesRoot.scrollTop = this.messagesRoot.scrollHeight;
      await this.refreshSessionList();
    } catch (error) {
      this.notify(error);
    } finally {
      this.refreshing = false;
      if (this.refreshPending) {
        this.refreshPending = false;
        this.refreshSession();
      }
    }
  }

  subscribe(id) {
    if (!this.active || !this.connected || id !== this.selectedSessionId) return;
    this.eventSource = new EventSource(apiUrl(`/api/agent/sessions/${id}/events`));
    this.eventSource.addEventListener("changed", () => this.refreshSession());
  }

  closeEvents() {
    this.eventSource?.close();
    this.eventSource = null;
  }

  renderSession() {
    const value = this.session;
    this.titleButton.textContent = value?.session.title ?? t("agent.noSessionSelected");
    this.subtitle.textContent = value ? value.model.name : "";
    this.renderUsage(value?.usage);
    this.renderMessages(value?.items ?? []);
    this.updateState();
  }

  renderUsage(usage) {
    this.usageRoot.hidden = !usage;
    if (!usage) return;
    const latest = usage.latestInput,
      context = usage.contextWindow,
      percent = latest == null || !context ? 0 : Math.min(100, (latest / context) * 100);
    this.contextLabel.textContent =
      latest == null
        ? t("agent.contextUnknown")
        : t("agent.contextUsage", { used: this.number(latest), total: this.number(context) });
    this.tokenLabel.textContent = t("agent.tokenUsage", {
      input: this.number(usage.input),
      cached: this.number(usage.cachedInput),
      output: this.number(usage.output),
      reasoning: this.number(usage.reasoning),
      total: this.number(usage.total),
    });
    this.contextProgress.value = percent;
  }

  renderMessages(items) {
    const nodes = [];
    for (const item of items) {
      if (item.kind === ItemKind.assistant && !item.content) continue;
      if (item.kind === ItemKind.user || item.kind === ItemKind.assistant) {
        nodes.push(this.renderMessage(item, items));
      } else if (item.kind === ItemKind.toolCall || item.kind === ItemKind.toolResult) {
        nodes.push(this.renderTool(item));
      } else if (item.kind === ItemKind.compaction) {
        nodes.push(this.renderCompaction(item));
      } else if (item.kind === ItemKind.error) {
        nodes.push(this.renderError(item));
      }
    }
    if (!nodes.length) {
      const empty = document.createElement("p");
      empty.className = "agent-empty muted";
      empty.textContent = this.session ? t("agent.emptySession") : t("agent.empty");
      nodes.push(empty);
    }
    this.messagesRoot.replaceChildren(...nodes);
  }

  renderMessage(item, items) {
    const article = document.createElement("article"),
      header = document.createElement("header"),
      role = document.createElement("strong"),
      status = document.createElement("span"),
      fork = document.createElement("button"),
      content = document.createElement("div");
    article.className = `agent-message ${item.kind === ItemKind.user ? "user" : "assistant"} ${this.stateName(item.state)}`;
    role.textContent = t(item.kind === ItemKind.user ? "agent.you" : "agent.assistant");
    status.textContent = t(`agent.status.${this.stateName(item.state)}`);
    fork.textContent = t("agent.forkHere");
    fork.type = "button";
    fork.onclick = () => this.fork(item.sequence);
    const hasCalls = items.some(
      (value) =>
        value.kind === ItemKind.toolCall && value.runId === item.runId && value.step === item.step,
    );
    fork.hidden = item.state !== ItemState.completed || (item.kind === ItemKind.assistant && hasCalls);
    content.className = "agent-message-content";
    content.textContent = item.content;
    header.append(role, status, fork);
    article.append(header, content);
    if (item.rawUsage) article.append(this.renderRawUsage(item.rawUsage));
    return article;
  }

  renderTool(item) {
    const details = document.createElement("details"),
      summary = document.createElement("summary"),
      content = document.createElement("pre");
    details.className = "agent-tool-event";
    summary.textContent = t(item.kind === ItemKind.toolCall ? "agent.toolCall" : "agent.toolResult", {
      name: item.name ?? "",
    });
    content.className = "agent-tool-content";
    content.textContent = this.prettyJson(item.content);
    details.append(summary, content);
    return details;
  }

  renderCompaction(item) {
    const details = document.createElement("details"),
      summary = document.createElement("summary"),
      content = document.createElement("div");
    details.className = "agent-compaction";
    summary.textContent = t("agent.compactionCreated");
    content.className = "agent-tool-content";
    content.textContent = item.content;
    details.append(summary, content);
    if (item.rawUsage) details.append(this.renderRawUsage(item.rawUsage));
    return details;
  }

  renderError(item) {
    const article = document.createElement("article"),
      header = document.createElement("header"),
      content = document.createElement("div");
    article.className = "agent-message agent-error";
    header.textContent = t("common.failed");
    content.className = "agent-message-content";
    content.textContent = item.content;
    article.append(header, content);
    return article;
  }

  renderRawUsage(raw) {
    const details = document.createElement("details"),
      summary = document.createElement("summary"),
      content = document.createElement("pre");
    details.className = "agent-raw-usage";
    summary.textContent = t("agent.rawUsage");
    content.className = "agent-tool-content";
    content.textContent = this.prettyJson(raw);
    details.append(summary, content);
    return details;
  }

  resizeComposer() {
    this.input.style.height = "auto";
    this.input.style.height = `${Math.min(160, this.input.scrollHeight + 2)}px`;
  }

  async send(disposition) {
    const content = this.input.value.trim();
    if (!this.connected || !this.selectedSessionId || !content) return;
    try {
      await this.post(`/api/agent/sessions/${this.selectedSessionId}/messages`, {
        content,
        disposition,
      });
      this.input.value = "";
      this.resizeComposer();
      await this.refreshSession();
      this.input.focus();
    } catch (error) {
      this.notify(error);
    }
  }

  async stop() {
    if (!this.selectedSessionId) return;
    try {
      await this.post(`/api/agent/sessions/${this.selectedSessionId}/stop`);
      await this.refreshSession();
    } catch (error) {
      this.notify(error);
    }
  }

  async compact() {
    if (!this.selectedSessionId) return;
    try {
      await this.post(`/api/agent/sessions/${this.selectedSessionId}/compact`);
      await this.refreshSession();
    } catch (error) {
      this.notify(error);
    }
  }

  async fork(throughSequence = null) {
    if (!this.selectedSessionId) return;
    try {
      const value = await this.post(`/api/agent/sessions/${this.selectedSessionId}/fork`, {
        throughSequence,
      });
      await this.loadSessions(value.session.id);
    } catch (error) {
      this.notify(error);
    }
  }

  async exportSession() {
    if (!this.selectedSessionId) return;
    try {
      const file = await this.download(`/api/agent/sessions/${this.selectedSessionId}/export`),
        url = URL.createObjectURL(file.blob),
        anchor = document.createElement("a");
      anchor.href = url;
      anchor.download = file.name;
      anchor.click();
      URL.revokeObjectURL(url);
    } catch (error) {
      this.notify(error);
    }
  }

  openNewSession() {
    if (!this.models.length) {
      this.notify(new Error(t("agent.modelRequired")));
      this.openModels();
      return;
    }
    this.newSessionForm.elements.modelId.replaceChildren(
      ...this.models.map((model) => new Option(model.name, model.id)),
    );
    this.newSessionForm.elements.title.value = t("agent.newSessionTitle");
    this.newSessionDialog.showModal();
  }

  async createSession() {
    if (!this.newSessionForm.reportValidity()) return;
    try {
      const value = await this.post("/api/agent/sessions", {
        modelId: this.newSessionForm.elements.modelId.value,
        title: this.newSessionForm.elements.title.value.trim(),
      });
      this.newSessionDialog.close();
      await this.loadSessions(value.session.id);
    } catch (error) {
      this.notify(error);
    }
  }

  openRename() {
    if (!this.session) return;
    this.renameForm.elements.title.value = this.session.session.title;
    this.renameDialog.showModal();
    this.renameForm.elements.title.select();
  }

  async renameSession() {
    if (!this.renameForm.reportValidity() || !this.selectedSessionId) return;
    try {
      this.session = await this.put(`/api/agent/sessions/${this.selectedSessionId}`, {
        title: this.renameForm.elements.title.value.trim(),
      });
      this.renameDialog.close();
      this.renderSession();
      await this.refreshSessionList();
    } catch (error) {
      this.notify(error);
    }
  }

  async deleteSession() {
    if (!this.session || !confirm(t("agent.confirmDeleteSession", { name: this.session.session.title }))) return;
    try {
      await this.remove(`/api/agent/sessions/${this.selectedSessionId}`);
      this.selectedSessionId = null;
      this.session = null;
      this.closeEvents();
      await this.loadSessions();
    } catch (error) {
      this.notify(error);
    }
  }

  async openModels() {
    try {
      if (!this.loaded) await this.loadConfiguration();
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
    document.querySelector("#agentCatalogModels").replaceChildren(
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
      if (this.selectedSessionId) await this.refreshSession();
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
      if (!this.loaded) await this.loadConfiguration();
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
      ...this.profileDocuments.map((document, index) => {
        const button = documentNode("button");
        button.type = "button";
        button.textContent = document.name || t("agent.documentName");
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

  updateState() {
    const state = this.session?.state,
      hasSession = !!this.session,
      running = !!state?.running,
      queued = state?.queued ?? 0,
      available = this.connected && hasSession;
    this.input.disabled = !available;
    this.sendButton.disabled = !available;
    this.sendButton.textContent = t(running ? "agent.queue" : "common.send");
    this.steerButton.hidden = !running;
    this.steerButton.disabled = !available || !running;
    this.stopButton.hidden = !running && !queued;
    this.stopButton.disabled = !available || (!running && !queued);
    this.compactButton.disabled = !available;
    this.forkButton.disabled = !hasSession;
    this.exportButton.disabled = !hasSession;
    this.deleteSessionButton.disabled = !hasSession || running;
    this.titleButton.disabled = !hasSession;
    this.runState.textContent = running
      ? queued
        ? t("agent.runningQueued", { count: queued })
        : t("agent.running")
      : queued
        ? t("agent.queued", { count: queued })
        : "";
    this.input.placeholder = !this.connected
      ? t("common.clientDisconnected")
      : hasSession
        ? t("agent.placeholder")
        : t("agent.sessionRequired");
  }

  stateName(value) {
    return (
      Object.entries(ItemState).find(([, state]) => state === value)?.[0] ?? "completed"
    );
  }

  prettyJson(value) {
    try {
      return JSON.stringify(JSON.parse(value), null, 2);
    } catch {
      return value;
    }
  }

  number(value) {
    return new Intl.NumberFormat(language).format(value ?? 0);
  }

  formatDate(value) {
    return new Intl.DateTimeFormat(language, { dateStyle: "short", timeStyle: "short" }).format(
      new Date(value),
    );
  }
}

function documentNode(name) {
  return document.createElement(name);
}
