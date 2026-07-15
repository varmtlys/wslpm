#pragma once
#include "wsl_bridge.h"
#include <vector>
#include <string>
#include <mutex>
#include <cstdint>

// ── Data structures ──────────────────────────────────────

struct PartitionInfo {
    int number = 0;
    uint64_t sizeBytes = 0;
    std::wstring typeId;
    std::wstring label;
    std::wstring sizeDisplay() const;
};

struct DiskInfo {
    int number = 0;
    std::wstring model;
    uint64_t sizeBytes = 0;
    std::wstring busType;
    std::wstring partStyle;
    bool isSystem = false;
    bool isBoot = false;
    std::vector<PartitionInfo> partitions;
    std::wstring sizeDisplay() const;
    std::wstring displayName() const;
};

struct MountedVolume {
    std::wstring device;
    std::wstring mountPoint;
    std::wstring volumeType;   // "ext4", "luks", "lvm", etc.
    int diskNumber = -1;
    std::wstring luksName;
    std::wstring lvmVG;
    std::wstring lvmLV;
    std::wstring distro;
    std::wstring displayDevice() const;
};

struct WSLImage {
    std::wstring name;      // distribution name
    std::wstring vhdxPath;  // full path to ext4.vhdx
    uint64_t sizeBytes = 0;
    std::wstring sizeDisplay() const;
};

// ── Operations ───────────────────────────────────────────

class Operations {
public:
    Operations() = default;
    WSLBridge bridge;

    // Disk management
    std::vector<DiskInfo> getPhysicalDisks();
    bool attachDisk(int diskNum, bool bare, std::wstring& msg);
    bool attachPartition(int diskNum, int partNum, const std::wstring& fsType, std::wstring& msg);
    bool detachDisk(int diskNum, std::wstring& msg);
    bool safeEject(int diskNum, std::wstring& msg);

    // Volume type detection
    std::wstring detectVolumeType(const std::wstring& device, const std::wstring& distro);
    std::wstring findWSLDevice(int diskNum, int partNum, uint64_t diskSize, uint64_t expectedSize, const std::wstring& distro);

    // Mount operations
    bool mountPlain(const std::wstring& device, const std::wstring& mountPoint,
                    const std::wstring& fsType, const std::wstring& distro,
                    bool readOnly, int diskNum, int partNum, std::wstring& msg);
    bool mountLUKS(const std::wstring& device, const std::wstring& mountPoint,
                   const std::string& password, const std::wstring& keyfile,
                   const std::wstring& fsType, const std::wstring& distro,
                   bool readOnly, int diskNum, int partNum, std::wstring& msg);
    bool mountLVM(const std::wstring& device, const std::wstring& mountPoint,
                  const std::wstring& distro, bool readOnly,
                  int diskNum, int partNum, std::wstring& msg);

    // Pick up volumes still mounted from a previous run; returns how many were added
    int rescanMounts(const std::wstring& distro);

    // Unmount
    bool unmountByMountPoint(const std::wstring& mountPoint, std::wstring& msg);
    bool unmountAll(std::wstring& msg);

    // Distro image compaction (shuts down ALL of WSL)
    std::vector<WSLImage> getWSLImages();
    bool compactDistro(const std::wstring& distro, const std::wstring& vhdxPath,
                       bool zeroFree, std::wstring& msg);

    // Explorer integration
    bool createShortcut(const std::wstring& distro, const std::wstring& mountPoint,
                        const std::wstring& label, std::wstring& msg);
    void openInExplorer(const std::wstring& distro, const std::wstring& mountPoint);
    void cleanupShortcuts();

    // Tracked volumes
    std::vector<MountedVolume>& mounted() { return m_mounted; }
    std::mutex& mtx() { return m_mtx; }

private:
    std::vector<MountedVolume> m_mounted;
    mutable std::mutex m_mtx;
    bool ensureTools(const std::wstring& distro, const std::vector<std::wstring>& tools, std::wstring& msg);
    bool createMountPoint(const std::wstring& path, const std::wstring& distro);
    std::wstring getUNCPath(const std::wstring& distro, const std::wstring& mountPoint);
    std::wstring shortcutDir();
};
