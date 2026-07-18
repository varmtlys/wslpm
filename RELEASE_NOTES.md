First stable release of **wslpm** — a native Win32 utility for mounting Linux volumes (LVM, LUKS, ext4, xfs, btrfs, NTFS, VFAT) in Windows 11 via WSL2, without dual-booting or a full VM.

## Highlights

- **Native C++ Win32** — single small executable, no runtime dependencies
- **Physical disk discovery** — model, size, bus type and partition layout for every connected disk
- **LUKS encryption** — password or keyfile unlock, including LUKS-on-LVM
- **LVM support** — activates volume groups and mounts all logical volumes
- **Filesystem auto-detection** — or manual choice: ext4, xfs, btrfs, NTFS (`ntfs3`), VFAT
- **Safe eject** — unmounts every volume on a disk and takes it offline
- **VHDX compaction** — shrinks a distribution's `ext4.vhdx`, with optional free-space zeroing and live progress
- **Mount restore** — volumes left mounted by a previous run are picked up on startup
- **Explorer integration** — optional `.lnk` shortcuts to `\\wsl.localhost\<distro>\...`
- **Command log** — every WSL/PowerShell command and its output, one click to copy

## Requirements

- Windows 11 (or Windows 10 2004+) with WSL2 and at least one Linux distribution
- `cryptsetup` and `lvm2` inside the distribution for LUKS/LVM mounts
- Administrator rights (requested automatically on launch)

## Install

Download `wslpm.exe` below and run it.
