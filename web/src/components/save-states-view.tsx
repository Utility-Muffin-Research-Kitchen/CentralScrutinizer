"use client";

import { useEffect, useState } from "react";
import JSZip from "jszip";

import { buildDownloadUrl, deleteItem, getSaveStates } from "../lib/api";
import type { PlatformSummary, SaveStateEntry } from "../lib/types";
import { tFormat, useT } from "../lib/i18n";

function formatDate(value: number): string {
  if (!value) {
    return "—";
  }

  return new Date(value * 1000).toLocaleString();
}

function formatSize(size: number): string {
  if (!size) {
    return "0 B";
  }

  const units = ["B", "KB", "MB", "GB"];
  const index = Math.min(Math.floor(Math.log(size) / Math.log(1024)), units.length - 1);
  const value = size / 1024 ** index;
  return `${value.toFixed(index === 0 ? 0 : value >= 10 ? 0 : 1)} ${units[index]}`;
}

function sanitizeFileName(value: string): string {
  return value.replace(/[^\w.-]+/g, "-").replace(/-+/g, "-").replace(/^-|-$/g, "") || "save-state";
}

function buildArchiveName(platform: PlatformSummary, entry: SaveStateEntry): string {
  return `${sanitizeFileName(platform.tag.toLowerCase())}-${sanitizeFileName(entry.title)}-${sanitizeFileName(entry.slotLabel.toLowerCase())}.zip`;
}

function zipEntryPath(path: string): string {
  return path;
}

