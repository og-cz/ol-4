"""Convert the Blender renders into plugin resources (PNG/JPEG that JUCE can load)."""
import json, math, os, sys
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from statecut import statecut

SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'render-out')
DST = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', 'Resources')
os.makedirs(DST, exist_ok=True)
lay = json.load(open(os.path.join(SRC, 'layout.json')))
BAY = np.array([18, 20, 21], np.float32)

# plate: tone the rack ears, flatten onto the rack colour (slots and edges show the rack), JPEG
pl = np.array(Image.open(os.path.join(SRC, 'plate.png')).convert('RGBA')).astype(np.float32)
ew = int(round(lay['ear'] * lay['ppm']))
if ew <= 0: ew = None
for sl in ((np.s_[:, :ew], np.s_[:, -ew:]) if ew else ()):
    pl[sl + (slice(0, 3),)] *= np.array([0.80, 0.80, 0.78])
a = pl[..., 3:4] / 255.0
rgb = pl[..., :3] * a + BAY * (1 - a)
Image.fromarray(rgb.clip(0, 255).astype(np.uint8)).save(os.path.join(DST, 'plate.jpg'), quality=92, subsampling=0)

# state crops: keep only what each control changes (difference matte against the base plate)
plate_rgba = Image.open(os.path.join(SRC, 'plate.png')).convert('RGBA')
for name, box in lay['crops'].items():
    im = Image.open(os.path.join(SRC, 'crop_%s.png' % name)).convert('RGBA')
    statecut(im, plate_rgba, box).save(os.path.join(DST, 'crop_%s.png' % name), optimize=True)

# knob film strip -> sprite sheet, softbox shadow faded before the crop edge
frames = sorted(f for f in os.listdir(os.path.join(SRC, 'knob')) if f.endswith('.png'))
fw, fh = Image.open(os.path.join(SRC, 'knob', frames[0])).size
ppm = lay['ppm']
yy, xx = np.mgrid[0:fh, 0:fw]
cx = -lay['knob_box'][0] - 0.5; cy = -lay['knob_box'][1] - 0.5
r_mm = np.hypot(xx - cx, yy - cy) / ppm
edge = min(fw, fh) / 2 / ppm - 0.15; kr = lay['mm']['knob_r']
fade = np.clip((edge - r_mm) / (edge - (kr + 0.6)), 0, 1); fade = fade * fade * (3 - 2 * fade)
cols = int(math.ceil(math.sqrt(len(frames)))); rows = int(math.ceil(len(frames) / cols))
sheet = Image.new('RGBA', (cols * fw, rows * fh), (0, 0, 0, 0))
for i, f in enumerate(frames):
    im = np.array(Image.open(os.path.join(SRC, 'knob', f)).convert('RGBA')).astype(np.float32)
    im[..., 3] *= fade
    sheet.paste(Image.fromarray(im.clip(0, 255).astype(np.uint8)), ((i % cols) * fw, (i // cols) * fh))
sheet.save(os.path.join(DST, 'knob.png'), optimize=True)

lay['sprite'] = {'cols': cols, 'rows': rows, 'fw': fw, 'fh': fh}
json.dump(lay, open(os.path.join(DST, 'layout.json'), 'w'), indent=1)
for f in sorted(os.listdir(DST)):
    print(f'{f:28s} {os.path.getsize(os.path.join(DST, f)) / 1e3:9.1f} kB')
