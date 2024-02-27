#!/bin/env python3
import math

cos = math.cos
pi = math.pi
sqrt = math.sqrt

def e(n) -> float :
    return sqrt(0.5) if n == 0 else 1

def dct(x : int, u: int):
    # the 13 bit precision scaled integer instead of double float
    res_float = e(x) * cos((2 * u + 1) * x * pi / 16.0)
    return int((res_float) * (1<<13))
    # return res_float

for x in range(8):
    coefs = ['{:8}'.format(dct(x, u)) for u in range(8)]
    print(', '.join(coefs + ['']))

def yuv_to_rgb(y, u, v):
    r = y + 1.280 * v
    g = y - 0.215 * u - 0.381 * v
    b = y + 2.218 * u
    return b

# v2r = ['{:.2f}'.format(yuv_to_rgb(0, u, 0)) for u in range(256)]
# print(', '.join(v2r))

def rgb_to_yuv(r, g, b):
    y = 0.299 * r + 0.587 * g + 0.114 * b
    u =  -0.168735892 * r - 0.331264108 * g + 0.5 * b
    v = 0.5 * r - 0.418687589 * g - 0.081312411 * b
    return y, u, v

r2y = ['{:.2f}'.format(rgb_to_yuv(r, 0, 0)[0]) for r in range(256)]
r2u = ['{:.2f}'.format(rgb_to_yuv(r, 0, 0)[1]) for r in range(256)]
r2v = ['{:.2f}'.format(rgb_to_yuv(r, 0, 0)[2]) for r in range(256)]
print('{')
print(', '.join(r2y))
print('},{')
print(', '.join(r2u))
print('},{')
print(', '.join(r2v))
print('}')
