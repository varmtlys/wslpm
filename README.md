# 🐧 WSL Mount Manager (C++ Native)

A native system utility for mounting Linux volumes (LVM, LUKS, ext4, xfs, btrfs, etc.) via WSL2 on Windows 11.

## ✨ Features
- **🚀 Native C++**: Fast performance and minimal resource usage.
- **🛡 Auto-UAC**: Automatic administrator privilege request on launch.
- **🎨 Modern UI**: Native Win32 interface with Windows 11 Dark Mode support.
- **🔒 LUKS & LVM**: Full support for encryption and volume groups.
- **📌 Explorer Integration**: Creates shortcuts in the Explorer sidebar.

## 🛠 Build Requirements
- **Visual Studio 2022** (or 2019)
- **"Desktop development with C++"** workload

## 📦 Build
Simply run the script in the project root:
```cmd
build.bat
```
The executable will appear at `build/WSLMountManager.exe`.

---
