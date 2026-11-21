import math, os
from PIL import Image, ImageDraw

EPD_WIDTH = 200
EPD_HEIGHT = 200
EPD_BUFFER_SIZE = (EPD_WIDTH * EPD_HEIGHT) // 8

class Framebuffer:
    def __init__(self):
        self.buf = bytearray([0xFF] * EPD_BUFFER_SIZE)

    def draw_pixel(self, x, y, color):
        if x < 0 or x >= EPD_WIDTH or y < 0 or y >= EPD_HEIGHT: return
        idx = y * 25 + (x >> 3)
        bit = 7 - (x & 0x07)
        if color: self.buf[idx] |= (1 << bit)
        else: self.buf[idx] &= ~(1 << bit)

    def get_pixel(self, x, y):
        if x < 0 or x >= EPD_WIDTH or y < 0 or y >= EPD_HEIGHT: return 1
        idx = y * 25 + (x >> 3)
        bit = 7 - (x & 0x07)
        return 1 if (self.buf[idx] & (1 << bit)) else 0

    def draw_line(self, x0, y0, x1, y1, color, thickness=1):
        dx = abs(x1 - x0); sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0); sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            if thickness <= 1:
                self.draw_pixel(x0, y0, color)
            else:
                half = thickness // 2
                for ty in range(-half, half + 1):
                    for tx in range(-half, half + 1):
                        self.draw_pixel(x0 + tx, y0 + ty, color)
            if x0 == x1 and y0 == y1: break
            e2 = 2 * err
            if e2 >= dy: err += dy; x0 += sx
            if e2 <= dx: err += dx; y0 += sy

    def draw_circle(self, xm, ym, r, color, fill=False):
        x = -r; y = 0; err = 2 - 2 * r
        while True:
            if fill:
                for i in range(xm + x, xm - x + 1):
                    self.draw_pixel(i, ym + y, color)
                    self.draw_pixel(i, ym - y, color)
            else:
                self.draw_pixel(xm - x, ym + y, color)
                self.draw_pixel(xm - y, ym - x, color)
                self.draw_pixel(xm + x, ym - y, color)
                self.draw_pixel(xm + y, ym + x, color)
            r_val = err
            if r_val <= y: y += 1; err += y * 2 + 1
            if r_val > x or err > y: x += 1; err += x * 2 + 1
            if x >= 0: break

    def to_image(self, scale=3):
        img = Image.new('RGB', (EPD_WIDTH * scale, EPD_HEIGHT * scale), (250, 250, 245))
        draw = ImageDraw.Draw(img)
        for y in range(EPD_HEIGHT):
            for x in range(EPD_WIDTH):
                if self.get_pixel(x, y) == 0:
                    draw.rectangle([x * scale, y * scale, (x + 1) * scale - 1, (y + 1) * scale - 1], fill=(22, 22, 26))
        return img

# EXACT COPY OF CURRENT faces_bitmaps.h:
def current_drawRudoBrows(fb, cy=88):
    fb.draw_line(34, cy - 28, 76, cy - 18, 0, 4)
    fb.draw_line(34, cy - 27, 74, cy - 17, 0, 2)
    fb.draw_line(166, cy - 28, 124, cy - 18, 0, 4)
    fb.draw_line(166, cy - 27, 126, cy - 17, 0, 2)
    fb.draw_line(97, cy - 21, 97, cy - 13, 0, 2)
    fb.draw_line(103, cy - 21, 103, cy - 13, 0, 2)

def current_drawRudoNose(fb, cx=100, cy=114):
    fb.draw_line(cx - 2, cy - 14, cx - 2, cy + 2, 0, 2)
    fb.draw_line(cx - 2, cy + 2, cx + 5, cy + 5, 0, 2)
    fb.draw_line(cx + 5, cy + 5, cx - 4, cy + 6, 0, 2)
    fb.draw_line(cx - 8, cy + 4, cx - 5, cy + 5, 0, 2)

def current_drawRudoEye(fb, cx, cy, pupilOffsetX, pupilOffsetY, blink):
    if blink == 3:
        for x in range(-18, 19):
            fb.draw_pixel(cx + x, cy, 0)
            fb.draw_pixel(cx + x, cy + 1, 0)
            fb.draw_pixel(cx + x, cy + 2, 0)
        return
    rx = 18
    ry = 7 if blink == 1 else (4 if blink == 2 else 13)
    for y in range(-ry, ry + 1):
        for x in range(-rx, rx + 1):
            if (x * x) / float(rx * rx) + (y * y) / float(ry * ry) <= 1.0:
                fb.draw_pixel(cx + x, cy + y, 0)
    if blink >= 2: return
    for y in range(-ry + 2, ry - 1):
        for x in range(-rx + 2, rx - 1):
            if (x * x) / float((rx - 2)**2) + (y * y) / float((ry - 2)**2) <= 1.0:
                fb.draw_pixel(cx + x, cy + y, 1)
    lidH = 3 if blink == 1 else 4
    for y in range(-ry, -ry + lidH + 1):
        for x in range(-rx, rx + 1):
            fb.draw_pixel(cx + x, cy + y, 0)
    fb.draw_line(cx - rx - 2, cy - ry + lidH, cx + rx + 2, cy - ry + lidH, 0, 2)
    pr = 4 if blink == 1 else 5
    pcx = cx + pupilOffsetX
    pcy = cy + pupilOffsetY + (0 if blink == 1 else 1)
    fb.draw_circle(pcx, pcy, pr, 0, True)
    fb.draw_pixel(pcx - pr + 2, pcy - pr + 2, 1)
    fb.draw_pixel(pcx - pr + 3, pcy - pr + 2, 1)
    fb.draw_pixel(pcx - pr + 2, pcy - pr + 3, 1)
    fb.draw_pixel(pcx - pr + 3, pcy - pr + 3, 1)
    if cx < 100:
        fb.draw_line(cx - rx - 3, cy - 2, cx - rx, cy, 0, 2)
    else:
        fb.draw_line(cx + rx + 3, cy - 2, cx + rx, cy, 0, 2)

