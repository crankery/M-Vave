#!/usr/bin/env python3
"""Select MT-32 presets by sound class for the FM-1 hybrid LA experiment.

ROM layout and PCM decoding follow Munt's Synth.cpp, Part.cpp, Structures.h,
and LA32FloatWaveGenerator.cpp. The FM-1 renderer approximates LA partials.
"""

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTROL = ROOT / "roms/mt32/mt32_ctrl_1_07.rom"
PCM = ROOT / "roms/mt32/mt32_pcm.rom"
OUTPUT = ROOT / "dinghy/firmware/src/mt32/mt32_roms.h"
CLASSES = ("piano", "organ", "keyboard", "sbrass", "synbass", "synth1", "synth2",
           "strings", "guitar", "bass", "wind1", "wind2", "brass", "mallet",
           "special", "percussion", "effects")
PARTIAL_STRUCT = (0, 0, 2, 2, 1, 3, 3, 0, 3, 0, 2, 1, 3)
PARTIAL_MIX = (0, 1, 0, 1, 1, 0, 1, 3, 3, 2, 2, 2, 2)
BIT_ORDER = (0, 9, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 8)
GROUP_INDEX = 0x70B0 - 128


def decode(pair: bytes) -> int:
    log = 0
    for bit in BIT_ORDER:
        source = pair[0] if bit < 8 else pair[1]
        log = (log << 1) | ((source >> (7 - (bit if bit < 8 else bit - 8))) & 1)
    value = round(127 * 2 ** (((log & 0x7FFF) - 32787) / 2048))
    return min(127, value) * (-1 if log & 0x8000 else 1)


def read_roms():
    control, pcm = CONTROL.read_bytes(), PCM.read_bytes()
    if len(control) != 65536 or len(pcm) != 524288:
        raise SystemExit("Expected 64 KiB MT-32 1.07 control ROM and 512 KiB MT-32 PCM ROM")
    actual = [control[0x70B0 + i * 14 + 3:0x70B0 + i * 14 + 12]
              .decode("ascii").strip(" |\x00").lower().replace("-", "") for i in range(17)]
    actual = [{"keybrd": "keyboard", "percusn": "percussion"}.get(s, s) for s in actual]
    if tuple(actual) != CLASSES:
        raise SystemExit("Control ROM sound groups differ from the expected MT-32 1.07 map")
    return control, pcm


def timbre_partials(control: bytes, preset: int):
    bank, number = divmod(preset, 64)
    map_address = 0x8000 if bank == 0 else 0xC000
    address = int.from_bytes(control[map_address + 2 * number:map_address + 2 * number + 2], "little")
    timbre = control[address + (0 if bank == 0 else 0x4000):][:246]
    if len(timbre) != 246:
        raise SystemExit(f"Preset {preset + 1}: invalid timbre address")
    partials = []
    for partial in range(4):
        block = timbre[14 + partial * 58:14 + (partial + 1) * 58]
        structure = PARTIAL_STRUCT[timbre[10 if partial < 2 else 11]]
        is_pcm = bool(structure & (2 if partial % 2 == 0 else 1))
        kind = 0 if not timbre[12] & (1 << partial) else (1 if is_pcm else 2 + (block[4] & 1))
        wave = block[5] if kind == 1 else 0
        if kind and block[2] > 16:
            raise SystemExit(f"Preset {preset + 1}: invalid pitch key follow {block[2]}")
        if wave >= 128:
            raise SystemExit(f"Preset {preset + 1}: invalid PCM wave {wave}")
        # WG 0..7, TVF 23..40, TVA 41..58. Keep ROM values so the
        # deliberately simple device renderer can use per-partial settings.
        partials.append((kind, wave, block[0], block[1], block[2], block[6],
                         block[23], block[24], block[28], block[41], block[42],
                         tuple(block[32:37]), tuple(block[37:41]),
                         tuple(block[49:54]), tuple(block[54:58])))
    return (timbre[:10].decode("ascii").strip(), timbre[13],
            (PARTIAL_MIX[timbre[10]], PARTIAL_MIX[timbre[11]]), tuple(partials))


def wave_info(control: bytes, number: int):
    pos, flags, lo, hi = control[0x3000 + number * 4:0x3004 + number * 4]
    start, length = pos * 2048, 2048 << ((flags >> 4) & 7)
    if start + length > 262144:
        raise SystemExit(f"Wave {number} exceeds PCM ROM")
    return start, length, bool(flags & 0x80), lo | hi << 8


def pack_waves(control: bytes, pcm: bytes, numbers):
    info = {number: wave_info(control, number) for number in numbers}
    merged = []
    for start, length, _, _ in sorted(info.values()):
        end = start + length
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    bases, total = [], 0
    for start, end in merged:
        bases.append(total)
        total += end - start
    data = [decode(pcm[2 * pos:2 * pos + 2]) for start, end in merged for pos in range(start, end)]
    entries = []
    for number in numbers:
        start, length, loop, pitch = info[number]
        for (begin, end), base in zip(merged, bases):
            if begin <= start and start + length <= end:
                entries.append((base + start - begin, length, number, loop, pitch))
                break
        else:
            raise AssertionError(f"Wave {number} was not packed")
    return data, entries


