#!/usr/bin/env python3
"""Generate the 32x32 hexagonal obstacle floorplan JSON used by the egress model."""

import argparse
import json
from pathlib import Path

TOGGLE_INDICES = [
    256, 276, 279, 288,
    512, 521, 524, 532, 535, 544,
    564, 565,
    595, 596,
    628, 629,
    659, 660,
    692, 693,
    723, 724,
    756, 757,
    768, 785, 788, 797,
    820, 821,
    851, 852,
    884, 885,
    915, 916,
    948, 949,
    979, 980,
    1012, 1013, 2000,
]

REGIONS_COUNT = 1024


def generate_values():
    values = []
    obstacle = False
    toggle_idx = 0
    for i in range(REGIONS_COUNT):
        if TOGGLE_INDICES[toggle_idx] == i:
            obstacle = not obstacle
            toggle_idx += 1
        values.append(1.0 if obstacle else 0.0)
    return values


def main():
    parser = argparse.ArgumentParser(description="Generate egress obstacles topology JSON.")
    parser.add_argument(
        "-o", "--output",
        type=Path,
        default=Path("topology.json"),
        help="Output JSON path (default: topology.json)",
    )
    args = parser.parse_args()

    payload = {
        "values": generate_values(),
        "regions_count": REGIONS_COUNT,
        "type": "obstacles",
        "geometry": "hexagons",
    }
    args.output.write_text(json.dumps(payload, separators=(",", ":")))


if __name__ == "__main__":
    main()
