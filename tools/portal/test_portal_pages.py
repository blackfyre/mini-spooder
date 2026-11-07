"""Tests of the portal page generator and check (web-portal tasks 1.2 and 1.3).

Run from the repository root: python -I -m unittest tools/portal/test_portal_pages.py
(or `mise run portal-check`, which runs them after the check).
"""

import filecmp
import glob
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
GEN = os.path.join(HERE, "gen_pages.py")
CHECK = os.path.join(HERE, "check_pages.py")
FIXTURES = os.path.join(HERE, "fixtures")
SRC = os.path.join(ROOT, "firmware", "portal", "pages")


def run(script, *args):
    return subprocess.run([sys.executable, "-I", script, *args], capture_output=True, text=True)


class PortalPagesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.out = os.path.join(self.tmp, "gen")
        r = run(GEN, "--out", self.out)
        self.assertEqual(r.returncode, 0, r.stderr)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def pages(self):
        return os.path.join(self.out, "pages")

    def test_generated_pages_pass(self):
        r = run(CHECK, "--pages", self.pages())
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_deterministic(self):
        other = os.path.join(self.tmp, "gen2")
        r = run(GEN, "--out", other)
        self.assertEqual(r.returncode, 0, r.stderr)
        for name in ["portal_pages.h"] + ["pages/%s.html" % p for p in
                                          ("controller", "zero", "setting", "editor")]:
            self.assertTrue(filecmp.cmp(os.path.join(self.out, name), os.path.join(other, name),
                                        shallow=False), name)

    def test_unknown_token_fails(self):
        src = os.path.join(self.tmp, "src")
        shutil.copytree(SRC, src)
        with open(os.path.join(src, "_style.css"), "a", encoding="utf-8") as f:
            f.write("p{color:var(--bf-x)}\n")
        out = os.path.join(self.tmp, "bad")
        r = run(GEN, "--out", out, "--src", src)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("bf-x", r.stderr)
        self.assertFalse(os.path.exists(os.path.join(out, "portal_pages.h")))

    def test_setting_placeholders_in_header(self):
        with open(os.path.join(self.out, "portal_pages.h"), encoding="utf-8") as f:
            h = f.read()
        self.assertIn("kPageSettingPartCount = 9", h)
        self.assertIn("kPageSettingTrimAfterPart[] = {4, 0, 5, 1, 6, 2, 7, 3}", h)
        self.assertNotIn("{{OFF", h)

    def test_page_fixtures_fail(self):
        fixtures = sorted(glob.glob(os.path.join(FIXTURES, "*.json")))
        page_fixtures = []
        for f in fixtures:
            with open(f, encoding="utf-8") as fh:
                if "page" in json.load(fh):
                    page_fixtures.append(f)
        self.assertEqual(len(page_fixtures), 3)
        for path in page_fixtures:
            with self.subTest(fixture=os.path.basename(path)):
                with open(path, encoding="utf-8") as f:
                    fx = json.load(f)
                pages = os.path.join(self.tmp, "fx-" + os.path.basename(path))
                shutil.copytree(self.pages(), pages)
                target = os.path.join(pages, fx["page"] + ".html")
                with open(target, encoding="utf-8") as f:
                    text = f.read()
                old, new = fx["replace"]
                self.assertIn(old, text, "fixture no longer applies to the generated page")
                with open(target, "w", encoding="utf-8") as f:
                    f.write(text.replace(old, new, 1))
                r = run(CHECK, "--pages", pages)
                self.assertEqual(r.returncode, 1, r.stdout)
                self.assertIn("[%s]" % fx["expect"], r.stdout)

    def test_label_fixture_fails(self):
        path = os.path.join(FIXTURES, "servo_labels_gpio14.json")
        r = run(CHECK, "--pages", self.pages(), "--labels", path)
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn("[labels]", r.stdout)

    def test_rewrites_only_when_needed(self):
        header = os.path.join(self.out, "portal_pages.h")
        before = os.path.getmtime(header)
        os.utime(header, (before + 100, before + 100))
        r = run(GEN, "--out", self.out)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertNotIn("portal_pages.h", r.stdout)  # unchanged and newer than its sources
        # A source newer than the output is regenerated, as `touch` before a build shows.
        src = os.path.join(self.tmp, "src")
        shutil.copytree(SRC, src)
        out = os.path.join(self.tmp, "touched")
        self.assertEqual(run(GEN, "--out", out, "--src", src).returncode, 0)
        h = os.path.join(out, "portal_pages.h")
        os.utime(h, (1, 1))
        r = run(GEN, "--out", out, "--src", src)
        self.assertIn("portal_pages.h", r.stdout)
        self.assertGreater(os.path.getmtime(h), 1)


