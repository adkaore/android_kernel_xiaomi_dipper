#!/usr/bin/env python3
"""Fail if Kconfig drops any explicit fragment request."""
import argparse
from pathlib import Path
import re
import sys


def read_config(path):
    result = {}
    for line in Path(path).read_text().splitlines():
        match = re.fullmatch(r'(CONFIG_\w+)=(.*)', line)
        disabled = re.fullmatch(r'# (CONFIG_\w+) is not set', line)
        if match:
            result[match[1]] = match[2]
        elif disabled:
            result[disabled[1]] = 'n'
    return result


def mismatches(requested, actual):
    return [(key, value, actual.get(key, 'n')) for key, value in requested.items()
            if value != actual.get(key, 'n')]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fragment')
    parser.add_argument('config')
    args = parser.parse_args()
    wanted = read_config(args.fragment)
    bad = mismatches(wanted, read_config(args.config))
    for key, value, actual in bad:
        print(f'{key}: requested {value}, effective {actual}', file=sys.stderr)
    print(f'Checked {len(wanted)} explicit requests; mismatches: {len(bad)}')
    sys.exit(bool(bad))
