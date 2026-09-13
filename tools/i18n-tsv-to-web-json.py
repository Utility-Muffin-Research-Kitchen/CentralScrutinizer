#!/usr/bin/env python3
"""Generate the Web dictionary from CentralScrutinizer's runtime TSV table."""
import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
entries = {}
for line in (root / "i18n/zh_CN.tsv").read_text(encoding="utf-8").splitlines():
    if line and not line.startswith("#") and "\t" in line:
        key, value = line.split("\t", 1)
        entries[key.replace("\\n", "\n").replace("\\t", "\t")] = value.replace("\\n", "\n").replace("\\t", "\t")
(root / "web/src/i18n/zh_CN.json").write_text(
    json.dumps(entries, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
)
