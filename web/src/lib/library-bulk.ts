import { parseCsv, serializeCsv } from "./csv";
import {
  buildMetadataChanges,
  DESCRIPTION_MAX_BYTES,
  libraryGameKey,
  metadataByteLength,
  normalizeMetadataText,
  TITLE_MAX_BYTES,
} from "./library-editor";
import type { LibraryGame, LibraryGameDescription, LibraryMetadataChange } from "./types";

export type BulkRow = {
  id: string;
  path: string;
  field: "title" | "description";
  before: string;
  after: string;
  change: LibraryMetadataChange;
};

export type BulkSkip = {
  label: string;
  reason: "no_game" | "other_card" | "too_long" | "empty_title" | "duplicate_row" | "no_description";
};

export type BulkPlan = {
  rows: BulkRow[];
  skipped: BulkSkip[];
  /* Titles more than one game would share afterwards. */
  duplicateTitles: number;
};

export class InvalidCsvError extends Error {}

function countDuplicateTitles(games: LibraryGame[], rows: BulkRow[]): number {
  const titles = new Map<string, number>();
  const changed = new Map(rows.filter((row) => row.field === "title").map((row) => [row.id, row.after]));
  const rowId = (game: LibraryGame) => `${libraryGameKey(game)}\u0000title`;

  for (const game of games) {
    const title = (changed.get(rowId(game)) ?? game.title).toLowerCase();

    titles.set(title, (titles.get(title) ?? 0) + 1);
  }

  const afterTitles = new Set([...changed.values()].map((title) => title.toLowerCase()));

  return [...afterTitles].filter((title) => (titles.get(title) ?? 0) > 1).length;
}

function descriptionOf(game: LibraryGame): LibraryGameDescription | null {
  return game.defaultDescription === undefined
    ? null
    : { descriptionOverride: game.descriptionOverride ?? null, defaultDescription: game.defaultDescription };
}

function toRows(
  game: LibraryGame,
  titleText: string,
  description: LibraryGameDescription | null,
  descriptionText: string,
): BulkRow[] {
  return buildMetadataChanges(game, titleText, description, descriptionText).map((change) => ({
    id: `${libraryGameKey(game)}\u0000${change.field}`,
    path: game.path,
    field: change.field as BulkRow["field"],
    before:
      change.field === "title" ? game.title : description?.descriptionOverride || description?.defaultDescription || "",
    after:
      change.value ??
      (change.field === "title" ? game.defaultTitle : (description?.defaultDescription ?? "")),
    change,
  }));
}

/* Runs a transform over each game's title. A title it would empty is left alone and
   reported, since an empty title would mean "back to default", not what was asked. */
export function planTitleTransform(games: LibraryGame[], transform: (title: string) => string): BulkPlan {
  const rows: BulkRow[] = [];
  const skipped: BulkSkip[] = [];

  for (const game of games) {
    const after = normalizeMetadataText(transform(game.title));

    if (after === normalizeMetadataText(game.title)) {
      continue;
    }
    if (after === "") {
      skipped.push({ label: game.title, reason: "empty_title" });
    } else if (metadataByteLength(after) > TITLE_MAX_BYTES) {
      skipped.push({ label: game.title, reason: "too_long" });
    } else {
      rows.push(...toRows(game, after, null, ""));
    }
  }
  return { rows, skipped, duplicateTitles: countDuplicateTitles(games, rows) };
}

/* Throws a SyntaxError for a regular expression that doesn't compile. */
export function makeFindReplace(
  find: string,
  replace: string,
  options: { regex: boolean; caseSensitive: boolean },
): (title: string) => string {
  const flags = options.caseSensitive ? "g" : "gi";

  if (options.regex) {
    const pattern = new RegExp(find, flags);

    return (title) => title.replace(pattern, replace);
  }

  const pattern = new RegExp(find.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), flags);

  /* A function, so "$1" in plain mode is just those characters. */
  return (title) => title.replace(pattern, () => replace);
}

/* Takes "(USA)", "[!]", "(Rev 1)" and the like off a title. Disc and side markers stay,
   since without them the discs of one game would share a title. */
export function stripRegionAndVersionTags(title: string): string {
  return title.replace(/\s*[([][^)\]]*[)\]]/g, (group) => (/\b(disc|disk|side)\b/i.test(group) ? group : ""));
}

export function buildCsvExport(games: LibraryGame[], includeDescriptions: boolean): string {
  const header = ["source", "rom_relpath", "card_key", "title"];

  if (includeDescriptions) {
    header.push("description", "default_title", "default_description");
  } else {
    header.push("default_title");
  }

  const rows = games.map((game) => {
    const row = [game.source, game.path, game.cardKey ?? "", game.title];

    if (includeDescriptions) {
      row.push(
        game.descriptionOverride || game.defaultDescription || "",
        game.defaultTitle,
        game.defaultDescription ?? "",
      );
    } else {
      row.push(game.defaultTitle);
    }
    return row;
  });

  return serializeCsv([header, ...rows]);
}

/* Works out what a CSV would change against the library as it is now. games must have
   been loaded with descriptions if the daemon supports them. */
export function planCsvImport(text: string, games: LibraryGame[], descriptionSupported: boolean): BulkPlan {
  const [header, ...lines] = parseCsv(text);
  const column = (name: string) => (header ? header.findIndex((cell) => cell.trim() === name) : -1);
  const sourceColumn = column("source");
  const pathColumn = column("rom_relpath");
  const cardColumn = column("card_key");
  const titleColumn = column("title");
  const descriptionColumn = column("description");

  if (sourceColumn < 0 || pathColumn < 0 || (titleColumn < 0 && descriptionColumn < 0)) {
    throw new InvalidCsvError("missing_columns");
  }

  const byIdentity = new Map(games.map((game) => [libraryGameKey(game), game]));
  const seen = new Set<string>();
  const rows: BulkRow[] = [];
  const skipped: BulkSkip[] = [];

  for (const line of lines) {
    const source = line[sourceColumn] ?? "";
    const path = line[pathColumn] ?? "";
    const key = libraryGameKey({ source, path });
    const game = byIdentity.get(key);
    const label = path || source;

    if (seen.has(key)) {
      skipped.push({ label, reason: "duplicate_row" });
      continue;
    }
    seen.add(key);
    if (!game) {
      skipped.push({ label, reason: "no_game" });
      continue;
    }
    if (game.cardKey !== null && (line[cardColumn] ?? "") !== game.cardKey) {
      skipped.push({ label, reason: "other_card" });
      continue;
    }

    const titleText = titleColumn >= 0 ? (line[titleColumn] ?? "") : game.title;
    const description = descriptionSupported ? descriptionOf(game) : null;
    const descriptionText =
      descriptionColumn >= 0
        ? (line[descriptionColumn] ?? "")
        : (description?.descriptionOverride || description?.defaultDescription || "");

    if (descriptionColumn >= 0 && !description && normalizeMetadataText(descriptionText) !== "") {
      skipped.push({ label, reason: "no_description" });
    }
    if (
      metadataByteLength(titleText) > TITLE_MAX_BYTES ||
      (description && metadataByteLength(descriptionText) > DESCRIPTION_MAX_BYTES)
    ) {
      skipped.push({ label, reason: "too_long" });
      continue;
    }
    rows.push(...toRows(game, titleText, description, descriptionText));
  }
  return { rows, skipped, duplicateTitles: countDuplicateTitles(games, rows) };
}
