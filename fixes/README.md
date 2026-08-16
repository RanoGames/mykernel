# Remaining fixes

Already on `main`:
- `timer.c` / `timer.h` — PIT 100 Hz, `timer_hz()`
- `kernel.c` — `vbetest` typo, boot message
- `Makefile` — `-soname libhello.so`
- `dyld.h` — comment fix

Apply the rest:

```bash
# after git pull, if fixes/ is present:
bash fixes/apply.sh
make clean && make
```

Or copy from this directory:
- `fixes/dyld.c` — full dynamic linker (fixes dynhello Invalid Opcode)
- `fixes/snake.c` — speed, pending turns, GAME OVER
- `fixes/shell.c` — Tab completion
- `fixes/settings.c` — system-wide VBE console
