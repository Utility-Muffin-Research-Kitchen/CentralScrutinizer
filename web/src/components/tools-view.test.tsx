import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { ToolsView } from "./tools-view";

afterEach(() => {
  cleanup();
});

describe("ToolsView", () => {
  it("shows the terminal card as disabled when handheld access is off", () => {
    render(
      <ToolsView
        onOpenFileBrowser={vi.fn()}
        onOpenLogs={vi.fn()}
        onOpenMacDotClean={vi.fn()}
        onOpenTerminal={vi.fn()}
        terminalEnabled={false}
      />,
    );

    expect(screen.getByRole("button", { name: /Terminal/ })).toHaveProperty("disabled", true);
    expect(screen.getByText(/Enable on handheld/i)).toBeTruthy();
    expect(screen.getByText(/disabled on the handheld/i)).toBeTruthy();
  });

  it("opens tools when cards are clicked", () => {
    const onOpenFileBrowser = vi.fn();
    const onOpenLogs = vi.fn();
    const onOpenMacDotClean = vi.fn();
    const onOpenTerminal = vi.fn();

    render(
      <ToolsView
        onOpenFileBrowser={onOpenFileBrowser}
        onOpenLogs={onOpenLogs}
        onOpenMacDotClean={onOpenMacDotClean}
        onOpenTerminal={onOpenTerminal}
        terminalEnabled
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: /File Browser/ }));
    fireEvent.click(screen.getByRole("button", { name: /Mac Dot Cleanup/ }));
    fireEvent.click(screen.getByRole("button", { name: /Log Viewer/ }));
    fireEvent.click(screen.getByRole("button", { name: /Terminal/ }));

    expect(onOpenFileBrowser).toHaveBeenCalledTimes(1);
    expect(onOpenMacDotClean).toHaveBeenCalledTimes(1);
    expect(onOpenLogs).toHaveBeenCalledTimes(1);
    expect(onOpenTerminal).toHaveBeenCalledTimes(1);
  });
});
