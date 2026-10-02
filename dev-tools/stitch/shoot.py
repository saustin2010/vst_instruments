import glob, os, sys
from playwright.sync_api import sync_playwright
D = sys.argv[1]
out = os.path.join(D, "shots")
with sync_playwright() as pw:
    b = pw.chromium.launch()
    pg = b.new_page(viewport={"width": 1400, "height": 760})
    for f in sorted(glob.glob(os.path.join(D, "*.md"))):
        name = os.path.basename(f)[:-3]
        pg.set_content(open(f).read(), wait_until="networkidle")
        pg.wait_for_timeout(500)
        tabs = pg.query_selector_all(".tab-btn, [data-tab], .tab, [onclick^=switchTab], [onclick^=selectTab], [onclick^=setTab], [onclick^=showTab]")
        tabs = [t for t in tabs if t.is_visible()] or [None]
        for i, t in enumerate(tabs):
            if t is not None:
                t.click()
                pg.wait_for_timeout(250)
            # the 1280x628 plugin screen, whatever its class (some designs make it a few px wider, or the body itself)
            box = pg.evaluate_handle("""() => [...document.querySelectorAll('body, body *')].find(e => {
                const r = e.getBoundingClientRect(); return Math.abs(r.width - 1280) <= 24 && r.height >= 600 && r.height <= 640; })""").as_element()
            box = box or pg.query_selector(".mpc-chassis")
            path = os.path.join(out, "%s_%d.png" % (name, i))
            box.screenshot(path=path)
            bb = box.bounding_box()
            print(name, i, (t.inner_text().strip() if t else "-"), "%dx%d" % (bb["width"], bb["height"]))
    b.close()
