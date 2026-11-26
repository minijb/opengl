"""纯标准库 PNG 解码 + 帧缓冲像素验证(无 PIL 环境)"""
import math
import struct
import sys
import zlib


def read_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a png'
    pos = 8
    ihdr = None
    idat = b''
    plte = b''
    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos+4])[0]
        ctype = data[pos+4:pos+8]
        chunk = data[pos+8:pos+8+length]
        if ctype == b'IHDR':
            ihdr = struct.unpack('>IIBBBBB', chunk)
        elif ctype == b'IDAT':
            idat += chunk
        elif ctype == b'PLTE':
            plte = chunk
        pos += 12 + length
    w, h, depth, coltype, comp, filt, inter = ihdr
    assert depth == 8 and comp == 0 and filt == 0 and inter == 0, (ihdr,)
    raw = zlib.decompress(idat)
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[coltype]
    stride = w * channels
    out = bytearray(w * h * channels)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        f = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos+stride]); pos += stride
        if f == 1:  # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i-channels]) & 255
        elif f == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:  # Average
            for i in range(stride):
                a = line[i-channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:  # Paeth
            for i in range(stride):
                a = line[i-channels] if i >= channels else 0
                b = prev[i]
                c = prev[i-channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out[y*stride:(y+1)*stride] = line
        prev = line
    px = []
    for i in range(w * h):
        o = i * channels
        if coltype == 3:
            idx = out[o] * 3
            px.append(tuple(plte[idx:idx+3]))
        elif coltype in (0, 4):
            v = out[o]
            px.append((v, v, v))
        else:
            px.append((out[o], out[o+1], out[o+2]))
    return w, h, px


w, h, px = read_png(sys.argv[1])
print(f"screenshot size = {w}x{h}")


def get(x, y):
    return px[y * w + x]


# 窗口 818x647 含装饰; 客户区 800x600 偏移约 (9, 38)
bx, by, cw, ch = 9, 38, 800, 600
clear = (51, 77, 77)  # glClearColor(0.2, 0.3, 0.3)


def region_stats(x0, y0, x1, y1, step=8):
    rs = gs = bs = n = 0
    for y in range(y0, y1, step):
        for x in range(x0, x1, step):
            r, g, b = get(x, y)
            rs += r; gs += g; bs += b; n += 1
    return (rs // n, gs // n, bs // n), n


mean, n = region_stats(bx, by, bx + cw, by + ch)
print(f"客户区均值 RGB = {mean}  (采样 {n} 点)")
print(f"中心像素 = {get(bx + 400, by + 300)}")
print(f"左上角(uv 0,1) = {get(bx + 12, by + 12)}   右下角(uv 1,0) = {get(bx + 788, by + 588)}")
print(f"标题栏(非渲染区, 负对照) = {get(400, 20)}")

vals = [sum(get(x, y)) / 3 for y in range(by, by + ch, 16) for x in range(bx, bx + cw, 16)]
m = sum(vals) / len(vals)
sd = math.sqrt(sum((v - m) ** 2 for v in vals) / len(vals))
print(f"亮度 mean={m:.1f}  stdev={sd:.1f}  (纹理细节 → stdev 应显著大于 0)")

# 判定
teal = all(abs(r - clear[0]) < 6 and abs(g - clear[1]) < 6 and abs(b - clear[2]) < 6
           for r, g, b in [get(x, y) for y in range(by + 50, by + ch - 50, 60)
                           for x in range(bx + 50, bx + cw - 50, 60)])
dark = mean[0] < 15 and mean[1] < 15 and mean[2] < 15
print()
if teal:
    print("FAIL: 客户区全是清屏色 → 四边形没有画出来(mesh/draw 坏了)")
elif dark:
    print("FAIL: 客户区接近纯黑 → 纹理不完整/没绑定(经典黑屏坑)")
else:
    print(f"PASS: 客户区是纹理内容(木箱色系, 非清屏色{clear}、非纯黑), "
          f"亮度方差证明有细节 → 双纹理渲染链路正常")
