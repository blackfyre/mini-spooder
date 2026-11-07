"""Generate the web portal's pages (web-portal design decision 2).

Reads firmware/portal/pages/*.html and the shared fragments (_head.html,
_nav.html, _status.html, _style.css, _script.js), the vendored design tokens
(firmware/portal/tokens/tokens.json) and the servo label table
(firmware/portal/servo_labels.json). Writes, into the output directory:

- portal_pages.h: one PROGMEM string per page; the Servo calibration page split
  at its {{OFF0}}..{{OFF7}} placeholders into nine parts;
- pages/<name>.html: the generated pages, for `mise run portal-check`.

Placeholders in the sources:
  {{head:<Title>}}  _head.html with {{title}} set
  {{nav:<page>}}    _nav.html, the current page marked aria-current=page
  {{status}}        _status.html
  {{style}}         _style.css        {{script}}  _script.js
  {{root}}          the :root block: the Paper (light) value of every bf-* token used
  {{label:<i>}}     "S<i> · GPIO<g>"
  {{font:<family>}} a token font family (sans, mono)
  {{ls:<style>}}    a token letter-spacing (label, kicker, micro)
  {{OFF<i>}}        left in place; the firmware fills in the trim at serve time

Exits non-zero, writing nothing, on an unknown placeholder or var(--bf-*).
Output is deterministic. A file is rewritten when its content changes or a
source is newer than it, so a build after touching a source shows a new
timestamp.
"""

import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAGES = ["controller", "zero", "setting", "editor"]
PAGE_SYMBOLS = {
    "controller": "kPageController",
    "zero": "kPageZero",
    "setting": "kPageSetting",
    "editor": "kPageEditor",
}
NAV_PAGES = ["controller", "zero", "setting", "editor"]
RAW_DELIM = "PORTAL"


class GenError(Exception):
    pass


def load_tokens(path):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    values = {}
    for group in (data["color"]["tokens"], data["radius"]["tokens"], data["shadow"]["tokens"],
                  data["spacing"]["tokens"]):
        for tok in group:
            v = tok["value"]
            values[tok["name"]] = v["light"] if isinstance(v, dict) else v
    fonts = dict(data["type"]["families"])
    spacing = {}
    for grp in data["type"]["groups"]:
        for style in grp["styles"]:
            spacing[style["name"]] = style["letterSpacing"]
    return values, fonts, spacing


def load_labels(path):
    with open(path, encoding="utf-8") as f:
        gpio = json.load(f)["gpio"]
    if len(gpio) != 8 or not all(isinstance(g, int) for g in gpio):
        raise GenError("%s: gpio must hold eight integers" % path)
    return gpio


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


PLACEHOLDER = re.compile(r"\{\{([a-z]+)(?::([^}]*))?\}\}")


def render(name, src_dir, tokens, fonts, spacing, gpio):
    frag = {k: read(os.path.join(src_dir, "_%s" % k + ext)).rstrip("\n")
            for k, ext in (("head", ".html"), ("nav", ".html"), ("status", ".html"),
                           ("style", ".css"), ("script", ".js"))}
    text = read(os.path.join(src_dir, name + ".html"))

    def expand_fragments(m):
        key, arg = m.group(1), m.group(2)
        if key == "head":
            return frag["head"].replace("{{title}}", arg or "")
        if key == "nav":
            if arg not in NAV_PAGES:
                raise GenError("%s: unknown nav page %r" % (name, arg))
            nav = frag["nav"]
            for p in NAV_PAGES:
                nav = nav.replace("{{cur:%s}}" % p, " aria-current=page" if p == arg else "")
            return nav
        if key in ("status", "style", "script"):
            return frag[key]
        return m.group(0)

    # Two passes: fragments may hold placeholders of their own (head holds style).
    for _ in range(2):
        text = PLACEHOLDER.sub(expand_fragments, text)

    def expand_values(m):
        key, arg = m.group(1), m.group(2)
        if key == "label":
            if arg is None or not arg.isdigit() or int(arg) > 7:
                raise GenError("%s: bad label %r" % (name, arg))
            return "S%s · GPIO%d" % (arg, gpio[int(arg)])
        if key == "font":
            if arg not in fonts:
                raise GenError("%s: unknown font family %r" % (name, arg))
            return fonts[arg]
        if key == "ls":
            if arg not in spacing:
                raise GenError("%s: unknown letter-spacing %r" % (name, arg))
            return spacing[arg]
        if key == "root":
            return m.group(0)
        raise GenError("%s: unknown placeholder %s" % (name, m.group(0)))

    text = PLACEHOLDER.sub(expand_values, text)
    # {{OFF<i>}} (upper case) is not matched above; check everything left.
    for m in re.finditer(r"\{\{([^}]*)\}\}", text):
        if m.group(1) != "root" and not re.fullmatch(r"OFF[0-7]", m.group(1)):
            raise GenError("%s: unknown placeholder %s" % (name, m.group(0)))

    used = sorted(set(re.findall(r"var\(--(bf-[a-z0-9-]+)\)", text)))
    unknown = [u for u in used if u not in tokens]
    if unknown:
        raise GenError("%s: unknown token(s) %s" % (name, ", ".join(unknown)))
    root = ":root{" + ";".join("--%s:%s" % (u, tokens[u]) for u in used) + "}"
    if text.count("{{root}}") != 1:
        raise GenError("%s: needs exactly one {{root}}" % name)
    text = text.replace("{{root}}", root)
    return text


