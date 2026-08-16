"""Собирает resources/icon.jpg из resources/icon.svg.

Иконка .nro — это JPEG ровно 256×256, его и ждёт elf2nro (см. PROJECT_ICON в
CMakeLists.txt). Рисуется иконка в SVG, потому что так её можно править
руками и смотреть в любом браузере, а JPEG — уже производная.

Растеризует headless Chrome (или Edge — он есть на любой Windows): SVG с
фильтрами размытия и тенями ни cairosvg, ни Pillow сами не осилят, а браузер
рисует его ровно так, как он выглядит в редакторе. Затем Pillow пережимает
PNG в JPEG без цветовой субдискретизации: на границах цветных панелей с белыми
силуэтами 4:2:0 даёт заметную грязь, а разница в размере — несколько килобайт.

Запуск: python tools/make_icon.py [--preview]
С --preview во временный каталог кладётся лист с иконкой в 256, 140 (как в
hbmenu) и 64 точки на тёмном и светлом фоне, чтобы оценить читаемость; путь
печатается.
"""

import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

# Сообщения на русском, а консоль Windows по умолчанию в cp866 — без этого
# скрипт печатает кракозябры или падает с UnicodeEncodeError.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.normpath(os.path.join(HERE, "..", "resources"))
SVG = os.path.join(RES, "icon.svg")
JPG = os.path.join(RES, "icon.jpg")
SIZE = 256

BROWSERS = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    "google-chrome", "chromium", "chromium-browser", "msedge",
]


def find_browser():
    for candidate in BROWSERS:
        if os.path.isabs(candidate):
            if os.path.exists(candidate):
                return candidate
        elif shutil.which(candidate):
            return shutil.which(candidate)
    return None


def rasterize(svg_path, png_path, browser):
    url = "file:///" + os.path.abspath(svg_path).replace("\\", "/")
    cmd = [
        browser, "--headless=new", "--disable-gpu", "--hide-scrollbars",
        "--no-first-run", "--no-default-browser-check",
        "--force-device-scale-factor=1",
        f"--window-size={SIZE},{SIZE}",
        f"--screenshot={png_path}",
        url,
    ]
    subprocess.run(cmd, check=True, capture_output=True, timeout=120)
    image = Image.open(png_path).convert("RGB")
    if image.size != (SIZE, SIZE):
        image = image.crop((0, 0, SIZE, SIZE))
    return image


def preview(image, path):
    sizes = (256, 140, 64)
    pad = 16
    width = sum(sizes) + pad * (len(sizes) + 1)
    height = 256 + pad * 2
    sheet = Image.new("RGB", (width * 2, height))
    draw = ImageDraw.Draw(sheet)
    for column, back in enumerate([(43, 43, 43), (235, 235, 235)]):
        draw.rectangle([column * width, 0, (column + 1) * width, height], fill=back)
        x = column * width + pad
        for size in sizes:
            sheet.paste(image.resize((size, size), Image.LANCZOS), (x, pad + (256 - size) // 2))
            x += size + pad
    sheet.save(path)


def main():
    browser = find_browser()
    if not browser:
        print("не найден Chrome/Edge для растеризации SVG", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, "icon.png")
        image = rasterize(SVG, png, browser)

    image.save(JPG, "JPEG", quality=92, subsampling=0, optimize=True)
    print(f"{os.path.relpath(JPG, os.getcwd())}: {image.size[0]}x{image.size[1]}, "
          f"{os.path.getsize(JPG)} байт")

    if "--preview" in sys.argv:
        # Не в resources: всё оттуда целиком уезжает в romfs.
        out = os.path.join(tempfile.gettempdir(), "splitscreen-hub-icon-preview.png")
        preview(image, out)
        print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
