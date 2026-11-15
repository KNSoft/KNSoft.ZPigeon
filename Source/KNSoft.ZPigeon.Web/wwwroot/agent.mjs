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
    this.newSessionDialog = document.querySelector("#agentNewSessionDialog");
    this.newSessionForm = this.newSessionDialog.querySelector("form");
    this.renameDialog = document.querySelector("#agentRenameSessionDialog");
    this.renameForm = this.renameDialog.querySelector("form");
    this.models = [];
    this.sessions = [];
    this.session = null;
    this.selectedSessionId = null;
    this.eventSource = null;
    this.connected = false;
    this.loaded = false;
    this.active = false;
    this.refreshing = false;
    this.refreshPending = false;
    this.searchTimer = null;
    this.bindEvents();
    this.renderSessions();
    this.renderSession();
  }

  bindEvents() {
    this.root.querySelector('[data-action="new-session"]').onclick = () => this.openNewSession();
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
    this.models = await this.get("/api/agent/models");
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

  async openNewSession() {
    try {
      await this.loadConfiguration();
      if (!this.models.length) {
        this.notify(new Error(t("agent.modelRequired")));
        return;
      }
      this.newSessionForm.elements.modelId.replaceChildren(
        ...this.models.map((model) => new Option(model.name, model.id)),
      );
      this.newSessionForm.elements.title.value = t("agent.newSessionTitle");
      this.newSessionDialog.showModal();
    } catch (error) {
      this.notify(error);
    }
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
