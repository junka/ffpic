#!/bin/env python3

from math import log10, sqrt
import numpy as np
import cv2
import sys
import os
from scipy.signal import convolve2d


def PSNR(original, compressed):
    mse = np.mean((original - compressed) ** 2)
    if mse < 1.0e-10:
        # MSE is zero means no noise is present in the signal .
        # Therefore PSNR have no importance.
        return 100
    max_pixel = 255.0
    psnr = 20 * log10(max_pixel / sqrt(mse))
    return psnr


def matlab_style_gauss2D(shape=(3, 3), sigma=0.5):
    m, n = [(ss - 1.) / 2. for ss in shape]
    y, x = np.ogrid[-m:m + 1, -n:n + 1]
    h = np.exp(-(x * x + y * y) / (2. * sigma * sigma))
    h[h < np.finfo(h.dtype).eps * h.max()] = 0
    sumh = h.sum()
    if sumh != 0:
        h /= sumh
    return h


def filter2(x, kernel, mode='same'):
    return convolve2d(x, np.rot90(kernel, 2), mode=mode)


def SSIM(original, compressed, k1=0.01, k2=0.04, win_size=11, L=255):
    if not original.shape == compressed.shape:
        raise ValueError("Input Imagees must have the same dimensions")
    if len(original.shape) > 2:
        raise ValueError("Please input the images with 1 channel")

    C1 = (k1 * L) ** 2
    C2 = (k2 * L) ** 2
    window = matlab_style_gauss2D(shape=(win_size, win_size), sigma=0.5)
    window = window / np.sum(np.sum(window))

    if original.dtype == np.uint8:
        original = np.double(original)
    if compressed.dtype == np.uint8:
        compressed = np.double(compressed)

    mu1 = filter2(original, window, 'valid')
    mu2 = filter2(compressed, window, 'valid')
    mu1_sq = mu1 * mu1
    mu2_sq = mu2 * mu2
    mu1_mu2 = mu1 * mu2
    sigma1_sq = filter2(original * original, window, 'valid') - mu1_sq
    sigma2_sq = filter2(compressed * compressed, window, 'valid') - mu2_sq
    sigmal2 = filter2(original * compressed, window, 'valid') - mu1_mu2

    ssim_map = ((2 * mu1_mu2 + C1) * (2 * sigmal2 + C2)) / ((mu1_sq + mu2_sq + C1) * (sigma1_sq + sigma2_sq + C2))

    return np.mean(np.mean(ssim_map))


def CompressRatio(original, compressed):
    """Get the size of a file in bytes."""
    return os.path.getsize(original)/os.path.getsize(compressed)


def main():
    ori = cv2.imread(sys.argv[1])
    res = cv2.imread(sys.argv[2])

    ori_gray = cv2.cvtColor(ori, cv2.COLOR_BGR2GRAY)
    res_gray = cv2.cvtColor(res, cv2.COLOR_BGR2GRAY)

    print('PSNR {:.4f} dB'.format(PSNR(ori_gray, res_gray)))
    print('SSIM {:.4f} '.format(SSIM(ori_gray, res_gray)))
    print('CompressRatio {:.4f}'.format(CompressRatio(sys.argv[1], sys.argv[2])))


if __name__ == '__main__':
    main()
