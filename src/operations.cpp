#include "operations.h"
#include <shlobj.h>
#include <algorithm>
#include <sstream>

// ── Helpers ──────────────────────────────────────────────

// Quote a string as a single-quoted bash argument ('\'' escaping)
static std::wstring q(const std::wstring& s) {
    std::wstring r = L"'";
    for (wchar_t c : s) {
        if (c == L'\'') r += L"'\\''";
        else r += c;
    }
    r += L"'";
    return r;
}

static void trimWS(std::wstring& s) {
    while (!s.empty() && (s.back()==L'\r'||s.back()==L'\n'||s.back()==L' '||s.back()==L'\t')) s.pop_back();
    while (!s.empty() && (s.front()==L' '||s.front()==L'\t')) s.erase(s.begin());
}

static std::wstring mountCmd(const std::wstring& fs, bool readOnly,
                             const std::wstring& device, const std::wstring& mountPoint) {
    std::wstring cmd = L"mount";
    if (!fs.empty() && fs != L"auto" && fs != L"lvm" && fs != L"luks") cmd += L" -t " + fs;
    if (readOnly) cmd += L" -o ro";
    return cmd + L" " + q(device) + L" " + q(mountPoint);
}

// Did the command fail because sudo wanted a password?
static bool sudoRefused(const CommandResult& r) {
    if (r.success()) return false;
    auto c = r.output + L" " + r.error;
    return c.find(L"password is required") != std::wstring::npos ||
           c.find(L"incorrect password") != std::wstring::npos ||
           c.find(L"Sorry, try again") != std::wstring::npos ||
           (c.find(L"sudo:") != std::wstring::npos && c.find(L"password") != std::wstring::npos);
}

static std::wstring FormatSize(uint64_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double sz = (double)bytes;
    int i = 0;
    for (; sz >= 1024.0 && i < 4; i++) sz /= 1024.0;
    wchar_t buf[64];
    swprintf_s(buf, L"%.1f %s", sz, units[i]);
    return buf;
}

std::wstring PartitionInfo::sizeDisplay() const { return FormatSize(sizeBytes); }
std::wstring DiskInfo::sizeDisplay() const { return FormatSize(sizeBytes); }
std::wstring DiskInfo::displayName() const {
    wchar_t buf[256];
    swprintf_s(buf, L"Disk %d: %s (%s)", number, model.c_str(), sizeDisplay().c_str());
    return buf;
}
std::wstring MountedVolume::displayDevice() const {
    if (!luksName.empty()) return L"\U0001F512 " + luksName;
    if (!lvmLV.empty()) return L"\U0001F4E6 " + lvmVG + L"/" + lvmLV;
    return device;
}

