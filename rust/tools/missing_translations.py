#!/usr/bin/env python3
"""Lists the texts of the Rust application (tr(), trf(), @tr(), and the static lists passed to tr) that have no
entry in the generated tables, i.e. the ones to add to the Qt translation files (or to a Rust-only file)."""

import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RUST = os.path.normpath(os.path.join(HERE, ".."))
STRING = r'"((?:[^"\\]|\\.)*)"'
PATTERNS = [
    re.compile(r"(?<![A-Za-z_])trf?\(\s*" + STRING),
    re.compile(r"@tr\(\s*" + STRING),
    # static lists of texts translated later: "..." entries of arrays mapped with tr
    re.compile(r"\.map\(\|n\| SharedString::from\(tr\(n\)\)\)"),
]


def unescape(text: str) -> str:
    return text.replace('\\"', '"').replace("\\n", "\n").replace("\\\\", "\\").replace("{{", "{").replace("}}", "}")


def sources():
    for base, _, files in os.walk(RUST):
        if "target" in base.split(os.sep) or "lang" in base.split(os.sep):
            continue
        for name in files:
            if name.endswith((".rs", ".slint")):
                yield os.path.join(base, name)


def main() -> int:
    with open(os.path.join(RUST, "babylon-app", "lang", "fr.json"), encoding="utf-8") as f:
        known = set(json.load(f))
    found = {}
    for path in sources():
        text = open(path, encoding="utf-8").read()
        for pattern in PATTERNS[:2]:
            for m in pattern.finditer(text):
                key = unescape(m.group(1))
                found.setdefault(key, os.path.relpath(path, RUST))
        # arrays of texts fed to tr(): ["A", "B"].iter().map(|n| ... tr(n))
        for m in re.finditer(r"\[((?:\s*" + STRING + r",?)+)\s*\]\s*\.iter\(\)\s*\.map\(\|[a-z]+\|[^)]*tr\(", text):
            for s in re.finditer(STRING, m.group(1)):
                found.setdefault(unescape(s.group(1)), os.path.relpath(path, RUST))
    missing = sorted(k for k in found if k not in known and k.strip())
    for k in missing:
        print("%-48s %s" % (found[k], k.replace("\n", "\\n")))
    print("\n%d texts, %d without translation" % (len(found), len(missing)), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
