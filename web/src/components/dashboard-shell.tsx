import type { PlatformGroup } from "../lib/types";
import { PlatformGrid } from "./platform-grid";

function DashboardSkeleton() {
  return (
    <div aria-busy="true" aria-live="polite" className="space-y-8" role="status">
      <span className="sr-only">Loading platforms...</span>
      {[0, 1].map((groupIndex) => (
        <section key={groupIndex}>
          <div className="mb-3 h-3 w-32 animate-pulse rounded bg-[var(--panel)]" />
          <div className="grid grid-cols-1 gap-3 lg:grid-cols-3">
            {[0, 1, 2].map((cardIndex) => (
              <div
                key={cardIndex}
                className="flex w-full animate-pulse items-center gap-4 rounded-xl border border-[var(--border)] bg-[var(--panel)] px-5 py-5"
              >
                <div className="h-12 w-12 shrink-0 rounded bg-[var(--panel-alt)]" />
                <div className="min-w-0 flex-1 space-y-2">
                  <div className="h-3 w-2/3 rounded bg-[var(--panel-alt)]" />
                  <div className="h-2.5 w-1/2 rounded bg-[var(--panel-alt)]" />
                </div>
              </div>
            ))}
          </div>
        </section>
      ))}
    </div>
  );
}

export function DashboardShell({
  catalogError,
  groups,
  isLoading = false,
  onSelectPlatform,
  onToggleShowEmpty,
  showEmptyPlatforms,
}: {
  catalogError?: { kind: string; path: string } | null;
  groups: PlatformGroup[];
  isLoading?: boolean;
  onSelectPlatform: (tag: string) => void;
  onToggleShowEmpty: (value: boolean) => void;
  showEmptyPlatforms: boolean;
}) {
  const visibleSystems = groups.reduce((count, group) => count + group.platforms.length, 0);
  const errorPath = catalogError?.path ? ` (${catalogError.path})` : "";

  return (
    <div className="space-y-8">
      <div className="flex flex-col gap-4 sm:flex-row sm:items-center sm:justify-between">
        <div>
          <h2 className="text-lg font-semibold">Platforms</h2>
          <p className="mt-1 text-sm text-[var(--muted)]">Browse library content by platform family.</p>
        </div>
        <div className="flex flex-wrap items-center gap-x-4 gap-y-2">
          <label className="flex items-center gap-2 text-sm text-[var(--muted)]">
            <input
              checked={showEmptyPlatforms}
              className="h-4 w-4 rounded border-[var(--border)] bg-[var(--bg)]"
              onChange={(event) => {
                onToggleShowEmpty(event.target.checked);
              }}
              type="checkbox"
            />
            Show empty consoles
          </label>
          <p className="text-sm text-[var(--muted)]">{visibleSystems} visible systems</p>
        </div>
      </div>
      {catalogError ? (
        <section className="rounded-lg border border-amber-500/40 bg-amber-500/10 px-5 py-4">
          <p className="text-sm text-[var(--text)]">
            Platform catalog unavailable: {catalogError.kind}
            {errorPath}.
          </p>
        </section>
      ) : null}
      {isLoading && groups.length === 0 ? (
        <DashboardSkeleton />
      ) : (
        <>
          <PlatformGrid groups={groups} onSelect={onSelectPlatform} />
          {isLoading ? (
            <p className="text-center text-sm italic text-[var(--muted)]" role="status">
              Loading more platforms...
            </p>
          ) : null}
        </>
      )}
      <footer className="border-t border-[var(--border)] pt-6 text-center text-xs text-[var(--muted)]/70">
        Platform icons from the{" "}
        <a
          className="underline hover:text-[var(--muted)]"
          href="https://git.libretro.com/libretro-assets/retroarch-assets/-/tree/e11d6708b49a893f392b238effc713c6c7cfadef/xmb/systematic"
          rel="noopener noreferrer"
          target="_blank"
        >
          libretro Systematic theme
        </a>{" "}
        (CC BY 4.0). All trademarks are property of their respective owners.
      </footer>
    </div>
  );
}