std::wstring Operations::shortcutDir() {
    wchar_t path[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, path);
    std::wstring dir = std::wstring(path) + L"\\.wslpm\\shortcuts";
    CreateDirectoryW((std::wstring(path) + L"\\.wslpm").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring Operations::getUNCPath(const std::wstring& distro, const std::wstring& mountPoint) {
    std::wstring p = mountPoint;
    std::replace(p.begin(), p.end(), L'/', L'\\');
    return L"\\\\wsl.localhost\\" + distro + p;
}

bool Operations::ensureTools(const std::wstring& distro,
                              const std::vector<std::wstring>& tools, std::wstring& msg) {
    std::wstring missing;
    for (auto& t : tools) {
        auto r = bridge.runWSL(L"which " + t + L" 2>/dev/null", distro);
        if (!r.success()) { if (!missing.empty()) missing += L", "; missing += t; }
    }
    if (!missing.empty()) {
        msg = L"Not found in WSL: " + missing + L". Install: sudo apt install cryptsetup lvm2";
        return false;
    }
    return true;
}

bool Operations::createMountPoint(const std::wstring& path, const std::wstring& distro) {
    return bridge.runWSLRoot(L"mkdir -p " + q(path), distro).success();
}

// ── Disk Discovery ───────────────────────────────────────

std::vector<DiskInfo> Operations::getPhysicalDisks() {
    std::vector<DiskInfo> disks;
    auto r = bridge.runPowerShell(
        L"Get-Disk | ForEach-Object {"
        L"  $d=$_; $pp=Get-Partition -DiskNumber $d.Number -EA SilentlyContinue;"
        L"  $pi=''; if($pp){$pi=($pp|ForEach-Object{"
        L"    \"$($_.PartitionNumber)|$($_.Size)|$($_.Type)|$($_.DriveLetter)\""
        L"  }) -join ';'};"
        L"  \"$($d.Number)\t$($d.FriendlyName)\t$($d.Size)\t$($d.BusType)\t"
        L"$($d.PartitionStyle)\t$($d.IsSystem)\t$($d.IsBoot)\t$pi\""
        L"}", 15000);

    if (!r.success()) return disks;
    std::wistringstream ss(r.output);
    std::wstring line;
    while (std::getline(ss, line)) {
        if (line.empty() || line.back() == L'\r') { if (!line.empty()) line.pop_back(); }
        if (line.empty()) continue;
        // Split by tab
        std::vector<std::wstring> cols;
        std::wistringstream ts(line);
        std::wstring col;
        while (std::getline(ts, col, L'\t')) cols.push_back(col);
        if (cols.size() < 7) continue;

        DiskInfo d;
        try { d.number = std::stoi(cols[0]); } catch (...) { continue; }
        d.model = cols[1];
        try { d.sizeBytes = std::stoull(cols[2]); } catch (...) {}
        d.busType = cols[3];
        d.partStyle = cols[4];
        d.isSystem = cols[5] == L"True";
        d.isBoot = cols[6] == L"True";

        if (cols.size() > 7 && !cols[7].empty()) {
            std::wistringstream ps(cols[7]);
            std::wstring pp;
            while (std::getline(ps, pp, L';')) {
                std::vector<std::wstring> pf;
                std::wistringstream pfs(pp);
                std::wstring f;
                while (std::getline(pfs, f, L'|')) pf.push_back(f);
                if (pf.size() >= 3) {
                    PartitionInfo pi;
                    try { pi.number = std::stoi(pf[0]); } catch (...) { continue; }
                    try { pi.sizeBytes = std::stoull(pf[1]); } catch (...) {}
                    pi.typeId = pf[2];
                    if (pf.size() > 3) pi.label = pf[3];
                    d.partitions.push_back(pi);
                }
            }
        }
        disks.push_back(std::move(d));
    }
    return disks;
}

// ── Attach / Detach ──────────────────────────────────────

bool Operations::attachDisk(int diskNum, bool bare, std::wstring& msg) {
    wchar_t dev[64]; swprintf_s(dev, L"\\\\.\\PHYSICALDRIVE%d", diskNum);
    std::vector<std::wstring> args = {L"--mount", dev};
    if (bare) args.push_back(L"--bare");
    auto r = bridge.runWSLMount(args, 30000);
    
    msg = r.output.empty() ? r.error : r.output;
    if (r.success() || msg.find(L"already") != std::wstring::npos
        || msg.find(L"ALREADY_ATTACHED") != std::wstring::npos
        || msg.find(L"уже") != std::wstring::npos) {  // localized wsl.exe output
        msg = L"Disk attached to WSL";
        return true;
    }

    if (msg.find(L"access") != std::wstring::npos || msg.find(L"denied") != std::wstring::npos
        || msg.find(L"Отказано") != std::wstring::npos || msg.find(L"отказано") != std::wstring::npos)
        msg = L"Access denied. Administrator privileges required.";
    return false;
}

bool Operations::attachPartition(int diskNum, int partNum, const std::wstring& fsType, std::wstring& msg) {
    wchar_t dev[64]; swprintf_s(dev, L"\\\\.\\PHYSICALDRIVE%d", diskNum);
    std::vector<std::wstring> args = {L"--mount", dev, L"--partition", std::to_wstring(partNum)};
    if (!fsType.empty()) { args.push_back(L"--type"); args.push_back(fsType); }
    auto r = bridge.runWSLMount(args, 30000);

    msg = r.output.empty() ? r.error : r.output;
    if (r.success() || msg.find(L"already") != std::wstring::npos
        || msg.find(L"ALREADY_ATTACHED") != std::wstring::npos
        || msg.find(L"уже") != std::wstring::npos) {  // localized wsl.exe output
        msg = L"Partition attached to WSL";
        return true;
    }

    return false;
}

bool Operations::detachDisk(int diskNum, std::wstring& msg) {
    wchar_t dev[64]; swprintf_s(dev, L"\\\\.\\PHYSICALDRIVE%d", diskNum);
    auto r = bridge.runWSLMount({L"--unmount", dev}, 30000);
    if (r.success()) { msg = L"Disk detached from WSL"; return true; }
    msg = r.output.empty() ? r.error : r.output;
    return false;
}

bool Operations::safeEject(int diskNum, std::wstring& msg) {
    detachDisk(diskNum, msg);
    wchar_t ps[256];
    swprintf_s(ps, L"$d=Get-Disk -Number %d;"
        L"if($d.BusType -eq 'USB' -or $d.BusType -eq 'iScsi'){"
        L"Set-Disk -Number %d -IsOffline $true;'OK'}else{'NOPE'}", diskNum, diskNum);
    auto r = bridge.runPowerShell(ps);
    if (r.success() && r.output.find(L"OK") != std::wstring::npos) {
        msg = L"Disk safely ejected"; return true;
    }
    if (r.output.find(L"NOPE") != std::wstring::npos)
        msg = L"Disk is not removable";
    else msg = L"Eject failed";
    return false;
}

// ── Detection ────────────────────────────────────────────

std::wstring Operations::detectVolumeType(const std::wstring& device, const std::wstring& distro) {
    auto r = bridge.runWSLRoot(L"blkid -o value -s TYPE " + q(device) + L" 2>/dev/null", distro);
    if (r.success()) {
        auto t = r.output; trimWS(t);
        if (t == L"crypto_LUKS") return L"luks";
        if (t == L"LVM2_member") return L"lvm";
        return t;  // ext4, xfs, btrfs, etc.
    }
    return L"auto";
}

std::wstring Operations::findWSLDevice(int diskNum, int partNum, uint64_t diskSize, uint64_t expectedSize, const std::wstring& distro) {
    auto r = bridge.runWSL(L"lsblk -b -n -o NAME,SIZE,TYPE -l 2>/dev/null", distro);
    if (!r.success()) return L"";  // cannot enumerate — do not guess a device

    std::wistringstream ss(r.output);
    std::wstring line;
    struct Candidate { std::wstring name; uint64_t diff; };
    std::vector<Candidate> disks;
    std::vector<Candidate> parts;

    while (std::getline(ss, line)) {
        std::wistringstream ts(line);
        std::wstring name, typeStr; 
        uint64_t sz = 0;
        if (!(ts >> name >> sz >> typeStr)) continue;

        // Skip non-sd/vdev names
        if (name.substr(0, 2) != L"sd" && name.substr(0, 2) != L"vd") continue;

        if (typeStr == L"disk") {
            uint64_t diff = (sz > diskSize) ? (sz - diskSize) : (diskSize - sz);
            if (diff < 1024 * 1024) disks.push_back({name, diff});
        }
        else if (typeStr == L"part") {
            uint64_t diff = (sz > expectedSize) ? (sz - expectedSize) : (expectedSize - sz);
            if (diff < 1024 * 1024) parts.push_back({name, diff});
        }
    }

    // Sort by smallest difference
    auto sortFn = [](const Candidate& a, const Candidate& b) { return a.diff < b.diff; };
    std::sort(disks.begin(), disks.end(), sortFn);
    std::sort(parts.begin(), parts.end(), sortFn);

    // Partition requested: only trust an exact partition-size match
    if (partNum > 0) return parts.empty() ? L"" : L"/dev/" + parts[0].name;

    // Whole disk requested
    return disks.empty() ? L"" : L"/dev/" + disks[0].name;
}

// ── Mount Plain ──────────────────────────────────────────

bool Operations::mountPlain(const std::wstring& device, const std::wstring& mountPoint,
                             const std::wstring& fsType, const std::wstring& distro,
                             bool readOnly, int diskNum, int partNum, std::wstring& msg) {
    createMountPoint(mountPoint, distro);
    auto r = bridge.runWSLRoot(mountCmd(fsType, readOnly, device, mountPoint), distro, 30000);

    // Older WSL kernels lack the ntfs3 driver — retry with the legacy read-only one
    if (!r.success() && fsType == L"ntfs3") {
        r = bridge.runWSLRoot(mountCmd(L"ntfs", true, device, mountPoint), distro, 30000);
        if (r.success()) {
            { std::lock_guard<std::mutex> lk(m_mtx);
            m_mounted.push_back({device, mountPoint, L"ntfs", diskNum, L"", L"", L"", distro}); }
            msg = L"Mounted read-only with the legacy ntfs driver (kernel has no ntfs3)";
            return true;
        }
    }
    if (r.success()) {
        { std::lock_guard<std::mutex> lk(m_mtx);
        m_mounted.push_back({device, mountPoint, fsType.empty()?L"auto":fsType,
                             diskNum, L"", L"", L"", distro}); }
        msg = L"Volume mounted at " + mountPoint; return true;
    }

    if (sudoRefused(r)) {
        msg = L"SUDO_PASSWORD_REQUIRED";
    } else {
        auto combined = r.output + L" " + r.error;
        msg = L"Mount error: " + (combined.empty() ? L"exit code " + std::to_wstring(r.exitCode) : combined);
    }
    return false;
}

// ── Mount LUKS ───────────────────────────────────────────

bool Operations::mountLUKS(const std::wstring& device, const std::wstring& mountPoint,
                            const std::string& password, const std::wstring& keyfile,
                            const std::wstring& fsType, const std::wstring& distro,
                            bool readOnly, int diskNum, int partNum, std::wstring& msg) {
    if (!ensureTools(distro, {L"cryptsetup"}, msg)) return false;
    if (!createMountPoint(mountPoint, distro)) { msg = L"Failed to create mount point"; return false; }

    // Convert a Windows keyfile path (C:\...) to a WSL path (/mnt/c/...)
    std::wstring kf = keyfile;
    if (!kf.empty() && (kf.find(L'\\') != std::wstring::npos ||
                        (kf.size() > 1 && kf[1] == L':'))) {
        std::wstring winPath = kf;
        std::replace(winPath.begin(), winPath.end(), L'\\', L'/');
        auto rp = bridge.runWSL(L"wslpath -u " + q(winPath), distro);
        if (!rp.success()) { msg = L"Failed to convert keyfile path to WSL path"; return false; }
        kf = rp.output;
        trimWS(kf);
        if (kf.empty()) { msg = L"Failed to convert keyfile path to WSL path"; return false; }
    }

    wchar_t nameBuf[64]; swprintf_s(nameBuf, L"luks_d%dp%d", diskNum, partNum);
    std::wstring luksName = nameBuf;
    std::wstring mapper = L"/dev/mapper/" + luksName;

    // Open the container unless a previous run left it open already
    if (!bridge.runWSL(L"test -e " + mapper, distro).success()) {
        CommandResult r;
        std::string pwd = password;
        if (!pwd.empty() && pwd.back() != '\n') pwd += '\n';

        // Small retry loop: the device may still be settling after attach
        for (int i = 0; i < 3; i++) {
            if (!kf.empty()) {
                r = bridge.runWSLRoot(L"cryptsetup luksOpen " + q(device) + L" " + q(luksName) + L" --key-file " + q(kf), distro);
            } else {
                r = bridge.runWSLRoot(L"cryptsetup luksOpen " + q(device) + L" " + q(luksName) + L" -d -", distro, 60000, pwd);
            }
            if (r.success()) break;
            if (r.output.find(L"does not exist") == std::wstring::npos) break;
            Sleep(1000);
        }

        if (!r.success()) {
            if (sudoRefused(r)) { msg = L"SUDO_PASSWORD_REQUIRED"; return false; }
            auto combined = r.output + L" " + r.error;
            if (combined.find(L"No key") != std::wstring::npos || combined.find(L"passphrase") != std::wstring::npos)
                msg = L"LUKS: Incorrect password or key";
            else msg = L"cryptsetup error: " + (combined.empty() ? L"exit code " + std::to_wstring(r.exitCode) : combined);
            return false;
        }
    }

    std::wstring innerFS = fsType;
    if (innerFS.empty()) {
        auto det = detectVolumeType(mapper, distro);
        if (det == L"lvm") return mountLVM(mapper, mountPoint, distro, readOnly, diskNum, partNum, msg);
        innerFS = det;
    }

    auto r = bridge.runWSLRoot(mountCmd(innerFS, readOnly, mapper, mountPoint), distro, 30000);
    if (r.success()) {
        { std::lock_guard<std::mutex> lk(m_mtx);
        m_mounted.push_back({mapper, mountPoint, L"luks", diskNum, luksName, L"", L"", distro}); }
        msg = L"LUKS volume mounted at " + mountPoint; return true;
    }
    bridge.runWSLRoot(L"cryptsetup luksClose " + q(luksName), distro);
    msg = L"LUKS opened but mount failed";
    return false;
}

// ── Mount LVM ────────────────────────────────────────────

bool Operations::mountLVM(const std::wstring& device, const std::wstring& mountPoint,
                           const std::wstring& distro, bool readOnly,
                           int diskNum, int partNum, std::wstring& msg) {
    if (!ensureTools(distro, {L"pvscan", L"vgscan", L"lvs"}, msg)) return false;
    if (!createMountPoint(mountPoint, distro)) { msg = L"Failed to create mount point"; return false; }

    bridge.runWSLRoot(L"pvscan --cache 2>/dev/null && vgscan 2>/dev/null && lvscan 2>/dev/null", distro, 15000);

    auto r = bridge.runWSLRoot(L"pvs --noheadings -o vg_name " + q(device) + L" 2>/dev/null", distro);
    std::wstring vg = r.output;
    trimWS(vg);
    if (vg.empty()) { msg = L"LVM volume group not found"; return false; }

    bridge.runWSLRoot(L"vgchange -ay " + q(vg) + L" 2>/dev/null", distro);

    r = bridge.runWSLRoot(L"lvs --noheadings -o lv_name " + q(vg) + L" 2>/dev/null", distro);
    std::vector<std::wstring> lvs;
    std::wistringstream ss(r.output);
    std::wstring lv;
    while (std::getline(ss, lv)) {
        trimWS(lv);
        if (!lv.empty()) lvs.push_back(lv);
    }
    if (lvs.empty()) { msg = L"Logical volumes not found"; return false; }

    int ok = 0;
    for (auto& lv : lvs) {
        auto lvDev = L"/dev/" + vg + L"/" + lv;
        auto mp = lvs.size() > 1 ? mountPoint + L"/" + lv : mountPoint;
        createMountPoint(mp, distro);
        auto fs = detectVolumeType(lvDev, distro);
        if (bridge.runWSLRoot(mountCmd(fs, readOnly, lvDev, mp), distro, 30000).success()) {
            { std::lock_guard<std::mutex> lk(m_mtx);
            m_mounted.push_back({lvDev, mp, L"lvm", diskNum, L"", vg, lv, distro}); }
            ok++;
        }
    }
    if (ok > 0) {
        wchar_t buf[128]; swprintf_s(buf, L"Mounted %d of %d LVs", ok, (int)lvs.size());
        msg = buf; return true;
    }
    msg = L"Failed to mount LVs"; return false;
}

// ── Rescan ───────────────────────────────────────────────

int Operations::rescanMounts(const std::wstring& distro) {
    auto r = bridge.runWSL(L"mount", distro);
    if (!r.success()) return 0;

    int added = 0;
    std::wistringstream ss(r.output);
    std::wstring line;
    while (std::getline(ss, line)) {
        // <device> on <mountpoint> type <fs> (options)
        std::wistringstream ts(line);
        std::wstring dev, on, mp, typeKw, fs;
        if (!(ts >> dev >> on >> mp >> typeKw >> fs)) continue;
        if (on != L"on" || typeKw != L"type") continue;
        if (dev.rfind(L"/dev/", 0) != 0) continue;               // only block devices
        if (mp.rfind(L"/mnt/", 0) != 0) continue;                // only our namespace
        if (mp.rfind(L"/mnt/wsl", 0) == 0) continue;             // WSL internals
        if (mp.size() == 6) continue;                            // /mnt/c drive letters
        if (fs == L"9p" || fs == L"drvfs" || fs == L"virtiofs") continue;

        std::lock_guard<std::mutex> lk(m_mtx);
        bool known = false;
        for (auto& v : m_mounted) if (v.mountPoint == mp) { known = true; break; }
        if (known) continue;

        MountedVolume v{dev, mp, fs, -1, L"", L"", L"", distro};
        // Recover teardown info from the device path
        if (dev.rfind(L"/dev/mapper/luks_", 0) == 0) {
            v.luksName = dev.substr(12); // after "/dev/mapper/"
            v.volumeType = L"luks";
        } else if (dev.rfind(L"/dev/mapper/", 0) == 0) {
            // /dev/mapper/<vg>-<lv>, where a literal '-' in a name is doubled to '--'
            std::wstring name = dev.substr(12);
            size_t sep = std::wstring::npos;
            for (size_t i = 0; i < name.size(); i++) {
                if (name[i] != L'-') continue;
                if (i + 1 < name.size() && name[i + 1] == L'-') { i++; continue; }
                sep = i; break;
            }
            if (sep != std::wstring::npos) {
                auto unesc = [](std::wstring s) {
                    std::wstring o;
                    for (size_t k = 0; k < s.size(); k++) {
                        if (s[k] == L'-' && k + 1 < s.size() && s[k + 1] == L'-') { o += L'-'; k++; }
                        else o += s[k];
                    }
                    return o;
                };
                v.lvmVG = unesc(name.substr(0, sep));
                v.lvmLV = unesc(name.substr(sep + 1));
                v.volumeType = L"lvm";
            }
        } else {
            auto s1 = dev.find(L'/', 5);
            if (s1 != std::wstring::npos && dev.find(L'/', s1 + 1) == std::wstring::npos) {
                // /dev/<vg>/<lv>
                v.lvmVG = dev.substr(5, s1 - 5);
                v.lvmLV = dev.substr(s1 + 1);
                v.volumeType = L"lvm";
            }
        }
        m_mounted.push_back(std::move(v));
        added++;
    }
    return added;
}

// ── Unmount ──────────────────────────────────────────────

bool Operations::unmountByMountPoint(const std::wstring& mountPoint, std::wstring& msg) {
    // Copy volume info under lock, run slow WSL commands without it
    MountedVolume v;
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        for (auto& m : m_mounted) {
            if (m.mountPoint == mountPoint) { v = m; found = true; break; }
        }
    }
    if (!found) { msg = L"Volume not found"; return false; }
    auto d = v.distro;

    auto r = bridge.runWSLRoot(L"umount " + q(v.mountPoint) + L" 2>/dev/null", d);
    if (!r.success()) {
        r = bridge.runWSLRoot(L"umount -l " + q(v.mountPoint) + L" 2>/dev/null", d);
        if (!r.success()) { msg = L"Failed to unmount. Volume may be busy."; return false; }
    }
    if (!v.lvmVG.empty())
        bridge.runWSLRoot(L"vgchange -an " + q(v.lvmVG) + L" 2>/dev/null", d);
    if (!v.luksName.empty())
        bridge.runWSLRoot(L"cryptsetup luksClose " + q(v.luksName) + L" 2>/dev/null", d);

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        for (auto it = m_mounted.begin(); it != m_mounted.end(); ++it) {
            if (it->mountPoint == mountPoint) { m_mounted.erase(it); break; }
        }
    }
    msg = L"Volume unmounted";
    return true;
}

