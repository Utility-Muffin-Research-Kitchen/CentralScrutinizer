import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

const mockApi = vi.hoisted(() => ({
  buildDownloadUrl: vi.fn(
    (scope: string, path: string, _tag?: string, _csrf?: string) =>
      `/api/download?scope=${scope}&path=${encodeURIComponent(path)}`,
  ),
  deleteItem: vi.fn(),
  getSaveStates: vi.fn(),
}));

vi.mock("../lib/api", () => mockApi);

import { SaveStatesView } from "./save-states-view";

function supportedResources() {
  return {
    roms: true,
    saves: true,
    states: true,
    bios: true,
    overlays: true,
    cheats: true,
  };
}

function emulatorState() {
  return {
    requiresEmulator: true,
    emulatorInstalled: true,
    emulatorWarning: null,
  };
}

describe("SaveStatesView", () => {
  afterEach(() => {
    cleanup();
    vi.restoreAllMocks();
    vi.clearAllMocks();
  });

  it("loads and renders save-state entries with warnings", async () => {
    mockApi.getSaveStates.mockResolvedValue({
      platformTag: "GBA",
      platformName: "Game Boy Advance",
      emuCode: "GBA",
      count: 1,
      truncated: false,
      entries: [
        {
          id: "mGBA:Pokemon Emerald:0",
          title: "Pokemon Emerald.gba",
          coreDir: "mGBA",
          slot: 0,
          slotLabel: "Slot 1",
          kind: "slot",
          format: "RetroArch",
          modified: 1_700_000_000,
          size: 4096,
          previewPath: "States/mGBA/Pokemon Emerald.state.png",
          downloadPaths: [
            "States/mGBA/Pokemon Emerald.state",
            "States/mGBA/Pokemon Emerald.state.png",
          ],
          deletePaths: [
            "States/mGBA/Pokemon Emerald.state",
            "States/mGBA/Pokemon Emerald.state.png",
          ],
          warnings: [],
        },
      ],
    });

    render(
      <SaveStatesView
        csrf="csrf-token"
        onBack={vi.fn()}
        platform={{
          tag: "GBA",
          name: "Game Boy Advance",
          group: "Nintendo",
          icon: "GBA",
          isCustom: false,
          ...emulatorState(),
          romPath: "Roms/Game Boy Advance (GBA)",
          savePath: "Saves/GBA",
          biosPath: "Bios/GBA",
          supportedResources: supportedResources(),
          counts: { roms: 2, saves: 1, states: 1, bios: 0, overlays: 0, cheats: 0 },
        }}
      />,
    );

    expect(await screen.findByText("Pokemon Emerald.gba")).toBeTruthy();
    expect(screen.getByText("Slot 1 · mGBA · RetroArch")).toBeTruthy();
    expect(mockApi.getSaveStates).toHaveBeenCalledWith("GBA", "csrf-token");
  });

  it("downloads the selected bundle as a zip", async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      arrayBuffer: async () => new Uint8Array([1, 2, 3]).buffer,
    });
    const createObjectURL = vi.fn(() => "blob:download");
    const revokeObjectURL = vi.fn();
    const clickSpy = vi.spyOn(HTMLAnchorElement.prototype, "click").mockImplementation(() => {});

    vi.stubGlobal("fetch", fetchMock);
    vi.stubGlobal("URL", {
      ...URL,
      createObjectURL,
      revokeObjectURL,
    });

    mockApi.getSaveStates.mockResolvedValue({
      platformTag: "GBA",
      platformName: "Game Boy Advance",
      emuCode: "GBA",
      count: 1,
      truncated: false,
      entries: [
        {
          id: "state-1",
          title: "Pokemon Emerald.gba",
          coreDir: "mGBA",
          slot: 0,
          slotLabel: "Slot 1",
          kind: "slot",
          format: "RetroArch",
          modified: 1_700_000_000,
          size: 4096,
          previewPath: "",
          downloadPaths: [
            "States/mGBA/Pokemon Emerald.state",
            "States/mGBA/Pokemon Emerald.state.png",
          ],
          deletePaths: ["States/mGBA/Pokemon Emerald.state"],
          warnings: [],
        },
      ],
    });

    render(
      <SaveStatesView
        csrf="csrf-token"
        onBack={vi.fn()}
        platform={{
          tag: "GBA",
          name: "Game Boy Advance",
          group: "Nintendo",
          icon: "GBA",
          isCustom: false,
          ...emulatorState(),
          romPath: "Roms/Game Boy Advance (GBA)",
          savePath: "Saves/GBA",
          biosPath: "Bios/GBA",
          supportedResources: supportedResources(),
          counts: { roms: 2, saves: 1, states: 1, bios: 0, overlays: 0, cheats: 0 },
        }}
      />,
    );

    fireEvent.click(await screen.findByRole("button", { name: "Download" }));

    await waitFor(() => {
      expect(fetchMock).toHaveBeenCalledTimes(2);
    });
    expect(clickSpy).toHaveBeenCalled();
    expect(createObjectURL).toHaveBeenCalled();
    expect(revokeObjectURL).toHaveBeenCalledWith("blob:download");
  });

  it("deletes all bundled files and refreshes the list", async () => {
    vi.stubGlobal("confirm", vi.fn(() => true));

    mockApi.getSaveStates
      .mockResolvedValueOnce({
        platformTag: "GBA",
        platformName: "Game Boy Advance",
        emuCode: "GBA",
        count: 1,
        truncated: false,
        entries: [
          {
            id: "state-1",
            title: "Pokemon Emerald.gba",
            coreDir: "mGBA",
            slot: 0,
            slotLabel: "Slot 1",
            kind: "slot",
            format: "RetroArch",
            modified: 1_700_000_000,
            size: 4096,
            previewPath: "",
            downloadPaths: ["States/mGBA/Pokemon Emerald.state"],
            deletePaths: [
              "States/mGBA/Pokemon Emerald.state",
              "States/mGBA/Pokemon Emerald.state.png",
            ],
            warnings: [],
          },
        ],
      })
      .mockResolvedValueOnce({
        platformTag: "GBA",
        platformName: "Game Boy Advance",
        emuCode: "GBA",
        count: 0,
        truncated: false,
        entries: [],
      });

    render(
      <SaveStatesView
        csrf="csrf-token"
        onBack={vi.fn()}
        onChanged={vi.fn()}
        platform={{
          tag: "GBA",
          name: "Game Boy Advance",
          group: "Nintendo",
          icon: "GBA",
          isCustom: false,
          ...emulatorState(),
          romPath: "Roms/Game Boy Advance (GBA)",
          savePath: "Saves/GBA",
          biosPath: "Bios/GBA",
          supportedResources: supportedResources(),
          counts: { roms: 2, saves: 1, states: 1, bios: 0, overlays: 0, cheats: 0 },
        }}
      />,
    );

    fireEvent.click(await screen.findByRole("button", { name: "Delete" }));

    await waitFor(() => {
      expect(mockApi.deleteItem).toHaveBeenCalledTimes(2);
    });
    expect(mockApi.deleteItem).toHaveBeenNthCalledWith(1, { scope: "files", path: "States/mGBA/Pokemon Emerald.state" }, "csrf-token");
    expect(mockApi.deleteItem).toHaveBeenNthCalledWith(2, { scope: "files", path: "States/mGBA/Pokemon Emerald.state.png" }, "csrf-token");
    expect(await screen.findByText("No save states found for this platform.")).toBeTruthy();
  });

  it("shows a truncation warning when the result set is capped", async () => {
    mockApi.getSaveStates.mockResolvedValue({
      platformTag: "GBA",
      platformName: "Game Boy Advance",
      emuCode: "GBA",
      count: 300,
      truncated: true,
      entries: [
        {
          id: "state-1",
          title: "Pokemon Emerald.gba",
          coreDir: "mGBA",
          slot: 0,
          slotLabel: "Slot 1",
          kind: "slot",
          format: "RetroArch",
          modified: 1_700_000_000,
          size: 4096,
          previewPath: "",
          downloadPaths: ["States/mGBA/Pokemon Emerald.state"],
          deletePaths: ["States/mGBA/Pokemon Emerald.state"],
          warnings: [],
        },
      ],
    });

    render(
      <SaveStatesView
        csrf="csrf-token"
        onBack={vi.fn()}
        platform={{
          tag: "GBA",
          name: "Game Boy Advance",
          group: "Nintendo",
          icon: "GBA",
          isCustom: false,
          ...emulatorState(),
          romPath: "Roms/Game Boy Advance (GBA)",
          savePath: "Saves/GBA",
          biosPath: "Bios/GBA",
          supportedResources: supportedResources(),
          counts: { roms: 2, saves: 1, states: 1, bios: 0, overlays: 0, cheats: 0 },
        }}
      />,
    );

    expect(await screen.findByText("Showing 1 of 300 save-state bundles. Refresh after deleting listed entries to load the rest.")).toBeTruthy();
  });
});
