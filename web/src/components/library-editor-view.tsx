"use client";

import { useEffect, useMemo, useState } from "react";

import { LibraryBulkTools } from "./library-bulk-tools";
import { LibraryOrderView } from "./library-order-view";
import { ApiError, getLibraryGame, getLibraryGames, saveLibraryMetadata } from "../lib/api";
import {
  buildMetadataChanges,
  DESCRIPTION_MAX_BYTES,
  libraryGameKey,
  metadataByteLength,
  sortLibraryGames,
  TITLE_MAX_BYTES,
} from "../lib/library-editor";
import type { LibraryGame, LibraryGameDescription, PlatformSummary } from "../lib/types";
import { tFormat, useT } from "../lib/i18n";

const ROWS_PER_PAGE = 200;

function loadErrorKey(error: unknown): string {
  if (error instanceof ApiError && error.code === "editor_unavailable") {
    return "This version of Leaf can't edit game info yet, or its library isn't ready.";
  }
  return "Could not load this system's games.";
}

function GameInfoEditor({
  csrf,
  descriptionSupported,
  game,
  platformTag,
  onClose,
  onSaved,
  onConflict,
}: {
  csrf: string;
  descriptionSupported: boolean;
  game: LibraryGame;
  platformTag: string;
  onClose: () => void;
  onSaved: (refreshed: boolean) => void;
  /* Reloads the list. Resolves false when this game is no longer in it. */
  onConflict: () => Promise<boolean>;
}) {
  const t = useT();
  const [titleText, setTitleText] = useState(game.titleOverride || game.defaultTitle);
  const [description, setDescription] = useState<LibraryGameDescription | null>(null);
  const [descriptionText, setDescriptionText] = useState("");
  const [loadingDescription, setLoadingDescription] = useState(descriptionSupported);
  const [saving, setSaving] = useState(false);
  const [notice, setNotice] = useState<string | null>(null);

  async function fetchDescription(): Promise<LibraryGameDescription> {
    return getLibraryGame(platformTag, game.source, game.path, csrf);
  }

  useEffect(() => {
    if (!descriptionSupported) {
      return;
    }

    let active = true;

    setLoadingDescription(true);
    fetchDescription()
      .then((loaded) => {
        if (active) {
          setDescription(loaded);
          setDescriptionText(loaded.descriptionOverride || loaded.defaultDescription);
        }
      })
      .catch(() => {
        if (active) {
          setNotice(t("Could not load this game's description."));
        }
      })
      .finally(() => {
        if (active) {
          setLoadingDescription(false);
        }
      });
    return () => {
      active = false;
    };
  }, [game.source, game.path, descriptionSupported]);

  const changes = buildMetadataChanges(game, titleText, description, descriptionText);
  const titleBytes = metadataByteLength(titleText);
  const descriptionBytes = metadataByteLength(descriptionText);
  const overLimit = titleBytes > TITLE_MAX_BYTES || descriptionBytes > DESCRIPTION_MAX_BYTES;

  async function handleSave() {
    setSaving(true);
    setNotice(null);
    try {
      const result = await saveLibraryMetadata(platformTag, changes, csrf);

      onSaved(result.refreshed);
    } catch (error) {
      if (error instanceof ApiError && error.code === "conflict") {
        /* The text stays, so nothing typed is lost, and the next save knows what it
           would now replace. */
        try {
          if (!(await onConflict())) {
            onClose();
            return;
          }
          if (descriptionSupported) {
            setDescription(await fetchDescription());
          }
          setNotice(t("This game changed somewhere else, such as on the handheld. Your text is still here: save again to replace that change."));
        } catch {
          setNotice(t("Could not save your changes."));
        }
      } else if (error instanceof ApiError && error.code === "scan_running") {
        setNotice(t("Leaf is scanning the library. Try again when the scan finishes."));
      } else {
        setNotice(t("Could not save your changes."));
      }
    } finally {
      setSaving(false);
    }
  }

  return (
    <div
      aria-labelledby="game-info-title"
      aria-modal="true"
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 p-4"
      role="dialog"
    >
      <div className="flex max-h-[85vh] w-full max-w-2xl flex-col overflow-hidden rounded-2xl border border-[var(--border)] bg-[var(--panel)] shadow-2xl">
        <div className="flex items-start justify-between gap-4 border-b border-[var(--line)] px-5 py-4">
          <div className="min-w-0">
            <h2 className="truncate text-base font-semibold text-[var(--text)]" id="game-info-title">
              {t("Edit game info")}
            </h2>
            <p className="truncate text-xs text-[var(--muted)]">{game.path}</p>
          </div>
          <button
            aria-label={t("Close editor")}
            className="rounded-md px-2 py-1 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
            onClick={onClose}
            type="button"
          >
            ✕
          </button>
        </div>

        <div className="flex-1 space-y-5 overflow-y-auto px-5 py-4">
          <div className="space-y-2">
            <div className="flex items-center justify-between gap-3">
              <label className="text-sm font-medium text-[var(--text)]" htmlFor="game-info-title-input">
                {t("Title")}
              </label>
              <button
                className="text-xs text-[var(--muted)] transition hover:text-[var(--text)] disabled:opacity-50"
                disabled={saving}
                onClick={() => setTitleText(game.defaultTitle)}
                type="button"
              >
                {t("Reset title")}
              </button>
            </div>
            <input
              className="w-full rounded-md border border-[var(--border)] bg-black/20 px-3 py-2 text-sm text-[var(--text)]"
              disabled={saving}
              id="game-info-title-input"
              onChange={(event) => setTitleText(event.target.value)}
              value={titleText}
            />
            {titleBytes > TITLE_MAX_BYTES ? (
              <p className="text-xs text-[var(--accent)]">{t("This title is too long.")}</p>
            ) : null}
          </div>

          {descriptionSupported ? (
            <div className="space-y-2">
              <div className="flex items-center justify-between gap-3">
                <label className="text-sm font-medium text-[var(--text)]" htmlFor="game-info-description-input">
                  {t("Description")}
                </label>
                <button
                  className="text-xs text-[var(--muted)] transition hover:text-[var(--text)] disabled:opacity-50"
                  disabled={saving || !description}
                  onClick={() => setDescriptionText(description?.defaultDescription ?? "")}
                  type="button"
                >
                  {t("Reset description")}
                </button>
              </div>
              <textarea
                className="h-44 w-full resize-none rounded-md border border-[var(--border)] bg-black/20 p-3 text-sm text-[var(--text)]"
                disabled={saving || loadingDescription || !description}
                id="game-info-description-input"
                onChange={(event) => setDescriptionText(event.target.value)}
                value={descriptionText}
              />
              <p className={`text-xs ${descriptionBytes > DESCRIPTION_MAX_BYTES ? "text-[var(--accent)]" : "text-[var(--muted)]"}`}>
                {descriptionBytes.toLocaleString()} / {DESCRIPTION_MAX_BYTES.toLocaleString()} {t("bytes")}.{" "}
                {t("The handheld shows descriptions in the Grid home layout. Line breaks show as spaces there, and rescraping keeps your description.")}
              </p>
            </div>
          ) : (
            <p className="text-xs text-[var(--muted)]">{t("Editing descriptions needs a newer version of Leaf.")}</p>
          )}

          {notice ? (
            <p className="text-sm text-[var(--accent)]" role="alert">
              {notice}
            </p>
          ) : null}
        </div>

        <div className="flex justify-end gap-2 border-t border-[var(--line)] px-5 py-4">
          <button
            className="rounded-md border border-[var(--border)] px-3 py-2 text-xs text-[var(--muted)] transition hover:text-[var(--text)] disabled:opacity-50"
            disabled={saving}
            onClick={onClose}
            type="button"
          >
            {t("Cancel")}
          </button>
          <button
            className="rounded-md border border-[var(--accent)] bg-[var(--accent)] px-3 py-2 text-xs font-semibold text-white transition disabled:opacity-50"
            disabled={saving || loadingDescription || overLimit || changes.length === 0}
            onClick={() => void handleSave()}
            type="button"
          >
            {t(saving ? "Saving..." : "Save")}
          </button>
        </div>
      </div>
    </div>
  );
}

