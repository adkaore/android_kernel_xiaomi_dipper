#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""List explicit dipper build targets; never glob a possibly stale build tree."""
import argparse
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=['all', 'standalone', 'composed'], default='all')
    parser.add_argument('--helpers', action='store_true', help='Also list overlay base DTBs')
    parser.add_argument('--prefix', default='')
    args = parser.parse_args()
    targets = json.loads((ROOT / 'Documentation/dipper/board-targets.json').read_text())['targets']
    names = [v['dtb'] for v in targets if args.kind in ('all', v['kind'])]
    if args.helpers:
        names += [v['base'] for v in targets if v['kind'] == 'composed']
    for name in dict.fromkeys(names):
        if not re.fullmatch(r'sdm845-xiaomi-dipper-[a-z0-9.\-]+\.dtb', name):
            raise SystemExit(f'Invalid board target: {name!r}')
        print(args.prefix + name)


if __name__ == '__main__':
    main()
