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
    getLibraryOrder: vi.fn(),
    saveLibraryMetadata: vi.fn(),
    undoLibraryMetadata: vi.fn(),
  };
});

vi.mock("../lib/api", () => mockApi);

import { LibraryOrderView } from "./library-order-view";
import type { LibraryOrderResponse, LibraryOrderRow, PlatformSummary } from "../lib/types";

const game = (name: string, rank = 0): LibraryOrderRow => ({
  kind: "game",
  source: "primary",
  path: `SFC/${name}.zip`,
  name,
  rank,
  cardKey: null,
});
const folder = (name: string, rank = 0): LibraryOrderRow => ({ kind: "folder", path: name, name, rank, games: 2 });
const level = (rows: LibraryOrderRow[], overrides: Partial<LibraryOrderResponse> = {}): LibraryOrderResponse => ({
  showFolders: true,
  folder: "",
  parent: null,
  rows,
  undoCount: 0,
  ...overrides,
});
const platform = { tag: "SFC", name: "SNES" } as PlatformSummary;
const button = (name: string | RegExp) => screen.getByRole("button", { name }) as HTMLButtonElement;

async function renderView() {
  render(<LibraryOrderView csrf="csrf" platform={platform} />);
  await screen.findByRole("region", { name: "Pinned" });
}

describe("LibraryOrderView", () => {
  beforeEach(() => {
    mockApi.getLibraryOrder.mockResolvedValue(level([game("Mario", 1), folder("Hacks", 2), game("Zelda"), game("Alpha")]));
    mockApi.saveLibraryMetadata.mockResolvedValue({ saved: 1, refreshed: true, undoKept: true });
  });

  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  it("shows pins first, then the rest, and offers no save until something moves", async () => {
    await renderView();
    const pinned = screen.getByRole("region", { name: "Pinned" });

    expect(pinned.textContent).toContain("Mario");
    expect(pinned.textContent).toContain("Hacks/");
    expect(pinned.textContent).not.toContain("Zelda");
    expect(button("Save order").disabled).toBe(true);
    expect(button("Move Mario up").disabled).toBe(true);
    expect(button("Move Hacks down").disabled).toBe(true);
  });

  it("pins, reorders and saves only what changed, as undoable", async () => {
    await renderView();
    fireEvent.click(button("Pin Zelda"));
    fireEvent.click(button("Move Zelda up"));
    fireEvent.click(button("Save order"));

    await waitFor(() => expect(mockApi.saveLibraryMetadata).toHaveBeenCalledTimes(1));
    expect(mockApi.saveLibraryMetadata).toHaveBeenCalledWith(
      "SFC",
      [
        { path: "Hacks", field: "folderRank", value: "3", expected: "2" },
        expect.objectContaining({ path: "SFC/Zelda.zip", field: "rank", value: "2", expected: null }),
      ],
      "csrf",
      { undoable: true },
    );
    expect((await screen.findByRole("status")).textContent).toMatch(/Order saved/);
  });

  it("unpins, and discards", async () => {
    await renderView();
    fireEvent.click(button("Unpin Mario"));
    expect(screen.getByRole("region", { name: "Pinned" }).textContent).not.toContain("Mario");
    expect(button("Save order").disabled).toBe(false);
    fireEvent.click(button("Discard changes"));
    expect(screen.getByRole("region", { name: "Pinned" }).textContent).toContain("Mario");
    expect(button("Save order").disabled).toBe(true);
  });

  it("opens a folder and goes back up, but not with unsaved changes", async () => {
    mockApi.getLibraryOrder.mockResolvedValueOnce(level([folder("Hacks"), game("Alpha")]));
    await renderView();
    mockApi.getLibraryOrder.mockResolvedValueOnce(
      level([game("Kaizo")], { folder: "Hacks", parent: "" }),
    );
    fireEvent.click(button("Open Hacks"));

    await screen.findByText("Hacks");
    expect(mockApi.getLibraryOrder).toHaveBeenLastCalledWith("SFC", "Hacks", "csrf");
    fireEvent.click(button("Pin Kaizo"));
    expect(button("Up one folder").disabled).toBe(true);
    fireEvent.click(button("Discard changes"));
    mockApi.getLibraryOrder.mockResolvedValueOnce(level([folder("Hacks"), game("Alpha")]));
    fireEvent.click(button("Up one folder"));
    await waitFor(() => expect(mockApi.getLibraryOrder).toHaveBeenLastCalledWith("SFC", "", "csrf"));
  });

  it("says when Show Folders is off", async () => {
    mockApi.getLibraryOrder.mockResolvedValue(level([game("Alpha")], { showFolders: false }));
    await renderView();
    expect(screen.getByText(/one flat list/)).toBeTruthy();
  });

  it("reloads on a conflict rather than saving over it", async () => {
    await renderView();
    fireEvent.click(button("Pin Zelda"));
    mockApi.saveLibraryMetadata.mockRejectedValueOnce(new mockApi.ApiError("conflict", 409, "conflict"));
    fireEvent.click(button("Save order"));

    expect((await screen.findByRole("alert")).textContent).toMatch(/changed somewhere else/);
    expect(mockApi.getLibraryOrder).toHaveBeenCalledTimes(2);
    expect(button("Save order").disabled).toBe(true);
  });

  it("undoes the last bulk change", async () => {
    mockApi.getLibraryOrder.mockResolvedValue(level([game("Alpha", 1)], { undoCount: 1 }));
    mockApi.undoLibraryMetadata.mockResolvedValue({ restored: 1, refreshed: true, skipped: [] });
    await renderView();
    fireEvent.click(button("Undo last bulk change (1)"));
    expect((await screen.findByRole("status")).textContent).toMatch(/Changes undone: 1/);
  });

  it("filters the other rows and explains a load failure", async () => {
    await renderView();
    fireEvent.change(screen.getByRole("searchbox"), { target: { value: "zel" } });
    expect(screen.queryByRole("button", { name: "Pin Alpha" })).toBeNull();
    expect(screen.queryByRole("button", { name: "Pin Zelda" })).not.toBeNull();
    cleanup();

    mockApi.getLibraryOrder.mockRejectedValue(new Error("no"));
    render(<LibraryOrderView csrf="csrf" platform={platform} />);
    expect((await screen.findByRole("alert")).textContent).toMatch(/Could not load/);
  });
});
