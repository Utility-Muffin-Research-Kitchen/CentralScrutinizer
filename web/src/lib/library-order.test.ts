import { describe, expect, it } from "vitest";

import { buildOrderChanges, moveKey, orderRowKey, pinnedKeysOf } from "./library-order";
import type { LibraryOrderRow } from "./types";

const game = (name: string, rank = 0, overrides: Partial<LibraryOrderRow> = {}): LibraryOrderRow => ({
  kind: "game",
  source: "primary",
  path: `SFC/${name}.zip`,
  name,
  rank,
  cardKey: null,
  ...overrides,
});
const folder = (name: string, rank = 0): LibraryOrderRow => ({ kind: "folder", path: name, name, rank, games: 2 });

describe("library order", () => {
  it("keeps the same path on two cards, and a folder of the same path, apart", () => {
    const keys = [game("a"), game("a", 0, { source: "secondary_sd" }), folder("SFC/a.zip")].map(orderRowKey);

    expect(new Set(keys).size).toBe(3);
  });

  it("reads pins from the row order", () => {
    expect(pinnedKeysOf([folder("f", 1), game("g", 2), game("h")])).toEqual([
      orderRowKey(folder("f")),
      orderRowKey(game("g")),
    ]);
  });

  it("moves within bounds only", () => {
    expect(moveKey(["a", "b", "c"], 2, 0)).toEqual(["c", "a", "b"]);
    expect(moveKey(["a", "b"], 1, 5)).toEqual(["a", "b"]);
  });

  it("writes nothing when nothing moved", () => {
    const rows = [game("a", 1), folder("f", 2), game("b")];

    expect(buildOrderChanges(rows, pinnedKeysOf(rows))).toEqual([]);
  });

  it("writes a game's rank and a folder's rank, each with what it replaces", () => {
    const rows = [game("a", 1, { source: "secondary_sd", cardKey: "card-a" }), folder("f"), game("b")];
    const changes = buildOrderChanges(rows, [orderRowKey(rows[1]), orderRowKey(rows[0])]);

    expect(changes).toEqual([
      {
        source: "secondary_sd",
        path: "SFC/a.zip",
        cardKey: "card-a",
        field: "rank",
        value: "2",
        expected: "1",
      },
      { path: "f", field: "folderRank", value: "1", expected: null },
    ]);
  });

  it("clears the rank of a row that's unpinned", () => {
    const rows = [game("a", 1), game("b", 2)];

    expect(buildOrderChanges(rows, [orderRowKey(rows[1])])).toEqual([
      expect.objectContaining({ path: "SFC/a.zip", value: null, expected: "1" }),
      expect.objectContaining({ path: "SFC/b.zip", value: "1", expected: "2" }),
    ]);
  });

  it("closes a gap left by a pin that isn't showing", () => {
    const rows = [game("a", 3), game("b", 7)];

    expect(buildOrderChanges(rows, pinnedKeysOf(rows)).map((change) => change.value)).toEqual(["1", "2"]);
  });
});
