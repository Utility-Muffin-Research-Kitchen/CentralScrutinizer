import type { LibraryMetadataChange, LibraryOrderRow } from "./types";

export function orderRowKey(row: Pick<LibraryOrderRow, "kind" | "source" | "path">): string {
  return `${row.kind}\u0000${row.source ?? ""}\u0000${row.path}`;
}

/* The rows pinned now, in the order the daemon drew them, which is by rank. */
export function pinnedKeysOf(rows: LibraryOrderRow[]): string[] {
  return rows.filter((row) => row.rank > 0).map(orderRowKey);
}

export function moveKey(keys: string[], from: number, to: number): string[] {
  if (from === to || from < 0 || to < 0 || from >= keys.length || to >= keys.length) {
    return keys;
  }

  const next = [...keys];
  const [moved] = next.splice(from, 1);

  next.splice(to, 0, moved);
  return next;
}

/* Ranks for this level's rows only. A pin on a game or folder this level doesn't show,
   such as one on a card that's out, is left for when it's back. */
export function buildOrderChanges(rows: LibraryOrderRow[], pinnedKeys: string[]): LibraryMetadataChange[] {
  const position = new Map(pinnedKeys.map((key, index) => [key, index + 1]));
  const changes: LibraryMetadataChange[] = [];

  for (const row of rows) {
    const rank = position.get(orderRowKey(row)) ?? 0;

    if (rank === row.rank) {
      continue;
    }

    const value = rank > 0 ? String(rank) : null;
    const expected = row.rank > 0 ? String(row.rank) : null;

    if (row.kind === "folder") {
      changes.push({ path: row.path, field: "folderRank", value, expected });
    } else {
      changes.push({
        source: row.source ?? "",
        path: row.path,
        cardKey: row.cardKey ?? null,
        field: "rank",
        value,
        expected,
      });
    }
  }
  return changes;
}
