#!/bin/bash
set -e
cd "$(dirname "$0")/.."
python3 -c '
import pathlib, zlib, base64
for name in ["dyld.c", "snake.c", "shell.c", "settings.c"]:
    enc = pathlib.Path("fixes/" + name + ".z64").read_text().strip()
    data = zlib.decompress(base64.b64decode(enc))
    pathlib.Path("src/" + name).write_bytes(data)
    print("wrote src/" + name, len(data))
'
echo "Done. Run: make clean && make"
