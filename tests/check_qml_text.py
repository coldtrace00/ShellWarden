#!/usr/bin/env python3
"""Fails if QML could show text from a server as rich text.

Qt shows a Text / Label (and the labels inside Button, MenuItem, CheckBox, …)
as HTML when the text looks like HTML. "<img src=…>" in it is then loaded —
from the web or, on Windows, from \\\\server\\share (an SMB connection outside the
proxy); in Qt 6.4 a Button with such text even crashes. So in qml/:

  * every Text, Label, TextEdit and TextArea sets textFormat explicitly, and
    only PlainText, except the few listed in RICH_OK (fixed strings only);
  * the controls with a built-in label are used only through our App* wrappers,
    which replace that label with a plain-text one.

Usage: python3 tests/check_qml_text.py [qml dir]
"""
import os
import re
import sys

TEXTS = ("Text", "Label", "TextEdit", "TextArea")
# controls whose default contentItem / label shows text as AutoText
LABELLED = ("Button", "ToolButton", "TabButton", "RoundButton", "DelayButton",
            "ItemDelegate", "MenuItem", "MenuBarItem", "CheckDelegate", "SwitchDelegate",
            "RadioDelegate", "SwipeDelegate", "CheckBox", "RadioButton", "Switch",
            "GroupBox", "Dialog", "Page", "Frame", "ToolTip")
# file -> number of allowed non-plain formats (fixed, translated strings only)
RICH_OK = {
    "components/ThemeEditor.qml": 4,   # colour preview, colours checked by hx()
    "pages/ProxyEditor.qml": 2,        # fixed help text with links
    "pages/SettingsPage.qml": 1,       # fixed help text
    "pages/HostEditor.qml": 1,         # fixed help text
    "Main.qml": 1,                     # release notes, cleaned by UpdateChecker::safeNotes
}


def blocks(src, names):
    """Yields (name, line, body at depth 1) for each `Name {` block."""
    pat = re.compile(r"(?<![\w.])(" + "|".join(names) + r")\s*\{")
    for m in pat.finditer(src):
        # skip "property Text foo" / "Text.PlainText" etc.
        i = m.end()
        depth, body = 1, []
        while i < len(src) and depth:
            ch = src[i]
            if ch == '"' or ch == "'":
                q = ch
                j = i + 1
                while j < len(src) and src[j] != q:
                    j += 2 if src[j] == "\\" else 1
                if depth == 1:
                    body.append(src[i:j + 1])
                i = j + 1
                continue
            if ch == "/" and src.startswith("//", i):
                j = src.find("\n", i)
                i = len(src) if j < 0 else j
                continue
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
            elif depth == 1:
                body.append(ch)
            i += 1
        yield m.group(1), src.count("\n", 0, m.start()) + 1, "".join(body)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "qml")
    problems, rich = [], {}
    for d, _, files in os.walk(root):
        for f in files:
            if not f.endswith(".qml"):
                continue
            path = os.path.join(d, f)
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            src = open(path, encoding="utf-8").read()
            for name, line, body in blocks(src, TEXTS):
                m = re.search(r"\btextFormat\s*:\s*([\w.]+)", body)
                if not m:
                    problems.append(f"{rel}:{line}: {name} without textFormat")
                elif not m.group(1).endswith("PlainText"):
                    rich[rel] = rich.get(rel, 0) + 1
            wrapper = f.startswith("App")
            for name, line, body in blocks(src, LABELLED):
                if wrapper and re.search(r"\b(contentItem|label|header)\s*:", body):
                    continue
                if name == "ToolTip" or not wrapper:
                    problems.append(f"{rel}:{line}: {name} used directly — use the App* wrapper "
                                    f"(its built-in label shows HTML)")
                else:
                    problems.append(f"{rel}:{line}: wrapper {name} does not replace its label")
    for rel, n in sorted(rich.items()):
        if n > RICH_OK.get(rel, 0):
            problems.append(f"{rel}: {n} non-plain text formats, {RICH_OK.get(rel, 0)} allowed")
    for p in problems:
        print(p)
    print(f"{'FAILED' if problems else 'ok'}: QML text formats ({len(problems)} problems)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
