"""Renders res/app.ico (16-256 px): a blue rounded tile with a white
three-node network and a green "link up" dot. Needs Pillow.

    python tools/make-icon.py
"""
from pathlib import Path

from PIL import Image, ImageDraw

SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
TOP, BOTTOM = (76, 154, 255), (29, 78, 216)  # matches the app's accent blue
GREEN = (63, 185, 80)


def render(size: int) -> Image.Image:
    s = 16  # supersampling
    n = size * s
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))

    # Vertical gradient clipped to a rounded square.
    grad = Image.new("RGBA", (n, n))
    gd = ImageDraw.Draw(grad)
    for y in range(n):
        t = y / (n - 1)
        gd.line([(0, y), (n, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(TOP, BOTTOM)) + (255,))
    mask = Image.new("L", (n, n), 0)
    inset = n * 0.04
    ImageDraw.Draw(mask).rounded_rectangle([inset, inset, n - inset, n - inset], radius=n * 0.22, fill=255)
    img.paste(grad, (0, 0), mask)

    d = ImageDraw.Draw(img)
    white = (255, 255, 255, 255)
    small = size <= 24  # fewer, bolder shapes so 16 px stays readable
    line = n * (0.085 if small else 0.06)

    def node(cx, cy, w, h):
        d.rounded_rectangle([n * (cx - w / 2), n * (cy - h / 2), n * (cx + w / 2), n * (cy + h / 2)],
                            radius=n * min(w, h) * 0.28, fill=white)

    top, left, right = (0.5, 0.30), (0.27, 0.71), (0.73, 0.71)
    bus_y = 0.52
    # Links: top node down to a bus, bus down to both lower nodes.
    d.line([(n * top[0], n * top[1]), (n * top[0], n * bus_y)], fill=white, width=round(line))
    d.line([(n * left[0], n * bus_y), (n * right[0], n * bus_y)], fill=white, width=round(line))
    for x, y in (left, right):
        d.line([(n * x, n * bus_y), (n * x, n * y)], fill=white, width=round(line))
    if small:
        node(*top, 0.36, 0.26)
        node(*left, 0.30, 0.24)
        node(*right, 0.30, 0.24)
    else:
        node(*top, 0.32, 0.22)
        node(*left, 0.26, 0.20)
        node(*right, 0.26, 0.20)
        # Screen cut-outs give the nodes a "device" look at larger sizes.
        for (cx, cy), w, h in ((top, 0.32, 0.22), (left, 0.26, 0.20), (right, 0.26, 0.20)):
            m = 0.045
            d.rounded_rectangle([n * (cx - w / 2 + m), n * (cy - h / 2 + m), n * (cx + w / 2 - m),
                                 n * (cy + h / 2 - m)], radius=n * 0.02, fill=BOTTOM + (255,))
        # Green status dot, top right.
        r = 0.075
        cx, cy = 0.80, 0.20
        d.ellipse([n * (cx - r), n * (cy - r), n * (cx + r), n * (cy + r)], fill=GREEN + (255,),
                  outline=white, width=round(n * 0.025))
    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    out = Path(__file__).resolve().parent.parent / "res" / "app.ico"
    images = [render(sz) for sz in SIZES]
    images[-1].save(out, format="ICO", sizes=[(sz, sz) for sz in SIZES], append_images=images[:-1])
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
