#!/usr/bin/env python3
"""Updates tools/translations/_keys.py from i18n/babylon_fr.ts.

Existing keys keep their position (the VALUES lists of the languages are aligned with it); sources that are new
are appended at the end and printed, so each VALUES list can be extended with their translation, in that order.
"""
import html
import importlib.util
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
KEYS_FILE = ROOT / "tools" / "translations" / "_keys.py"

existing = []
if KEYS_FILE.exists():
    spec = importlib.util.spec_from_file_location("_keys", KEYS_FILE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    existing = list(module.KEYS)

ts = (ROOT / "i18n" / "babylon_fr.ts").read_text(encoding="utf-8")
found = []
for match in re.finditer(r"<source>(.*?)</source>", ts, re.S):
    source = html.unescape(match.group(1))
    if source not in found:
        found.append(source)

keys = [k for k in existing if k in found]            # sources that disappeared are dropped...
dropped = [k for k in existing if k not in found]
added = [k for k in found if k not in keys]
if dropped:
    print("WARNING: sources no longer in the code (the VALUES lists must lose these indexes too):")
    for k in dropped:
        print("   ", existing.index(k), repr(k))
keys += added

lines = [
    "# -*- coding: utf-8 -*-",
    '"""English sources of the interface, in the order of the aligned VALUES lists of the other languages."""',
    "KEYS = [",
]
lines += [f"    {s!r},  # {i}" for i, s in enumerate(keys)]
lines.append("]")
KEYS_FILE.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(len(keys), "keys;", len(added), "new:")
for i, k in enumerate(added):
    print("   ", len(keys) - len(added) + i, repr(k))
