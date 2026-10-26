"""Proof for proposed GPU raw-layout conversion; no GPU code is implemented here.

The reference loads production block/column tables and depth XOR constants.
The candidate uses explicit address-bit permutations suitable for GLSL.
"""
from pathlib import Path
import random
import re
import math
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
GS = ROOT / 'app/src/main/cpp/pcsx2/pcsx2/GS'
TABLES = (GS / 'GSTables.cpp').read_text()
HEADER = (GS / 'GSLocalMemory.h').read_text()

def table(name):
    body = re.search(r'\b' + name + r'\[\d+\]\[\d+\]\s*=\s*\{(.*?)\n\};', TABLES, re.S).group(1)
    return [[int(v) for v in re.findall(r'\d+', row)] for row in re.findall(r'\{([^{}]+)\}', body)]

BLOCKS = {kind: table('_blockTable' + kind) for kind in ('32', '16', '16S')}
COLUMNS = {kind: table('columnTable' + kind) for kind in ('32', '16')}
# Each list says which coordinate bit occupies halfword address bit 0..11.
BITS = {
    '32': ('half', 'x0', 'y0', 'x1', 'x2', 'y1', 'y2', 'x3', 'y3', 'x4', 'y4', 'x5'),
    '16': ('x3', 'x0', 'y0', 'x1', 'x2', 'y1', 'y2', 'y3', 'x4', 'y4', 'x5', 'y5'),
    '16S': ('x3', 'x0', 'y0', 'x1', 'x2', 'y1', 'y2', 'y3', 'x4', 'y5', 'y4', 'x5'),
}

def depth_xor(kind):
    return int(re.search(r'swizzle' + kind + r'Z\s*\{swizzleTables' + kind + r',\s*(0x[0-9a-fA-F]+)', HEADER).group(1), 16)

