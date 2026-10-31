"use client";

import { useRef, useState } from "react";

import { ApiError, getLibraryGames, saveLibraryMetadata, undoLibraryMetadata } from "../lib/api";
import {
  buildCsvExport,
  InvalidCsvError,
  makeFindReplace,
  planCsvImport,
  planTitleTransform,
  stripRegionAndVersionTags,
  type BulkPlan,
  type BulkSkip,
} from "../lib/library-bulk";
import type { LibraryGame, PlatformSummary } from "../lib/types";
import { tFormat, useT } from "../lib/i18n";

const SKIP_REASON_KEYS: Record<BulkSkip["reason"], string> = {
  no_game: "No game with this file",
  other_card: "From a different second card",
  too_long: "Too long",
  empty_title: "Would leave the title empty",
  duplicate_row: "Listed more than once",
  no_description: "This version of Leaf can't show its description",
};

function BulkPreviewDialog({
  plan,
  saving,
  error,
  onApply,
  onCancel,
}: {
  plan: BulkPlan;
  saving: boolean;
  error: string | null;
  onApply: (ids: Set<string>) => void;
  onCancel: () => void;
}) {
  const t = useT();
  const [excluded, setExcluded] = useState<Set<string>>(new Set());
  const selectedCount = plan.rows.length - excluded.size;

  return (
    <div
      aria-labelledby="bulk-preview-title"
      aria-modal="true"
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 p-4"
      role="dialog"
    >
      <div className="flex max-h-[85vh] w-full max-w-3xl flex-col overflow-hidden rounded-2xl border border-[var(--border)] bg-[var(--panel)] shadow-2xl">
        <div className="border-b border-[var(--line)] px-5 py-4">
          <h2 className="text-base font-semibold text-[var(--text)]" id="bulk-preview-title">
            {t("Review changes")}
          </h2>
          <p className="text-xs text-[var(--muted)]">
            {tFormat(t, "Changes: {count}. Untick any you don't want.", { count: plan.rows.length })}
          </p>
          {plan.duplicateTitles > 0 ? (
            <p className="mt-1 text-xs text-[var(--accent)]">
              {tFormat(t, "Titles shared by more than one game afterwards: {count}", { count: plan.duplicateTitles })}
            </p>
          ) : null}
        </div>

        <div className="flex-1 overflow-y-auto">
          {plan.skipped.length > 0 ? (
            <details className="border-b border-[var(--line)] px-5 py-3 text-xs text-[var(--muted)]">
              <summary>{tFormat(t, "Rows left out: {count}", { count: plan.skipped.length })}</summary>
              <ul className="mt-2 space-y-1">
                {plan.skipped.map((skip, index) => (
                  <li key={`${skip.label}-${index}`}>
                    <span className="break-all text-[var(--text)]">{skip.label}</span>: {t(SKIP_REASON_KEYS[skip.reason])}
                  </li>
                ))}
              </ul>
            </details>
          ) : null}
          {plan.rows.length === 0 ? (
            <p className="px-5 py-4 text-sm text-[var(--muted)]">{t("Nothing would change.")}</p>
          ) : (
            <ul className="divide-y divide-[var(--line)]">
              {plan.rows.map((row) => (
                <li key={row.id} className="flex items-start gap-3 px-5 py-3">
                  <input
                    aria-label={tFormat(t, "Include {name}", { name: row.path })}
                    checked={!excluded.has(row.id)}
                    className="mt-1"
                    disabled={saving}
                    onChange={(event) => {
                      const next = new Set(excluded);

                      if (event.target.checked) {
                        next.delete(row.id);
                      } else {
                        next.add(row.id);
                      }
                      setExcluded(next);
                    }}
                    type="checkbox"
                  />
                  <div className="min-w-0 text-sm">
                    <p className="break-words text-[var(--muted)] line-through">{row.before || t("(empty)")}</p>
                    <p className="break-words text-[var(--text)]">{row.after || t("(empty)")}</p>
                    <p className="truncate text-xs text-[var(--muted)]">
                      {t(row.field === "title" ? "Title" : "Description")} · {row.path}
                    </p>
                  </div>
                </li>
              ))}
            </ul>
          )}
        </div>

        {error ? (
          <p className="border-t border-[var(--line)] px-5 py-3 text-sm text-[var(--accent)]" role="alert">
            {error}
          </p>
        ) : null}
        <div className="flex justify-end gap-2 border-t border-[var(--line)] px-5 py-4">
          <button
            className="rounded-md border border-[var(--border)] px-3 py-2 text-xs text-[var(--muted)] transition hover:text-[var(--text)] disabled:opacity-50"
            disabled={saving}
            onClick={onCancel}
            type="button"
          >
            {t("Cancel")}
          </button>
          <button
            className="rounded-md border border-[var(--accent)] bg-[var(--accent)] px-3 py-2 text-xs font-semibold text-white transition disabled:opacity-50"
            disabled={saving || selectedCount === 0}
            onClick={() => onApply(new Set(plan.rows.filter((row) => !excluded.has(row.id)).map((row) => row.id)))}
            type="button"
          >
            {saving ? t("Saving...") : tFormat(t, "Apply changes ({count})", { count: selectedCount })}
          </button>
        </div>
      </div>
    </div>
  );
}

