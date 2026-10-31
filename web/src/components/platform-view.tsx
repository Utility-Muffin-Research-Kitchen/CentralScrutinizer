import type { PlatformResource, PlatformSummary } from "../lib/types";
import { ResourceCardGrid } from "./resource-card-grid";
import { useT } from "../lib/i18n";

export function PlatformView({
  platform,
  onBack,
  onOpenResource,
  onOpenGameInfo,
}: {
  platform: PlatformSummary;
  onBack: () => void;
  onOpenResource: (resource: PlatformResource) => void;
  onOpenGameInfo?: () => void;
}) {
  const t = useT();
  return (
    <div className="space-y-6">
      <button
        className="inline-flex items-center gap-2 text-sm text-[var(--muted)] transition hover:text-[var(--text)]"
        onClick={onBack}
        type="button"
      >
        <span aria-hidden="true">←</span>
        {t("Back to Library")}
      </button>
      <ResourceCardGrid onOpenGameInfo={onOpenGameInfo} onSelect={onOpenResource} platform={platform} />
    </div>
  );
}