bool Operations::unmountAll(std::wstring& msg) {
    std::vector<std::wstring> mps;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        for (auto& m : m_mounted) mps.push_back(m.mountPoint);
    }
    int n = 0, failed = 0;
    for (auto& mp : mps) {
        std::wstring m;
        if (unmountByMountPoint(mp, m)) n++; else failed++;
    }
    wchar_t buf[96];
    if (failed > 0)
        swprintf_s(buf, L"Unmounted %d volume(s), %d failed (busy?)", n, failed);
    else
        swprintf_s(buf, L"Unmounted %d volume(s)", n);
    msg = buf;
    return failed == 0;
}

// ── VHDX Compaction ──────────────────────────────────────

static uint64_t FileSizeOf(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    return ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
}

std::wstring WSLImage::sizeDisplay() const { return FormatSize(sizeBytes); }

// Enumerate all registered WSL distributions with their VHDX images
std::vector<WSLImage> Operations::getWSLImages() {
    std::vector<WSLImage> images;
    auto r = bridge.runPowerShell(
        L"Get-ChildItem HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Lxss -EA SilentlyContinue"
        L" | ForEach-Object { $p = Get-ItemProperty $_.PSPath;"
        L" if ($p.DistributionName -and $p.BasePath) {"
        L" \"$($p.DistributionName)`t$(Join-Path $p.BasePath 'ext4.vhdx')\" } }", 15000);
    if (!r.success()) return images;

    std::wistringstream ss(r.output);
    std::wstring line;
    while (std::getline(ss, line)) {
        trimWS(line);
        auto tab = line.find(L'\t');
        if (tab == std::wstring::npos) continue;
        WSLImage img;
        img.name = line.substr(0, tab);
        img.vhdxPath = line.substr(tab + 1);
        if (img.vhdxPath.rfind(L"\\\\?\\", 0) == 0)
            img.vhdxPath = img.vhdxPath.substr(4); // BasePath may carry a \\?\ prefix
        img.sizeBytes = FileSizeOf(img.vhdxPath);
        if (img.sizeBytes == 0) continue; // image file missing
        images.push_back(std::move(img));
    }
    return images;
}

