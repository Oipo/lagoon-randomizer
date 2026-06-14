#!/usr/bin/env python3

import argparse
import collections
import dataclasses
import pathlib
from typing import Callable


def parse_int(value: str) -> int:
    return int(value, 0)


def format_hex(value: int, width: int = 6) -> str:
    return f"0x{value:0{width}X}"


def load_rom(path: pathlib.Path) -> tuple[bytes, int]:
    data = path.read_bytes()
    header_size = 0x200 if len(data) % 1024 == 512 else 0
    return data[header_size:], header_size


def detect_map_mode(rom: bytes) -> tuple[str, int]:
    map_mode = rom[0x7FD5]
    mode_name = {
        0x20: "LoROM",
        0x21: "HiROM",
        0x30: "LoROM+FastROM",
        0x31: "HiROM+FastROM",
        0x32: "ExLoROM",
        0x35: "ExHiROM",
    }.get(map_mode, "Unknown")
    return mode_name, map_mode


def lorom_to_pc(value: int, size: int) -> int | None:
    bank = (value >> 16) & 0xFF
    addr = value & 0xFFFF
    if addr < 0x8000:
        return None
    return (((bank & 0x7F) * 0x8000) + (addr & 0x7FFF)) % size


def hirom_to_pc(value: int, size: int) -> int | None:
    bank = (value >> 16) & 0xFF
    addr = value & 0xFFFF
    return (((bank & 0x3F) * 0x10000) + addr) % size


def raw_to_pc(value: int, size: int) -> int | None:
    if value >= size:
        return None
    return value


def classify_target(rom: bytes, pc: int) -> str:
    sample = rom[pc:pc + 32]
    if len(sample) < 16:
        return "short"
    if all(b == 0 for b in sample):
        return "zero"

    code_opcodes = {0x20, 0x22, 0x60, 0x6B, 0x8D, 0x9C, 0xA0, 0xA2, 0xA5, 0xA9, 0xAD, 0xAF, 0xBD, 0xC2, 0xE2}
    code_hits = sum(1 for b in sample[:16] if b in code_opcodes)
    if code_hits >= 5:
        return "code-ish"

    if all(sample[i + 1] == 0 for i in range(0, min(len(sample) - 1, 30), 2)):
        if len({sample[i] for i in range(0, min(len(sample), 32), 2)}) >= 6:
            return "tilemap-ish"

    graphic_bytes = {0x00, 0xFF, 0xF0, 0x0F, 0xAA, 0x55, 0x80, 0x7F}
    if sum(1 for b in sample if b in graphic_bytes) >= 16:
        return "gfx-ish"

    mostly_small = sum(1 for b in sample if b <= 0x1F) >= 20
    if mostly_small:
        return "table-ish"

    return "data-ish"


@dataclasses.dataclass
class MappingSummary:
    name: str
    valid_count: int
    unique_targets: int
    top_blocks: list[tuple[int, int]]
    target_kinds: list[tuple[str, int]]
    top_targets: list[tuple[int, int]]
    score: int
    notes: list[str]


@dataclasses.dataclass
class WindowSummary:
    offset: int
    values: list[int]
    byte_uniques: list[int]
    likely_attribute: bool
    attribute_notes: list[str]
    mappings: list[MappingSummary]


