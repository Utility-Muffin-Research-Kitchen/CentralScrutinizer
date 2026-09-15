import type { KeyboardEvent } from "react";
import { useEffect, useId, useRef, useState } from "react";

import { buildDownloadUrl, buildImagePreviewUrl } from "../lib/api";
import { useT } from "../lib/i18n";
import type { BrowserEntry, BrowserScope } from "../lib/types";

function PreviewImageContent({ alt, src }: { alt: string; src: string }) {
  const t = useT();
  const [status, setStatus] = useState<"loading" | "loaded" | "error">("loading");

  return (
    <div className="flex min-h-[40vh] items-center justify-center">
      {status === "loading" ? (
        <p className="text-sm italic text-[var(--muted)]">{t("Loading...")}</p>
      ) : null}
      {status === "error" ? (
        <p className="text-sm text-rose-200">{t("Couldn't load this image.")}</p>
      ) : null}
      <img
        key={src}
        alt={alt}
        className={`max-h-[70vh] max-w-full object-contain ${status === "loaded" ? "block" : "hidden"}`}
        onError={() => {
          setStatus("error");
        }}
        onLoad={() => {
          setStatus("loaded");
        }}
        src={src}
      />
    </div>
  );
}

export function ImagePreviewModal({
  csrf,
  entry,
  hasNext = false,
  hasPrevious = false,
  onClose,
  onNext,
  onPrevious,
  scope,
  tag,
}: {
  csrf?: string | null;
  entry: BrowserEntry;
  hasNext?: boolean;
  hasPrevious?: boolean;
  onClose: () => void;
  onNext?: () => void;
  onPrevious?: () => void;
  scope: BrowserScope;
  tag?: string;
}) {
  const t = useT();
  const dialogRef = useRef<HTMLDialogElement | null>(null);
  const closeButtonRef = useRef<HTMLButtonElement | null>(null);
  const titleId = useId();
  const previewUrl = buildImagePreviewUrl(scope, entry.path, tag, csrf);
  const downloadUrl = buildDownloadUrl(scope, entry.path, tag, csrf);
  const showNavigation = Boolean(onPrevious || onNext);

  useEffect(() => {
    const dialog = dialogRef.current;

    if (dialog && !dialog.open && typeof dialog.showModal === "function") {
      dialog.showModal();
    }
    closeButtonRef.current?.focus();
  }, []);

  function handleKeyDown(event: KeyboardEvent<HTMLDialogElement>) {
    if (event.metaKey || event.ctrlKey || event.altKey || event.shiftKey) {
      return;
    }
    if (event.key === "ArrowLeft" && onPrevious && hasPrevious) {
      event.preventDefault();
      onPrevious();
      return;
    }
    if (event.key === "ArrowRight" && onNext && hasNext) {
      event.preventDefault();
      onNext();
    }
  }

  return (
    <dialog
      ref={dialogRef}
      aria-labelledby={titleId}
      className="m-auto w-[calc(100%-2rem)] max-w-4xl rounded-2xl border border-[var(--border)] bg-[var(--panel)] p-0 text-[var(--text)] shadow-[var(--shadow)] backdrop:bg-black/70"
      onCancel={(event) => {
        event.preventDefault();
        onClose();
      }}
      onClick={(event) => {
        if (event.target === dialogRef.current) {
          onClose();
        }
      }}
      onClose={() => {
        onClose();
      }}
      onKeyDown={handleKeyDown}
    >
      <div className="flex flex-col">
        <header className="flex items-start justify-between gap-4 border-b border-[var(--line)] px-5 py-4">
          <h2 className="min-w-0 truncate text-base font-semibold" id={titleId}>
            {entry.name}
          </h2>
          <button
            ref={closeButtonRef}
            aria-label={t("Close")}
            className="rounded-md px-2 py-1 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
            onClick={onClose}
            type="button"
          >
            ✕
          </button>
        </header>

        <div className="px-5 py-4">
          <PreviewImageContent key={previewUrl} alt={entry.name} src={previewUrl} />
        </div>

        <footer className="flex flex-wrap items-center justify-between gap-3 border-t border-[var(--line)] px-5 py-4">
          <div className="flex items-center gap-2">
            {showNavigation ? (
              <>
                <button
                  className="rounded-md border border-[var(--border)] px-3 py-2 text-xs font-medium transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-40"
                  disabled={!hasPrevious}
                  onClick={onPrevious}
                  type="button"
                >
                  {t("Previous image")}
                </button>
                <button
                  className="rounded-md border border-[var(--border)] px-3 py-2 text-xs font-medium transition hover:border-[var(--accent)]/50 disabled:cursor-not-allowed disabled:opacity-40"
                  disabled={!hasNext}
                  onClick={onNext}
                  type="button"
                >
                  {t("Next image")}
                </button>
              </>
            ) : null}
          </div>
          <div className="flex items-center gap-2">
            <button
              className="rounded-md border border-[var(--border)] px-3 py-2 text-xs text-[var(--muted)] transition hover:text-[var(--text)]"
              onClick={onClose}
              type="button"
            >
              {t("Close")}
            </button>
            <a
              className="rounded-md bg-[var(--accent)] px-3 py-2 text-xs font-semibold text-white transition hover:bg-[var(--accent-strong)]"
              download
              href={downloadUrl}
            >
              {t("Download")}
            </a>
          </div>
        </footer>
      </div>
    </dialog>
  );
}