export function SaveStatesView({
  csrf,
  platform,
  onBack,
  onChanged,
}: {
  csrf: string | null;
  platform: PlatformSummary;
  onBack: () => void;
  onChanged?: () => void;
}) {
  const t = useT();
  const [entries, setEntries] = useState<SaveStateEntry[]>([]);
  const [entryCount, setEntryCount] = useState(0);
  const [loading, setLoading] = useState(true);
  const [notice, setNotice] = useState<string | null>(null);
  const [busyEntryId, setBusyEntryId] = useState<string | null>(null);
  const [truncated, setTruncated] = useState(false);

  async function loadStates() {
    if (!csrf) {
      setEntries([]);
      setEntryCount(0);
      setTruncated(false);
      setNotice(t("Missing session csrf token."));
      setLoading(false);
      return;
    }

    setLoading(true);
    setNotice(null);
    try {
      const response = await getSaveStates(platform.tag, csrf);

      setEntries(response.entries);
      setEntryCount(response.count);
      setTruncated(response.truncated);
    } catch (error) {
      setEntries([]);
      setEntryCount(0);
      setTruncated(false);
      setNotice(error instanceof Error ? t(error.message) : t("Could not load save states."));
    } finally {
      setLoading(false);
    }
  }

  useEffect(() => {
    void loadStates();
  }, [csrf, platform.tag]);

  async function handleDownload(entry: SaveStateEntry) {
    if (!csrf) {
      setNotice(t("Missing session csrf token."));
      return;
    }

    setBusyEntryId(entry.id);
    setNotice(null);
    try {
      const zip = new JSZip();

      for (const path of entry.downloadPaths) {
        const response = await fetch(buildDownloadUrl("files", path, undefined, csrf));

        if (!response.ok) {
          throw new Error(tFormat(t, "Could not download {name}", { name: path }));
        }
        zip.file(zipEntryPath(path), await response.arrayBuffer());
      }

      const blob = await zip.generateAsync({ type: "blob" });
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");

      link.href = url;
      link.download = buildArchiveName(platform, entry);
      link.click();
      URL.revokeObjectURL(url);
    } catch (error) {
      setNotice(error instanceof Error ? t(error.message) : t("Download failed."));
    } finally {
      setBusyEntryId(null);
    }
  }

  async function handleDelete(entry: SaveStateEntry) {
    if (!csrf) {
      setNotice(t("Missing session csrf token."));
      return;
    }
    if (!window.confirm(`${t("Delete")} ${entry.title} (${entry.slotLabel})?`)) {
      return;
    }

    setBusyEntryId(entry.id);
    setNotice(null);
    try {
      const results = await Promise.allSettled(
        entry.deletePaths.map(async (path) => deleteItem({ scope: "files", path }, csrf)),
      );
      const successCount = results.filter((result) => result.status === "fulfilled").length;
      const failureCount = entry.deletePaths.length - successCount;

      await loadStates();
      onChanged?.();

      if (failureCount === 0) {
        setNotice(`${t("Deleted")} ${entry.title} (${entry.slotLabel}).`);
        return;
      }
      if (successCount === 0) {
        setNotice(`${t("Failed to delete")} ${entry.title} (${entry.slotLabel}).`);
        return;
      }

      setNotice(`${t("Deleted")} ${successCount} / ${entry.deletePaths.length} ${t("files for")} ${entry.title}. ${failureCount} ${t("failed")}.`);
    } catch (error) {
      setNotice(error instanceof Error ? t(error.message) : t("Delete failed."));
    } finally {
      setBusyEntryId(null);
    }
  }

  const primaryActionClass =
    "rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-semibold text-white transition hover:bg-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-50";
  const secondaryActionClass =
    "inline-flex min-h-11 items-center justify-center rounded-md border border-[var(--border)] px-3 py-2 text-sm font-medium leading-none text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:opacity-50";
  const destructiveActionClass =
    "inline-flex min-h-11 items-center justify-center rounded-md border border-rose-300/30 px-3 py-2 text-sm font-medium leading-none text-rose-100 transition hover:border-rose-200/40 disabled:opacity-50";

  return (
    <div className="space-y-6">
      <div className="flex items-start justify-between gap-4">
        <div>
          <button
            className="inline-flex items-center gap-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
            onClick={onBack}
            type="button"
          >
            <span aria-hidden="true">←</span>
            {t("Back")}
          </button>
          <h2 className="mt-4 text-lg font-semibold">{t("Save States")}</h2>
          <p className="mt-1 text-sm text-[var(--muted)]">
            {t("Download or remove grouped save-state bundles for")} {platform.name}.
          </p>
        </div>
        <button
          className={secondaryActionClass}
          disabled={loading || !csrf}
          onClick={() => {
            void loadStates();
          }}
          type="button"
        >
          {t("Refresh")}
        </button>
      </div>

      {notice ? (
        <div className="rounded-xl border border-[var(--border)] bg-[var(--panel)] px-4 py-3 text-sm text-[var(--muted)]">
          {notice}
        </div>
      ) : null}

      {!loading && truncated ? (
        <div className="rounded-2xl border border-amber-300/30 bg-amber-200/10 px-4 py-3 text-sm text-amber-50">
          {t("Showing")} {entries.length} {t("of")} {entryCount} {t("save-state bundles. Refresh after deleting listed entries to load the rest.")}
        </div>
      ) : null}

      {loading ? (
        <div className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-5 py-10 text-center text-sm text-[var(--muted)]">
          {t("Loading save states...")}
        </div>
      ) : entries.length === 0 ? (
        <div className="rounded-[24px] border border-[var(--border)] bg-[var(--panel)] px-5 py-10 text-center text-sm text-[var(--muted)]">
          {t("No save states found for this platform.")}
        </div>
      ) : (
        <div className="space-y-4">
          {entries.map((entry) => {
            const previewUrl =
              csrf && entry.previewPath ? buildDownloadUrl("files", entry.previewPath, undefined, csrf) : null;
            const isBusy = busyEntryId === entry.id;

            return (
              <article
                key={entry.id}
                className="grid gap-4 rounded-[24px] border border-[var(--border)] bg-[var(--panel)] p-4 lg:grid-cols-[220px,1fr]"
              >
                <div className="overflow-hidden rounded-2xl border border-[var(--border)] bg-black/20">
                  {previewUrl ? (
                    <img alt={`${entry.title} preview`} className="aspect-[4/3] w-full object-cover" src={previewUrl} />
                  ) : (
                    <div className="flex aspect-[4/3] items-center justify-center px-6 text-center text-sm text-[var(--muted)]">
                      {t("No preview available")}
                    </div>
                  )}
                </div>
                <div className="space-y-4">
                  <div className="flex flex-wrap items-start justify-between gap-3">
                    <div>
                      <h3 className="text-base font-semibold">{entry.title}</h3>
                      <p className="mt-1 text-sm text-[var(--muted)]">
                        {entry.slotLabel} · {entry.coreDir} · {entry.format}
                      </p>
                    </div>
                    <div className="flex flex-wrap gap-2">
                      <button
                        className={primaryActionClass}
                        disabled={isBusy || entry.downloadPaths.length === 0}
                        onClick={() => {
                          void handleDownload(entry);
                        }}
                        type="button"
                      >
                        {t("Download")}
                      </button>
                      <button
                        className={destructiveActionClass}
                        disabled={isBusy || entry.deletePaths.length === 0}
                        onClick={() => {
                          void handleDelete(entry);
                        }}
                        type="button"
                      >
                        {t("Delete")}
                      </button>
                    </div>
                  </div>

                  <dl className="grid gap-3 text-sm text-[var(--muted)] sm:grid-cols-2 xl:grid-cols-4">
                    <div>
                      <dt className="text-xs uppercase tracking-[0.14em]">{t("Modified")}</dt>
                      <dd className="mt-1 text-[var(--text)]">{formatDate(entry.modified)}</dd>
                    </div>
                    <div>
                      <dt className="text-xs uppercase tracking-[0.14em]">{t("Bundle Size")}</dt>
                      <dd className="mt-1 text-[var(--text)]">{formatSize(entry.size)}</dd>
                    </div>
                    <div>
                      <dt className="text-xs uppercase tracking-[0.14em]">{t("Download Files")}</dt>
                      <dd className="mt-1 text-[var(--text)]">{entry.downloadPaths.length}</dd>
                    </div>
                    <div>
                      <dt className="text-xs uppercase tracking-[0.14em]">{t("Delete Paths")}</dt>
                      <dd className="mt-1 text-[var(--text)]">{entry.deletePaths.length}</dd>
                    </div>
                  </dl>

                  {entry.warnings.length > 0 ? (
                    <div className="rounded-2xl border border-amber-300/30 bg-amber-200/10 px-4 py-3 text-sm text-amber-50">
                      {entry.warnings.map((warning) => (
                        <p key={warning}>{warning}</p>
                      ))}
                    </div>
                  ) : null}
                </div>
              </article>
            );
          })}
        </div>
      )}
    </div>
  );
}
