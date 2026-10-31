from playwright.sync_api import sync_playwright

errs = []
with sync_playwright() as p:
    b = p.chromium.launch(channel="msedge")
    pg = b.new_page(viewport={"width": 1440, "height": 980})
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.on("console", lambda m: errs.append(m.text) if m.type == "error" else None)
    pg.goto("http://127.0.0.1:8770/", wait_until="domcontentloaded")
    pg.wait_for_timeout(3000)
    print("chip-ux   :", pg.inner_text("#chip-ux"))
    print("preview   :", pg.eval_on_selector("#preview", 'e => e.naturalWidth + " x " + e.naturalHeight'))
    print("log lines :", len(pg.inner_text("#log").splitlines()))
    pg.screenshot(path="shots/ui2.png", full_page=True)
    b.close()
print("errors    :", errs[:4] or "none")
