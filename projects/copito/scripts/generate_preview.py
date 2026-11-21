import math, os
from PIL import Image, ImageDraw

EPD_WIDTH = 200
EPD_HEIGHT = 200
EPD_BUFFER_SIZE = (EPD_WIDTH * EPD_HEIGHT) // 8

class Framebuffer:
    def __init__(self):
        self.buf = bytearray([0xFF] * EPD_BUFFER_SIZE)

    def clear(self, color=1):
        val = 0xFF if color else 0x00
        for i in range(EPD_BUFFER_SIZE):
            self.buf[i] = val

    def draw_pixel(self, x, y, color):
        if x < 0 or x >= EPD_WIDTH or y < 0 or y >= EPD_HEIGHT:
            return
        idx = y * 25 + (x >> 3)
        bit = 7 - (x & 0x07)
        if color:
            self.buf[idx] |= (1 << bit)
        else:
            self.buf[idx] &= ~(1 << bit)

    def get_pixel(self, x, y):
        if x < 0 or x >= EPD_WIDTH or y < 0 or y >= EPD_HEIGHT:
            return 1
        idx = y * 25 + (x >> 3)
        bit = 7 - (x & 0x07)
        return 1 if (self.buf[idx] & (1 << bit)) else 0

    def draw_dither_pixel(self, x, y, gray_level_16):
        if x < 0 or x >= EPD_WIDTH or y < 0 or y >= EPD_HEIGHT:
            return
        bayer = [
            [  0,  8,  2, 10 ],
            [ 12,  4, 14,  6 ],
            [  3, 11,  1,  9 ],
            [ 15,  7, 13,  5 ]
        ]
        threshold = bayer[y & 3][x & 3]
        self.draw_pixel(x, y, 1 if gray_level_16 > threshold else 0)

    def draw_line(self, x0, y0, x1, y1, color, thickness=1):
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            if thickness <= 1:
                self.draw_pixel(x0, y0, color)
            else:
                half = thickness // 2
                for ty in range(-half, half + 1):
                    for tx in range(-half, half + 1):
                        self.draw_pixel(x0 + tx, y0 + ty, color)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def draw_circle(self, xm, ym, r, color, fill=False):
        x = -r
        y = 0
        err = 2 - 2 * r
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
            if r_val <= y:
                y += 1
                err += y * 2 + 1
            if r_val > x or err > y:
                x += 1
                err += x * 2 + 1
            if x >= 0:
                break

    def draw_rect(self, x, y, w, h, color, fill=False):
        if fill:
            for cy in range(y, y + h):
                self.draw_line(x, cy, x + w - 1, cy, color, 1)
        else:
            self.draw_line(x, y, x + w - 1, y, color, 1)
            self.draw_line(x, y + h - 1, x + w - 1, y + h - 1, color, 1)
            self.draw_line(x, y, x, y + h - 1, color, 1)
            self.draw_line(x + w - 1, y, x + w - 1, y + h - 1, color, 1)

    def to_image(self, scale=3):
        img = Image.new('RGB', (EPD_WIDTH * scale, EPD_HEIGHT * scale), (255, 255, 255))
        draw = ImageDraw.Draw(img)
        for y in range(EPD_HEIGHT):
            for x in range(EPD_WIDTH):
                color = (250, 250, 245) if self.get_pixel(x, y) == 1 else (20, 20, 25)
                if scale == 1:
                    img.putpixel((x, y), color)
                else:
                    draw.rectangle([x * scale, y * scale, (x + 1) * scale - 1, (y + 1) * scale - 1], fill=color)
        return img

print("Base setup OK")
