#!/usr/bin/env python3
"""Trim MP3 effects and pack 16 kHz mono IMA ADPCM for the dub siren."""

import json
import math
from pathlib import Path
import struct
import subprocess


ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "dinghy/firmware/src/dubsiren"
MANIFEST = APP / "samples.json"
OUTPUT = APP / "dubsiren_samples.h"
STEP = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
    41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157,
    173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
    1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
    13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
)
INDEX = (-1, -1, -1, -1, 2, 4, 6, 8)


def decode_mp3(path: Path, sample_rate: int) -> list[int]:
    result = subprocess.run(
        ["sox", str(path), "-t", "raw", "-e", "signed-integer", "-b", "16",
         "-L", "-r", str(sample_rate), "-c", "1", "-"],
        check=True, capture_output=True,
    )
    pcm = result.stdout
    return list(struct.unpack(f"<{len(pcm) // 2}h", pcm))


def trim(samples: list[int], sample_rate: int) -> tuple[list[int], float, float]:
    # 10 ms RMS windows ignore MP3 noise and isolated low-level clicks.
    window = sample_rate // 100
    threshold = 10 ** (-54 / 20) * 32768
    active = [i for i in range(0, len(samples), window)
              if math.sqrt(sum(v * v for v in samples[i:i + window]) /
                           len(samples[i:i + window])) >= threshold]
    if not active:
        raise ValueError("sample has no audio above -54 dBFS")
    start = max(0, active[0] - 2 * window)  # 20 ms before attack
    end = min(len(samples), active[-1] + window + 12 * window)  # 120 ms tail
    return samples[start:end], start / sample_rate, end / sample_rate


def loudest_rms(samples: list[int], sample_rate: int) -> float:
    """Use the loudest 100 ms, including short sounds and long effects alike."""
    window = sample_rate // 10
    stride = window // 2
    return max(math.sqrt(sum(v * v for v in samples[i:i + window]) /
                         len(samples[i:i + window]))
               for i in range(0, len(samples), stride)
               if len(samples[i:i + window]) >= window // 2)


def match_level(samples: list[int], sample_rate: int, target_dbfs: float) -> tuple[list[int], float, float]:
    """Match the active level, gently limiting peaks before ADPCM encoding."""
    target = 32768 * 10 ** (target_dbfs / 20)
    ceiling = 30000

    def limited(gain: float) -> list[int]:
        return [round(ceiling * math.tanh(v * gain / ceiling)) for v in samples]

    # Keep brief, high-crest-factor clicks punchy instead of flattening them
    # just to meet the sustained-sound RMS target.
    low, high = 0.0, 3.0
    for _ in range(15):
        middle = (low + high) / 2
        if loudest_rms(limited(middle), sample_rate) < target:
            low = middle
        else:
            high = middle
    gain = (low + high) / 2
    normalized = limited(gain)
    return normalized, gain, loudest_rms(normalized, sample_rate)


def encode_adpcm(samples: list[int]) -> bytes:
    predictor = index = 0
    nibbles = []
    for value in samples:
        step = STEP[index]
        diff = value - predictor
        nibble = 0
        if diff < 0:
            nibble = 8
            diff = -diff
        delta = step >> 3
        if diff >= step:
            nibble |= 4
            diff -= step
            delta += step
        if diff >= step >> 1:
            nibble |= 2
            diff -= step >> 1
            delta += step >> 1
        if diff >= step >> 2:
            nibble |= 1
            delta += step >> 2
        predictor += -delta if nibble & 8 else delta
        predictor = max(-32768, min(32767, predictor))
        index = max(0, min(88, index + INDEX[nibble & 7]))
        nibbles.append(nibble)
    return bytes(nibbles[i] | ((nibbles[i + 1] if i + 1 < len(nibbles) else 0) << 4)
                 for i in range(0, len(nibbles), 2))


def main() -> None:
    manifest = json.loads(MANIFEST.read_text())
    rate = manifest["sample_rate"]
    if rate != 16000:
        raise ValueError("the firmware decoder expects 16 kHz audio")
    target_dbfs = manifest["target_window_rms_dbfs"]
    if not -30 <= target_dbfs <= -3:
        raise ValueError("target_window_rms_dbfs must be between -30 and -3")
    info = []
    data = bytearray()
    keys = [255] * 15
    for slot, entry in enumerate(manifest["samples"]):
        key = entry["key"]
        if not 12 <= key <= 26 or keys[key - 12] != 255:
            raise ValueError(f"invalid or duplicate sample key: {key}")
        name = entry["name"]
        if not name.isascii() or '"' in name or len(name) > 22:
            raise ValueError(f"unsupported sample name: {name!r}")
        source = ROOT / entry["source"]
        pcm, start, end = trim(decode_mp3(source, rate), rate)
        pcm, gain, level = match_level(pcm, rate, target_dbfs)
        packed = encode_adpcm(pcm)
        info.append((len(data), len(pcm), name))
        data.extend(packed)
        keys[key - 12] = slot
        print(f"{name}: {start:.2f}-{end:.2f}s, {len(pcm)/rate:.2f}s, "
              f"{len(packed)} bytes, gain {gain:.2f}x, "
              f"100 ms RMS {20 * math.log10(level / 32768):.1f} dBFS")

    lines = [
        "/* Generated by tools/dubsiren_embed.py from the local reference MP3s. */",
        "#ifndef DUBSIREN_SAMPLES_H",
        "#define DUBSIREN_SAMPLES_H",
        f"#define DUBSIREN_SAMPLE_COUNT {len(info)}u",
        "typedef struct { uint32_t offset, frames; const char *name; } dubsiren_sample_info_t;",
        "static const dubsiren_sample_info_t dubsiren_sample_info[] = {",
        *[f'    {{{offset}u, {frames}u, "{name}"}},' for offset, frames, name in info],
        "};",
        "static const uint8_t dubsiren_key_sample[15] = {",
        "    " + ", ".join(f"{key}u" for key in keys) + ",",
        "};",
        "static const uint8_t dubsiren_sample_data[] = {",
    ]
    for i in range(0, len(data), 20):
        lines.append("    " + ", ".join(f"0x{v:02x}" for v in data[i:i + 20]) + ",")
    lines.extend(["};", "#endif"])
    OUTPUT.write_text("\n".join(lines) + "\n")
    print(f"{OUTPUT}: {len(data)} ADPCM bytes")


if __name__ == "__main__":
    main()
