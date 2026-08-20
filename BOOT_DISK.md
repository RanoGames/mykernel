# Boot from disk (no -kernel)

## Correct workflow (host builds bootloader)

```bash
# WSL
cd ~/mykernel
make clean && make
bash boot/build_boot.sh
make disk.img
cp disk.img /mnt/c/Users/Max/Desktop/disk.img
cp build/mykernel.bin /mnt/c/Users/Max/Desktop/mykernel.bin
```

PowerShell — **only** disk (no install needed):

```powershell
& "C:\Program Files\qemu\qemu-system-i386w.exe" -hda C:\Users\Max\Desktop\disk.img -m 128
```

You should briefly see `stage2` then the kernel.

## Do NOT run `install` on a host-made disk.img unless mbr/stage2 are linked into the kernel

`install` used to overwrite MBR boot code with a dummy jump → SeaBIOS loops on "Booting from Hard Disk...".

If you already ran `install`, **recreate disk.img** with `make disk.img` and copy again.

## Debug (kernel + disk)

```powershell
& "C:\Program Files\qemu\qemu-system-i386w.exe" -kernel C:\Users\Max\Desktop\mykernel.bin -hda C:\Users\Max\Desktop\disk.img -m 128
```
