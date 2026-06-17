import struct
from pathlib import Path
from PIL import Image

def rgb565_to_rgb888(v: int) -> tuple[int, int, int]:
    r = ((v >> 11) & 0x1F) << 3
    g = ((v >> 5) & 0x3F) << 2
    b = (v & 0x1F) << 3
    return r, g, b

w, h = 128, 128
path = Path("Test_Lua/tile_test/tiles/tiles.bin")
data = path.read_bytes()
assert len(data) == w * h * 2

img = Image.new("RGB", (w, h))
px = img.load()
i = 0
for y in range(h):
    for x in range(w):
        v = struct.unpack_from("<H", data, i)[0]
        px[x, y] = rgb565_to_rgb888(v)
        i += 2

img.save("tiles_preview.png")
print("saved tiles_preview.png")