bool Operations::compactDistro(const std::wstring& distro, const std::wstring& vhdxPath,
                               bool zeroFree, std::wstring& msg) {
    const std::wstring& vhdx = vhdxPath;
    uint64_t before = FileSizeOf(vhdx);
    if (before == 0) { msg = L"VHDX file not found: " + vhdx; return false; }

    // 2. Optionally zero free space inside the distro so compact can reclaim it.
    //    dd exits non-zero when the disk fills up — that is expected.
    if (zeroFree) {
        bridge.note(L"=== Compact 1/3: zeroing free space inside '" + distro +
                    L"' (can take many minutes, dd fills the disk then removes the file)...");
        bridge.runWSLRoot(
            L"sh -c 'dd if=/dev/zero of=/.wslpm_zerofill bs=1M 2>/dev/null; sync;"
            L" rm -f /.wslpm_zerofill; sync'",
            distro, 1800000);
    }

    // 3. Shut down WSL entirely to release the VHDX file lock
    bridge.note(L"=== Compact 2/3: shutting down WSL...");
    bridge.CloseSession();
    auto rs = bridge.RunProcess(WSLBridge::SysExe(L"wsl.exe") + L" --shutdown", 60000, "", OutEnc::UTF16LE);
    if (rs.exitCode != 0) { msg = L"wsl --shutdown failed"; return false; }
    Sleep(3000); // give the utility VM time to release the file

    // 4. Compact via diskpart (works without Hyper-V's Optimize-VHD).
    //    Unique temp name so a local user can't pre-place/redirect the script.
    wchar_t tmpDir[MAX_PATH], scriptBuf[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpDir);
    if (!GetTempFileNameW(tmpDir, L"wpm", 0, scriptBuf)) {
        msg = L"Failed to create temp script file"; return false;
    }
    std::wstring scriptPath = scriptBuf;
    std::wstring scriptW =
        L"select vdisk file=\"" + vhdx + L"\"\r\n"
        L"attach vdisk readonly\r\n"
        L"compact vdisk\r\n"
        L"detach vdisk\r\n";
    // diskpart reads /s scripts in the system ANSI codepage
    int an = WideCharToMultiByte(CP_ACP, 0, scriptW.c_str(), (int)scriptW.size(), nullptr, 0, nullptr, nullptr);
    std::string scriptA(an, 0);
    WideCharToMultiByte(CP_ACP, 0, scriptW.c_str(), (int)scriptW.size(), scriptA.data(), an, nullptr, nullptr);
    HANDLE hf = CreateFileW(scriptPath.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) { msg = L"Failed to write diskpart script"; return false; }
    DWORD written;
    WriteFile(hf, scriptA.data(), (DWORD)scriptA.size(), &written, nullptr);
    CloseHandle(hf);

    bridge.note(L"=== Compact 3/3: diskpart compact vdisk (progress below)...");
    // diskpart prints in the console OEM codepage; stream progress lines to the log
    auto rd = bridge.RunProcess(WSLBridge::SysExe(L"diskpart.exe") + L" /s \"" + scriptPath + L"\"",
                                1800000, "", OutEnc::OEM, true);
    DeleteFileW(scriptPath.c_str());
    if (rd.exitCode != 0) {
        msg = L"diskpart compact failed (exit code " + std::to_wstring(rd.exitCode) + L"): " + rd.output;
        return false;
    }

    uint64_t after = FileSizeOf(vhdx);
    uint64_t saved = before > after ? before - after : 0;
    msg = L"VHDX compacted: " + FormatSize(before) + L" → " + FormatSize(after) +
          L" (saved " + FormatSize(saved) + L")";
    return true;
}

