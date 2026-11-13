#!/usr/bin/env python3
"""Generate fixed-size, fixed-font OCR stimuli and their provenance."""

import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, __version__ as pillow_version

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "main/test_images/benchmark"
FONT = ROOT / "main/font_assets/cjk_full.ttf"
SAMPLES = {"line15": "上海市徐汇区田林路智能硬件中心", "line2": "上海", "line3": "上海市"}


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype(str(FONT), 40)
    manifest = {
        "canvas": [736, 736], "font_size_px": 40, "text_origin": [48, 320],
        "font": str(FONT.relative_to(ROOT)), "font_sha256": hashlib.sha256(FONT.read_bytes()).hexdigest(),
        "background": "white", "foreground": "black", "jpeg_quality": 95,
        "jpeg_subsampling": 0, "pillow_version": pillow_version, "samples": [],
    }
    for key, text in SAMPLES.items():
        image = Image.new("RGB", (736, 736), "white")
        draw = ImageDraw.Draw(image)
        box = draw.textbbox((48, 320), text, font=font)
        assert box[2] < 736 and box[3] < 736
        draw.text((48, 320), text, font=font, fill="black")
        path = OUTPUT / f"{key}.jpg"
        image.save(path, quality=95, subsampling=0)
        manifest["samples"].append({
            "id": key, "text": text, "characters": len(text), "text_bbox": box,
            "path": str(path.relative_to(ROOT)), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        })
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print(f"Generated {len(SAMPLES)} samples in {OUTPUT}")


if __name__ == "__main__":
    main()