def c_raw(s):
    if (")%s\"" % RAW_DELIM) in s:
        raise GenError("page text contains the raw-string delimiter")
    return 'R"%s(%s)%s"' % (RAW_DELIM, s, RAW_DELIM)


def header(pages):
    out = [
        "// Generated by tools/portal/gen_pages.py from firmware/portal/. Do not edit.",
        "#pragma once",
        "",
        "#include <pgmspace.h>",
        "#include <stddef.h>",
        "",
    ]
    for name in PAGES:
        sym = PAGE_SYMBOLS[name]
        text = pages[name]
        if name == "setting":
            parts = re.split(r"\{\{OFF([0-7])\}\}", text)
            # parts: text, index, text, index, ..., text; each index 0-7 once.
            idx = parts[1::2]
            if sorted(idx) != [str(i) for i in range(8)]:
                raise GenError("setting: needs each of {{OFF0}}..{{OFF7}} exactly once")
            texts = parts[0::2]
            for i, t in enumerate(texts):
                out.append("static const char %sPart%d[] PROGMEM = %s;" % (sym, i, c_raw(t)))
            out.append("static const char* const %sParts[] = {%s};" % (
                sym, ", ".join("%sPart%d" % (sym, i) for i in range(len(texts)))))
            out.append("static const size_t %sPartLengths[] = {%s};" % (
                sym, ", ".join("sizeof %sPart%d - 1" % (sym, i) for i in range(len(texts)))))
            out.append("constexpr size_t %sPartCount = %d;" % (sym, len(texts)))
            out.append("// The servo whose trim follows part i (parts 0-7; the last part ends the page).")
            out.append("static const unsigned char %sTrimAfterPart[] = {%s};" % (sym, ", ".join(idx)))
        else:
            if "{{OFF" in text:
                raise GenError("%s: {{OFF}} placeholders belong to setting only" % name)
            out.append("static const char %s[] PROGMEM = %s;" % (sym, c_raw(text)))
        out.append("")
    return "\n".join(out)


def write_if_needed(path, content, newest_source):
    data = content.encode("utf-8")
    try:
        with open(path, "rb") as f:
            same = f.read() == data
        stale = os.path.getmtime(path) < newest_source
    except OSError:
        same, stale = False, True
    if same and not stale:
        return False
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)
    return True


def generate(out_dir, src_dir, tokens_path, labels_path):
    tokens, fonts, spacing = load_tokens(tokens_path)
    gpio = load_labels(labels_path)
    pages = {n: render(n, src_dir, tokens, fonts, spacing, gpio) for n in PAGES}
    head = header(pages)
    sources = [os.path.join(src_dir, f) for f in os.listdir(src_dir)]
    sources += [tokens_path, labels_path, os.path.abspath(__file__)]
    newest = max(os.path.getmtime(s) for s in sources)
    os.makedirs(os.path.join(out_dir, "pages"), exist_ok=True)
    written = []
    for n in PAGES:
        p = os.path.join(out_dir, "pages", n + ".html")
        if write_if_needed(p, pages[n], newest):
            written.append(p)
    p = os.path.join(out_dir, "portal_pages.h")
    if write_if_needed(p, head, newest):
        written.append(p)
    return written


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "portal"))
    ap.add_argument("--src", default=os.path.join(ROOT, "firmware", "portal", "pages"))
    ap.add_argument("--tokens", default=os.path.join(ROOT, "firmware", "portal", "tokens",
                                                     "tokens.json"))
    ap.add_argument("--labels", default=os.path.join(ROOT, "firmware", "portal",
                                                     "servo_labels.json"))
    args = ap.parse_args(argv)
    try:
        written = generate(args.out, args.src, args.tokens, args.labels)
    except (GenError, OSError, KeyError, ValueError) as e:
        print("gen_pages: %s" % e, file=sys.stderr)
        return 1
    for w in written:
        print("gen_pages: wrote %s" % w)
    return 0


if __name__ == "__main__":
    sys.exit(main())
