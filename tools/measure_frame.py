# Measure the screen hole in Bezel frame PNGs (avoids the Dynamic Island
# overlay by scanning inside the screen edge).
from PIL import Image
import os

D = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                  "client-src", "src", "assets", "frame")

def opaque(px, x, y):
    return px[x, y][3] > 32

for f in sorted(os.listdir(D)):
    if not f.endswith('Portrait.png'):
        continue
    im = Image.open(os.path.join(D, f)).convert('RGBA')
    w, h = im.size
    px = im.load()
    # portrait: scan a column just inside the left screen edge
    x = 110
    top = bot = h // 2
    while top > 0 and not opaque(px, x, top - 1):
        top -= 1
    while bot < h - 1 and not opaque(px, x, bot + 1):
        bot += 1
    left = right = w // 2
    y = h // 2
    while left > 0 and not opaque(px, left - 1, y):
        left -= 1
    while right < w - 1 and not opaque(px, right + 1, y):
        right += 1
    hole_w, hole_h = right - left + 1, bot - top + 1
    print('%s' % f)
    print('  img %dx%d  hole x %d..%d  y %d..%d  -> %dx%d  margins L%d R%d T%d B%d'
          % (w, h, left, right, top, bot, hole_w, hole_h,
             left, w - 1 - right, top, h - 1 - bot))
    print('  hole aspect %.4f  frame px per screen px: %.4f x %.4f'
          % (hole_w / hole_h, hole_w / 1320.0, hole_h / 2868.0))
    # island bbox: opaque pixels within the hole near the top center
    cx = w // 2
    ys = [y for y in range(top, top + 400) if opaque(px, cx, y)]
    xs = [x for x in range(left, right) if opaque(px, x, top + 120)]
    print('  island at center col rows %s; at row top+120 cols %s'
          % ((min(ys), max(ys)) if ys else 'none',
             (min(xs), max(xs)) if xs else 'none'))
