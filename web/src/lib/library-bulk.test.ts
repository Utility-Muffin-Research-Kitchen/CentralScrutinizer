import { describe, expect, it } from "vitest";

import {
  buildCsvExport,
  InvalidCsvError,
  makeFindReplace,
  planCsvImport,
  planTitleTransform,
  stripRegionAndVersionTags,
} from "./library-bulk";
import type { LibraryGame } from "./types";

function game(title: string, overrides: Partial<LibraryGame> = {}): LibraryGame {
  return {
    source: "primary",
    path: `GBA/${title}.gba`,
    cardKey: null,
    title,
    defaultTitle: title,
    titleOverride: null,
    descriptionOverride: null,
    defaultDescription: "",
    ...overrides,
  };
}

const plainFind = { regex: false, caseSensitive: false };

describe("title tools", () => {
  it("strips region and version tags but keeps discs", () => {
    expect(stripRegionAndVersionTags("Mario (USA) (Rev 1) [!]")).toBe("Mario");
    expect(stripRegionAndVersionTags("Final Fantasy VII (USA) (Disc 2)")).toBe("Final Fantasy VII (Disc 2)");
    expect(stripRegionAndVersionTags("No Tags")).toBe("No Tags");
  });

  it("treats plain find and replace literally", () => {
    expect(makeFindReplace("(USA)", "$1", plainFind)("Mario (usa)")).toBe("Mario $1");
    expect(makeFindReplace("a.c", "x", plainFind)("abc a.c")).toBe("abc x");
    expect(makeFindReplace("mario", "x", { regex: false, caseSensitive: true })("Mario mario")).toBe("Mario x");
  });

  it("supports regular expressions, and rejects broken ones", () => {
    expect(makeFindReplace("^(\\w+) (\\w+)$", "$2 $1", { regex: true, caseSensitive: false })("Man Mega")).toBe(
      "Mega Man",
    );
    expect(() => makeFindReplace("(", "x", { regex: true, caseSensitive: false })).toThrow(SyntaxError);
  });

  it("plans only real changes, with what each expects to replace", () => {
    const plan = planTitleTransform(
      [game("Mario (USA)"), game("Zelda"), game("Mine (USA)", { titleOverride: "Mine (USA)", defaultTitle: "File" })],
      stripRegionAndVersionTags,
    );

    expect(plan.rows.map((row) => [row.before, row.after])).toEqual([
      ["Mario (USA)", "Mario"],
      ["Mine (USA)", "Mine"],
    ]);
    expect(plan.rows[1].change).toMatchObject({ field: "title", value: "Mine", expected: "Mine (USA)" });
  });

  it("clears the override when a transform lands back on the default", () => {
    const edited = game("Mario!", { titleOverride: "Mario!", defaultTitle: "Mario" });

    expect(planTitleTransform([edited], (title) => title.replace("!", "")).rows[0].change).toMatchObject({
      value: null,
      expected: "Mario!",
    });
  });

  it("reports a title it would empty, and one too long", () => {
    const plan = planTitleTransform(
      [game("(USA)"), game("Fine")],
      (title) => (title === "Fine" ? "x".repeat(256) : stripRegionAndVersionTags(title)),
    );

    expect(plan.rows).toEqual([]);
    expect(plan.skipped.map((skip) => skip.reason)).toEqual(["empty_title", "too_long"]);
  });

  it("counts titles that would end up shared", () => {
    const plan = planTitleTransform(
      [game("Tetris (USA)"), game("Tetris (Japan)"), game("Solo (USA)"), game("Ties"), game("Ties (USA)")],
      stripRegionAndVersionTags,
    );

    expect(plan.duplicateTitles).toBe(2);
  });
});

describe("csv import and export", () => {
  const library = [
    game("Zelda", { descriptionOverride: "Mine", defaultDescription: "Scraped" }),
    game("Mario", { defaultDescription: "About Mario" }),
    game("Second", { source: "secondary_sd", cardKey: "card-a", defaultDescription: "" }),
  ];

  it("changes nothing when a file is imported as exported", () => {
    const plan = planCsvImport(buildCsvExport(library, true), library, true);

    expect(plan.rows).toEqual([]);
    expect(plan.skipped).toEqual([]);
  });

  it("changes only the edited cells", () => {
    const csv = buildCsvExport(library, true).replace("Mario,About Mario", "New Mario,New words");
    const plan = planCsvImport(csv, library, true);

    expect(plan.rows.map((row) => [row.field, row.after])).toEqual([
      ["title", "New Mario"],
      ["description", "New words"],
    ]);
    expect(plan.rows[1].change).toMatchObject({ value: "New words", expected: null });
  });

  it("clears an override when its cell is empty", () => {
    const csv = [
      "source,rom_relpath,description",
      "primary,GBA/Zelda.gba,",
    ].join("\n");

    expect(planCsvImport(csv, library, true).rows[0].change).toMatchObject({
      field: "description",
      value: null,
      expected: "Mine",
    });
  });

  it("works with a title column alone, leaving descriptions be", () => {
    const csv = "source,rom_relpath,title\nprimary,GBA/Mario.gba,New\n";
    const plan = planCsvImport(csv, library, true);

    expect(plan.rows).toHaveLength(1);
    expect(plan.rows[0].field).toBe("title");
  });

  it("reports rows it can't apply rather than dropping them silently", () => {
    const csv = [
      "source,rom_relpath,card_key,title",
      "primary,GBA/Gone.gba,,X",
      "secondary_sd,GBA/Second.gba,card-other,X",
      "primary,GBA/Mario.gba,,A",
      "primary,GBA/Mario.gba,,B",
      `primary,GBA/Zelda.gba,,${"x".repeat(256)}`,
    ].join("\n");
    const plan = planCsvImport(csv, library, true);

    expect(plan.skipped.map((skip) => skip.reason)).toEqual(["no_game", "other_card", "duplicate_row", "too_long"]);
    expect(plan.rows.map((row) => row.after)).toEqual(["A"]);
  });

  it("the same path on the other card is a different game", () => {
    const twoCards = [game("One"), game("Two", { source: "secondary_sd", cardKey: "card-a", path: "GBA/One.gba" })];
    const csv = "source,rom_relpath,card_key,title\nsecondary_sd,GBA/One.gba,card-a,Changed\n";
    const plan = planCsvImport(csv, twoCards, true);

    expect(plan.rows[0].change).toMatchObject({ source: "secondary_sd", cardKey: "card-a", value: "Changed" });
  });

  it("leaves descriptions out on a Leaf without them", () => {
    const old = [game("Mario", { descriptionOverride: undefined, defaultDescription: undefined })];
    const exported = buildCsvExport(old, false);

    expect(exported).not.toContain("description");
    const plan = planCsvImport("source,rom_relpath,description\nprimary,GBA/Mario.gba,Words\n", old, false);

    expect(plan.rows).toEqual([]);
    expect(plan.skipped[0].reason).toBe("no_description");
  });

  it("rejects a file without the columns it needs", () => {
    expect(() => planCsvImport("a,b\n1,2\n", library, true)).toThrow(InvalidCsvError);
    expect(() => planCsvImport("", library, true)).toThrow(InvalidCsvError);
    expect(() => planCsvImport("source,rom_relpath\nprimary,x\n", library, true)).toThrow(InvalidCsvError);
  });
});
