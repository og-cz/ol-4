"""Crop the render to the faceplate only (no rack ears) and shift the layout to match."""
import json, os, shutil, sys
from PIL import Image

SRC, DST = sys.argv[1], sys.argv[2]
if os.path.exists(DST): shutil.rmtree(DST)
shutil.copytree(SRC, DST)
lay = json.load(open(os.path.join(SRC, 'layout.json')))
dx = int(round(lay['ear'] * lay['ppm']))
W = lay['width'] - 2 * dx
im = Image.open(os.path.join(SRC, 'plate.png'))
im.crop((dx, 0, dx + W, lay['height'])).save(os.path.join(DST, 'plate.png'))
lay['width'] = W
lay['face'] = [lay['face'][0] - dx, lay['face'][1], lay['face'][2] - dx, lay['face'][3]]
for k in ('knob_thr', 'knob_out', 'tog_st', 'tog_pwr', 'led_pwr', 'led_ovl'):
    lay[k] = [lay[k][0] - dx, lay[k][1]]
for name, b in lay['crops'].items():
    lay['crops'][name] = [b[0] - dx, b[1], b[2] - dx, b[3]]
lay['ear'] = 0
json.dump(lay, open(os.path.join(DST, 'layout.json'), 'w'), indent=1)
print('faceplate only:', W, 'x', lay['height'])
