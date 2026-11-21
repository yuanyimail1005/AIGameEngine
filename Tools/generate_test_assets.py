#!/usr/bin/env python3
"""Generates the binary assets of the feature test project (TestProject/Assets) deterministically.

Assets: a textured glTF model (embedded buffer + external PNG texture), an equirectangular HDRI (.hdr),
and WAV sound clips. Only the Python standard library is used.

Usage: python3 Tools/generate_test_assets.py [asset_directory]
"""

import base64
import json
import math
import os
import struct
import sys
import wave
import zlib


def write_png(path, width, height, pixel_fn):
    """Writes an RGBA8 PNG. pixel_fn(x, y) -> (r, g, b, a) in 0..255."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none
        for x in range(width):
            raw.extend(bytes(pixel_fn(x, y)))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


def write_hdr(path, width, height, pixel_fn):
    """Writes an uncompressed Radiance RGBE .hdr. pixel_fn(u, v) -> (r, g, b) linear floats; v=0 is the top (sky)."""

    def to_rgbe(r, g, b):
        m = max(r, g, b)
        if m < 1e-32:
            return bytes((0, 0, 0, 0))
        mantissa, exponent = math.frexp(m)
        scale = mantissa * 256.0 / m
        return bytes((int(r * scale), int(g * scale), int(b * scale), exponent + 128))

    data = bytearray(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n")
    data += f"-Y {height} +X {width}\n".encode()
    for y in range(height):
        for x in range(width):
            data += to_rgbe(*pixel_fn((x + 0.5) / width, (y + 0.5) / height))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def write_wav(path, seconds, frequency, volume=0.4, sample_rate=22050, decay=False):
    frames = int(seconds * sample_rate)
    samples = bytearray()
    for i in range(frames):
        t = i / sample_rate
        envelope = math.exp(-t * 6.0) if decay else 1.0
        value = int(32767 * volume * envelope * math.sin(2.0 * math.pi * frequency * t))
        samples += struct.pack("<h", value)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(bytes(samples))


def write_gltf_crate(path, texture_uri):
    """A unit cube with per-face UVs, normals and an external base color texture (a crate)."""
    faces = [
        ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
        ((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
        ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ]
    positions, normals, uvs, indices = [], [], [], []
    for normal, right, up in faces:
        base = len(positions)
        for cx, cy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            positions.append(tuple(0.5 * (normal[i] + right[i] * cx + up[i] * cy) for i in range(3)))
            normals.append(normal)
            uvs.append(((cx + 1) * 0.5, 1.0 - (cy + 1) * 0.5))
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]

    blob = bytearray()
    views = []

    def add_view(data):
        while len(blob) % 4:
            blob.append(0)
        offset = len(blob)
        blob.extend(data)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data)})
        return len(views) - 1

    pos_view = add_view(b"".join(struct.pack("<3f", *p) for p in positions))
    nrm_view = add_view(b"".join(struct.pack("<3f", *n) for n in normals))
    uv_view = add_view(b"".join(struct.pack("<2f", *t) for t in uvs))
    idx_view = add_view(b"".join(struct.pack("<H", i) for i in indices))

    gltf = {
        "asset": {"version": "2.0", "generator": "Ember generate_test_assets.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Crate", "mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "materials": [{"name": "CrateMaterial", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                                                                         "metallicFactor": 0.0, "roughnessFactor": 0.7}}],
        "textures": [{"source": 0}],
        "images": [{"uri": texture_uri}],
        "buffers": [{"byteLength": len(blob), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(blob)).decode()}],
        "bufferViews": views,
        "accessors": [
            {"bufferView": pos_view, "componentType": 5126, "count": len(positions), "type": "VEC3", "min": [-0.5] * 3, "max": [0.5] * 3},
            {"bufferView": nrm_view, "componentType": 5126, "count": len(normals), "type": "VEC3"},
            {"bufferView": uv_view, "componentType": 5126, "count": len(uvs), "type": "VEC2"},
            {"bufferView": idx_view, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
        ],
    }
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(gltf, f, indent=1)


def main():
    assets = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "TestProject", "Assets")
    assets = os.path.abspath(assets)

    # Crate texture: wooden planks with a dark frame.
    def crate(x, y):
        border = x < 4 or y < 4 or x > 59 or y > 59 or abs(x - y) < 3
        plank = 150 + int(20 * math.sin(y * 0.8)) if not border else 70
        return (plank, int(plank * 0.68), int(plank * 0.38), 255)

    write_png(os.path.join(assets, "Models", "textures", "crate.png"), 64, 64, crate)
    write_gltf_crate(os.path.join(assets, "Models", "Crate.gltf"), "textures/crate.png")

    # Checker texture for materials.
    write_png(os.path.join(assets, "Textures", "checker.png"), 32, 32,
              lambda x, y: (230, 230, 230, 255) if ((x // 8) + (y // 8)) % 2 == 0 else (60, 60, 70, 255))

    # Sky HDRI: blue gradient sky, bright sun, warm ground.
    sun_u, sun_v = 0.62, 0.28

    def sky(u, v):
        if v < 0.5:
            h = 1.0 - v * 2.0  # 1 at zenith, 0 at horizon
            color = [0.75 + (0.20 - 0.75) * h, 0.85 + (0.42 - 0.85) * h, 1.00 + (0.95 - 1.00) * h]
        else:
            g = (v - 0.5) * 2.0
            color = [0.35 - 0.15 * g, 0.30 - 0.12 * g, 0.25 - 0.10 * g]
        du = min(abs(u - sun_u), 1.0 - abs(u - sun_u)) * 2.0
        dist = math.hypot(du, (v - sun_v))
        sun = 60.0 * math.exp(-(dist / 0.012) ** 2) + 0.8 * math.exp(-(dist / 0.08) ** 2)
        return (color[0] + sun, color[1] + sun * 0.95, color[2] + sun * 0.85)

    write_hdr(os.path.join(assets, "HDRI", "sky.hdr"), 256, 128, sky)

    # Audio clips
    write_wav(os.path.join(assets, "Audio", "hum.wav"), 1.0, 110.0, volume=0.2)
    write_wav(os.path.join(assets, "Audio", "bounce.wav"), 0.25, 660.0, decay=True)
    print("Generated test assets in", assets)


if __name__ == "__main__":
    main()
