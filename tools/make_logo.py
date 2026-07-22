import os
import sys

from PIL import Image, ImageDraw

CANVAS = 256.0
SEGMENTS = 64

APEX = (128, 54)
OUTER_CTRL = (198, 82)
TIP = (244, 182)
INNER_CTRL = (172, 126)
INNER = (128, 108)

BLACK = (13, 13, 13, 255)
WHITE = (245, 245, 245, 255)

ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


def qbez(p0, p1, p2, n=SEGMENTS):
    pts = []
    for i in range(n + 1):
        t = i / n
        u = 1.0 - t
        pts.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
                    u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))
    return pts


def mirror(pts):
    return [(2 * 128.0 - x, y) for (x, y) in pts]


def wing():
    return qbez(APEX, OUTER_CTRL, TIP) + qbez(TIP, INNER_CTRL, INNER)


def scale(pts, size):
    u = size / CANVAS
    return [(x * u, y * u) for (x, y) in pts]


def render(size, background, foreground, supersample=4):
    s = size * supersample
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    if background is not None:
        draw.rounded_rectangle([0, 0, s - 1, s - 1], radius=int(s * 0.21), fill=background)

    right = wing()
    draw.polygon(scale(right, s), fill=foreground)
    draw.polygon(scale(mirror(right), s), fill=foreground)

    return img.resize((size, size), Image.LANCZOS)


def svg(foreground):
    def fmt(p):
        return "%g,%g" % (p[0], p[1])

    right = "M%s Q%s %s Q%s %s Z" % (fmt(APEX), fmt(OUTER_CTRL), fmt(TIP),
                                     fmt(INNER_CTRL), fmt(INNER))
    left = "M%s Q%s %s Q%s %s Z" % (fmt(APEX), fmt(mirror([OUTER_CTRL])[0]),
                                    fmt(mirror([TIP])[0]), fmt(mirror([INNER_CTRL])[0]),
                                    fmt(INNER))

    return (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">\n'
        '  <path d="%s" fill="%s"/>\n'
        '  <path d="%s" fill="%s"/>\n'
        '</svg>\n' % (right, foreground, left, foreground)
    )


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "assets"
    os.makedirs(out, exist_ok=True)

    render(256, BLACK, WHITE).save(os.path.join(out, "kestrel.ico"),
                                   format="ICO", sizes=[(n, n) for n in ICO_SIZES])

    render(512, BLACK, WHITE).save(os.path.join(out, "logo.png"))
    render(512, None, WHITE).save(os.path.join(out, "logo-white.png"))
    render(512, None, BLACK).save(os.path.join(out, "logo-black.png"))

    with open(os.path.join(out, "logo.svg"), "w", encoding="utf-8") as f:
        f.write(svg("#0d0d0d"))

    with open(os.path.join(out, "logo-white.svg"), "w", encoding="utf-8") as f:
        f.write(svg("#f5f5f5"))

    print("wrote logo assets to %s" % out)


if __name__ == "__main__":
    main()
