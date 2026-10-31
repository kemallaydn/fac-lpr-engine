#!/usr/bin/env python3
"""Generate reproducible, labelled *synthetic* Turkish plate OCR fixtures.

Not real camera evidence. Never treat successful image generation as model accuracy.
Output is compatible with tools/evaluation/evaluate.py's TSV manifest format.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import random

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont

# Intentionally limited and human-reviewed. No claimed real registrations.
PLATES = (
    "34ABL489",  # B / 8 ambiguity
    "06OZ123",   # O / 0 ambiguity
    "35BI123",   # I / 1 ambiguity
    "38VU055",   # V / Y, O / 0 ambiguity
    "34ABC123",  # B / 8, C / G ambiguity
    "34VZ7387",
    "27BGS417",
    "06AB123",
)
VARIANTS = ("clean", "blur", "dark", "tilted")
SIZE = (520, 132)
FONT_CANDIDATES = (
    "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/local/share/fonts/DejaVuSans-Bold.ttf",
)


def find_font(font_path: str | None = None) -> Path:
    options = (font_path,) if font_path is not None else FONT_CANDIDATES
    for option in options:
        if option and Path(option).is_file():
            return Path(option)
    raise FileNotFoundError("Specify --font with a licensed, locally installed TrueType font")


def render_plate(text: str, font_path: Path) -> Image.Image:
    image = Image.new("RGB", SIZE, (218, 221, 223))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((4, 8, 515, 123), radius=9, fill="white", outline=(24, 29, 33), width=5)
    draw.rectangle((8, 12, 61, 119), fill=(18, 78, 166))
    draw.text((34, 81), "TR", anchor="mm", fill="white", font=ImageFont.truetype(str(font_path), 23))
    # Known text groups, and the same visual character order as the manifest.
    display = f"{text[:2]} {text[2:-3]} {text[-3:]}" if len(text) <= 8 else f"{text[:2]} {text[2:-4]} {text[-4:]}"
    font_size = 66
    while font_size >= 30:
        font = ImageFont.truetype(str(font_path), font_size)
        left, top, right, bottom = draw.textbbox((0, 0), display, font=font)
        if right - left <= 428 and bottom - top <= 91:
            break
        font_size -= 2
    draw.text((288, 67), display, anchor="mm", font=font, fill=(15, 15, 15), stroke_width=0)
    return image


def apply_variant(image: Image.Image, variant: str, seed: int) -> Image.Image:
    if variant == "clean":
        return image
    if variant == "blur":
        return image.filter(ImageFilter.GaussianBlur(radius=1.6))
    if variant == "dark":
        rng = random.Random(seed)
        image = ImageEnhance.Brightness(image).enhance(0.49)
        image = ImageEnhance.Contrast(image).enhance(0.82)
        # Subtle, deterministic sensor noise; no nondeterministic numpy dependency.
        image = image.copy()
        pixels = image.load()
        for _ in range(1300):
            x = rng.randrange(image.width)
            y = rng.randrange(image.height)
            old = pixels[x, y]
            delta = rng.randrange(-10, 11)
            pixels[x, y] = tuple(max(0, min(255, value + delta)) for value in old)
        return image
    if variant == "tilted":
        return image.rotate(4.2, resample=Image.Resampling.BICUBIC, expand=False, fillcolor=(72, 75, 78))
    raise ValueError("unknown variant: " + variant)


def generate(out: Path, font_path: Path) -> list[tuple[str, str]]:
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for index, plate in enumerate(PLATES):
        clean = render_plate(plate, font_path)
        for variant_index, variant in enumerate(VARIANTS):
            filename = f"{index:02d}_{plate}_{variant}.png"
            transformed = apply_variant(clean, variant, seed=index * 10 + variant_index)
            transformed.save(out / filename, format="PNG", optimize=False)
            rows.append((filename, plate))
    # The evaluator expects required plates and an optional fourth column.
    with (out / "manifest.tsv").open("w", encoding="utf-8", newline="\n") as target:
        target.write("# filename\texpected_status\trequired_plates\toptional_plates\n")
        for filename, plate in rows:
            target.write(f"{filename}\tANY\t{plate}\t\n")
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--font", help="Path to installed bold TrueType font")
    args = parser.parse_args()
    rows = generate(args.out, find_font(args.font))
    print(f"Created {len(rows)} labelled synthetic plate PNGs and {args.out / 'manifest.tsv'}")


if __name__ == "__main__":
    main()
