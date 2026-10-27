#!/usr/bin/env python3
# The guide's code is tested: every ```lisp block in docs/guide/ (and in
# README.md) follows a line <!-- code: path -->, naming a program, and
# must appear in that program word for word. The programs are in
# docs/guide/code/, each with a .expected beside it, so t/run.sh runs
# them as golden tests. A block after <!-- output: path --> must be that
# .expected, whole. A block that's only a sketch, never run, is fenced
# without the "lisp".
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
os.chdir(ROOT)

def pages():
    yield "README.md"
    for dirpath, _, names in os.walk("docs/guide"):
        for name in sorted(names):
            if name.endswith(".md"):
                yield os.path.join(dirpath, name)

def blocks(text):
    """(line, fence's language, (kind, path) or None, text) for each
    fenced block; the marker is the line before it."""
    lines = text.split("\n")
    i, marker = 0, None
    while i < len(lines):
        line = lines[i].strip()
        if line.startswith("<!-- ") and line.endswith(" -->") and ":" in line:
            kind, path = line[len("<!-- "):-len(" -->")].split(":", 1)
            marker = (kind.strip(), path.strip())
        elif line.startswith("```"):
            start = i
            i += 1
            while i < len(lines) and lines[i].strip() != "```":
                i += 1
            yield start + 1, line[3:], marker, "\n".join(lines[start + 1:i]) + "\n"
            marker = None
        elif line:
            marker = None
        i += 1

failed = 0
def fail(msg):
    global failed
    failed += 1
    print(msg)

count = 0
for page in pages():
    with open(page) as f:
        text = f.read()
    for line, lang, marker, code in blocks(text):
        where = f"{page}:{line}"
        if marker is None:
            if lang == "lisp":
                count += 1
                fail(f"{where}: a lisp block without <!-- code: path --> before it")
            continue
        count += 1
        kind, path = marker
        src = os.path.normpath(os.path.join(os.path.dirname(page), path))
        if kind not in ("code", "output"):
            fail(f"{where}: <!-- {kind}: --> is neither code nor output")
        elif not os.path.isfile(src):
            fail(f"{where}: no such file: {src}")
        elif kind == "output":
            with open(src) as f:
                if code != f.read():
                    fail(f"{where}: the block isn't {src} as it stands")
        else:
            if not os.path.isfile(src[:-len(".slight")] + ".expected"):
                fail(f"{where}: {src} has no .expected, so it's never run")
            with open(src) as f:
                if code not in f.read():
                    fail(f"{where}: the block isn't in {src} as it stands")

print(f"{count} blocks, {failed} failed")
sys.exit(1 if failed else 0)
