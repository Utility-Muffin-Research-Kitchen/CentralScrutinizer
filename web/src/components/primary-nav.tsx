import type { AppDestination } from "../lib/navigation";
import { useT } from "../lib/i18n";

export function PrimaryNav({
  active,
  onChange,
}: {
  active: AppDestination;
  onChange: (destination: AppDestination) => void;
}) {
  const t = useT();
  const items = [
    { key: "library" as const, label: t("Library") },
    { key: "tools" as const, label: t("Tools") },
  ];
  return (
    <nav aria-label={t("Primary")} className="hidden items-center gap-2 md:flex">
      {items.map((item) => (
        <button
          key={item.key}
          aria-current={active === item.key ? "page" : undefined}
          className={`rounded-full px-4 py-2 text-sm font-semibold transition ${
            active === item.key
              ? "bg-[var(--accent-soft)] text-[var(--accent)]"
              : "text-[var(--muted)] hover:bg-[var(--panel)] hover:text-[var(--text)]"
          }`}
          onClick={() => {
            onChange(item.key);
          }}
          type="button"
        >
          {item.label}
        </button>
      ))}
    </nav>
  );
}
