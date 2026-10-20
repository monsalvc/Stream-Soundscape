# Draws Resources/icon.png (the app icon): python3 make_icon.py icon.png /path/to/InterDisplay-ExtraBold.otf
# Colours are the app's poppy-field palette. Inter is licensed under the SIL Open Font License.
import math, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

S = 4  # supersampling
W = 1024 * S
out = sys.argv[1]
font_path = sys.argv[2]

def hexc(h, a=255):
    h = h.lstrip('#'); return (int(h[0:2],16), int(h[2:4],16), int(h[4:6],16), a)
def lerp(a, b, t): return tuple(int(round(a[i] + (b[i]-a[i])*t)) for i in range(4))

forest, pine, deep = hexc('#142920'), hexc('#1F4035'), hexc('#0E1D17')
outline, mist, sage = hexc('#2F5648'), hexc('#CDDBDE'), hexc('#64817D')
peach, orange = hexc('#FCA26E'), hexc('#F5751C')

img = Image.new('RGBA', (W, W), (0,0,0,0))

# Rounded-square body (macOS icon grid: 824px body inside 1024 canvas).
x0, y0, x1, y1 = 100*S, 100*S, 924*S, 924*S
rad = 185*S
mask = Image.new('L', (W, W), 0)
ImageDraw.Draw(mask).rounded_rectangle((x0, y0, x1, y1), rad, fill=255)
grad = Image.new('RGBA', (W, W))
gd = ImageDraw.Draw(grad)
for y in range(W):
    t = min(1, max(0, (y - y0) / (y1 - y0)))
    gd.line([(0, y), (W, y)], fill=lerp(pine, deep, t))
# soft drop shadow
shadow = Image.new('RGBA', (W, W), (0,0,0,0))
ImageDraw.Draw(shadow).rounded_rectangle((x0, y0+14*S, x1, y1+14*S), rad, fill=(0,0,0,110))
shadow = shadow.filter(ImageFilter.GaussianBlur(18*S))
img.alpha_composite(shadow)
img.paste(grad, (0,0), mask)
d = ImageDraw.Draw(img)
d.rounded_rectangle((x0, y0, x1, y1), rad, outline=hexc('#3A6655', 160), width=3*S)

# Knob ring: 270-degree sweep with the gap at the bottom.
cx, cy, r = 512*S, 478*S, 270*S
thick = 40*S
box = (cx-r, cy-r, cx+r, cy+r)
start, sweep = 135, 270
d.arc(box, start, start+sweep, fill=outline, width=thick)
value = 0.74
steps = 160
for i in range(steps):
    a0 = start + sweep*value*i/steps
    a1 = start + sweep*value*(i+1)/steps + 0.6
    d.arc(box, a0, a1, fill=lerp(peach, orange, i/(steps-1)), width=thick)
# rounded caps on the value arc
def at(angle, rr):
    a = math.radians(angle); return (cx + rr*math.cos(a), cy + rr*math.sin(a))
rc = r - thick/2
for ang, col in ((start+sweep, outline), (start, peach), (start+sweep*value, orange)):
    px, py = at(ang, rc); d.ellipse((px-thick/2, py-thick/2, px+thick/2, py+thick/2), fill=col)

# "SR" monogram
font = ImageFont.truetype(font_path, 250*S)
text = "SR"
bb = d.textbbox((0,0), text, font=font)
tw, th = bb[2]-bb[0], bb[3]-bb[1]
d.text((cx - tw/2 - bb[0], cy - th/2 - bb[1] + 4*S), text, font=font, fill=mist)

# Small fader in the gap under the knob: a track with an orange cap.
fy = 810*S
d.rounded_rectangle((330*S, fy-7*S, 694*S, fy+7*S), 7*S, fill=outline)
d.rounded_rectangle((330*S, fy-7*S, 560*S, fy+7*S), 7*S, fill=sage)
cap_w, cap_h = 46*S, 92*S
capx = 560*S
d.rounded_rectangle((capx-cap_w/2, fy-cap_h/2, capx+cap_w/2, fy+cap_h/2), 12*S, fill=orange)
d.line((capx-cap_w/2+8*S, fy, capx+cap_w/2-8*S, fy), fill=forest, width=5*S)

img = img.resize((1024, 1024), Image.LANCZOS)
img.save(out)
print("saved", out)