def current_drawCigarAndSmoke(fb, mouthX=100, mouthY=138, iterFrame=0):
    x0 = mouthX + 12
    y0 = mouthY - 2
    x1 = mouthX + 60
    y1 = mouthY + 8
    for offset in range(-5, 6):
        ox = (offset * 2) // 10
        oy = offset
        fb.draw_line(x0 + ox, y0 + oy, x1 + ox, y1 + oy, 0, 1)
    for offset in range(-4, 5):
        ox = (offset * 2) // 10
        oy = offset
        fb.draw_line(mouthX + 36 + ox, mouthY + 3 + oy, mouthX + 42 + ox, mouthY + 4 + oy, 1, 1)
    fb.draw_line(mouthX + 35, mouthY - 2, mouthX + 37, mouthY + 8, 0, 1)
    fb.draw_line(mouthX + 43, mouthY - 1, mouthX + 45, mouthY + 9, 0, 1)
    fb.draw_pixel(mouthX + 39, mouthY + 3, 0)
    fb.draw_line(x1 + 1, y1 - 4, x1 + 1, y1 + 5, 0, 2)
    fb.draw_pixel(x1, y1 - 3, 0)
    fb.draw_pixel(x1 - 1, y1 - 1, 0)
    fb.draw_pixel(x1, y1 + 2, 0)
    fb.draw_pixel(x1 - 1, y1, 1)
    fb.draw_pixel(x1 - 2, y1, 1)
    fb.draw_pixel(x1 - 2, y1 + 1, 1)
    smokeBaseX = x1 + 4
    smokeBaseY = y1 - 2
    for y in range(0, 85, 2):
        t = y / 85.0
        sy = smokeBaseY - y
        if sy < 26 or sy >= 200: continue
        wave = math.sin(t * 6.28 - iterFrame * 0.8) * (2.5 + t * 6.0)
        cx = smokeBaseX + 5.0 + (t * 12.0) + wave
        if cx < 168.0: cx = 168.0
        if cx > 195.0: cx = 195.0
        sx = int(cx)
        sw = int(1.0 + t * 3.5)
        fb.draw_pixel(sx, sy, 0)
        fb.draw_pixel(sx + 1, sy, 0)
        if sw >= 2 and (y % 3 != 0):
            fb.draw_pixel(sx - 1, sy, 0)
            fb.draw_pixel(sx + 2, sy, 0)
        if sw >= 3 and (y % 2 == 0):
            fb.draw_pixel(sx - 2, sy, 0)
            fb.draw_pixel(sx + 3, sy, 0)
    ringY = smokeBaseY - 60 - ((iterFrame * 3) % 20)
    ringX = 184 + ((iterFrame * 2) % 6)
    if 28 <= ringY <= 75:
        fb.draw_circle(ringX, ringY, 4, 0, False)
        fb.draw_circle(ringX + 3, ringY - 5, 3, 0, False)

def current_drawMouth(fb, cx=100, cy=138):
    for x in range(-20, 21):
        norm = x / 20.0
        y = cy + int((1.0 - norm * norm) * 7.0)
        fb.draw_pixel(cx + x, y, 0)
        fb.draw_pixel(cx + x, y + 1, 0)
        fb.draw_pixel(cx + x, y + 2, 0)
    fb.draw_pixel(cx - 21, cy - 1, 0)
    fb.draw_pixel(cx - 22, cy - 2, 0)
    fb.draw_pixel(cx + 21, cy - 1, 0)
    fb.draw_pixel(cx + 22, cy - 2, 0)

fb_curr = Framebuffer()
fb_curr.draw_line(10, 18, 22, 18, 0, 2)
fb_curr.draw_line(164, 8, 186, 8, 0, 1)
fb_curr.draw_line(164, 18, 186, 18, 0, 1)

current_drawRudoEye(fb_curr, 58, 92, 0, 0, 0)
current_drawRudoEye(fb_curr, 142, 92, 0, 0, 1) # wink
current_drawRudoBrows(fb_curr, 92)
current_drawRudoNose(fb_curr, 100, 114)
current_drawMouth(fb_curr, 100, 138)
current_drawCigarAndSmoke(fb_curr, 100, 138, 0)
fb_curr.draw_line(16, 162, 184, 162, 0, 1)

fb_curr.to_image(scale=3).save("scratch/current_file_render.png")
print("Saved scratch/current_file_render.png")
