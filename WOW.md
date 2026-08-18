# Wow-effect features

## Ring 3
- GDT: user code 0x18, user data 0x20 (RPL3 → 0x1B / 0x23)
- TSS for kernel stack on `int 0x80`
- `process_exec` enters via **IRET** to ring 3
- Syscall gate already DPL=3

```text
exec hello
```

## Desktop
- On start: Welcome + Clock windows
- Icon **Demo** opens third app window

## Ping
```text
ping 127.0.0.1
```
Loopback works. External hosts need NIC driver (next).

## Boot from disk
```bash
make && bash boot/build_boot.sh && make disk.img
qemu-system-i386 -hda disk.img -m 128
```
Or in OS: `install` then reboot with `-hda` (payload if KERNEL_PAYLOAD=1).