def write_header(data, entries, programs):
    wave_index = {number: i for i, (_, _, number, _, _) in enumerate(entries)}
    program_index = [255] * 128
    for index, (number, _, _, _, _) in enumerate(programs):
        program_index[number] = index
    active_entries = entries or [(0, 1, 0, False, 0)]
    active_data = data or [0]
    lines = ["/* Generated by tools/mt32_embed.py from local Roland ROMs. Do not edit. */",
             "#ifndef MT32_ROMS_H", "#define MT32_ROMS_H", "#include <stdint.h>",
             f"#define MT32_WAVE_COUNT {len(active_entries)}u", f"#define MT32_PROGRAM_COUNT {len(programs)}u",
             f"static const int8_t mt32_pcm_data[{len(active_data)}] = {{"]
    for i in range(0, len(active_data), 24):
        lines.append("    " + ", ".join(map(str, active_data[i:i + 24])) + ",")
    lines += ["};", "typedef struct { uint32_t offset, length; uint8_t number, loop; } mt32_wave_t;",
              "static const mt32_wave_t mt32_waves[MT32_WAVE_COUNT] = {",
              *[f"    {{{off}u, {length}u, {number}u, {int(loop)}u}},"
                for off, length, number, loop, _ in active_entries], "};",
              "typedef struct { uint8_t kind, wave, coarse, fine, keyfollow, pulse, cutoff, resonance,",
              "                 filter_depth, level, velo, filter_time[5], filter_env[4], time[5], env[4]; } mt32_partial_t;",
              "typedef struct { uint8_t number, no_sustain, mix[2]; char name[11]; mt32_partial_t partial[4]; } mt32_program_t;",
              "static const mt32_program_t mt32_programs[MT32_PROGRAM_COUNT] = {",
              *[f"    {{{number}u, {no_sustain}u, {{{mixes[0]}u, {mixes[1]}u}}, {json.dumps(name)}, {{" +
                ", ".join("{" + ", ".join(f"{v}u" for v in (kind, wave_index[wave] if kind == 1 else 0,
                                                       coarse, fine, keyfollow, pulse, cutoff, resonance, filter_depth, level, velo)) +
                          ", {" + ", ".join(f"{v}u" for v in filter_times) + "}, {" +
                          ", ".join(f"{v}u" for v in filter_levels) + "}, {" +
                          ", ".join(f"{v}u" for v in times) + "}, {" +
                          ", ".join(f"{v}u" for v in levels) + "}}"
                          for kind, wave, coarse, fine, keyfollow, pulse, cutoff, resonance, filter_depth, level, velo, filter_times, filter_levels, times, levels in partials) +
                "}},"
                for number, name, no_sustain, mixes, partials in programs], "};",
              "static const uint8_t mt32_program_index[128] = {"]
    for i in range(0, 128, 16):
        lines.append("    " + ", ".join(f"{v}u" for v in program_index[i:i + 16]) + ",")
    lines += ["};", "static const uint32_t mt32_synth_step[128] = {"]
    synth_steps = [round(440 * 2 ** ((note - 69) / 12) / 44100 * 2 ** 32) for note in range(128)]
    for i in range(0, 128, 8):
        lines.append("    " + ", ".join(f"{v}u" for v in synth_steps[i:i + 8]) + ",")
    lines += ["};", "static const uint32_t mt32_note_step[MT32_WAVE_COUNT][128] = {"]
    for _, _, number, _, pitch in active_entries:
        steps = [round((32000 / 44100) * 2 ** ((pitch / 4096) - 5 + (note - 60) / 12) * 65536)
                 for note in range(128)]
        lines.append(f"    {{ /* ROM wave {number}, base pitch {pitch} */")
        for i in range(0, 128, 8):
            lines.append("        " + ", ".join(f"{v}u" for v in steps[i:i + 8]) + ",")
        lines.append("    },")
    lines += ["};", "#endif", ""]
    OUTPUT.write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--classes", default="piano organ keyboard", help="space or comma separated classes")
    parser.add_argument("--max-bytes", type=int, default=196608, help="selected bank byte budget")
    parser.add_argument("--list-classes", action="store_true", help="list classes and PCM-backed preset counts")
    args = parser.parse_args()
    control, pcm = read_roms()
    groups = control[GROUP_INDEX:GROUP_INDEX + 128]
    if len(groups) != 128 or max(groups) > 16:
        raise SystemExit("Invalid MT-32 sound-group index")
    if args.list_classes:
        for group, name in enumerate(CLASSES):
            members = [i for i, found in enumerate(groups) if found == group]
            pcm_count = sum(any(p[0] == 1 for p in timbre_partials(control, i)[3]) for i in members)
            print(f"{name:11s} {len(members):2d} presets, {pcm_count:2d} with PCM partials")
        return
    names = args.classes.replace(",", " ").lower().split()
    if not names or any(name not in CLASSES for name in names) or len(set(names)) != len(names):
        parser.error(f"select distinct classes from: {', '.join(CLASSES)}")
    if args.max_bytes <= 0:
        parser.error("--max-bytes must be positive")
    programs = []
    for name in names:
        group = CLASSES.index(name)
        programs.extend((i, *timbre_partials(control, i)) for i, found in enumerate(groups) if found == group)
    numbers = list(dict.fromkeys(p[1] for _, _, _, _, partials in programs for p in partials if p[0] == 1))
    data, entries = pack_waves(control, pcm, numbers)
    estimated = max(1, len(data)) + max(1, len(entries)) * (12 + 128 * 4) + len(programs) * 131 + 640
    if estimated > args.max_bytes:
        raise SystemExit(f"Too many sounds: {', '.join(names)} needs about {estimated:,} bytes "
                         f"({len(data):,} PCM bytes), over the {args.max_bytes:,}-byte MT32_ROM_BUDGET. "
                         "Remove classes or raise the budget in the Makefile.")
    write_header(data, entries, programs)
    print(f"{OUTPUT}: {len(programs)} presets, {len(entries)} PCM waves, "
          f"{len(data):,} PCM bytes; bank about {estimated:,}/{args.max_bytes:,} bytes")


if __name__ == "__main__":
    main()
