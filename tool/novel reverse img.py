import struct
from pathlib import Path
from PIL import Image
from pathlib import Path
def rgb565_to_rgb888(v: int) -> tuple[int, int, int]:
    r = ((v >> 11) & 0x1F) << 3
    g = ((v >> 5) & 0x3F) << 2
    b = (v & 0x1F) << 3
    return r, g, b

def bin_to_png(bin_path: str, w: int, h: int, png_path: str) -> None:
    data = Path(bin_path).read_bytes()
    expected = w * h * 2
    if len(data) != expected:
        raise ValueError(f"サイズ不一致: {len(data)} != {expected} ({w}x{h})")

    img = Image.new("RGB", (w, h))
    px = img.load()
    i = 0
    for y in range(h):
        for x in range(w):
            v = struct.unpack_from("<H", data, i)[0]
            px[x, y] = rgb565_to_rgb888(v)
            i += 2
    img.save(png_path)
    print(f"saved {png_path}")


bg_dir = Path("Test_Lua/visual_novel/images/bg")
for bin_file in bg_dir.glob("*.bin"):
    out = bin_file.with_suffix(".png")
    bin_to_png(str(bin_file), 320, 168, str(out))