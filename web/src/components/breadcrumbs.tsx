import type { Breadcrumb } from "../lib/types";
import { useT } from "../lib/i18n";

export function Breadcrumbs({
  ariaLabel,
  items,
  onSelect,
  rootLabel,
}: {
  ariaLabel?: string;
  items: Breadcrumb[];
  onSelect: (path?: string) => void;
  rootLabel?: string;
}) {
  const t = useT();
  const resolvedAriaLabel = ariaLabel ?? t("Current path");
  const resolvedRootLabel = rootLabel ?? t("Root");
  return (
    <nav aria-label={resolvedAriaLabel} className="flex flex-wrap items-center gap-2 text-sm text-[var(--muted)]">
      <button
        className="transition hover:text-white"
        onClick={() => {
          onSelect(undefined);
        }}
        type="button"
      >
        {resolvedRootLabel}
      </button>
      {items.map((item) => (
        <span key={item.path} className="flex items-center gap-2">
          <span>/</span>
          <button
            className="transition hover:text-white"
            onClick={() => {
              onSelect(item.path);
            }}
            type="button"
          >
            {item.label}
          </button>
        </span>
      ))}
    </nav>
  );
}
