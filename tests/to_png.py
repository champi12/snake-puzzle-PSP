#!/usr/bin/env python3
"""Pasa las capturas PPM de host_shot a PNG (y una hoja con todas)."""
import os
import sys

from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
ims = []
for f in sorted(os.listdir(src)):
    if f.endswith('.ppm'):
        im = Image.open(os.path.join(src, f))
        im.save(os.path.join(dst, f[:-4] + '.png'))
        ims.append(im)
if ims:
    cols = 3
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new('RGB', (480 * cols, 272 * rows))
    for i, im in enumerate(ims):
        sheet.paste(im, ((i % cols) * 480, (i // cols) * 272))
    sheet.save(os.path.join(dst, 'todas.png'))
print('%d capturas' % len(ims))