export function LibraryEditorView({
  csrf,
  platform,
  platformName,
  onBack,
}: {
  csrf: string | null;
  platform: PlatformSummary;
  platformName: string;
  onBack: () => void;
}) {
  const t = useT();
  const [games, setGames] = useState<LibraryGame[]>([]);
  const [descriptionSupported, setDescriptionSupported] = useState(false);
  const [undoCount, setUndoCount] = useState(0);
  const [orderSupported, setOrderSupported] = useState(false);
  const [tab, setTab] = useState<"info" | "order">("info");
  const [loading, setLoading] = useState(true);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [query, setQuery] = useState("");
  const [visibleCount, setVisibleCount] = useState(ROWS_PER_PAGE);
  const [editingKey, setEditingKey] = useState<string | null>(null);

  async function reload(): Promise<LibraryGame[]> {
    if (!csrf) {
      throw new Error("Missing session csrf token.");
    }

    const response = await getLibraryGames(platform.tag, csrf);
    const sorted = sortLibraryGames(response.games);

    setGames(sorted);
    setDescriptionSupported(response.descriptionSupported);
    setUndoCount(response.undoCount ?? 0);
    setOrderSupported(response.orderSupported === true);
    return sorted;
  }

  useEffect(() => {
    let active = true;

    setLoading(true);
    setLoadError(null);
    reload()
      .catch((error: unknown) => {
        if (active) {
          setGames([]);
          setLoadError(t(loadErrorKey(error)));
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

  const matches = useMemo(() => {
    const needle = query.trim().toLowerCase();

    return needle
      ? games.filter((game) => game.title.toLowerCase().includes(needle) || game.path.toLowerCase().includes(needle))
      : games;
  }, [games, query]);
  const editing = editingKey ? games.find((game) => libraryGameKey(game) === editingKey) : undefined;

  return (
    <div className="space-y-6">
      <button
        className="inline-flex items-center gap-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
        onClick={onBack}
        type="button"
      >
        <span aria-hidden="true">←</span>
        {tFormat(t, "Back to {name}", { name: platformName })}
      </button>

      <div className="space-y-1">
        <h1 className="text-xl font-semibold text-[var(--text)]">{t("Game Info")}</h1>
        <p className="text-sm text-[var(--muted)]">
          {t("Change the title and description the handheld shows for a game. Your files aren't renamed, so saves keep working.")}
        </p>
      </div>

      {orderSupported && csrf ? (
        <div className="flex gap-2" role="tablist">
          {(
            [
              ["info", "Titles and descriptions"],
              ["order", "Order"],
            ] as const
          ).map(([key, label]) => (
            <button
              aria-selected={tab === key}
              className={`rounded-md border px-3 py-2 text-xs transition ${
                tab === key
                  ? "border-[var(--accent)] text-[var(--text)]"
                  : "border-[var(--border)] text-[var(--muted)] hover:text-[var(--text)]"
              }`}
              key={key}
              onClick={() => {
                setTab(key);
                setNotice(null);
                if (key === "info") {
                  void reload().catch(() => undefined);
                }
              }}
              role="tab"
              type="button"
            >
              {t(label)}
            </button>
          ))}
        </div>
      ) : null}

      {notice ? (
        <p className="rounded-md border border-[var(--border)] px-3 py-2 text-sm text-[var(--text)]" role="status">
          {notice}
        </p>
      ) : null}

      {tab === "order" && csrf ? (
        <LibraryOrderView csrf={csrf} platform={platform} />
      ) : loading ? (
        <p className="text-sm italic text-[var(--muted)]">{t("Loading games...")}</p>
      ) : loadError ? (
        <p className="text-sm text-[var(--accent)]" role="alert">
          {loadError}
        </p>
      ) : (
        <div className="space-y-3">
          {csrf ? (
            <LibraryBulkTools
              csrf={csrf}
              onChanged={reload}
              onNotice={setNotice}
              platform={platform}
              shownGames={matches}
              undoCount={undoCount}
            />
          ) : null}
          <input
            aria-label={t("Search games")}
            className="w-full rounded-md border border-[var(--border)] bg-[var(--panel)] px-3 py-2 text-sm text-[var(--text)]"
            onChange={(event) => {
              setQuery(event.target.value);
              setVisibleCount(ROWS_PER_PAGE);
            }}
            placeholder={t("Search games...")}
            type="search"
            value={query}
          />
          {matches.length === 0 ? (
            <p className="text-sm text-[var(--muted)]">{t("No games found.")}</p>
          ) : (
            <ul className="divide-y divide-[var(--line)] rounded-xl border border-[var(--border)] bg-[var(--panel)]">
              {matches.slice(0, visibleCount).map((game) => (
                <li key={libraryGameKey(game)}>
                  <button
                    className="flex w-full items-center justify-between gap-3 px-4 py-3 text-left transition hover:bg-[var(--accent-soft)]"
                    onClick={() => {
                      setNotice(null);
                      setEditingKey(libraryGameKey(game));
                    }}
                    type="button"
                  >
                    <span className="min-w-0">
                      <span className="block truncate text-sm font-medium text-[var(--text)]">{game.title}</span>
                      <span className="block truncate text-xs text-[var(--muted)]">{game.path}</span>
                    </span>
                    <span className="flex shrink-0 gap-2 text-xs text-[var(--muted)]">
                      {game.source === "secondary_sd" ? <span>{t("Second card")}</span> : null}
                      {game.titleOverride ? <span>{t("Edited")}</span> : null}
                    </span>
                  </button>
                </li>
              ))}
            </ul>
          )}
          {matches.length > visibleCount ? (
            <button
              className="w-full rounded-md border border-[var(--border)] px-3 py-2 text-xs text-[var(--muted)] transition hover:text-[var(--text)]"
              onClick={() => setVisibleCount((count) => count + ROWS_PER_PAGE)}
              type="button"
            >
              {tFormat(t, "Show more ({count} left)", { count: matches.length - visibleCount })}
            </button>
          ) : null}
        </div>
      )}

      {editing && csrf ? (
        <GameInfoEditor
          key={editingKey}
          csrf={csrf}
          descriptionSupported={descriptionSupported}
          game={editing}
          onClose={() => setEditingKey(null)}
          onConflict={async () => {
            const fresh = await reload();

            if (fresh.some((game) => libraryGameKey(game) === editingKey)) {
              return true;
            }
            setNotice(t("That game isn't available any more. The second card may have been swapped."));
            return false;
          }}
          onSaved={(refreshed) => {
            setEditingKey(null);
            setNotice(
              t(refreshed ? "Saved." : "Saved. The handheld will show this after its next library refresh."),
            );
            void reload().catch(() => undefined);
          }}
          platformTag={platform.tag}
        />
      ) : null}
    </div>
  );
}
