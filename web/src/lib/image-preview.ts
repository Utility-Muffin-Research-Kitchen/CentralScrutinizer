const PREVIEWABLE_IMAGE_EXTENSIONS = new Set([
  "png",
  "jpg",
  "jpeg",
  "gif",
  "webp",
  "bmp",
]);

export function isPreviewableImageFileName(name: string): boolean {
  const dot = name.lastIndexOf(".");

  if (dot <= 0 || dot === name.length - 1) {
    return false;
  }

  return PREVIEWABLE_IMAGE_EXTENSIONS.has(name.slice(dot + 1).toLowerCase());
}
