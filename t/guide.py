#!/usr/bin/env python3
# The guide's code is tested: every ```lisp block in docs/guide/ (and in
# README.md) follows a line <!-- code: path -->, naming a program, and
# must appear in that program word for word. The programs are in
# docs/guide/code/, each with a .expected beside it, so t/run.sh runs
# them as golden tests (or, for a file of functions alone, with a program
# that includes it). A block after <!-- output: path --> must be that
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
            # A block in a list item is indented as its fence is.
            indent = len(lines[i]) - len(lines[i].lstrip())
            start = i
            i += 1
            while i < len(lines) and lines[i].strip() != "```":
                i += 1
            body = [l[indent:] if l[:indent].strip() == "" else l for l in lines[start + 1:i]]
            yield start + 1, line[3:], marker, "\n".join(body) + "\n"
            marker = None
        elif line:
            marker = None
        i += 1

# A file of functions alone, shared by the guide's programs, is run by
# the programs that include it.
def included(src):
    dir = os.path.dirname(src)
    form = f'(@include "{os.path.basename(src)}")'
    for name in os.listdir(dir):
        path = os.path.join(dir, name)
        if name.endswith(".slight") and os.path.isfile(path[:-len(".slight")] + ".expected"):
            with open(path) as f:
                if form in f.read():
                    return True
    return False

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
            if not os.path.isfile(src[:-len(".slight")] + ".expected") and not included(src):
                fail(f"{where}: {src} has no .expected, nor does a program that includes it, so it's never run")
            with open(src) as f:
                if code not in f.read():
                    fail(f"{where}: the block isn't in {src} as it stands")

print(f"{count} blocks, {failed} failed")
sys.exit(1 if failed else 0)