function downloadText(text: string, fileName: string) {
  const url = URL.createObjectURL(new Blob([text], { type: "text/csv;charset=utf-8" }));
  const link = document.createElement("a");

  link.href = url;
  link.download = fileName;
  link.click();
  URL.revokeObjectURL(url);
}

const toolButton =
  "rounded-md border border-[var(--border)] px-3 py-2 text-xs text-[var(--muted)] transition hover:text-[var(--text)] disabled:opacity-50";

export function LibraryBulkTools({
  csrf,
  platform,
  shownGames,
  undoCount,
  onChanged,
  onNotice,
}: {
  csrf: string;
  platform: PlatformSummary;
  /* What the search currently leaves in the list, which is what title tools act on. */
  shownGames: LibraryGame[];
  undoCount: number;
  onChanged: () => Promise<unknown>;
  onNotice: (notice: string) => void;
}) {
  const t = useT();
  const [findOpen, setFindOpen] = useState(false);
  const [find, setFind] = useState("");
  const [replace, setReplace] = useState("");
  const [regex, setRegex] = useState(false);
  const [caseSensitive, setCaseSensitive] = useState(false);
  const [plan, setPlan] = useState<BulkPlan | null>(null);
  const [saving, setSaving] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [previewError, setPreviewError] = useState<string | null>(null);
  const fileInput = useRef<HTMLInputElement>(null);

  function previewFindReplace() {
    setError(null);
    try {
      setPlan(planTitleTransform(shownGames, makeFindReplace(find, replace, { regex, caseSensitive })));
      setPreviewError(null);
    } catch {
      setError(t("That regular expression isn't valid."));
    }
  }

  function saveErrorMessage(caught: unknown): string {
    if (caught instanceof ApiError && caught.code === "scan_running") {
      return t("Leaf is scanning the library. Try again when the scan finishes.");
    }
    if (caught instanceof ApiError && caught.code === "undo_unavailable") {
      return t("Nothing was saved, because the undo copy couldn't be written to the SD card.");
    }
    return t("Could not save your changes.");
  }

  async function applyPlan(ids: Set<string>) {
    if (!plan) {
      return;
    }
    setSaving(true);
    setPreviewError(null);
    try {
      const changes = plan.rows.filter((row) => ids.has(row.id)).map((row) => row.change);
      const result = await saveLibraryMetadata(platform.tag, changes, csrf, { undoable: true });

      setPlan(null);
      await onChanged();
      onNotice(
        [
          tFormat(t, "Changes saved: {count}.", { count: result.saved }),
          result.refreshed ? "" : t("The handheld will show them after its next library refresh."),
          result.undoKept ? "" : t("These changes can't be undone, because the undo copy couldn't be kept."),
        ]
          .filter(Boolean)
          .join(" "),
      );
    } catch (caught) {
      if (caught instanceof ApiError && caught.code === "conflict") {
        setPlan(null);
        await onChanged().catch(() => undefined);
        onNotice(t("Nothing was saved, because some of these games changed since the preview. Please try again."));
      } else {
        setPreviewError(saveErrorMessage(caught));
      }
    } finally {
      setSaving(false);
    }
  }

  async function handleExport() {
    setBusy(true);
    setError(null);
    try {
      const response = await getLibraryGames(platform.tag, csrf, { descriptions: true });

      downloadText(buildCsvExport(response.games, response.descriptionSupported), `${platform.tag}-game-info.csv`);
    } catch {
      setError(t("Could not export this system's games."));
    } finally {
      setBusy(false);
    }
  }

  async function handleImport(file: File) {
    setBusy(true);
    setError(null);
    try {
      const [text, response] = await Promise.all([file.text(), getLibraryGames(platform.tag, csrf, { descriptions: true })]);

      setPlan(planCsvImport(text, response.games, response.descriptionSupported));
      setPreviewError(null);
    } catch (caught) {
      setError(
        caught instanceof InvalidCsvError
          ? t("That file needs source and rom_relpath columns, and a title or description column.")
          : t("Could not read that file."),
      );
    } finally {
      setBusy(false);
    }
  }

  async function handleUndo() {
    setBusy(true);
    setError(null);
    try {
      const result = await undoLibraryMetadata(platform.tag, csrf);
      const left = result.skipped.map((row) => row.path);

      await onChanged();
      onNotice(
        [
          tFormat(t, "Changes undone: {count}.", { count: result.restored }),
          left.length > 0
            ? tFormat(t, "Left alone because they changed since or their card is out ({count}): {names}", {
                count: left.length,
                names: left.slice(0, 20).join(", "),
              })
            : "",
          result.refreshed ? "" : t("The handheld will show them after its next library refresh."),
        ]
          .filter(Boolean)
          .join(" "),
      );
    } catch (caught) {
      setError(saveErrorMessage(caught));
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="space-y-3">
      <div className="flex flex-wrap gap-2">
        <button className={toolButton} disabled={busy} onClick={() => setFindOpen((open) => !open)} type="button">
          {t("Find and replace")}
        </button>
        <button
          className={toolButton}
          disabled={busy}
          onClick={() => {
            setError(null);
            setPreviewError(null);
            setPlan(planTitleTransform(shownGames, stripRegionAndVersionTags));
          }}
          type="button"
        >
          {t("Strip region and version tags")}
        </button>
        <button className={toolButton} disabled={busy} onClick={() => void handleExport()} type="button">
          {t("Export CSV")}
        </button>
        <button className={toolButton} disabled={busy} onClick={() => fileInput.current?.click()} type="button">
          {t("Import CSV")}
        </button>
        <input
          accept=".csv,text/csv"
          aria-label={t("CSV file to import")}
          className="hidden"
          onChange={(event) => {
            const file = event.target.files?.[0];

            event.target.value = "";
            if (file) {
              void handleImport(file);
            }
          }}
          ref={fileInput}
          type="file"
        />
        {undoCount > 0 ? (
          <button className={toolButton} disabled={busy} onClick={() => void handleUndo()} type="button">
            {tFormat(t, "Undo last bulk change ({count})", { count: undoCount })}
          </button>
        ) : null}
      </div>

      <p className="text-xs text-[var(--muted)]">
        {tFormat(t, "Find and replace and tag stripping act on the games shown below ({count}). Use the search to narrow them.", {
          count: shownGames.length,
        })}
      </p>

      {findOpen ? (
        <div className="space-y-2 rounded-xl border border-[var(--border)] bg-[var(--panel)] p-4">
          <div className="grid gap-2 sm:grid-cols-2">
            <input
              aria-label={t("Find")}
              className="w-full rounded-md border border-[var(--border)] bg-black/20 px-3 py-2 text-sm text-[var(--text)]"
              onChange={(event) => setFind(event.target.value)}
              placeholder={t("Find")}
              value={find}
            />
            <input
              aria-label={t("Replace with")}
              className="w-full rounded-md border border-[var(--border)] bg-black/20 px-3 py-2 text-sm text-[var(--text)]"
              onChange={(event) => setReplace(event.target.value)}
              placeholder={t("Replace with")}
              value={replace}
            />
          </div>
          <div className="flex flex-wrap items-center gap-4 text-xs text-[var(--muted)]">
            <label className="flex items-center gap-2">
              <input checked={regex} onChange={(event) => setRegex(event.target.checked)} type="checkbox" />
              {t("Regular expression")}
            </label>
            <label className="flex items-center gap-2">
              <input
                checked={caseSensitive}
                onChange={(event) => setCaseSensitive(event.target.checked)}
                type="checkbox"
              />
              {t("Match case")}
            </label>
            <button className={toolButton} disabled={find === ""} onClick={previewFindReplace} type="button">
              {t("Preview changes")}
            </button>
          </div>
        </div>
      ) : null}

      {error ? (
        <p className="text-sm text-[var(--accent)]" role="alert">
          {error}
        </p>
      ) : null}

      {plan ? (
        <BulkPreviewDialog
          error={previewError}
          onApply={(ids) => void applyPlan(ids)}
          onCancel={() => setPlan(null)}
          plan={plan}
          saving={saving}
        />
      ) : null}
    </div>
  );
}
