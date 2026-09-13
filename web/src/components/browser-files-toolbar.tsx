import type { BrowserResponse } from "../lib/types";
import { Breadcrumbs } from "./breadcrumbs";
import { useT } from "../lib/i18n";

export function BrowserFilesToolbar({
  canRunSearch = false,
  busy,
  canUploadFolder = false,
  onClearSearch,
  onCreateFolder,
  onNavigate,
  onRefresh,
  onRunSearch,
  onSearchChange,
  onUploadFolder,
  onUploadZip,
  onUploadFile,
  response,
  searchResultsActive = false,
  search,
}: {
  canRunSearch?: boolean;
  busy: boolean;
  canUploadFolder?: boolean;
  onClearSearch?: () => void;
  onCreateFolder: () => void;
  onNavigate: (path?: string) => void;
  onRefresh: () => void;
  onRunSearch?: () => void;
  onSearchChange: (value: string) => void;
  onUploadFolder?: () => void;
  onUploadZip?: () => void;
  onUploadFile: () => void;
  response: BrowserResponse;
  searchResultsActive?: boolean;
  search: string;
}) {
  const t = useT();
  return (
    <section className="rounded-[20px] border border-[var(--border)] bg-[var(--panel)] px-4 py-4">
      <div className="flex flex-col gap-4">
        <div className="flex flex-wrap items-center justify-between gap-4">
          <Breadcrumbs ariaLabel={t("Files path")} items={response.breadcrumbs} onSelect={onNavigate} rootLabel={t("SD Card")} />
          <div className="flex flex-wrap items-center gap-2">
            <button
              className="rounded-md bg-[var(--accent)] px-4 py-2 text-sm font-semibold text-white transition hover:bg-[var(--accent-strong)] disabled:cursor-not-allowed disabled:opacity-50"
              disabled={busy}
              onClick={onUploadFile}
              type="button"
            >
              {t("Upload File")}
            </button>
            {canUploadFolder && onUploadFolder ? (
              <button
                className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
                disabled={busy}
                onClick={onUploadFolder}
                type="button"
              >
                {t("Upload Folder")}
              </button>
            ) : null}
            {onUploadZip ? (
              <button
                className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
                disabled={busy}
                onClick={onUploadZip}
                type="button"
              >
                {t("Upload ZIP")}
              </button>
            ) : null}
            <button
              className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
              disabled={busy}
              onClick={onCreateFolder}
              type="button"
            >
              {t("New Folder")}
            </button>
            <button
              className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
              disabled={busy}
              onClick={onRefresh}
              type="button"
            >
              {t("Refresh")}
            </button>
            {onRunSearch ? (
              <button
                className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-50"
                disabled={busy || !canRunSearch}
                onClick={onRunSearch}
                type="button"
              >
                {t("Search Tree")}
              </button>
            ) : null}
            {searchResultsActive && onClearSearch ? (
              <button
                className="rounded-md border border-[var(--border)] bg-[var(--panel-alt)] px-4 py-2 text-sm font-medium text-[var(--text)] transition hover:border-[var(--accent)]/50"
                onClick={onClearSearch}
                type="button"
              >
                {t("Clear Results")}
              </button>
            ) : null}
          </div>
        </div>
        <div>
          <input
            aria-label={t("Search current folder")}
            className="h-11 w-full rounded-xl border border-[var(--border)] bg-[var(--bg)] px-4 text-sm text-[var(--text)] outline-none focus:border-[var(--accent)]"
            onChange={(event) => {
              onSearchChange(event.target.value);
            }}
            placeholder={t("Search in current folder")}
            value={search}
          />
        </div>
      </div>
    </section>
  );
}
