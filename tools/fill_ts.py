#!/usr/bin/env python3
"""Fills the Qt Linguist .ts files of the interface with the translations kept in tools/translations/<code>.py.

Usage (after running lupdate):  python tools/fill_ts.py [fr es ...]
Each translations/<code>.py defines TRANSLATIONS = {english source text: translated text}.
Sources without a translation stay "unfinished" and fall back to English at run time.
"""
import html
import importlib.util
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESSAGE = re.compile(r"<message>(.*?)</message>", re.S)


def load(code):
    path = ROOT / "tools" / "translations" / f"{code}.py"
    spec = importlib.util.spec_from_file_location(f"tr_{code}", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.TRANSLATIONS


def fill(code):
    translations = load(code)
    ts = ROOT / "i18n" / f"babylon_{code}.ts"
    text = ts.read_text(encoding="utf-8")
    missing, done = [], 0

    def repl(match):
        nonlocal done
        block = match.group(1)
        src = re.search(r"<source>(.*?)</source>", block, re.S)
        if not src:
            return match.group(0)
        source = html.unescape(src.group(1))
        if source not in translations:
            missing.append(source)
            return match.group(0)
        escaped = html.escape(translations[source], quote=False)
        block = re.sub(r"<translation[^>]*>.*?</translation>", lambda _: f"<translation>{escaped}</translation>",
                       block, count=1, flags=re.S)
        done += 1
        return f"<message>{block}</message>"

    ts.write_text(MESSAGE.sub(repl, text), encoding="utf-8")
    print(f"{code}: {done} translated, {len(missing)} missing")
    for m in missing:
        print("   missing:", repr(m))


if __name__ == "__main__":
    codes = sys.argv[1:] or [p.stem for p in (ROOT / "tools" / "translations").glob("*.py")]
    for c in codes:
        fill(c)
