# wslpm — WSL Partition Manager (C++ Native)

> Originally conceived to solve a specific problem: mounting encrypted Linux partitions (LUKS, LVM) directly in Windows via WSL2 — without dual-booting or running a full VM.

A native Win32 utility for mounting Linux volumes (LVM, LUKS, ext4, xfs, btrfs, NTFS, VFAT) via WSL2 on Windows 11.

## Features

- **Native C++ Win32** — fast, minimal resource usage, no runtime dependencies
- **Auto-UAC** — requests administrator privileges on launch via embedded manifest
- **Physical disk discovery** — lists all connected disks with model, size, bus type, partition layout
- **Volume types** — auto-detect or manual: LUKS, LVM, ext4, xfs, btrfs, NTFS (`ntfs3`), VFAT
- **LUKS encryption** — password or keyfile unlock, including LUKS-on-LVM
- **LVM support** — scans physical volumes, activates volume groups, mounts all logical volumes
- **Mount point creation** — auto-creates directories in WSL with `sudo mkdir -p`
- **Explorer shortcuts** — creates `.lnk` files to `\\wsl.localhost\<distro>\...` for quick access
- **Safe eject** — unmounts all volumes on a disk and takes it offline (USB/iSCSI)
- **VHDX compaction** — shrinks a distribution's `ext4.vhdx` by reclaiming unused space, with an image picker and a live progress bar
- **Mount restore** — rescans WSL at startup and re-lists volumes left mounted by a previous run
- **Command log** — built-in debug log of every WSL/PowerShell command and its output, with a Copy button

## Build Requirements

- **Visual Studio 2022** (or 2019) with **"Desktop development with C++"** workload
- Windows 11 with WSL2 installed and at least one Linux distribution

## Build

```cmd
build.bat
# OR
cmake -B build && cmake --build build
```

Output: `build\wslpm.exe`

## How to Use

1. **Run as Administrator** — the app requests elevation automatically. Admin rights are required to attach physical disks to WSL2.

2. **Select a disk** — the sidebar lists all physical disks. System/boot disks are marked with a warning. Click a disk to select it.

3. **Choose a partition** — select "Entire disk (bare)" for raw access, or pick a specific partition from the dropdown.

4. **Select volume type** — choose from the radio buttons. "Auto" runs `blkid` inside WSL to detect the filesystem automatically.

5. **LUKS password** — if mounting a LUKS volume, enter the password in the dialog (or browse for a keyfile). The password is sent to `cryptsetup luksOpen` via stdin and is never stored on disk.

6. **Set mount point** — default is `/mnt/disk<N>`. Change it to any path inside the WSL filesystem. The directory is created automatically if needed.

7. **WSL Distribution** — select which WSL distribution to use. Commands run as `wsl -d <distro> -- bash -c ...`.

8. **Options** — enable "Create Explorer Shortcut" to add a `.lnk` file (opens in `\\wsl.localhost\<distro>\...`). "Read Only" (mounts with `-o ro`) is on by default; uncheck it for write access.

9. **Click "Mount Volume"** — the app attaches the disk to WSL2 via `wsl --mount`, detects the device path in `/dev/`, and runs the appropriate mount command.

10. **Manage mounted volumes** — the list shows all active mounts. Use Unmount to detach a single volume, Eject to unmount and offline the disk, or Open (or double-click) to browse in Explorer. These buttons are enabled only when a volume is selected.

11. **Compact VHDX** — click the toolbar button, pick a distribution image from the list, and optionally zero its free space first for a better result. All WSL distributions are shut down (`wsl --shutdown`), then the image is compacted with `diskpart`. Progress is shown on the toolbar progress bar. Unmount all volumes before compacting.

## How It Works (Internals)

### Architecture

```
main.cpp          — WinMain entry point, Common Controls init
app.cpp / app.h   — Window creation, UI controls, event handling
wsl_bridge.cpp/h  — Process spawning (CreateProcess + pipes), WSL/PowerShell communication
operations.cpp/h  — High-level disk discovery, mount/unmount logic
theme.h           — Color palette and layout constants
resource.h        — Control IDs and custom window messages
```

### Disk Discovery

`Operations::getPhysicalDisks()` runs a PowerShell script via `powershell.exe -EncodedCommand`:

