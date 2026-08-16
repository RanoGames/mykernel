#!/bin/bash
set -e
cp fixes/dyld.c src/dyld.c
cp fixes/snake.c src/snake.c
cp fixes/shell.c src/shell.c
cp fixes/settings.c src/settings.c
echo "Applied. Run: make clean && make"
