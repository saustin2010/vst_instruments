"""Contact sheet of a port's waveform pictures (for checking them):  python3 sheet.py <port> <param> <out.png>
Runs in the mpc-vst-html-art container (Playwright)."""
import glob, json, os, re, sys
from playwright.sync_api import sync_playwright
port, param, out = sys.argv[1:4]
D = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "schwung-ports", port)
opts = next(x for x in json.load(open(os.path.join(D, "params.json")))["params"] if x["key"] == param)["options"]
cells = "".join('<figure><img src="file://%s/images/waves/%s_%d.svg"><figcaption>%d %s</figcaption></figure>'
                % (D, param, i, i, o) for i, o in enumerate(opts))
html = ('<html><body style="margin:0;background:#111;font:12px monospace;color:#ccc">'
        '<div style="display:grid;grid-template-columns:repeat(6,280px);gap:6px;padding:6px">%s</div>'
        '<style>img{width:280px;height:60px}figure{margin:0}</style></body></html>' % cells)
tmp = os.path.join(D, "build", "sheet.html")
open(tmp, "w").write(html)
with sync_playwright() as pw:
    b = pw.chromium.launch()
    pg = b.new_page(viewport={"width": 1730, "height": 400})
    pg.goto("file://" + tmp)
    pg.wait_for_timeout(500)
    pg.screenshot(path=out, full_page=True)
    b.close()
print("wrote", out)
