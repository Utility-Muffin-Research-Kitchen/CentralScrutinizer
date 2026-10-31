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
    getLibraryGame: vi.fn(),
    getLibraryGames: vi.fn(),
    saveLibraryMetadata: vi.fn(),
  };
});

vi.mock("../lib/api", () => mockApi);

import { LibraryEditorView } from "./library-editor-view";
import type { PlatformSummary } from "../lib/types";

const zelda = {
  source: "primary",
  path: "GBA/Zelda.gba",
  cardKey: null,
  title: "Zelda",
  defaultTitle: "Zelda",
  titleOverride: null,
};
const platform = { tag: "GBA", name: "Game Boy Advance" } as PlatformSummary;

function renderView() {
  render(<LibraryEditorView csrf="csrf" onBack={() => undefined} platform={platform} platformName="GBA" />);
}

function fieldValue(label: string): string {
  return (screen.getByLabelText(label) as HTMLInputElement).value;
}

function saveButton(): HTMLButtonElement {
  return screen.getByRole("button", { name: "Save" }) as HTMLButtonElement;
}

async function openZelda() {
  renderView();
  fireEvent.click(await screen.findByRole("button", { name: /Zelda/ }));
}

describe("LibraryEditorView", () => {
  beforeEach(() => {
    mockApi.getLibraryGames.mockResolvedValue({ games: [zelda], descriptionSupported: true });
    mockApi.getLibraryGame.mockResolvedValue({ descriptionOverride: null, defaultDescription: "Scraped" });
    mockApi.saveLibraryMetadata.mockResolvedValue({ saved: 1, refreshed: true });
  });

  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
  });

  it("explains a Leaf that can't edit yet", async () => {
    mockApi.getLibraryGames.mockRejectedValue(new mockApi.ApiError("no", 503, "editor_unavailable"));
    renderView();
    expect((await screen.findByRole("alert")).textContent).toMatch(/can't edit game info/);
  });

  it("filters by title or file", async () => {
    mockApi.getLibraryGames.mockResolvedValue({
      games: [zelda, { ...zelda, path: "GBA/mario.gba", title: "Super Mario", defaultTitle: "Super Mario" }],
      descriptionSupported: false,
    });
    renderView();
    await screen.findByRole("button", { name: /Zelda/ });
    fireEvent.change(screen.getByRole("searchbox"), { target: { value: "mario" } });
    expect(screen.queryByRole("button", { name: /Zelda/ })).toBeNull();
    expect(screen.queryByRole("button", { name: /Super Mario/ })).not.toBeNull();
  });

  it("marks games on the second card and edited ones", async () => {
    mockApi.getLibraryGames.mockResolvedValue({
      games: [{ ...zelda, source: "secondary_sd", cardKey: "card-a", titleOverride: "Mine", title: "Mine" }],
      descriptionSupported: false,
    });
    renderView();
    const row = await screen.findByRole("button", { name: /Mine/ });
    expect(row.textContent).toMatch("Second card");
    expect(row.textContent).toMatch("Edited");
  });

  it("saves only what changed, with what it expects to replace", async () => {
    await openZelda();
    const save = await screen.findByRole("button", { name: "Save" });
    await waitFor(() => expect(fieldValue("Description")).toBe("Scraped"));
    expect((save as HTMLButtonElement).disabled).toBe(true);

    fireEvent.change(screen.getByLabelText("Title"), { target: { value: "My Zelda" } });
    fireEvent.click(save);

    await waitFor(() => expect(mockApi.saveLibraryMetadata).toHaveBeenCalledTimes(1));
    expect(mockApi.saveLibraryMetadata).toHaveBeenCalledWith(
      "GBA",
      [
        {
          source: "primary",
          path: "GBA/Zelda.gba",
          cardKey: null,
          field: "title",
          value: "My Zelda",
          expected: null,
        },
      ],
      "csrf",
    );
    expect((await screen.findByRole("status")).textContent).toMatch("Saved.");
    expect(screen.queryByRole("dialog")).toBeNull();
  });

  it("resets by clearing the override", async () => {
    mockApi.getLibraryGames.mockResolvedValue({
      games: [{ ...zelda, title: "Mine", titleOverride: "Mine" }],
      descriptionSupported: false,
    });
    renderView();
    fireEvent.click(await screen.findByRole("button", { name: /Mine/ }));
    fireEvent.click(await screen.findByRole("button", { name: "Reset title" }));
    expect(fieldValue("Title")).toBe("Zelda");
    fireEvent.click(screen.getByRole("button", { name: "Save" }));

    await waitFor(() => expect(mockApi.saveLibraryMetadata).toHaveBeenCalledTimes(1));
    expect(mockApi.saveLibraryMetadata.mock.calls[0][1][0]).toMatchObject({ value: null, expected: "Mine" });
  });

  it("says when a save didn't reach the launcher", async () => {
    mockApi.saveLibraryMetadata.mockResolvedValue({ saved: 1, refreshed: false });
    await openZelda();
    await waitFor(() => expect(fieldValue("Description")).toBe("Scraped"));
    fireEvent.change(screen.getByLabelText("Title"), { target: { value: "My Zelda" } });
    fireEvent.click(screen.getByRole("button", { name: "Save" }));
    expect((await screen.findByRole("status")).textContent).toMatch(/next library refresh/);
  });

  it("keeps your text on a conflict, and the next save replaces the new value", async () => {
    await openZelda();
    await waitFor(() => expect(fieldValue("Description")).toBe("Scraped"));
    fireEvent.change(screen.getByLabelText("Title"), { target: { value: "From browser" } });

    mockApi.saveLibraryMetadata.mockRejectedValueOnce(new mockApi.ApiError("conflict", 409, "conflict"));
    mockApi.getLibraryGames.mockResolvedValue({
      games: [{ ...zelda, title: "From handheld", titleOverride: "From handheld" }],
      descriptionSupported: true,
    });
    fireEvent.click(screen.getByRole("button", { name: "Save" }));

    expect((await screen.findByRole("alert")).textContent).toMatch(/changed somewhere else/);
    expect(fieldValue("Title")).toBe("From browser");

    fireEvent.click(screen.getByRole("button", { name: "Save" }));
    await waitFor(() => expect(mockApi.saveLibraryMetadata).toHaveBeenCalledTimes(2));
    expect(mockApi.saveLibraryMetadata.mock.calls[1][1][0]).toMatchObject({
      value: "From browser",
      expected: "From handheld",
    });
  });

  it("closes the editor when a conflict means the game is gone", async () => {
    await openZelda();
    await waitFor(() => expect(fieldValue("Description")).toBe("Scraped"));
    fireEvent.change(screen.getByLabelText("Title"), { target: { value: "Mine" } });

    mockApi.saveLibraryMetadata.mockRejectedValueOnce(new mockApi.ApiError("conflict", 409, "conflict"));
    mockApi.getLibraryGames.mockResolvedValue({ games: [], descriptionSupported: true });
    fireEvent.click(screen.getByRole("button", { name: "Save" }));

    expect((await screen.findByRole("status")).textContent).toMatch(/isn't available any more/);
    expect(screen.queryByRole("dialog")).toBeNull();
  });

  it("asks to retry during a scan", async () => {
    await openZelda();
    await waitFor(() => expect(fieldValue("Description")).toBe("Scraped"));
    fireEvent.change(screen.getByLabelText("Title"), { target: { value: "Mine" } });
    mockApi.saveLibraryMetadata.mockRejectedValueOnce(new mockApi.ApiError("scan", 409, "scan_running"));
    fireEvent.click(screen.getByRole("button", { name: "Save" }));
    expect((await screen.findByRole("alert")).textContent).toMatch(/scanning the library/);
    expect(fieldValue("Title")).toBe("Mine");
  });

  it("offers no description on a Leaf that wouldn't show it", async () => {
    mockApi.getLibraryGames.mockResolvedValue({ games: [zelda], descriptionSupported: false });
    await openZelda();
    await screen.findByText(/needs a newer version of Leaf/);
    expect(screen.queryByLabelText("Description")).toBeNull();
    expect(mockApi.getLibraryGame).not.toHaveBeenCalled();
  });

  it("blocks a description that's too long", async () => {
    await openZelda();
    await waitFor(() => expect(fieldValue("Description")).toBe("Scraped"));
    fireEvent.change(screen.getByLabelText("Description"), { target: { value: "x".repeat(1200) } });
    expect(saveButton().disabled).toBe(true);
    fireEvent.change(screen.getByLabelText("Description"), { target: { value: "x".repeat(1199) } });
    expect(saveButton().disabled).toBe(false);
  });
});
