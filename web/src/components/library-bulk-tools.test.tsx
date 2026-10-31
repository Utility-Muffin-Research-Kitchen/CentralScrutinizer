import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

const mockApi = vi.hoisted(() => {
  class ApiError extends Error {
    code?: string;
    status: number;

    constructor(message: string, status: number, code?: string) {
      super(message);
      this.status = status;
      this.code = code;
    }
  }

  return {
    ApiError,
    getLibraryGames: vi.fn(),
    saveLibraryMetadata: vi.fn(),
    undoLibraryMetadata: vi.fn(),
  };
});

vi.mock("../lib/api", () => mockApi);

import { LibraryBulkTools } from "./library-bulk-tools";
import type { LibraryGame, PlatformSummary } from "../lib/types";

function game(title: string, overrides: Partial<LibraryGame> = {}): LibraryGame {
  return {
    source: "primary",
    path: `GBA/${title}.gba`,
    cardKey: null,
    title,
    defaultTitle: title,
    titleOverride: null,
    ...overrides,
  };
}

const platform = { tag: "GBA", name: "Game Boy Advance" } as PlatformSummary;
const onChanged = vi.fn();
const onNotice = vi.fn();

function renderTools(games: LibraryGame[], undoCount = 0) {
  render(
    <LibraryBulkTools
      csrf="csrf"
      onChanged={onChanged}
      onNotice={onNotice}
      platform={platform}
      shownGames={games}
      undoCount={undoCount}
    />,
  );
}

/* jsdom's File has no text(). */
function csvFile(content: string): File {
  const file = new File([content], "a.csv");

  file.text = async () => content;
  return file;
}

const button = (name: string | RegExp) => screen.getByRole("button", { name }) as HTMLButtonElement;