// ── Explorer Integration ─────────────────────────────────

bool Operations::createShortcut(const std::wstring& distro, const std::wstring& mountPoint,
                                 const std::wstring& label, std::wstring& msg) {
    auto unc = getUNCPath(distro, mountPoint);
    auto lbl = label.empty() ? mountPoint.substr(mountPoint.rfind(L'/') + 1) : label;
    if (lbl.empty()) lbl = L"WSL_Mount";

    auto dir = shortcutDir();
    auto lnkPath = dir + L"\\" + lbl + L".lnk";

    CoInitialize(nullptr);
    IShellLinkW* sl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, (void**)&sl);
    if (SUCCEEDED(hr)) {
        sl->SetPath(unc.c_str());
        sl->SetDescription((L"WSL: " + mountPoint).c_str());
        IPersistFile* pf = nullptr;
        hr = sl->QueryInterface(IID_IPersistFile, (void**)&pf);
        if (SUCCEEDED(hr)) {
            pf->Save(lnkPath.c_str(), TRUE);
            pf->Release();
        }
        sl->Release();
    }

    // Also put in user Links folder
    wchar_t profile[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, profile);
    auto linksLnk = std::wstring(profile) + L"\\Links\\WSL_" + lbl + L".lnk";
    CopyFileW(lnkPath.c_str(), linksLnk.c_str(), FALSE);

    CoUninitialize();
    msg = L"Shortcut created"; return true;
}

void Operations::openInExplorer(const std::wstring& distro, const std::wstring& mountPoint) {
    auto p = getUNCPath(distro, mountPoint);
    ShellExecuteW(nullptr, L"explore", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void Operations::cleanupShortcuts() {
    auto dir = shortcutDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.lnk").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { DeleteFileW((dir + L"\\" + fd.cFileName).c_str()); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    wchar_t profile[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, profile);
    auto linksDir = std::wstring(profile) + L"\\Links";
    h = FindFirstFileW((linksDir + L"\\WSL_*.lnk").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { DeleteFileW((linksDir + L"\\" + fd.cFileName).c_str()); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}
