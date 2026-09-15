import { readFileSync } from "node:fs";

import { expect, test, type Page } from "@playwright/test";

async function pair(page: Page) {
  await page.goto("/");
  await page.getByLabel("Pairing code").fill("7391");
  await page.getByRole("button", { name: "Pair Browser" }).click();
}

test("preserves redesigned file workspace affordances while navigating folders", async ({ page }) => {
  await page.goto("http://127.0.0.1:8877/");
  await page.getByLabel("Pairing code").fill("7391");
  await page.getByRole("button", { name: "Pair Browser" }).click();
  await page.goto("http://127.0.0.1:8877/?view=files");

  const filesPath = page.getByRole("navigation", { name: "Files path" });
  const search = page.getByPlaceholder("Search in current folder");

  await expect(filesPath).toBeVisible();
  await expect(search).toBeVisible();
  await expect(page.getByRole("button", { name: "SD Card" })).toBeVisible();
  // Bulk move/delete added selection checkboxes to the file workspace; the header checkbox is
  // the affordance for selecting every visible item.
  await expect(page.getByRole("checkbox", { name: "Select all visible items" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Open Roms" })).toBeVisible();

  await search.fill("Roms");
  await expect(page.getByRole("button", { name: "Open Roms" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Open Saves" })).toHaveCount(0);
  await search.fill("");

  await page.getByRole("button", { name: "Open Roms" }).click();
  await expect(filesPath).toContainText("SD Card");
  await expect(filesPath).toContainText("Roms");
  await expect(page.getByRole("button", { name: "Go to parent folder" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Open Game Boy Advance (GBA)" })).toBeVisible();
});

test("renders a staged PNG preview from the authenticated download route", async ({ page }) => {
  await pair(page);
  await page.goto("/?view=files&path=Screenshots");

  const previewButton = page.getByRole("button", { name: "Preview preview-sample.png" }).first();

  await previewButton.click();

  const dialog = page.getByRole("dialog");

  await expect(dialog).toBeVisible();
  await expect(dialog.getByRole("heading", { name: "preview-sample.png" })).toBeVisible();

  const image = dialog.locator("img");

  await expect
    .poll(() => image.evaluate((node: HTMLImageElement) => node.complete && node.naturalWidth > 0))
    .toBe(true);
});

test("traps focus, dismisses on Escape and restores focus to the invoking control", async ({ page }) => {
  await pair(page);
  await page.goto("/?view=files&path=Screenshots");

  const previewButton = page.getByRole("button", { name: "Preview preview-sample.png" }).first();

  await previewButton.click();

  const dialog = page.getByRole("dialog");

  await expect(dialog).toBeVisible();
  await expect(dialog.getByRole("button", { name: "Close" }).first()).toBeFocused();

  // Chromium sends Tab from the last modal control to browser chrome (document.body)
  // before returning to the dialog. Background document controls stay inert, so focus
  // may only be inside the dialog or on the document body, never on a background control.
  const assertFocusContained = async () => {
    const focusState = await dialog.evaluate((node) => ({
      insideDialog: node.contains(document.activeElement),
      onBody: document.activeElement === document.body,
    }));

    expect(focusState.insideDialog || focusState.onBody).toBe(true);
  };

  await expect(page.getByRole("button", { name: "Back" })).not.toBeFocused();

  for (let index = 0; index < 6; index += 1) {
    await page.keyboard.press("Tab");
    await assertFocusContained();
  }
  for (let index = 0; index < 6; index += 1) {
    await page.keyboard.press("Shift+Tab");
    await assertFocusContained();
  }

  await page.keyboard.press("Escape");
  await expect(dialog).toBeHidden();
  await expect(previewButton).toBeFocused();
});

test("downloads the image from the dialog and displays the inline preview URL in a new tab", async ({ page }) => {
  await pair(page);
  await page.goto("/?view=files&path=Screenshots");

  await page.getByRole("button", { name: "Preview preview-sample.png" }).first().click();

  const dialog = page.getByRole("dialog");

  await expect(dialog).toBeVisible();

  const downloadPromise = page.waitForEvent("download");

  await dialog.getByRole("link", { name: "Download" }).click();

  const download = await downloadPromise;

  expect(download.suggestedFilename()).toBe("preview-sample.png");

  const downloadedPath = await download.path();
  const fixturePath = new URL("../../fixtures/mock_sdcard/Screenshots/preview-sample.png", import.meta.url);

  expect(readFileSync(downloadedPath)).toEqual(readFileSync(fixturePath));

  const previewSrc = await dialog.locator("img").getAttribute("src");

  expect(previewSrc).toContain("inline=1");

  const viewer = await page.context().newPage();
  const response = await viewer.goto(`http://127.0.0.1:8877${previewSrc}`);

  expect(response?.headers()["content-type"]).toContain("image/png");
  await expect
    .poll(() => viewer.locator("img").evaluate((node: HTMLImageElement) => node.naturalWidth))
    .toBeGreaterThan(0);
  await viewer.close();
});

test("renders a staged JPEG preview from the authenticated download route", async ({ page }) => {
  await pair(page);
  await page.goto("/?view=files&path=Screenshots");

  await page.getByRole("button", { name: "Preview preview-sample.jpg" }).first().click();

  const dialog = page.getByRole("dialog");

  await expect(dialog.getByRole("heading", { name: "preview-sample.jpg" })).toBeVisible();
  await expect
    .poll(() => dialog.locator("img").evaluate((node: HTMLImageElement) => node.complete && node.naturalWidth > 0))
    .toBe(true);
});

test("keeps background controls unreachable while the preview is open", async ({ page }) => {
  await pair(page);
  await page.goto("/?view=files&path=Screenshots");

  const backButton = page.getByRole("button", { name: "Back" });
  const backBox = await backButton.boundingBox();

  expect(backBox).not.toBeNull();

  await page.getByRole("button", { name: "Preview preview-sample.png" }).first().click();

  const dialog = page.getByRole("dialog");

  await expect(dialog).toBeVisible();

  const centerX = (backBox?.x ?? 0) + (backBox?.width ?? 0) / 2;
  const centerY = (backBox?.y ?? 0) + (backBox?.height ?? 0) / 2;
  const hitsBackground = await page.evaluate(
    ([x, y]) => {
      const hit = document.elementFromPoint(x, y);
      const dialogNode = document.querySelector("dialog");

      return Boolean(hit && dialogNode && !dialogNode.contains(hit) && hit !== dialogNode);
    },
    [centerX, centerY],
  );

  expect(hitsBackground).toBe(false);

  // The click lands on the backdrop: it dismisses the preview without activating Back.
  await page.mouse.click(centerX, centerY);
  await expect(dialog).toBeHidden();
  await expect(page).toHaveURL(/view=files/);
  await expect(page).toHaveURL(/path=Screenshots/);
  await expect(page.getByRole("button", { name: "Preview preview-sample.png" }).first()).toBeVisible();
});

test("previews a library image from the More actions menu and returns focus to that button", async ({ page }) => {
  await pair(page);

  const session = await (await page.request.get("/api/session")).json();
  const upload = await page.request.post("/api/upload", {
    headers: { "X-CS-CSRF": session.csrf },
    multipart: {
      scope: "bios",
      tag: "GBA",
      file: {
        name: "bios-preview.png",
        mimeType: "image/png",
        buffer: readFileSync(new URL("../../fixtures/mock_sdcard/Screenshots/preview-sample.png", import.meta.url)),
      },
    },
  });

  expect(upload.ok()).toBe(true);

  await page.goto("/?view=browser&scope=bios&tag=GBA");

  const moreButton = page.getByRole("button", { name: "More actions for bios-preview.png" });

  await moreButton.click();
  await page.getByRole("menuitem", { name: "Preview" }).click();

  const dialog = page.getByRole("dialog");

  await expect(dialog.getByRole("heading", { name: "bios-preview.png" })).toBeVisible();
  await expect
    .poll(() => dialog.locator("img").evaluate((node: HTMLImageElement) => node.complete && node.naturalWidth > 0))
    .toBe(true);

  await page.keyboard.press("Escape");
  await expect(dialog).toBeHidden();
  await expect(moreButton).toBeFocused();
});
