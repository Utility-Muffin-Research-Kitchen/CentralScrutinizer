"use client";

import { useEffect, useMemo, useRef, useState } from "react";

import { ApiError, getLibraryOrder, saveLibraryMetadata, undoLibraryMetadata } from "../lib/api";
import { buildOrderChanges, moveKey, orderRowKey, pinnedKeysOf } from "../lib/library-order";
import type { LibraryOrderResponse, LibraryOrderRow, PlatformSummary } from "../lib/types";
import { tFormat, useT } from "../lib/i18n";

const ROWS_PER_PAGE = 200;

const smallButton =
  "rounded-md border border-[var(--border)] px-2 py-1 text-xs text-[var(--muted)] transition hover:text-[var(--text)] disabled:opacity-40";

function rowLabel(row: LibraryOrderRow): string {
  return row.kind === "folder" ? `${row.name}/` : row.name;
}

export function LibraryOrderView({ csrf, platform }: { csrf: string; platform: PlatformSummary }) {
  const t = useT();
  const [folder, setFolder] = useState("");
  const [level, setLevel] = useState<LibraryOrderResponse | null>(null);
  const [pinned, setPinned] = useState<string[]>([]);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [notice, setNotice] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [query, setQuery] = useState("");
  const [visibleCount, setVisibleCount] = useState(ROWS_PER_PAGE);
  const dragIndex = useRef<number | null>(null);

  async function load(target: string) {
    const response = await getLibraryOrder(platform.tag, target, csrf);

    setLevel(response);
    setPinned(pinnedKeysOf(response.rows));
    setFolder(response.folder);
  }

  useEffect(() => {
    let active = true;

    setLoading(true);
    setError(null);
    getLibraryOrder(platform.tag, "", csrf)
      .then((response) => {
        if (active) {
          setLevel(response);
          setPinned(pinnedKeysOf(response.rows));
          setFolder(response.folder);
        }
      })
      .catch(() => {
        if (active) {
          setError(t("Could not load this system's order."));
        }
      })
      .finally(() => {
        if (active) {
          setLoading(false);
        }
      });
    return () => {
      active = false;
    };
  }, [csrf, platform.tag]);

  const rows = level?.rows ?? [];
  const byKey = useMemo(() => new Map(rows.map((row) => [orderRowKey(row), row])), [rows]);
  const changes = useMemo(() => buildOrderChanges(rows, pinned), [rows, pinned]);
  const dirty = changes.length > 0;
  const others = useMemo(() => {
    const needle = query.trim().toLowerCase();

    return rows.filter(
      (row) =>
        !pinned.includes(orderRowKey(row)) && (needle === "" || row.name.toLowerCase().includes(needle)),
    );
  }, [rows, pinned, query]);

  async function goTo(target: string) {
    setLoading(true);
    setError(null);
    setNotice(null);
    setQuery("");
    setVisibleCount(ROWS_PER_PAGE);
    try {
      await load(target);
    } catch {
      setError(t("Could not load this system's order."));
    } finally {
      setLoading(false);
    }
  }

  async function save() {
    setSaving(true);
    setError(null);
    setNotice(null);
    try {
      const result = await saveLibraryMetadata(platform.tag, changes, csrf, { undoable: true });

      await load(folder);
      setNotice(
        result.refreshed ? t("Order saved.") : t("Order saved. The handheld will show it after its next library refresh."),
      );
    } catch (caught) {
      if (caught instanceof ApiError && caught.code === "conflict") {
        await load(folder).catch(() => undefined);
        setError(t("Nothing was saved, because this list changed somewhere else. It's been reloaded, so please try again."));
      } else if (caught instanceof ApiError && caught.code === "scan_running") {
        setError(t("Leaf is scanning the library. Try again when the scan finishes."));
      } else {
        setError(t("Could not save your changes."));
      }
    } finally {
      setSaving(false);
    }
  }

  async function undo() {
    setSaving(true);
    setError(null);
    setNotice(null);
    try {
      const result = await undoLibraryMetadata(platform.tag, csrf);

      await load(folder);
      setNotice(tFormat(t, "Changes undone: {count}.", { count: result.restored }));
    } catch (caught) {
      setError(
        caught instanceof ApiError && caught.code === "scan_running"
          ? t("Leaf is scanning the library. Try again when the scan finishes.")
          : t("Could not save your changes."),
      );
    } finally {
      setSaving(false);
    }
  }

  /* Pointer events, so one implementation works with a finger and a mouse. */
  function dragOver(clientX: number, clientY: number) {
    if (dragIndex.current === null) {
      return;
    }

    const target = document.elementFromPoint(clientX, clientY)?.closest<HTMLElement>("[data-pin-index]");
    const over = target ? Number(target.dataset.pinIndex) : -1;

    if (over >= 0 && over !== dragIndex.current) {
      const from = dragIndex.current;

      dragIndex.current = over;
      setPinned((keys) => moveKey(keys, from, over));
    }
  }

  if (loading && !level) {
    return <p className="text-sm italic text-[var(--muted)]">{t("Loading games...")}</p>;
  }

  return (
    <div className="space-y-4">
      <p className="text-sm text-[var(--muted)]">
        {t("Pinned rows come first on the handheld, in this order. Everything else stays sorted.")}{" "}
        {level?.showFolders
          ? t("A game pinned inside a folder also rises if you turn Show Folders off.")
          : t("Show Folders is off on the handheld, so this is one flat list.")}
      </p>

      {notice ? (
        <p className="rounded-md border border-[var(--border)] px-3 py-2 text-sm text-[var(--text)]" role="status">
          {notice}
        </p>
      ) : null}
      {error ? (
        <p className="text-sm text-[var(--accent)]" role="alert">
          {error}
        </p>
      ) : null}

      {level && level.parent !== null ? (
        <div className="flex items-center gap-3 text-sm">
          <button className={smallButton} disabled={dirty || loading} onClick={() => void goTo(level.parent ?? "")} type="button">
            {t("Up one folder")}
          </button>
          <span className="truncate text-[var(--muted)]">{level.folder}</span>
        </div>
      ) : null}

      <section aria-label={t("Pinned")} className="rounded-xl border border-[var(--border)] bg-[var(--panel)]">
        <h2 className="border-b border-[var(--line)] px-4 py-2 text-sm font-semibold text-[var(--text)]">
          {t("Pinned")}
        </h2>
        {pinned.length === 0 ? (
          <p className="px-4 py-3 text-sm text-[var(--muted)]">{t("Nothing pinned here. Pin a row below to move it to the top.")}</p>
        ) : (
          <ol className="divide-y divide-[var(--line)]">
            {pinned.map((key, index) => {
              const row = byKey.get(key);

              if (!row) {
                return null;
              }
              return (
                <li key={key} className="flex items-center gap-2 px-3 py-2" data-pin-index={index}>
                  <span
                    aria-hidden="true"
                    className="cursor-grab select-none px-1 text-[var(--muted)]"
                    data-testid="drag-handle"
                    onPointerDown={(event) => {
                      dragIndex.current = index;
                      event.currentTarget.setPointerCapture?.(event.pointerId);
                    }}
                    onPointerMove={(event) => dragOver(event.clientX, event.clientY)}
                    onPointerUp={() => {
                      dragIndex.current = null;
                    }}
                    onPointerCancel={() => {
                      dragIndex.current = null;
                    }}
                    style={{ touchAction: "none" }}
                  >
                    ⠿
                  </span>
                  <span className="min-w-0 flex-1 truncate text-sm text-[var(--text)]">{rowLabel(row)}</span>
                  <button
                    aria-label={tFormat(t, "Move {name} up", { name: row.name })}
                    className={smallButton}
                    disabled={index === 0 || saving}
                    onClick={() => setPinned((keys) => moveKey(keys, index, index - 1))}
                    type="button"
                  >
                    ↑
                  </button>
                  <button
                    aria-label={tFormat(t, "Move {name} down", { name: row.name })}
                    className={smallButton}
                    disabled={index === pinned.length - 1 || saving}
                    onClick={() => setPinned((keys) => moveKey(keys, index, index + 1))}
                    type="button"
                  >
                    ↓
                  </button>
                  <button
                    aria-label={tFormat(t, "Unpin {name}", { name: row.name })}
                    className={smallButton}
                    disabled={saving}
                    onClick={() => setPinned((keys) => keys.filter((other) => other !== key))}
                    type="button"
                  >
                    {t("Unpin")}
                  </button>
                </li>
              );
            })}
          </ol>
        )}
      </section>

      <div className="flex flex-wrap items-center gap-2">
        <button
          className="rounded-md border border-[var(--accent)] bg-[var(--accent)] px-3 py-2 text-xs font-semibold text-white transition disabled:opacity-50"
          disabled={!dirty || saving}
          onClick={() => void save()}
          type="button"
        >
          {saving ? t("Saving...") : t("Save order")}
        </button>
        <button
          className={smallButton}
          disabled={!dirty || saving}
          onClick={() => setPinned(pinnedKeysOf(rows))}
          type="button"
        >
          {t("Discard changes")}
        </button>
        {level && level.undoCount > 0 && !dirty ? (
          <button className={smallButton} disabled={saving} onClick={() => void undo()} type="button">
            {tFormat(t, "Undo last bulk change ({count})", { count: level.undoCount })}
          </button>
        ) : null}
        {dirty ? <span className="text-xs text-[var(--muted)]">{t("Save or discard before opening another folder.")}</span> : null}
      </div>

      <input
        aria-label={t("Search this list")}
        className="w-full rounded-md border border-[var(--border)] bg-[var(--panel)] px-3 py-2 text-sm text-[var(--text)]"
        onChange={(event) => {
          setQuery(event.target.value);
          setVisibleCount(ROWS_PER_PAGE);
        }}
        placeholder={t("Search this list...")}
        type="search"
        value={query}
      />

      {others.length === 0 ? (
        <p className="text-sm text-[var(--muted)]">{t("No other rows here.")}</p>
      ) : (
        <ul className="divide-y divide-[var(--line)] rounded-xl border border-[var(--border)] bg-[var(--panel)]">
          {others.slice(0, visibleCount).map((row) => (
            <li key={orderRowKey(row)} className="flex items-center gap-2 px-4 py-2">
              <span className="min-w-0 flex-1 truncate text-sm text-[var(--text)]">{rowLabel(row)}</span>
              {row.kind === "folder" ? (
                <button
                  aria-label={tFormat(t, "Open {name}", { name: row.name })}
                  className={smallButton}
                  disabled={dirty || loading}
                  onClick={() => void goTo(row.path)}
                  type="button"
                >
                  {t("Open")}
                </button>
              ) : null}
              <button
                aria-label={tFormat(t, "Pin {name}", { name: row.name })}
                className={smallButton}
                disabled={saving}
                onClick={() => setPinned((keys) => [...keys, orderRowKey(row)])}
                type="button"
              >
                {t("Pin")}
              </button>
            </li>
          ))}
        </ul>
      )}
      {others.length > visibleCount ? (
        <button
          className="w-full rounded-md border border-[var(--border)] px-3 py-2 text-xs text-[var(--muted)] transition hover:text-[var(--text)]"
          onClick={() => setVisibleCount((count) => count + ROWS_PER_PAGE)}
          type="button"
        >
          {tFormat(t, "Show more ({count} left)", { count: others.length - visibleCount })}
        </button>
      ) : null}
    </div>
  );
}