def reference(kind, depth, x, y, bw, bp=32, half=0):
    h = 32 if kind == '32' else 64
    block_w = 8 if kind == '32' else 16
    col = COLUMNS['32' if kind == '32' else '16']
    block = BLOCKS[kind][(y % h) // 8][(x % 64) // block_w]
    block ^= depth_xor(kind) if depth else 0
    page = (y // h) * bw + x // 64
    return bp * 128 + page * 4096 + block * 128 + col[y % 8][x % block_w] * (2 if kind == '32' else 1) + (half if kind == '32' else 0)

def candidate(kind, depth, x, y, bw, bp=32, half=0):
    h = 32 if kind == '32' else 64
    local = 0
    for address_bit, coordinate in enumerate(BITS[kind]):
        value = half if coordinate == 'half' else ((x if coordinate[0] == 'x' else y) >> int(coordinate[1:])) & 1
        local |= value << address_bit
    local ^= (depth_xor(kind) * 128) if depth else 0
    return bp * 128 + ((y // h) * bw + x // 64) * 4096 + local

def inverse(kind, depth, address, bw, bp=32):
    page, local = divmod(address - bp * 128, 4096)
    local ^= (depth_xor(kind) * 128) if depth else 0
    x = y = half = 0
    for bit, coordinate in enumerate(BITS[kind]):
        v = (local >> bit) & 1
        if coordinate == 'half':
            half = v
        elif coordinate[0] == 'x':
            x |= v << int(coordinate[1:])
        else:
            y |= v << int(coordinate[1:])
    return x + (page % bw) * 64, y + (page // bw) * (32 if kind == '32' else 64), half

class RawLayoutMapping(unittest.TestCase):
    def test_all_six_production_formats_and_page_pitches(self):
        cases = 0
        for depth in (False, True):
            for kind in BITS:
                h = 32 if kind == '32' else 64
                for bw in (1, 2, 5):
                    for page_x, page_y in ((0, 0), (bw - 1, 2)):
                        for y in range(h):
                            for x in range(64):
                                xx, yy = x + page_x * 64, y + page_y * h
                                for half in range(2 if kind == '32' else 1):
                                    actual = candidate(kind, depth, xx, yy, bw, half=half)
                                    self.assertEqual(actual, reference(kind, depth, xx, yy, bw, half=half))
                                    self.assertEqual(inverse(kind, depth, actual, bw), (xx, yy, half))
                                    cases += 1
        print(f'PASS {cases} production-table address/inverse checks across six formats')

    def test_complete_page_conversion_both_directions(self):
        for depth in (False, True):
            for kind in ('16', '16S'):
                inverse32 = {reference('32', depth, x, y, 1, half=h): (x, y, h)
                             for y in range(32) for x in range(64) for h in range(2)}
                inverse16 = {reference(kind, depth, x, y, 1): (x, y)
                             for y in range(64) for x in range(64)}
                self.assertEqual(set(inverse32), set(inverse16))
                for address, xy16 in inverse16.items():
                    self.assertEqual(inverse('32', depth, candidate(kind, depth, *xy16, 1), 1), inverse32[address])
                for address, xyh32 in inverse32.items():
                    x, y, half = xyh32
                    self.assertEqual(inverse(kind, depth, candidate('32', depth, x, y, 1, half=half), 1)[:2], inverse16[address])

    def test_all_rgb5a1_values_round_trip(self):
        for raw in range(65536):
            rgba = ((raw & 31) << 3, ((raw >> 5) & 31) << 3, ((raw >> 10) & 31) << 3, ((raw >> 15) & 1) << 7)
            packed = (rgba[0] >> 3) | ((rgba[1] >> 3) << 5) | ((rgba[2] >> 3) << 10) | ((rgba[3] >> 7) << 15)
            self.assertEqual(packed, raw)
            self.assertEqual((raw | raw << 16) & 0xffff, raw)

    def test_all_rgb5a1_rta_unorm_round_trip(self):
        def f32(x):
            return struct.unpack('f', struct.pack('f', x))[0]

        def unorm8(x):
            return min(255, max(0, math.floor(f32(x * 255) + 0.5)))

        for backend in ('opengl', 'vulkan'):
            shader = (ROOT / 'app/src/main/cpp/pcsx2/bin/resources/shaders' / backend / 'convert.glsl').read_text()
            correction = re.search(r'value.a / \(([0-9.]+)f / ([0-9.]+)f\)', shader)
            decorrection = re.search(r'value.a \* \(([0-9.]+)f / ([0-9.]+)f\)', shader)
            self.assertIsNotNone(correction)
            self.assertIsNotNone(decorrection)
            encode_ratio = f32(float(correction[1]) / float(correction[2]))
            decode_ratio = f32(float(decorrection[1]) / float(decorrection[2]))
            for raw in range(65536):
                guest_alpha = (raw >> 15) * 128
                stored_alpha = unorm8(f32(f32(guest_alpha / 255) / encode_ratio))
                restored_alpha = unorm8(f32(f32(stored_alpha / 255) * decode_ratio))
                self.assertEqual(stored_alpha, 255 if guest_alpha else 0)
                self.assertEqual(restored_alpha, guest_alpha)
                self.assertEqual((raw & 0x7fff) | ((restored_alpha >> 7) << 15), raw)

    def test_known_clear_pattern_and_coverage_caveat(self):
        rng = random.Random(194)
        for word in [0x80102030, 0, 0xffffffff] + [1 << b for b in range(32)] + [rng.getrandbits(32) for _ in range(128)]:
            for kind in ('16', '16S'):
                for x in range(64):
                    for y in range(64):
                        half = reference(kind, False, x, y, 1) & 1
                        self.assertEqual((word >> (16 * half)) & 0xffff, (word >> (16 if x & 8 else 0)) & 0xffff)
        # Same dimensions do not mean same owned bytes when bpp changes.
        old16 = {reference('16', False, x, y, 1) for y in range(64) for x in range(64)}
        new32 = {reference('32', False, x, y, 1, half=h) for y in range(64) for x in range(64) for h in range(2)}
        self.assertEqual(len(new32 - old16), 4096)

if __name__ == '__main__':
    unittest.main()
