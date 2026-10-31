import { describe, expect, it } from "vitest";

import { buildMetadataChanges, metadataByteLength, normalizeMetadataText, sortLibraryGames } from "./library-editor";
import type { LibraryGame } from "./types";

function game(overrides: Partial<LibraryGame> = {}): LibraryGame {
  return {
    source: "primary",
    path: "GBA/Zelda.gba",
    cardKey: null,
    title: "Zelda",
    defaultTitle: "Zelda",
    titleOverride: null,
    ...overrides,
  };
}

const noDescription = { descriptionOverride: null, defaultDescription: "Scraped" };

describe("library editor changes", () => {
  it("sends nothing when the text is what's already there", () => {
    expect(buildMetadataChanges(game(), "Zelda", null, "")).toEqual([]);
    expect(buildMetadataChanges(game(), "  Zelda ", noDescription, "Scraped")).toEqual([]);
    expect(buildMetadataChanges(game({ titleOverride: "My  Zelda" }), "My  Zelda", null, "")).toEqual([]);
  });

  it("carries identity and what the change expects to replace", () => {
    const secondCard = game({ source: "secondary_sd", cardKey: "card-a", titleOverride: "Old" });

    expect(buildMetadataChanges(secondCard, "New", null, "")).toEqual([
      {
        source: "secondary_sd",
        path: "GBA/Zelda.gba",
        cardKey: "card-a",
        field: "title",
        value: "New",
        expected: "Old",
      },
    ]);
  });

  it("clears the override rather than freezing the default", () => {
    const edited = game({ titleOverride: "Mine" });

    expect(buildMetadataChanges(edited, "Zelda", null, "")[0]).toMatchObject({ value: null, expected: "Mine" });
    expect(buildMetadataChanges(edited, "   ", null, "")[0]).toMatchObject({ value: null, expected: "Mine" });
  });

  it("clears an empty override left by something else", () => {
    expect(buildMetadataChanges(game({ titleOverride: "" }), "Zelda", null, "")[0]).toMatchObject({
      value: null,
      expected: "",
    });
  });

  it("handles the description on its own terms", () => {
    expect(buildMetadataChanges(game(), "Zelda", noDescription, "Mine\n\nToo")).toEqual([
      expect.objectContaining({ field: "description", value: "Mine Too", expected: null }),
    ]);
    expect(
      buildMetadataChanges(
        game(),
        "Zelda",
        { descriptionOverride: "Mine", defaultDescription: "Scraped" },
        "Scraped",
      ),
    ).toEqual([expect.objectContaining({ field: "description", value: null, expected: "Mine" })]);
  });

  it("normalizes and measures the way the server does", () => {
    expect(normalizeMetadataText("\t One\r\n two  three\n")).toBe("One two three");
    expect(metadataByteLength(" é ")).toBe(2);
  });

  it("sorts numbers as numbers, ignoring case", () => {
    const titles = ["Mega Man 10", "zelda", "Mega Man 2", "aardvark"].map((title) => game({ title, path: title }));

    expect(sortLibraryGames(titles).map((entry) => entry.title)).toEqual([
      "aardvark",
      "Mega Man 2",
      "Mega Man 10",
      "zelda",
    ]);
  });
});