# In-page probe: XMLHttpRequest replaced by a recorder that answers with the
# status and body set in ANSWER (or never, for a timeout); report() writes the
# results into the DOM, which a headless browser dumps.
PROBE = """<script>
var LOG=[],ANSWER={status:200,body:''};
window.XMLHttpRequest=function(){var x=this;this.open=function(m,u){x.u=u};
 this.send=function(){LOG.push(x.u);if(ANSWER.status==null){x.ontimeout&&x.ontimeout();return}
  x.status=ANSWER.status;x.responseText=ANSWER.body;x.onload&&x.onload()}};
function report(extra){
 document.documentElement.style.width='320px';
 var right=0,minB=1e9;
 [].forEach.call(document.querySelectorAll('body *'),function(e){right=Math.max(right,e.getBoundingClientRect().right)});
 [].forEach.call(document.querySelectorAll('button'),function(b){minB=Math.min(minB,b.getBoundingClientRect().height)});
 document.body.setAttribute('data-report',JSON.stringify({right:right,minButton:minB,log:LOG,extra:extra}));
}
</script>"""

SCENARIOS = {
    "controller": """
[].forEach.call(document.querySelectorAll('button'),function(b){b.click()});
var ok=$('status').textContent;
ANSWER={status:409,body:'remote in control'};document.querySelector('[data-pm="2"]').click();
var locked=$('status').textContent;
ANSWER={status:null};document.querySelector('[data-pm="2"]').click();
var timeout=$('status').textContent;
ANSWER={status:503,body:'servo output fault'};document.querySelector('[data-pm="2"]').click();
var fault=$('status').textContent;
report({ok:ok,locked:locked,timeout:timeout,fault:fault});""",
    "zero": """
[].forEach.call(document.querySelectorAll('button'),function(b){b.click()});
ANSWER={status:503,body:'servo output off'};document.querySelector('[data-servo="0"]').click();
report({off:$('status').textContent});""",
    "setting": """
var shown=$('servo_2').value;
$('servo_5').value='4';document.querySelector('[data-key="5"]').click();
ANSWER={status:409,body:'motion playing'};document.querySelector('[data-key="5"]').click();
var playing=$('status').textContent;
ANSWER={status:200,body:''};document.querySelector('[data-key="100"]').click();
var v=[];for(var i=0;i<8;i++)v.push($('servo_'+i).value);
report({shown:shown,playing:playing,fields:v});""",
    "editor": """
var initial=$('actionCode').value;
var r=$('range_6');r.value=120;r.dispatchEvent(new Event('change'));
var after6=$('actionCode').value,field6=$('servo_6').value;
$('servo_0').value='100';document.querySelector('[data-send="0"]').click();
var range100=$('range_0').value;
$('servo_0').value='170';document.querySelector('[data-send="0"]').click();
report({initial:initial,after6:after6,field6:field6,range100:range100,range0:$('range_0').value,
 field0:$('servo_0').value,code:$('actionCode').value});""",
}


def browser():
    for name in ("google-chrome", "chromium-browser", "chromium"):
        path = shutil.which(name)
        if path:
            return path
    return None