```powershell
Get-Disk | ForEach-Object { ... Get-Partition ... }
```

Output is parsed tab-delimited: disk number, model, size, bus type, partition style, partition list.

### WSL Communication

`WSLBridge::RunProcess()` spawns child processes with redirected stdin/stdout/stderr:

- **PowerShell commands** — base64-encoded script, output decoded as UTF-8 (progress records silenced to keep stderr clean)
- **`wsl.exe` commands** — output decoded as UTF-16LE (native) or UTF-8 (bash)
- **`diskpart`** — output decoded with the console OEM codepage; progress lines are streamed to the log and drive the progress bar
- **Persistent session** — `EnsureSession()` opens a long-lived `wsl -d <distro> -- bash` process for sudo caching; commands are sent via stdin, completion detected by a unique marker. The session is dropped on timeout or a rejected sudo password so stale output can't desync the next command.

All system binaries are launched by absolute `System32` path (`SysExe()`) rather than by bare name, to avoid PATH/CWD hijacking under elevation. User-supplied paths and device names are single-quoted for the shell; the distribution name is quoted for `wsl.exe`.

### Mount Flow

1. **Attach**: `wsl.exe --mount \\.\PHYSICALDRIVE<N> [--bare] [--partition <N>] [--type <fs>]`
2. **Discover**: `lsblk -b -n -o NAME,SIZE,TYPE -l` — matches device by disk/partition size
3. **Detect** (if auto): `blkid -o value -s TYPE <device>` → maps to type
4. **Mount**:
   - Plain: `mount [-t <fs>] [-o ro] <dev> <path>`
   - LUKS: `cryptsetup luksOpen <dev> <name>` → `mount /dev/mapper/<name> <path>`
   - LVM: `pvscan && vgscan && lvscan` → `vgchange -ay <VG>` → `mount` each LV
5. **Explorer shortcut**: creates `.lnk` via `IShellLink` COM interface pointing to `\\wsl.localhost\<distro>\<path>`

Plain NTFS falls back from the `ntfs3` kernel driver to the legacy read-only `ntfs` driver when the kernel lacks `ntfs3`.

### Mount Restore

At startup `Operations::rescanMounts()` parses `mount` inside the default distribution and re-lists any block-device mounts under `/mnt/` that aren't already tracked. LUKS mapper names (`luks_*`) and LVM device paths (`/dev/<vg>/<lv>` and `/dev/mapper/<vg>-<lv>`, with `--` unescaping) are parsed back so those volumes still tear down cleanly.

### VHDX Compaction

`Operations::compactDistro()` locates a distribution's `ext4.vhdx` from the `HKCU\...\Lxss` registry, optionally zero-fills its free space (`dd if=/dev/zero` then `rm`), runs `wsl --shutdown` to release the file lock, and compacts the image with a `diskpart` script (`select vdisk` → `attach vdisk readonly` → `compact vdisk` → `detach vdisk`). This works without Hyper-V's `Optimize-VHD`, so it runs on Windows Home. The script is written to a unique temp file.

### Sudo Handling

Sudo authorization is done once per session:
- Password is collected in a modal dialog on the UI thread
- `sudo -v -S` is sent to the persistent bash session
- Subsequent `runWSLRoot()` calls prefix commands with `sudo -n` (non-interactive)

### Thread Safety

All background work runs on detached `std::thread`s:
- `m_terminating` atomic flag prevents posting to destroyed windows
- `m_pendingThreads` counter ensures `onDestroy()` waits for all threads
- `std::mutex` guards the shared `m_mounted` vector

### Password Security

- Passwords are collected via a modal dialog with `ES_PASSWORD` style (masked input)
- Stored in-memory as `std::wstring` / `std::string`; the sudo password is cleared on a rejected/failed authorization so it is re-prompted
- Sent to child processes via stdin pipe — never appears in command-line arguments or the command log

## Requirements for Target System

- **Windows 11** (or Windows 10 2004+) with WSL2 enabled
- At least one WSL2 Linux distribution installed (`wsl --list -v`)
- The following tools in the WSL distribution (auto-detected): `cryptsetup`, `lvm2` (for LUKS/LVM mounts)
