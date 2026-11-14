"""Compose the two original project marks into SpatialFabric's unified documentation brand.
Requires Pillow 12+: python -m pip install "Pillow>=12"
Original artwork remains in assets/logo.png and assets/audioloom-source.jpg.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]


def artwork(path, box, width):
    image = Image.open(path).convert("RGB").crop(box)
    rgba = image.convert("RGBA")
    rgba.putalpha(Image.new("L", image.size))
    pixels = []
    for r, g, b in image.get_flattened_data():
        saturation = max(r, g, b) - min(r, g, b)
        alpha = min(255, max(0, saturation * 3)) if max(r, g, b) > 18 else 0
        pixels.append(alpha)
    mask = Image.new("L", image.size)
    mask.putdata(pixels)
    rgba.putalpha(mask)
    return rgba.resize((width, round(width * rgba.height / rgba.width)), Image.Resampling.LANCZOS)


def font(size):
    for path in [Path("C:/Windows/Fonts/seguisb.ttf"), Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")]:
        if path.exists():
            return ImageFont.truetype(str(path), size)
    raise RuntimeError("Install Segoe UI or DejaVu Sans to reproduce branding text.")


def main():
    fabric = artwork(ROOT / "assets/logo.png", (275, 200, 730, 585), 370)
    audio = artwork(ROOT / "assets/audioloom-source.jpg", (240, 240, 790, 535), 580)
    mark = Image.new("RGBA", (720, 640), (0, 0, 0, 0))
    mark.alpha_composite(fabric, (215, 50))
    mark.alpha_composite(audio, (60, 280))
    mark.save(ROOT / "assets/spatialfabric-mark.png")
    icon = Image.new("RGBA", (640, 640), (7, 11, 22, 255))
    icon.alpha_composite(mark.resize((640, 569), Image.Resampling.LANCZOS), (0, 18))
    icon.resize((128, 128), Image.Resampling.LANCZOS).save(ROOT / "Resources/Icon128.png")
    banner = Image.new("RGBA", (1600, 640), (7, 11, 22, 255))
    banner.alpha_composite(mark, (0, 0))
    draw = ImageDraw.Draw(banner)
    draw.text((735, 177), "SpatialFabric", font=font(102), fill=(244, 247, 255))
    draw.text((742, 314), "Spatial control. Direct playback.", font=font(37), fill=(174, 191, 217))
    draw.text((743, 383), "AudioLoom, woven in.", font=font(30), fill=(119, 172, 255))
    for x, color in [(745, (42, 157, 255)), (816, (151, 67, 239)), (887, (255, 111, 32))]:
        draw.rounded_rectangle((x, 458, x + 55, 464), radius=3, fill=color)
    banner.convert("RGB").save(ROOT / "assets/spatialfabric-unified.png", optimize=True)


if __name__ == "__main__":
    main()