def summarize_mapping(
    name: str,
    values: list[int],
    translate: Callable[[int, int], int | None],
    rom: bytes,
    top_n: int,
    map_mode_name: str,
) -> MappingSummary:
    targets: list[int] = []
    for value in values:
        pc = translate(value, len(rom))
        if pc is not None:
            targets.append(pc)

    block_counts = collections.Counter(target // 0x200 for target in targets)
    target_counts = collections.Counter(targets)
    kind_counts = collections.Counter(classify_target(rom, target) for target in targets)

    score = 0
    notes: list[str] = []
    valid_ratio = len(targets) / max(len(values), 1)

    if valid_ratio >= 0.75:
        score += 3
        notes.append("most rows decode cleanly")
    elif valid_ratio >= 0.40:
        score += 1
        notes.append("many rows decode cleanly")
    else:
        notes.append("few rows decode cleanly")

    if len(block_counts) <= max(4, len(values) // 6):
        score += 2
        notes.append("targets cluster into a small number of ROM neighborhoods")
    elif len(block_counts) <= max(12, len(values) // 2):
        score += 1

    meaningful_kinds = sum(count for kind, count in kind_counts.items() if kind not in {"zero", "short"})
    if meaningful_kinds >= max(4, len(values) // 8):
        score += 2
        notes.append("decoded targets look like structured ROM content")

    if kind_counts.get("tilemap-ish", 0) or kind_counts.get("gfx-ish", 0):
        score += 2
        notes.append("decoded targets include tilemap/gfx-like data")

    if name == "LoROM":
        lorom_shaped = sum(1 for value in values if (value & 0xFFFF) >= 0x8000)
        if lorom_shaped >= max(4, (len(values) * 3) // 4):
            score += 2
            notes.append("raw values mostly live in the LoROM ROM window $8000-$FFFF")
        if "LoROM" in map_mode_name:
            score += 3
            notes.append(f"ROM header map mode favors {map_mode_name}")
    elif name == "HiROM" and "HiROM" in map_mode_name:
        score += 3
        notes.append(f"ROM header map mode favors {map_mode_name}")
    elif name == "RawPC":
        in_range = sum(1 for value in values if value < len(rom))
        if in_range >= max(4, (len(values) * 3) // 4):
            score += 2
            notes.append("raw 24-bit values are mostly in file range")

    top_blocks = [(block * 0x200, count) for block, count in block_counts.most_common(top_n)]
    top_targets = target_counts.most_common(top_n)
    target_kinds = kind_counts.most_common(top_n)
    return MappingSummary(
        name=name,
        valid_count=len(targets),
        unique_targets=len(target_counts),
        top_blocks=top_blocks,
        target_kinds=target_kinds,
        top_targets=top_targets,
        score=score,
        notes=notes,
    )


def analyze_window(rows: list[bytes], offset: int, rom: bytes, top_n: int, map_mode_name: str) -> WindowSummary:
    values = [
        row[offset] | (row[offset + 1] << 8) | (row[offset + 2] << 16)
        for row in rows
    ]

    byte_columns = [[row[offset + i] for row in rows] for i in range(3)]
    byte_uniques = [len(set(column)) for column in byte_columns]

    attribute_notes: list[str] = []
    likely_attribute = False

    if max(values) <= 0x1FFFFF:
        small_bytes = sum(1 for column in byte_columns if max(column) <= 0x1F)
        if small_bytes >= 2:
            likely_attribute = True
            attribute_notes.append("two or more bytes stay in a small enum-like range")

    if len(set(values)) <= max(4, len(rows) // 8):
        likely_attribute = True
        attribute_notes.append("very low value diversity across rows")

    if all(set(column).issubset({0x00, 0xFF}) for column in byte_columns):
        likely_attribute = True
        attribute_notes.append("bytes behave like binary flags")

    mappings = [
        summarize_mapping("LoROM", values, lorom_to_pc, rom, top_n, map_mode_name),
        summarize_mapping("HiROM", values, hirom_to_pc, rom, top_n, map_mode_name),
        summarize_mapping("RawPC", values, raw_to_pc, rom, top_n, map_mode_name),
    ]
    return WindowSummary(
        offset=offset,
        values=values,
        byte_uniques=byte_uniques,
        likely_attribute=likely_attribute,
        attribute_notes=attribute_notes,
        mappings=mappings,
    )


def format_summary(summary: WindowSummary, rows: int) -> list[str]:
    lines = [
        f"Window +{summary.offset}: 24-bit values from bytes [{summary.offset}..{summary.offset + 2}]",
        f"  byte unique counts: {summary.byte_uniques[0]}, {summary.byte_uniques[1]}, {summary.byte_uniques[2]}",
    ]

    if summary.likely_attribute:
        lines.append("  attribute hints: " + "; ".join(summary.attribute_notes))
    else:
        lines.append("  attribute hints: none")

    ranked_mappings = sorted(summary.mappings, key=lambda item: item.score, reverse=True)
    for mapping in ranked_mappings:
        kind_text = ", ".join(f"{kind}:{count}" for kind, count in mapping.target_kinds) or "none"
        block_text = ", ".join(f"{format_hex(block)}:{count}" for block, count in mapping.top_blocks) or "none"
        target_text = ", ".join(f"{format_hex(target)}:{count}" for target, count in mapping.top_targets) or "none"
        note_text = "; ".join(mapping.notes)
        lines.append(
            f"  {mapping.name:5s} score={mapping.score} valid={mapping.valid_count}/{rows} "
            f"unique_targets={mapping.unique_targets}"
        )
        lines.append(f"    target kinds: {kind_text}")
        lines.append(f"    top 0x200 blocks: {block_text}")
        lines.append(f"    top exact targets: {target_text}")
        lines.append(f"    notes: {note_text}")
    return lines


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Scan row-structured ROM data and classify each 3-byte window."
    )
    parser.add_argument("rom", type=pathlib.Path, help="headered or deheadered SNES ROM")
    parser.add_argument("start", type=parse_int, help="start PC offset, deheadered by default")
    parser.add_argument("end", type=parse_int, help="inclusive end PC offset, deheadered by default")
    parser.add_argument("--stride", type=parse_int, default=8, help="row size in bytes")
    parser.add_argument("--base-offset", type=parse_int, default=0, help="first byte offset inside each row")
    parser.add_argument("--windows", type=parse_int, nargs="*", help="specific 3-byte window offsets to inspect")
    parser.add_argument("--headered-input", action="store_true", help="treat start/end as headered file offsets")
    parser.add_argument("--top", type=parse_int, default=6, help="number of top targets/blocks to print")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rom, header_size = load_rom(args.rom)
    map_mode_name, map_mode = detect_map_mode(rom)

    start = args.start - header_size if args.headered_input else args.start
    end = args.end - header_size if args.headered_input else args.end
    if start < 0 or end < start:
        raise SystemExit("invalid range after header adjustment")

    region = rom[start:end + 1]
    if len(region) % args.stride != 0:
        raise SystemExit("range length must be an exact multiple of stride")

    rows = [region[i:i + args.stride] for i in range(0, len(region), args.stride)]
    max_window = args.stride - 3
    windows = args.windows if args.windows is not None else list(range(args.base_offset, max_window + 1))

    print(f"ROM: {args.rom}")
    print(f"Copier header detected: {'yes' if header_size else 'no'}")
    print(f"Header map mode: {map_mode_name} ({format_hex(map_mode, 2)})")
    print(f"Analyzing deheadered PC range {format_hex(start)}-{format_hex(end)}")
    print(f"Rows: {len(rows)}, stride: {args.stride}")
    print()

    for offset in windows:
        if offset < 0 or offset > max_window:
            raise SystemExit(f"window offset {offset} is outside stride {args.stride}")
        summary = analyze_window(rows, offset, rom, args.top, map_mode_name)
        for line in format_summary(summary, len(rows)):
            print(line)
        print()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
