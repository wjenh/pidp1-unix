#!/usr/bin/env python3
"""Copy a checked UNIX V0 filesystem into a fresh IBM 1301 module-zero image."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile

BLOCK_WORDS = 64
FIELD_WORDS = 4096
DRUM_FIELDS = 32
FS_BLOCKS = 1984
INTERLEAVE = 27
RECORDS_PER_TRACK = 5


def copy_volume(drum, destination, v0fs, dsk1301):
    subprocess.run([v0fs, 'check', drum], check=True)
    words = struct.unpack(f'<{DRUM_FIELDS * FIELD_WORDS}I', drum.read_bytes())
    if destination.exists() or destination.is_symlink():
        raise FileExistsError(destination)

    with tempfile.TemporaryDirectory(prefix='.ibmvolume-', dir=destination.parent) as tmp:
        staged = Path(tmp) / 'module0'
        data = Path(tmp) / 'record'
        subprocess.run([dsk1301, 'make', staged, '0'], check=True)
        for block in range(FS_BLOCKS):
            field, slot = divmod(block, FIELD_WORDS // BLOCK_WORDS)
            start = field * FIELD_WORDS + BLOCK_WORDS * ((slot * INTERLEAVE) % 64)
            contents = words[start:start + BLOCK_WORDS]
            if not any(contents):
                continue
            data.write_bytes(bytes((word >> shift) & 63
                                   for word in contents for shift in (12, 6, 0)))
            track, offset = divmod(block, RECORDS_PER_TRACK)
            subprocess.run([dsk1301, 'put', staged, str(track), str(offset + 1), data],
                           check=True)
        os.link(staged, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('drum', type=Path)
    parser.add_argument('image', type=Path, help='new IBM image, assigned to module zero')
    parser.add_argument('--v0fs', default='v0fs')
    parser.add_argument('--dsk1301', default='dsk1301')
    args = parser.parse_args()
    try:
        copy_volume(args.drum, args.image, args.v0fs, args.dsk1301)
    except (OSError, struct.error, subprocess.CalledProcessError) as error:
        parser.exit(1, f'ibmvolume: {error}\n')


if __name__ == '__main__':
    main()
