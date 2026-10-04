/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Host e-paper pixel harness: RGB565 to gray, gray to packed 1-bit frames.
// The runner extracts the production pixel code into epaper_pixels.inc.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "epaper_pixels.inc"

#define W 64
#define H 32

static uint8_t gray[W * H];
static uint8_t bits[W / 8 * H];
static int16_t err[2 * (W + 2)];

static int white_count(void) {
    int n = 0;
    for (size_t i = 0; i < sizeof(bits); i++) n += __builtin_popcount(bits[i]);
    return n;
}

static void check_luma(void) {
    assert(luma565(0x0000) == 0);
    assert(luma565(0xFFFF) == 255);
    // Green counts most, blue least.
    assert(luma565(0x07E0) > luma565(0xF800));
    assert(luma565(0xF800) > luma565(0x001F));
    // Mid gray stays mid gray.
    uint8_t mid = luma565(0x8410);
    assert(mid >= 126 && mid <= 134);
}

static void check_dither(void) {
    // Pure black and white come through exactly, 1 for white, leftmost pixel
    // in the top bit.
    memset(gray, 255, sizeof(gray));
    dither_frame(gray, bits, W, H, err);
    for (size_t i = 0; i < sizeof(bits); i++) assert(bits[i] == 0xFF);
    memset(gray, 0, sizeof(gray));
    gray[0] = 255;
    gray[W + 9] = 255;
    dither_frame(gray, bits, W, H, err);
    assert(bits[0] == 0x80);
    assert(bits[W / 8 + 1] == 0x40);
    assert(white_count() == 2);

    // A checkerboard of black and white is left alone.
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) gray[y * W + x] = (x + y) % 2 ? 255 : 0;
    }
    dither_frame(gray, bits, W, H, err);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W / 8; x++) assert(bits[y * W / 8 + x] == (y % 2 ? 0xAA : 0x55));
    }

    // Grays come out as that share of white dots.
    const int levels[] = {32, 128, 192};
    for (size_t l = 0; l < sizeof(levels) / sizeof(levels[0]); l++) {
        memset(gray, levels[l], sizeof(gray));
        dither_frame(gray, bits, W, H, err);
        int expect = W * H * levels[l] / 255;
        int got = white_count();
        assert(got > expect - W * H / 50 && got < expect + W * H / 50);
    }
}

int main(void) {
    check_luma();
    check_dither();
    puts("PASS epaper pixels: RGB565 luma, exact black and white, bit order, checkerboard, gray levels as dot density");
    return 0;
}