describe("LibraryBulkTools", () => {
  beforeEach(() => {
    onChanged.mockResolvedValue(undefined);
    mockApi.saveLibraryMetadata.mockResolvedValue({ saved: 1, refreshed: true, undoKept: true });
    mockApi.getLibraryGames.mockResolvedValue({ games: [], descriptionSupported: true, undoCount: 0 });
  });

  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  it("previews tag stripping, and sends only ticked rows as undoable", async () => {
    renderTools([game("Mario (USA)"), game("Zelda (USA)"), game("Clean")]);
    fireEvent.click(button("Strip region and version tags"));

    expect(screen.getByRole("dialog").textContent).toMatch(/Changes: 2/);
    expect(screen.queryByText("Clean")).toBeNull();

    fireEvent.click(screen.getByLabelText("Include GBA/Zelda (USA).gba"));
    fireEvent.click(button("Apply changes (1)"));

    await waitFor(() => expect(mockApi.saveLibraryMetadata).toHaveBeenCalledTimes(1));
    expect(mockApi.saveLibraryMetadata).toHaveBeenCalledWith(
      "GBA",
      [expect.objectContaining({ path: "GBA/Mario (USA).gba", value: "Mario", expected: null })],
      "csrf",
      { undoable: true },
    );
    await waitFor(() => expect(onNotice).toHaveBeenCalledWith("Changes saved: 1."));
    expect(onChanged).toHaveBeenCalled();
    expect(screen.queryByRole("dialog")).toBeNull();
  });

  it("acts on the games it's given, which is what the search left", () => {
    renderTools([game("Mario (USA)")]);
    expect(screen.getByText(/shown below \(1\)/)).toBeTruthy();
  });

  it("finds and replaces, and says so when a regular expression is broken", () => {
    renderTools([game("Mega Man")]);
    fireEvent.click(button("Find and replace"));
    expect(button("Preview changes").disabled).toBe(true);

    fireEvent.change(screen.getByLabelText("Find"), { target: { value: "(" } });
    fireEvent.click(screen.getByLabelText("Regular expression"));
    fireEvent.click(button("Preview changes"));
    expect(screen.getByRole("alert").textContent).toMatch(/isn't valid/);
    expect(screen.queryByRole("dialog")).toBeNull();

    fireEvent.click(screen.getByLabelText("Regular expression"));
    fireEvent.change(screen.getByLabelText("Find"), { target: { value: "man" } });
    fireEvent.change(screen.getByLabelText("Replace with"), { target: { value: "Woman" } });
    fireEvent.click(button("Preview changes"));
    expect(screen.getByRole("dialog").textContent).toMatch(/Mega Woman/);
    expect(screen.queryByRole("alert")).toBeNull();
  });

  it("warns about titles that would become shared", () => {
    renderTools([game("Tetris (USA)"), game("Tetris (Japan)")]);
    fireEvent.click(button("Strip region and version tags"));
    expect(screen.getByRole("dialog").textContent).toMatch(/shared by more than one game afterwards: 1/);
  });

  it("closes the preview and saves nothing when games changed since it", async () => {
    mockApi.saveLibraryMetadata.mockRejectedValue(new mockApi.ApiError("conflict", 409, "conflict"));
    renderTools([game("Mario (USA)")]);
    fireEvent.click(button("Strip region and version tags"));
    fireEvent.click(button("Apply changes (1)"));

    await waitFor(() => expect(onNotice).toHaveBeenCalledWith(expect.stringMatching(/Nothing was saved/)));
    expect(onChanged).toHaveBeenCalled();
    expect(screen.queryByRole("dialog")).toBeNull();
  });

  it("keeps the preview open when a save can't start", async () => {
    mockApi.saveLibraryMetadata.mockRejectedValue(new mockApi.ApiError("undo", 500, "undo_unavailable"));
    renderTools([game("Mario (USA)")]);
    fireEvent.click(button("Strip region and version tags"));
    fireEvent.click(button("Apply changes (1)"));

    expect((await screen.findByRole("alert")).textContent).toMatch(/undo copy/);
    expect(screen.queryByRole("dialog")).not.toBeNull();
  });

  it("says when changes can't be undone or didn't reach the launcher", async () => {
    mockApi.saveLibraryMetadata.mockResolvedValue({ saved: 1, refreshed: false, undoKept: false });
    renderTools([game("Mario (USA)")]);
    fireEvent.click(button("Strip region and version tags"));
    fireEvent.click(button("Apply changes (1)"));

    await waitFor(() => expect(onNotice).toHaveBeenCalled());
    expect(onNotice.mock.calls[0][0]).toMatch(/next library refresh/);
    expect(onNotice.mock.calls[0][0]).toMatch(/can't be undone/);
  });

  it("imports a CSV through the same preview, against fresh data", async () => {
    mockApi.getLibraryGames.mockResolvedValue({
      games: [game("Mario", { descriptionOverride: null, defaultDescription: "" })],
      descriptionSupported: true,
      undoCount: 0,
    });
    renderTools([]);
    const file = csvFile("source,rom_relpath,title\nprimary,GBA/Mario.gba,New\nprimary,GBA/Gone.gba,X\n");

    fireEvent.change(screen.getByLabelText("CSV file to import"), { target: { files: [file] } });

    const dialog = await screen.findByRole("dialog");
    expect(mockApi.getLibraryGames).toHaveBeenCalledWith("GBA", "csrf", { descriptions: true });
    expect(dialog.textContent).toMatch(/Changes: 1/);
    expect(dialog.textContent).toMatch(/Rows left out: 1/);
    expect(dialog.textContent).toMatch(/No game with this file/);
  });

  it("explains a CSV without the right columns", async () => {
    renderTools([]);
    fireEvent.change(screen.getByLabelText("CSV file to import"), {
      target: { files: [csvFile("a,b\n1,2\n")] },
    });
    expect((await screen.findByRole("alert")).textContent).toMatch(/rom_relpath/);
  });

  it("exports through a download", async () => {
    const created = vi.fn(() => "blob:csv");
    const revoked = vi.fn();
    const clicked = vi.spyOn(HTMLAnchorElement.prototype, "click").mockImplementation(() => undefined);

    vi.stubGlobal("URL", Object.assign(URL, { createObjectURL: created, revokeObjectURL: revoked }));
    mockApi.getLibraryGames.mockResolvedValue({ games: [game("Mario")], descriptionSupported: false, undoCount: 0 });
    renderTools([]);
    fireEvent.click(button("Export CSV"));

    await waitFor(() => expect(clicked).toHaveBeenCalled());
    expect(created).toHaveBeenCalled();
    expect(revoked).toHaveBeenCalled();
    clicked.mockRestore();
  });

  it("offers undo only when there's something to undo, and names what it left alone", async () => {
    renderTools([game("Mario")], 0);
    expect(screen.queryByRole("button", { name: /Undo last bulk change/ })).toBeNull();
    cleanup();

    mockApi.undoLibraryMetadata.mockResolvedValue({
      restored: 2,
      refreshed: true,
      skipped: [{ path: "GBA/Later.gba", field: "title", reason: "changed_since" }],
    });
    renderTools([game("Mario")], 3);
    fireEvent.click(button("Undo last bulk change (3)"));

    await waitFor(() => expect(onNotice).toHaveBeenCalled());
    expect(onNotice.mock.calls[0][0]).toMatch(/Changes undone: 2/);
    expect(onNotice.mock.calls[0][0]).toMatch(/GBA\/Later\.gba/);
    expect(onChanged).toHaveBeenCalled();
  });
});
