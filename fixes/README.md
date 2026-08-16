# Fixes

## Already on main (git pull)
- `src/timer.c`, `src/timer.h` — PIT 100 Hz, `timer_hz()`
- `src/kernel.c` — typo `vbtest`→`vbetest`, boot message
- `Makefile` — `-soname libhello.so`
- `src/dyld.h` — comment fix

## Apply remaining (dynhello, snake, Tab, system VBE console)

```bash
git pull
bash fixes/decode_and_apply.sh   # needs fixes/*.z64 files
make clean && make
```

If `.z64` files are missing, use tarball from the chat or wait for next push of `fixes/*.z64`.
