"""Genera el logo de ReKapture (ico, png) y las imagenes del instalador.
Uso: python tools/make_icons.py   (requiere Pillow)
"""
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS = os.path.join(ROOT, "resources", "icons")
INSTALLER = os.path.join(ROOT, "installer")
os.makedirs(ICONS, exist_ok=True)
os.makedirs(INSTALLER, exist_ok=True)

S = 1024  # lienzo de trabajo (se reduce con suavizado)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def make_logo():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # Fondo: cuadrado redondeado con degradado vertical oscuro
    bg = Image.new("RGBA", (S, S))
    px = bg.load()
    top, bottom = (40, 40, 46), (18, 18, 20)
    for y in range(S):
        c = lerp(top, bottom, y / (S - 1)) + (255,)
        for x in range(S):
            px[x, y] = c
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((24, 24, S - 24, S - 24), radius=230, fill=255)
    img.paste(bg, (0, 0), mask)

    d = ImageDraw.Draw(img)
    cx = cy = S // 2

    # Anillo rojo con degradado (mas claro arriba a la izquierda)
    ring = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    rd = ImageDraw.Draw(ring)
    r_out, r_in = 330, 250
    rd.ellipse((cx - r_out, cy - r_out, cx + r_out, cy + r_out), fill=(229, 57, 69, 255))
    rd.ellipse((cx - r_in, cy - r_in, cx + r_in, cy + r_in), fill=(0, 0, 0, 0))
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.arc((cx - r_out + 40, cy - r_out + 40, cx + r_out - 40, cy + r_out - 40), start=195, end=285,
           fill=(255, 120, 130, 200), width=34)
    glow = glow.filter(ImageFilter.GaussianBlur(10))
    img = Image.alpha_composite(img, ring)
    img = Image.alpha_composite(img, glow)

    # Punto central blanco con halo suave
    halo = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(halo).ellipse((cx - 190, cy - 190, cx + 190, cy + 190), fill=(229, 57, 69, 90))
    halo = halo.filter(ImageFilter.GaussianBlur(30))
    img = Image.alpha_composite(img, halo)
    d = ImageDraw.Draw(img)
    d.ellipse((cx - 140, cy - 140, cx + 140, cy + 140), fill=(244, 244, 244, 255))
    # Brillo del punto
    d.ellipse((cx - 95, cy - 105, cx - 20, cy - 40), fill=(255, 255, 255, 255))
    return img


def save_assets():
    logo = make_logo()
    sizes = [16, 24, 32, 48, 64, 128, 256]
    frames = [logo.resize((s, s), Image.LANCZOS) for s in sizes]
    frames[-1].save(os.path.join(ICONS, "rekapture.png"))
    logo.resize((512, 512), Image.LANCZOS).save(os.path.join(ICONS, "rekapture-512.png"))
    # ICO multitamano (Pillow guarda cada tamano pedido)
    logo.resize((256, 256), Image.LANCZOS).save(
        os.path.join(ICONS, "rekapture.ico"), format="ICO", sizes=[(s, s) for s in sizes])

    # Imagenes del asistente del instalador (Inno Setup: BMP)
    font_path = "/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf"
    try:
        font = ImageFont.truetype(font_path, 26)
        small = ImageFont.truetype(font_path, 13)
    except OSError:
        font = small = ImageFont.load_default()

    wiz = Image.new("RGB", (164, 314))
    wp = wiz.load()
    for y in range(314):
        c = lerp((34, 34, 40), (14, 14, 16), y / 313)
        for x in range(164):
            wp[x, y] = c
    wiz.paste(logo.resize((112, 112), Image.LANCZOS), (26, 52), logo.resize((112, 112), Image.LANCZOS))
    wd = ImageDraw.Draw(wiz)
    wd.text((82, 190), "Re", font=font, fill=(244, 244, 244), anchor="rm")
    wd.text((82, 190), "Kapture", font=font, fill=(229, 57, 69), anchor="lm")
    wd.text((82, 230), "Grabador de gameplays", font=small, fill=(160, 160, 160), anchor="mm")
    wiz.save(os.path.join(INSTALLER, "wizard.bmp"))

    sm = Image.new("RGB", (55, 58), (21, 21, 21))
    lg = logo.resize((48, 48), Image.LANCZOS)
    sm.paste(lg, (4, 5), lg)
    sm.save(os.path.join(INSTALLER, "wizard-small.bmp"))


if __name__ == "__main__":
    save_assets()
    print("Iconos e imagenes generados.")
