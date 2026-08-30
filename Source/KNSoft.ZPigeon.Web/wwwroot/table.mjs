import { t } from "./i18n.mjs";

const collator = new Intl.Collator(undefined, { numeric: true, sensitivity: "base" }),
  selector = "table:not([data-table-static])";

function compare(left, right) {
  const a = left.dataset.sortValue ?? left.textContent.trim(),
    b = right.dataset.sortValue ?? right.textContent.trim(),
    an = Number(a.replaceAll(",", "")),
    bn = Number(b.replaceAll(",", ""));
  return Number.isFinite(an) && Number.isFinite(bn) ? an - bn : collator.compare(a, b);
}

function sort(table, index, direction) {
  const body = table.tBodies[0];
  if (!body) return;
  const rows = [...body.rows].map((row, order) => ({ row, order }));
  rows.sort(
    (left, right) =>
      direction * compare(left.row.cells[index] ?? left.row, right.row.cells[index] ?? right.row) ||
      left.order - right.order,
  );
  if (rows.some((item, index) => item.row !== body.rows[index])) body.append(...rows.map((item) => item.row));
}

function prepare(table) {
  if (table.dataset.tableReady) return;
  table.dataset.tableReady = "true";
  let column = -1,
    direction = 1,
    resizing = false,
    suppressClick = false;
  const configureCells = () => {
    for (const cell of table.querySelectorAll("td[data-copyable]:not(.copyable-cell)")) {
      cell.classList.add("copyable-cell");
      const value = document.createElement("span"),
        mask = cell.hasAttribute("data-copy-mask") ? document.createElement("span") : null,
        button = document.createElement("button");
      value.className = "cell-copy-value";
      value.append(...cell.childNodes);
      if (mask) {
        value.textContent = cell.dataset.copyValue || "—";
        mask.className = "cell-copy-mask";
        mask.textContent = cell.dataset.copyMask;
        mask.setAttribute("aria-hidden", "true");
      }
      button.className = "cell-copy";
      button.type = "button";
      button.title = button.ariaLabel = t("common.copy");
      button.onclick = async (event) => {
        event.stopPropagation();
        await navigator.clipboard.writeText(cell.dataset.copyValue ?? value.textContent);
      };
      if (mask) cell.append(value, mask, button);
      else cell.append(value, button);
    }
  },
  configure = () => {
    const headers = [...(table.tHead?.rows[0]?.cells ?? [])];
    headers.forEach((header, index) => {
      if (header.dataset.tableHeader) return;
      header.dataset.tableHeader = "true";
      const sortable = !header.hasAttribute("data-table-unsortable");
      if (getComputedStyle(header).position === "static") header.style.position = "relative";
      const resize = document.createElement("span");
      resize.className = "column-resizer";
      resize.setAttribute("aria-hidden", "true");
      header.append(resize);
      if (sortable) {
        header.tabIndex = 0;
        header.classList.add("sortable-column");
        const apply = () => {
          if (resizing || suppressClick) {
            suppressClick = false;
            return;
          }
          if (column === index) direction = -direction;
          else {
            column = index;
            direction = 1;
          }
          headers.forEach((item) => item.removeAttribute("aria-sort"));
          header.setAttribute("aria-sort", direction > 0 ? "ascending" : "descending");
          sort(table, column, direction);
        };
        header.onclick = apply;
        header.onkeydown = (event) => {
          if (event.key === "Enter" || event.key === " ") {
            event.preventDefault();
            apply();
          }
        };
      }
      resize.onpointerdown = (event) => {
        event.preventDefault();
        event.stopPropagation();
        resizing = true;
        suppressClick = sortable;
        resize.setPointerCapture(event.pointerId);
        const widths = headers.map((item) => item.getBoundingClientRect().width),
          width = widths[index],
          total = widths.reduce((sum, value) => sum + value, 0),
          start = event.clientX,
          finish = (finishEvent) => {
            resizing = false;
            if (finishEvent.type === "pointercancel") suppressClick = false;
            resize.onpointermove = resize.onpointerup = resize.onpointercancel = null;
          };
        headers.forEach((item, index) => (item.style.width = `${widths[index]}px`));
        resize.onpointermove = (move) => {
          const next = Math.max(48, width + move.clientX - start);
          header.style.width = `${next}px`;
          table.style.width = `${Math.max(table.parentElement.clientWidth, total - width + next)}px`;
        };
        resize.onpointerup = resize.onpointercancel = finish;
      };
    });
  };
  configure();
  configureCells();
  new MutationObserver(() => {
    configure();
    configureCells();
    if (column >= 0) {
      const header = table.tHead?.rows[0]?.cells[column];
      if (header && !header.hasAttribute("data-table-unsortable")) sort(table, column, direction);
      else {
        column = -1;
        direction = 1;
      }
    }
  }).observe(table, { childList: true, subtree: true });
}

export function enableManagedTables(root = document) {
  root.querySelectorAll(selector).forEach(prepare);
  new MutationObserver((records) =>
    records.forEach((record) =>
      record.addedNodes.forEach((node) => {
        if (node.nodeType !== Node.ELEMENT_NODE) return;
        if (node.matches?.(selector)) prepare(node);
        node.querySelectorAll?.(selector).forEach(prepare);
      }),
    ),
  ).observe(root, { childList: true, subtree: true });
}

export function revealTableRow(row) {
  requestAnimationFrame(() => {
    if (row?.isConnected) row.scrollIntoView({ block: "center", inline: "nearest" });
  });
}
