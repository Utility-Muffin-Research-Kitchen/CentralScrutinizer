#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parent.parent
rows = []
for line in (root / "i18n/zh_CN.tsv").read_text(encoding="utf-8").splitlines():
    if not line or line.startswith("#") or "\t" not in line:
        continue
    key, value = line.split("\t", 1)
    def decode(value):
        return value.replace("\\n", "\n").replace("\\t", "\t")

    def esc(value):
        return decode(value).replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")
    rows.append(f'msgid "{esc(key)}"\nmsgstr "{esc(value)}"\n')
header = '# Simplified Chinese translation for CentralScrutinizer.\nmsgid ""\nmsgstr "Content-Type: text/plain; charset=UTF-8\\nLanguage: zh_CN\\n"\n\n'
(root / "i18n/zh_CN.po").write_text(header + "\n".join(rows), encoding="utf-8")
