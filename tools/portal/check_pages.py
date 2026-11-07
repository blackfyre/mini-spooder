"""Check the generated portal pages (web-portal design decision 2, task 1.3).

Usage: check_pages.py [--pages DIR] [--tokens FILE] [--labels FILE] [--pins FILE]

Checks, per page: every --bf-* value in :root equals its Paper token value; every
var(--bf-*) used is defined; no colour, shadow or radius outside the tokens; no
external reference; the viewport meta; the 16 KB budget; the stock control table
(requests, defaults, slider directions, arrangement by servo index); that no page
sends pms or /online. Also checks that servo_labels.json equals servo-output's
index-to-GPIO table (kServoPins) in the firmware source.

Each problem is printed as "<page>: [<category>] <text>". Exits 1 on any problem.
"""

import argparse
import html.parser
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAGES = ["controller", "zero", "setting", "editor"]
BUDGET = 16 * 1024
VIEWPORT = '<meta name=viewport content="width=device-width,initial-scale=1">'
NAV_LINKS = ["/", "/zero", "/setting", "/editor"]

# The stock control table (baseline/portal-strings.txt; spec "Controller page").
CONTROLLER_GROUPS = [
    [("Turn left", 6), ("Forward", 2), ("Turn right", 7),
     ("Left shift", 4), ("Backward", 3), ("Right shift", 5)],
    [("Standby", 1), ("Say Hi", 9), ("Push up", 11),
     ("Lie", 8), ("Fighting", 10), ("Sleep", 12),
     ("Dance 1", 13), ("Dance 2", 14), ("Dance 3", 15)],
]
# Two-column arrangement by servo index: 4 0 / 5 1 / 6 2 / 7 3.
ARRANGEMENT = [4, 0, 5, 1, 6, 2, 7, 3]
ZERO_ANGLE = {0: 135, 1: 45, 2: 135, 3: 45, 4: 45, 5: 135, 6: 45, 7: 135}
GROUP_A = {0, 3, 4, 7}  # the stock yellow group
EDITOR_DEFAULT = {0: 70, 1: 90, 2: 90, 3: 110, 4: 110, 5: 90, 6: 90, 7: 70}
EDITOR_RTL = {0, 1, 4, 5}  # 135 on the left, 45 on the right


