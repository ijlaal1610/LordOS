# Genera assets/icons/lordos-crown.png: la corona blanca del logo de LordOS (fondo transparente).
# Uso: python3 tools/gen-logo.py   (necesita Pillow; se corre en la máquina de desarrollo)
from PIL import Image, ImageDraw, ImageFilter

S = 512                      # se dibuja grande y se reduce: bordes suaves
img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
W = (255, 255, 255, 255)
# cuerpo de la corona: base y tres picos (el del medio más alto)
crown = [(96, 360), (78, 170), (176, 262), (256, 112), (336, 262), (434, 170), (416, 360)]
d.polygon(crown, fill=W)
# banda de la base
d.rounded_rectangle((92, 372, 420, 420), radius=16, fill=W)
# piedras en las puntas
for cx, cy, r in ((78, 160, 26), (256, 100, 32), (434, 160, 26)):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 214, 10, 255))
# hueco de la banda (marca)
d.rounded_rectangle((132, 386, 380, 404), radius=9, fill=(0, 0, 0, 0))
img = img.resize((256, 256), Image.LANCZOS)
img.save('assets/icons/lordos-crown.png')
print('assets/icons/lordos-crown.png')
