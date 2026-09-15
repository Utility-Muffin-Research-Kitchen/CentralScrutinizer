import { describe, expect, it } from "vitest";

import { isPreviewableImageFileName } from "./image-preview";

describe("isPreviewableImageFileName", () => {
  it("matches raster image extensions case-insensitively", () => {
    for (const name of [
      "shot.png",
      "SHOT.PNG",
      "photo.jpg",
      "photo.jpeg",
      "frame.gif",
      "still.webp",
      "bitmap.bmp",
    ]) {
      expect(isPreviewableImageFileName(name)).toBe(true);
    }
  });

  it("rejects svg, text and names without a usable extension", () => {
    for (const name of ["vector.svg", "notes.txt", "rom.gba", "README", ".png", "png."]) {
      expect(isPreviewableImageFileName(name)).toBe(false);
    }
  });
});