class Elements(html.parser.HTMLParser):
    """Collects elements with their attributes, text and enclosing classes."""

    VOID = {"meta", "input", "br", "link", "img", "hr"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.items = []
        self.stack = []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        classes = []
        for el in self.stack:
            classes += (el["attrs"].get("class") or "").split()
        el = {"tag": tag, "attrs": a, "text": "", "outer": classes}
        self.items.append(el)
        if tag not in self.VOID:
            self.stack.append(el)

    def handle_endtag(self, tag):
        for i in range(len(self.stack) - 1, -1, -1):
            if self.stack[i]["tag"] == tag:
                del self.stack[i:]
                break

    def handle_data(self, data):
        for el in self.stack:
            el["text"] += data


def tokens_light(path):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    out = {}
    for group in ("color", "radius", "shadow", "spacing"):
        for t in data[group]["tokens"]:
            v = t["value"]
            out[t["name"]] = v["light"] if isinstance(v, dict) else v
    return out


def firmware_pins(path):
    with open(path, encoding="utf-8") as f:
        src = f.read()
    m = re.search(r"kServoPins\s*\[[^\]]*\]\s*=\s*\{([^}]*)\}", src)
    if not m:
        return None
    return [int(x) for x in re.findall(r"\d+", m.group(1))]


def styles_of(text):
    return "\n".join(re.findall(r"<style>(.*?)</style>", text, re.S))


def check_page(name, text, tokens, gpio):
    errs = []

    def err(cat, msg):
        errs.append("%s: [%s] %s" % (name, cat, msg))

    if len(text.encode("utf-8")) > BUDGET:
        err("size", "%d bytes, over the %d byte budget" % (len(text.encode("utf-8")), BUDGET))
    if not text.lstrip().lower().startswith("<!doctype html>"):
        err("document", "not a complete HTML document")
    if text.count(VIEWPORT) != 1:
        err("viewport", "missing the viewport meta %s" % VIEWPORT)

    css = styles_of(text)
    roots = re.findall(r":root\{([^}]*)\}", css)
    defined = {}
    if len(roots) != 1:
        err("token", "needs exactly one :root block, found %d" % len(roots))
    else:
        for decl in roots[0].split(";"):
            if not decl.strip():
                continue
            prop, _, value = decl.partition(":")
            prop = prop.strip()
            if not prop.startswith("--bf-"):
                err("token", "non-token property %s in :root" % prop)
                continue
            tok = prop[2:]
            defined[tok] = value.strip()
            if tok not in tokens:
                err("token", "%s is not a design token" % tok)
            elif value.strip() != tokens[tok]:
                err("token", "%s is %r, the Paper token is %r" % (tok, value.strip(), tokens[tok]))
    for used in sorted(set(re.findall(r"var\(--(bf-[a-z0-9-]+)\)", text))):
        if used not in defined:
            err("undefined-var", "var(--%s) is not defined" % used)

    # Outside :root, colours, shadows and radii must name tokens.
    body_css = re.sub(r":root\{[^}]*\}", "", css)
    inline = " ".join(re.findall(r"\sstyle=\"([^\"]*)\"", text) +
                      re.findall(r"\sstyle=([^\s>\"]+)", text))
    for src in (body_css, inline):
        for m in re.finditer(r"#[0-9a-fA-F]{3,8}\b|\brgba?\(|\bhsla?\(", src):
            err("style", "literal colour %r outside the tokens" % m.group(0))
        for prop in ("box-shadow", "border-radius"):
            for m in re.finditer(r"%s\s*:\s*([^;}]*)" % prop, src):
                v = m.group(1).strip()
                if not (re.fullmatch(r"var\(--bf-[a-z0-9-]+\)", v) or v in ("none", "0")):
                    err("style", "%s %r is not a token" % (prop, v))

    # External references.
    lower = text.lower()
    for pat, what in ((r"https?:", "http(s) URL"), (r"@import", "@import"),
                      (r"<link\b", "<link> element"), (r"\burl\(", "CSS url()"),
                      (r"\ssrc\s*=", "src= attribute"),
                      (r"[\"'=(]\s*//", "protocol-relative URL")):
        if re.search(pat, lower):
            err("external", "contains %s" % what)
    for m in re.finditer(r"\shref\s*=\s*(\"[^\"]*\"|'[^']*'|[^\s>]+)", text):
        target = m.group(1).strip("\"'")
        if target not in NAV_LINKS:
            err("external", "href to %r" % target)

    if re.search(r"\bpms\b", text):
        err("controls", "sends pms")
    if "online" in lower:
        err("controls", "sends /online")

    p = Elements()
    p.feed(text)
    els = p.items

    # Navigation bar: the four pages, the current one marked.
    links = [e for e in els if e["tag"] == "a"]
    if [e["attrs"].get("href") for e in links] != NAV_LINKS:
        err("nav", "navigation bar must link %s" % ", ".join(NAV_LINKS))
    current = [e["attrs"].get("href") for e in links if e["attrs"].get("aria-current") == "page"]
    want = {"controller": "/", "zero": "/zero", "setting": "/setting", "editor": "/editor"}[name]
    if current != [want]:
        err("nav", "the current page %s must be marked, found %s" % (want, current))
    if not any(e["attrs"].get("id") == "status" for e in els):
        err("status", "no status line")

    buttons = [e for e in els if e["tag"] == "button"]
    for b in buttons:
        if b["attrs"].get("type") != "button":
            err("controls", "button %r without type=button (would submit)" % b["text"])

    def label(i):
        return "S%d · GPIO%d" % (i, gpio[i])

    if name == "controller":
        pm_buttons = [b for b in buttons if "data-pm" in b["attrs"]]
        got = [(b["text"].strip(), int(b["attrs"]["data-pm"])) for b in pm_buttons]
        want_list = CONTROLLER_GROUPS[0] + CONTROLLER_GROUPS[1]
        if got != want_list:
            err("controls", "motion buttons %s, expected %s" % (got, want_list))
        if len(buttons) != 15:
            err("controls", "%d buttons, expected exactly 15" % len(buttons))
        grids = [e for e in els if e["tag"] == "div" and "grid3" in (e["attrs"].get("class") or "")]
        if len(grids) != 2:
            err("controls", "expected two groups of three-column rows")
        for text_ in ("科睿", "aliexpress", "My Store"):
            if text_ in text:
                err("controls", "stock vendor text %r present" % text_)
        if "'controller?pm='+d.pm" not in text:
            err("controls", "the click handler does not send controller?pm=<n>")

    elif name == "zero":
        servo_buttons = [b for b in buttons if "data-servo" in b["attrs"]]
        got = [(int(b["attrs"]["data-servo"]), int(b["attrs"]["data-value"]), b["text"].strip())
               for b in servo_buttons]
        want_list = [(i, ZERO_ANGLE[i], label(i)) for i in ARRANGEMENT]
        if got != want_list:
            err("controls", "servo buttons %s, expected %s" % (got, want_list))
        for b in servo_buttons:
            i = int(b["attrs"]["data-servo"])
            grp = "g-a" if i in GROUP_A else "g-b"
            if grp not in b["outer"]:
                err("controls", "servo %d is not in group %s" % (i, grp))
        zero = [b for b in buttons if b["attrs"].get("data-pm") == "100"]
        if len(zero) != 1 or zero[0]["text"].strip() != "Zero position":
            err("controls", "needs one Zero position button sending pm=100")
        if len(buttons) != 9:
            err("controls", "%d buttons, expected 9" % len(buttons))
        if "'controller?servo='+d.servo+'&value='+d.value" not in text:
            err("controls", "the click handler does not send controller?servo=<i>&value=<a>")

    elif name == "setting":
        fields = [e for e in els if e["tag"] == "input"]
        got = [(e["attrs"].get("id"), e["attrs"].get("value")) for e in fields]
        want_list = [("servo_%d" % i, "{{OFF%d}}" % i) for i in ARRANGEMENT]
        if got != want_list:
            err("controls", "trim fields %s, expected %s" % (got, want_list))
        for i in range(8):
            if text.count("{{OFF%d}}" % i) != 1:
                err("controls", "placeholder {{OFF%d}} must appear once" % i)
        labels = [e["text"].strip() for e in els if e["tag"] == "label"]
        if labels != [label(i) for i in ARRANGEMENT]:
            err("controls", "field labels %s" % labels)
        keys = [b["attrs"].get("data-key") for b in buttons]
        if keys != [str(i) for i in ARRANGEMENT] + ["100"]:
            err("controls", "SET and RESET ALL buttons %s" % keys)
        if [b["text"].strip().lower() for b in buttons] != ["set"] * 8 + ["reset all"]:
            err("controls", "button labels must be SET x8 and RESET ALL")
        for needed in ("'save?key='+k+'&value='+encodeURIComponent(v)",
                       "'save?key=100&value=0'", "$('servo_'+i).value='0'"):
            if needed not in text:
                err("controls", "script lacks %s" % needed)

    elif name == "editor":
        ranges = [e for e in els if e["tag"] == "input" and e["attrs"].get("type") == "range"]
        texts = [e for e in els if e["tag"] == "input" and e["attrs"].get("type") == "text"]
        got = [(e["attrs"].get("id"), e["attrs"].get("min"), e["attrs"].get("max"),
                e["attrs"].get("value"), e["attrs"].get("dir") == "rtl") for e in ranges]
        want_list = [("range_%d" % i, "45", "135", str(EDITOR_DEFAULT[i]), i in EDITOR_RTL)
                     for i in ARRANGEMENT]
        if got != want_list:
            err("controls", "sliders %s, expected %s" % (got, want_list))
        got = [(e["attrs"].get("id"), e["attrs"].get("value")) for e in texts]
        want_list = [("servo_%d" % i, str(EDITOR_DEFAULT[i])) for i in ARRANGEMENT]
        if got != want_list:
            err("controls", "fields %s, expected %s" % (got, want_list))
        lbls = [e["text"].strip() for e in els if "lbl" in (e["attrs"].get("class") or "").split()]
        if lbls[:8] != [label(i) for i in ARRANGEMENT]:
            err("controls", "servo labels %s" % lbls[:8])
        sends = [b["attrs"].get("data-send") for b in buttons if "data-send" in b["attrs"]]
        if sends != [str(i) for i in ARRANGEMENT]:
            err("controls", "Send buttons %s" % sends)
        pms = [b["attrs"].get("data-pm") for b in buttons if "data-pm" in b["attrs"]]
        if pms != ["1"] or not any(b["text"].strip() == "Standby" for b in buttons):
            err("controls", "needs exactly one Standby button sending pm=1, found %s" % pms)
        if len(buttons) != 9:
            err("controls", "%d buttons, expected 9" % len(buttons))
        for needed in ("'controller?servo='+i+'&value='+encodeURIComponent(value)",
                       "id=actionCode", "v.join(', ')"):
            if needed not in text:
                err("controls", "lacks %s" % needed)
    return errs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--pages", default=os.path.join(ROOT, "build", "portal", "pages"))
    ap.add_argument("--tokens", default=os.path.join(ROOT, "firmware", "portal", "tokens",
                                                     "tokens.json"))
    ap.add_argument("--labels", default=os.path.join(ROOT, "firmware", "portal",
                                                     "servo_labels.json"))
    ap.add_argument("--pins", default=os.path.join(ROOT, "firmware", "src", "servo_driver.cpp"))
    args = ap.parse_args(argv)

    errs = []
    tokens = tokens_light(args.tokens)
    with open(args.labels, encoding="utf-8") as f:
        gpio = json.load(f)["gpio"]
    pins = firmware_pins(args.pins)
    if pins is None:
        errs.append("labels: [labels] kServoPins not found in %s" % args.pins)
    elif pins != gpio:
        errs.append("labels: [labels] servo_labels.json %s differs from kServoPins %s"
                    % (gpio, pins))
    for name in PAGES:
        path = os.path.join(args.pages, name + ".html")
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except OSError as e:
            errs.append("%s: [missing] %s" % (name, e))
            continue
        errs += check_page(name, text, tokens, gpio)
    for e in errs:
        print(e)
    if errs:
        print("check_pages: %d problem(s)" % len(errs), file=sys.stderr)
        return 1
    print("check_pages: %d pages ok" % len(PAGES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
