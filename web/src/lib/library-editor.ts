import type { LibraryGame, LibraryGameDescription, LibraryMetadataChange } from "./types";

/* What fits the launcher's fixed fields. The server enforces these too. */
export const TITLE_MAX_BYTES = 255;
export const DESCRIPTION_MAX_BYTES = 1199;

const collator = new Intl.Collator(undefined, { numeric: true, sensitivity: "base" });

export function libraryGameKey(game: Pick<LibraryGame, "source" | "path">): string {
  return `${game.source}\u0000${game.path}`;
}

export function sortLibraryGames(games: LibraryGame[]): LibraryGame[] {
  return [...games].sort((a, b) => collator.compare(a.title, b.title) || a.path.localeCompare(b.path));
}

/* Matches the server: the launcher draws one wrapped line, so line breaks and runs of
   whitespace become single spaces. */
export function normalizeMetadataText(text: string): string {
  return text.replace(/[\u0000- \u007f]+/g, " ").trim();
}

export function metadataByteLength(text: string): number {
  return new TextEncoder().encode(normalizeMetadataText(text)).length;
}

/* What the override should become: null when the text is empty or just the default,
   since there's then nothing to override and later scraped or imported text can show. */
function desiredOverride(text: string, defaultText: string): string | null {
  const normalized = normalizeMetadataText(text);

  return normalized === "" || normalized === normalizeMetadataText(defaultText) ? null : normalized;
}

function isUnchanged(desired: string | null, override: string | null): boolean {
  if (override === null) {
    return desired === null;
  }
  return override !== "" && desired === normalizeMetadataText(override);
}

export function buildMetadataChanges(
  game: LibraryGame,
  titleText: string,
  description: LibraryGameDescription | null,
  descriptionText: string,
): LibraryMetadataChange[] {
  const changes: LibraryMetadataChange[] = [];
  const identity = { source: game.source, path: game.path, cardKey: game.cardKey };
  const title = desiredOverride(titleText, game.defaultTitle);

  if (!isUnchanged(title, game.titleOverride)) {
    changes.push({ ...identity, field: "title", value: title, expected: game.titleOverride });
  }
  if (description) {
    const value = desiredOverride(descriptionText, description.defaultDescription);

    if (!isUnchanged(value, description.descriptionOverride)) {
      changes.push({ ...identity, field: "description", value, expected: description.descriptionOverride });
    }
  }
  return changes;
}
