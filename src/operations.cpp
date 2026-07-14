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
        auto t = r.output; while (!t.empty()&&(t.back()==L'\r'||t.back()==L'\n')) t.pop_back();
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

    // If we specifically want a partition and it was matched by exact partition size
    if (partNum > 0 && !parts.empty()) return L"/dev/" + parts[0].name;
    
    // If partition not found explicitly (or we want the whole disk), fallback to the disk size match.
    if (!disks.empty()) return L"/dev/" + disks[0].name + (partNum > 0 ? std::to_wstring(partNum) : L"");

    return L"";  // nothing matched by size — do not guess a device
}

// ── Mount Plain ──────────────────────────────────────────

bool Operations::mountPlain(const std::wstring& device, const std::wstring& mountPoint,
                             const std::wstring& fsType, const std::wstring& distro,
                             bool readOnly, int diskNum, int partNum, std::wstring& msg) {
    std::wstring cmd = L"mount";
    if (!fsType.empty() && fsType != L"auto") cmd += L" -t " + fsType;
    if (readOnly) cmd += L" -o ro";
    cmd += L" " + q(device) + L" " + q(mountPoint);

    auto r = bridge.runWSLRoot(cmd, distro, 30000);
    if (r.success()) {
        { std::lock_guard<std::mutex> lk(m_mtx);
        m_mounted.push_back({device, mountPoint, fsType.empty()?L"auto":fsType,
                             diskNum, L"", L"", L"", distro}); }
        msg = L"Volume mounted at " + mountPoint; return true;
    }

    auto combined = r.output + L" " + r.error;
    if (combined.find(L"password is required") != std::wstring::npos || 
        combined.find(L"incorrect password") != std::wstring::npos ||
        combined.find(L"Sorry, try again") != std::wstring::npos ||
        (r.exitCode != 0 && combined.find(L"sudo:") != std::wstring::npos && combined.find(L"password") != std::wstring::npos && combined.find(L"mkdir") == std::wstring::npos)) {
        // Only if it really looks like a sudo refusal
        msg = L"SUDO_PASSWORD_REQUIRED";
    } else {
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

    // Check already open — if so, mount the existing mapper directly
    if (bridge.runWSL(L"test -e /dev/mapper/" + luksName, distro).success()) {
        std::wstring mapper = L"/dev/mapper/" + luksName;
        auto innerFS = fsType;
        if (innerFS.empty()) {
            auto det = detectVolumeType(mapper, distro);
            if (det == L"lvm") return mountLVM(mapper, mountPoint, distro, readOnly, diskNum, partNum, msg);
            innerFS = det;
        }
        std::wstring cmd = L"mount";
        if (!innerFS.empty() && innerFS != L"auto") cmd += L" -t " + innerFS;
        if (readOnly) cmd += L" -o ro";
        cmd += L" " + q(mapper) + L" " + q(mountPoint);
        auto r = bridge.runWSLRoot(cmd, distro, 30000);
        if (r.success()) {
            { std::lock_guard<std::mutex> lk(m_mtx);
            m_mounted.push_back({mapper, mountPoint, L"luks", diskNum, luksName, L"", L"", distro}); }
            msg = L"LUKS volume (already open) mounted at " + mountPoint; return true;
        }
        msg = L"LUKS container opened but mount failed"; return false;
    }

    // Open LUKS (with small retry for device settling)
    CommandResult r;
    std::string pwd = password;
    if (!pwd.empty() && pwd.back() != '\n') pwd += '\n';

    for(int i=0; i<3; i++) {
        if (!kf.empty()) {
            r = bridge.runWSLRoot(L"cryptsetup luksOpen " + q(device) + L" " + q(luksName) + L" --key-file " + q(kf), distro);
        } else {
            r = bridge.runWSLRoot(L"cryptsetup luksOpen " + q(device) + L" " + q(luksName) + L" -d -", distro, 60000, pwd);
        }
        if (r.success()) break;
        if (r.output.find(L"does not exist") == std::wstring::npos) break;
        Sleep(1000); // Wait for device to appear
    }

    if (!r.success()) {
        auto combined = r.output + L" " + r.error;
        if (combined.find(L"password is required") != std::wstring::npos || 
            combined.find(L"incorrect password") != std::wstring::npos ||
            combined.find(L"Sorry, try again") != std::wstring::npos ||
            (r.exitCode != 0 && combined.find(L"sudo:") != std::wstring::npos && combined.find(L"password") != std::wstring::npos && combined.find(L"cryptsetup") == std::wstring::npos)) {
            msg = L"SUDO_PASSWORD_REQUIRED";
            return false;
        }
        if (combined.find(L"No key") != std::wstring::npos || combined.find(L"passphrase") != std::wstring::npos)
            msg = L"LUKS: Incorrect password or key";
        else msg = L"cryptsetup error: " + (combined.empty() ? L"exit code " + std::to_wstring(r.exitCode) : combined);
        return false;
    }

    std::wstring mapper = L"/dev/mapper/" + luksName;

    // Detect inner FS
    std::wstring innerFS = fsType;
    if (innerFS.empty()) {
        auto det = detectVolumeType(mapper, distro);
        if (det == L"lvm") {
            // LUKS + LVM
            return mountLVM(mapper, mountPoint, distro, readOnly, diskNum, partNum, msg);
        }
        innerFS = det;
    }

    // Mount
    std::wstring cmd = L"mount";
    if (!innerFS.empty() && innerFS != L"auto") cmd += L" -t " + innerFS;
    if (readOnly) cmd += L" -o ro";
    cmd += L" " + q(mapper) + L" " + q(mountPoint);
    r = bridge.runWSLRoot(cmd, distro, 30000);
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
        while (!lv.empty() && (lv.back()==L'\r'||lv.back()==L' ')) lv.pop_back();
        while (!lv.empty() && lv.front()==L' ') lv.erase(lv.begin());
        if (!lv.empty()) lvs.push_back(lv);
    }
    if (lvs.empty()) { msg = L"Logical volumes not found"; return false; }

    int ok = 0;
    for (auto& lv : lvs) {
        auto lvDev = L"/dev/" + vg + L"/" + lv;
        auto mp = lvs.size() > 1 ? mountPoint + L"/" + lv : mountPoint;
        createMountPoint(mp, distro);
        auto fs = detectVolumeType(lvDev, distro);
        std::wstring cmd = L"mount";
        if (!fs.empty() && fs != L"auto" && fs != L"lvm" && fs != L"luks") cmd += L" -t " + fs;
        if (readOnly) cmd += L" -o ro";
        cmd += L" " + q(lvDev) + L" " + q(mp);
        if (bridge.runWSLRoot(cmd, distro, 30000).success()) {
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
