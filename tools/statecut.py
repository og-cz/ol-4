"""Cut a state crop out of its render by its difference from the base plate.
Only pixels the control (and its own shadow / glow) actually changes keep alpha, so a crop has
no visible edge and never leaves a ghost of another state behind."""
import numpy as np
from PIL import Image, ImageFilter

def statecut(crop_rgba, plate_rgba, box, lo=0.8, hi=5.0, border_px=10):
    x0, y0, x1, y1 = [int(v) for v in box]
    c = np.asarray(crop_rgba, np.float32)
    b = np.asarray(plate_rgba, np.float32)[y0:y1, x0:x1]
    h, w = c.shape[:2]
    b = b[:h, :w]
    d = np.abs(c[..., :3] - b[..., :3]).max(axis=2)
    a = np.clip((d - lo) / (hi - lo), 0, 1)
    a = a * a * (3 - 2 * a)
    # soften the matte a little so anti-aliased edges and soft shadows blend
    am = Image.fromarray((a * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))
    a = np.maximum(a, np.asarray(am, np.float32) / 255 * 0.9)
    yy, xx = np.mgrid[0:h, 0:w]
    edge = np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy)).astype(np.float32)
    a *= np.clip(edge / border_px, 0, 1)
    out = c.copy()
    out[..., 3] = a * (c[..., 3] / 255) * 255
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), 'RGBA')