@unittest.skipIf(browser() is None, "no headless Chrome or Chromium found")
class PortalPagesBrowserTest(unittest.TestCase):
    """Runs the generated pages' scripts in a headless browser (tasks 3.1-3.5)."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        out = os.path.join(cls.tmp, "gen")
        r = run(GEN, "--out", out)
        assert r.returncode == 0, r.stderr
        cls.reports = {}
        for name, scenario in SCENARIOS.items():
            with open(os.path.join(out, "pages", name + ".html"), encoding="utf-8") as f:
                text = f.read()
            # The served calibration page: trim i shown as -i.
            text = re.sub(r"\{\{OFF(\d)\}\}", lambda m: "-" + m.group(1), text)
            text = text.replace("</head>", PROBE + "</head>", 1)
            text = text.replace("</body>", "<script>" + scenario + "</script></body>", 1)
            page = os.path.join(cls.tmp, name + ".html")
            with open(page, "w", encoding="utf-8") as f:
                f.write(text)
            dom = subprocess.run([browser(), "--headless=new", "--no-sandbox", "--disable-gpu",
                                  "--user-data-dir=" + os.path.join(cls.tmp, "profile"),
                                  "--dump-dom", "file://" + page],
                                 capture_output=True, text=True, timeout=120).stdout
            m = re.search(r'data-report="([^"]*)"', dom)
            cls.reports[name] = json.loads(html.unescape(m.group(1))) if m else None

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def report(self, name):
        r = self.reports[name]
        self.assertIsNotNone(r, "%s: the page's script did not run to the end" % name)
        self.assertLessEqual(r["right"], 320, "%s: wider than 320 px" % name)
        self.assertGreaterEqual(r["minButton"], 44, "%s: a button under 44 px" % name)
        return r

    def test_controller(self):
        r = self.report("controller")
        self.assertEqual(r["log"][:15], ["controller?pm=%d" % n for n in
                                         (6, 2, 7, 4, 3, 5, 1, 9, 11, 8, 10, 12, 13, 14, 15)])
        e = r["extra"]
        self.assertTrue(e["ok"].startswith("Sent"), e["ok"])
        self.assertIn("remote is driving", e["locked"])
        self.assertIn("locked until its session ends", e["locked"])
        self.assertIn("may still have run", e["timeout"])
        self.assertIn("Servo calibration page", e["fault"])

    def test_zero(self):
        r = self.report("zero")
        want = ["controller?servo=%d&value=%d" % (i, a) for i, a in
                ((4, 45), (0, 135), (5, 135), (1, 45), (6, 45), (2, 135), (7, 135), (3, 45))]
        self.assertEqual(r["log"][:9], want + ["controller?pm=100"])
        self.assertIn("did not accept the servo command", r["extra"]["off"])

    def test_setting(self):
        r = self.report("setting")
        e = r["extra"]
        self.assertEqual(e["shown"], "-2")
        self.assertEqual(r["log"], ["save?key=5&value=4", "save?key=5&value=4",
                                    "save?key=100&value=0"])
        self.assertIn("motion is playing", e["playing"])
        self.assertEqual(e["fields"], ["0"] * 8)

    def test_editor(self):
        r = self.report("editor")
        e = r["extra"]
        self.assertEqual(e["initial"], "70, 90, 90, 110, 110, 90, 90, 70")
        self.assertEqual(e["after6"], "70, 90, 90, 110, 110, 90, 120, 70")
        self.assertEqual(e["field6"], "120")
        self.assertEqual(e["range100"], "100")
        self.assertEqual(r["log"], ["controller?servo=6&value=120", "controller?servo=0&value=100",
                                    "controller?servo=0&value=170"])
        self.assertEqual(e["range0"], "135")
        self.assertEqual(e["field0"], "170")
        self.assertEqual(e["code"], "170, 90, 90, 110, 110, 90, 120, 70")


if __name__ == "__main__":
    unittest.main()
