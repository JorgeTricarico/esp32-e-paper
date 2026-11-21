import hashlib
import struct

with open(".pio/build/esp32-s3-paper/firmware.bin", "rb") as f:
    fw = f.read()

APP_OFF = 0  # firmware.bin de pio = solo app
print("magic at 0:", hex(fw[0]))
magic, segcount = struct.unpack_from("BB", fw, APP_OFF)
assert magic == 0xE9, f"bad magic {magic:#x}"
off = APP_OFF + 24
for _ in range(segcount):
    _addr, datalen = struct.unpack_from("<II", fw, off)
    off += 8 + datalen
sketch = fw[APP_OFF:off]
print("sketch_size:", len(sketch))
print("sketch_md5:", hashlib.md5(sketch).hexdigest())
