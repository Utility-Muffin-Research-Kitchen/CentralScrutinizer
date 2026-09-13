import { useT } from "../lib/i18n";

function ToolCard({
  title,
  description,
  disabled = false,
  onClick,
}: {
  title: string;
  description: string;
  disabled?: boolean;
  onClick: () => void;
}) {
  const t = useT();
  return (
    <button
      aria-disabled={disabled}
      className={`group flex flex-col gap-3 rounded-2xl border p-5 text-left transition ${
        disabled
          ? "cursor-not-allowed border-[var(--border)] bg-[var(--panel)]/60 text-[var(--muted)]"
          : "border-[var(--border)] bg-[var(--panel)] hover:border-[var(--accent)]/50"
      }`}
      disabled={disabled}
      onClick={onClick}
      type="button"
    >
      <div>
        <p className={`font-semibold ${disabled ? "" : "group-hover:text-[var(--accent)]"}`}>{title}</p>
        <p className="mt-1 text-sm text-[var(--muted)]">{description}</p>
      </div>
      <span className="text-xs uppercase tracking-[0.2em] text-[var(--muted)]">
        {t(disabled ? "Enable on handheld" : "Open tool")}
      </span>
    </button>
  );
}

export function ToolsView({
  onOpenMacDotClean,
  terminalEnabled,
  onOpenFileBrowser,
  onOpenLogs,
  onOpenTerminal,
}: {
  onOpenMacDotClean: () => void;
  terminalEnabled: boolean;
  onOpenFileBrowser: () => void;
  onOpenLogs: () => void;
  onOpenTerminal: () => void;
}) {
  const t = useT();
  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between gap-3">
        <div>
          <h2 className="text-lg font-semibold">{t("Tools")}</h2>
          <p className="mt-1 text-sm text-[var(--muted)]">
            {t("Device utilities for files, logs, and direct shell access.")}
          </p>
        </div>
      </div>
      <div className="grid grid-cols-1 gap-4 lg:grid-cols-3">
        <ToolCard
          description={t("Browse the SD card filesystem and manage folders from the tools workspace.")}
          onClick={onOpenFileBrowser}
          title={t("File Browser")}
        />
        <ToolCard
          description={t("Scan for safe macOS transfer artifacts like .DS_Store, ._ sidecars, and __MACOSX folders.")}
          onClick={onOpenMacDotClean}
          title={t("Mac Dot Cleanup")}
        />
        <ToolCard
          description={t("Scan Leaf app logs, download them, and follow output live.")}
          onClick={onOpenLogs}
          title={t("Log Viewer")}
        />
        <ToolCard
          description={
            terminalEnabled
              ? t("Open a PTY-backed shell in the browser after acknowledging the safety warning.")
              : t("Terminal access is disabled on the handheld. Enable it from the device settings screen.")
          }
          disabled={!terminalEnabled}
          onClick={onOpenTerminal}
          title={t("Terminal")}
        />
      </div>
    </div>
  );
}
