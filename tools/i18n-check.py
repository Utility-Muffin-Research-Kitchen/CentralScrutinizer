#!/usr/bin/env python3
"""Validate translation keys used by the C and Web sources."""
import json
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TSV = ROOT / "i18n" / "zh_CN.tsv"
PO = ROOT / "i18n" / "zh_CN.po"
POT = ROOT / "i18n" / "central-scrutinizer.pot"


def decode(value: str) -> str:
    return value.replace("\\n", "\n").replace("\\t", "\t")


def load_tsv():
    rows = []
    for line in TSV.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#") and "\t" in line:
            key, value = line.split("\t", 1)
            rows.append((decode(key), decode(value)))
    return rows

def load_po(path):
    entries = []
    for block in path.read_text(encoding="utf-8").split("\n\n"):
        mid = re.search(r'^msgid "(.*)"$', block, re.M)
        mstr = re.search(r'^msgstr "(.*)"$', block, re.M)
        if mid and mstr and mid.group(1):
            entries.append((decode(mid.group(1)), decode(mstr.group(1)), "#, fuzzy" in block))
    return entries

def fmt_sig(value):
    return tuple(re.findall(r'%(?:[-+ #0-9.*\']*)(?:h|l|L|q|j|z|t)?[A-Za-z]', value))


def main() -> int:
    rows = load_tsv()
    keys = [key for key, _ in rows]
    available = set(keys)
    errors = []

    for key, count in Counter(keys).items():
        if count > 1:
            errors.append(f"duplicate TSV key: {key}")
    for key, value in rows:
        if not value:
            errors.append(f"empty translation: {key}")

    if PO.exists() and POT.exists():
        po_entries = load_po(PO)
        pot_keys = {decode(m.group(1)) for m in re.finditer(r'^msgid "((?:[^"\\]|\\.)*)"$', POT.read_text(encoding="utf-8"), re.M) if m.group(1)}
        seen = Counter(key for key, _, _ in po_entries)
        errors.extend(f"duplicate PO key: {key}" for key, count in seen.items() if count > 1)
        errors.extend(f"orphan PO key: {key}" for key, _, _ in po_entries if key not in available)
        errors.extend(f"PO placeholder mismatch: {key}" for key, value, _ in po_entries if key in pot_keys and value and fmt_sig(key) != fmt_sig(value))
        reviewed = sum(bool(value) and not fuzzy for key, value, fuzzy in po_entries if key in pot_keys)
        if reviewed * 100 // max(1, len(pot_keys)) < 90:
            errors.append("PO reviewed coverage is below 90%")

    c_text = "\n".join(path.read_text(encoding="utf-8", errors="replace") for path in (ROOT / "src").glob("*.c"))
    c_keys = {
        decode(key)
        for key in re.findall(r'CS_T\("((?:[^"\\]|\\.)*)"\)', c_text)
    }
    for key in sorted(c_keys - available):
        errors.append(f"missing C translation: {key}")

    web_text = "\n".join(path.read_text(encoding="utf-8") for path in list((ROOT / "web" / "src").rglob("*.tsx")) + list((ROOT / "web" / "src").rglob("*.ts")))
    web_keys = set(re.findall(r'\bt\(\s*["\']([^"\']+)["\']\s*\)', web_text))
    for key in sorted(web_keys - available):
        errors.append(f"missing Web translation: {key}")

    json_path = ROOT / "web" / "src" / "i18n" / "zh_CN.json"
    dictionary = json.loads(json_path.read_text(encoding="utf-8"))
    if set(dictionary) != available:
        errors.append("Web JSON keys differ from TSV keys")
    if PO.exists():
        po_map = {key: value for key, value, _ in load_po(PO) if key}
        errors.extend(f"TSV/PO mismatch: {key}" for key, value in rows if po_map.get(key) != value)

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1

    print(f"i18n check passed: {len(available)} keys")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
