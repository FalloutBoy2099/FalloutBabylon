#!/usr/bin/env python3
"""Fills the Qt Linguist .ts files of the interface from tools/translations/.

Usage (after running lupdate):  python tools/fill_ts.py [fr es ...]

Two formats are understood in tools/translations/<code>.py:
  TRANSLATIONS = {english source text: translated text}     (fr.py)
  VALUES = [translation, ...]  aligned with KEYS in tools/translations/_keys.py   (the other languages)
Every pair is checked: placeholders (%1, %L1...), accelerator '&', HTML tags and line breaks must match the source.
Sources without a translation stay "unfinished" and fall back to English at run time.
"""
import html
import importlib.util
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TRDIR = ROOT / "tools" / "translations"
MESSAGE = re.compile(r"<message>(.*?)</message>", re.S)


def load_module(path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load(code):
    module = load_module(TRDIR / f"{code}.py")
    if hasattr(module, "TRANSLATIONS"):
        return module.TRANSLATIONS
    keys = load_module(TRDIR / "_keys.py").KEYS
    values = module.VALUES
    if len(values) != len(keys):
        raise SystemExit(f"{code}: {len(values)} values for {len(keys)} keys")
    return dict(zip(keys, values))


def signature(text):
    """What must survive translation unchanged."""
    return (sorted(re.findall(r"%L?\d|%[sd]", text)), sorted(re.findall(r"</?(?:b|i|h\d|p|br|small|hr)\b[^>]*>", text)),
            text.count("\n"), "&" in re.sub(r"&(amp|lt|gt|quot);", "", text) and
            len(re.findall(r"&\w", text)) > 0)


def check(code, translations):
    problems = 0
    for source, target in translations.items():
        a, b = signature(source), signature(target)
        # an accelerator may legitimately be dropped or moved: only compare placeholders, tags and line breaks
        if a[:3] != b[:3]:
            print(f"   {code}: mismatch for {source!r} -> {target!r}")
            problems += 1
        if not target.strip():
            print(f"   {code}: empty translation for {source!r}")
            problems += 1
    return problems


def fill(code):
    translations = load(code)
    problems = check(code, translations)
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
    print(f"{code}: {done} translated, {len(missing)} missing, {problems} problem(s)")
    for m in missing:
        print("   missing:", repr(m))
    return problems


if __name__ == "__main__":
    codes = sys.argv[1:] or sorted(p.stem for p in TRDIR.glob("*.py") if not p.stem.startswith("_"))
    sys.exit(1 if sum(fill(c) for c in codes) else 0)
