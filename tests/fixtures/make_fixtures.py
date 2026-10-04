#!/usr/bin/env python3
"""Write the JPEG files dmimg_jpeg_test.c decodes (needs Pillow).

37 x 23 (not a multiple of 8 or 16) of a smooth pattern - the test checks
the decoded pixels against it with a tolerance for the compression:
    R = x * 6, G = y * 10, B = 120
"""

from PIL import Image

W, H = 37, 23
im = Image.new('RGB', (W, H))
p = im.load()
for y in range(H):
    for x in range(W):
        p[x, y] = (x * 6, y * 10, 120)

im.save('s420.jpg', quality=95, subsampling=2)          # 4:2:0 - MCU 16x16
im.save('s444.jpg', quality=95, subsampling=0)          # 4:4:4 - MCU 8x8
im.convert('L').save('gray.jpg', quality=95)
im.save('progressive.jpg', quality=95, progressive=True)    # not supported
im.resize((5, 5)).save('tiny.jpg', quality=95)          # no 1/8: smaller than a pixel
