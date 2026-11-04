#!/usr/bin/env python3
"""Generates the translation tables of the Rust application from the Qt translation files (../../i18n/*.ts).

For every language it writes
  rust/babylon-app/lang/<code>.json                           English text -> translation (used by babylon-core)
  rust/babylon-app/lang/<code>/LC_MESSAGES/babylon-app.po     the same for the @tr("...") texts of the Slint files

The English text is the key (without the Qt mnemonic '&'). Run it again after the .ts files change.
"""

import json
import os
import re
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extra_translations import EXTRA  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
TS_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "i18n"))
OUT_DIR = os.path.normpath(os.path.join(HERE, "..", "babylon-app", "lang"))

MNEMONIC = re.compile(r"&(?!&)")


def plain(text: str) -> str:
    """Qt marks keyboard accelerators with '&'; the Rust interface has none."""
    return MNEMONIC.sub("", text).replace("&&", "&")


def read_ts(path: str) -> dict:
    table = {}
    root = ET.parse(path).getroot()
    for message in root.iter("message"):
        source = message.findtext("source")
        translation = message.find("translation")
        if not source or translation is None:
            continue
        if translation.get("type") in ("unfinished", "vanished", "obsolete"):
            continue
        # plural forms (numerusform) are not used by the application
        text = translation.text or ""
        if not text.strip():
            continue
        key = plain(source)
        # the first translation of a text wins (the same sentence may exist in several contexts)
        table.setdefault(key, plain(text))
    return table


def po_quote(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t") + '"'


def write_po(path: str, code: str, table: dict) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    lines = [
        'msgid ""',
        'msgstr ""',
        '"Content-Type: text/plain; charset=UTF-8\\n"',
        '"Content-Transfer-Encoding: 8bit\\n"',
        '"Language: %s\\n"' % code,
        "",
    ]
    for key in sorted(table):
        lines.append("msgid " + po_quote(key))
        lines.append("msgstr " + po_quote(table[key]))
        lines.append("")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


def main() -> int:
    if not os.path.isdir(TS_DIR):
        print("no translation folder:", TS_DIR)
        return 1
    count = 0
    keys = set()
    for name in sorted(os.listdir(TS_DIR)):
        match = re.fullmatch(r"babylon_([a-z]+)\.ts", name)
        if not match:
            continue
        code = match.group(1)
        table = read_ts(os.path.join(TS_DIR, name))
        for key, by_language in EXTRA.items():
            if code in by_language:
                table.setdefault(key, by_language[code])
        os.makedirs(OUT_DIR, exist_ok=True)
        with open(os.path.join(OUT_DIR, code + ".json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(table, f, ensure_ascii=False, indent=0, sort_keys=True)
        write_po(os.path.join(OUT_DIR, code, "LC_MESSAGES", "babylon-app.po"), code, table)
        print("%s: %d texts" % (code, len(table)))
        count += 1
        keys = set(keys) | set(table)
    # English is the source language: an empty translation selects the text itself
    write_po(os.path.join(OUT_DIR, "en", "LC_MESSAGES", "babylon-app.po"), "en", {k: "" for k in keys})
    return 0 if count else 1


if __name__ == "__main__":
    sys.exit(main())
