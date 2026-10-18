# Generates assets/icons/lordos-crown.png: the white crown of the LordOS logo (transparent background).
# Usage: python3 tools/gen-logo.py   (needs Pillow; run on the development machine)
from PIL import Image, ImageDraw, ImageFilter

S = 512                      # drawn large and scaled down: smooth edges
img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
W = (255, 255, 255, 255)
# crown body: base and three peaks (the middle one taller)
crown = [(96, 360), (78, 170), (176, 262), (256, 112), (336, 262), (434, 170), (416, 360)]
d.polygon(crown, fill=W)
# base band
d.rounded_rectangle((92, 372, 420, 420), radius=16, fill=W)
# gems on the tips
for cx, cy, r in ((78, 160, 26), (256, 100, 32), (434, 160, 26)):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 214, 10, 255))
# band groove (detail)
d.rounded_rectangle((132, 386, 380, 404), radius=9, fill=(0, 0, 0, 0))
img = img.resize((256, 256), Image.LANCZOS)
img.save('assets/icons/lordos-crown.png')
print('assets/icons/lordos-crown.png')
