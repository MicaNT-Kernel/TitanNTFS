#include "titanntfs/ntfs.hpp"
#include "titanntfs/device.hpp"

#ifdef _WIN32
#include "titanntfs/winfsp_bridge.hpp"
#include <csignal>
#include <atomic>
#include <iostream>
#include <filesystem>

using namespace titanntfs;

namespace {
std::atomic<bool> g_Running{true};
WinFspMountSession g_Session;

void SigHandler(int) {
    std::cout << "\n[TitanNTFS Mount] Stop signal received. Unmounting volume cleanly...\n";
    g_Running = false;
    g_Session.unmount();
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "========================================================================\n";
        std::cout << "    TitanNTFS Filesystem Mount Driver (WinFsp Userland Bridge)         \n";
        std::cout << "========================================================================\n\n";
        std::cout << "Usage: titanntfs_mount <image_path_or_device> <drive_letter:> [options]\n\n";
        std::cout << "Options:\n";
        std::cout << "  --format          Format image as clean NTFS volume before mounting\n";
        std::cout << "  --label <name>    Set volume label (default: Local Disk)\n";
        std::cout << "  --cluster <size>  Cluster size in bytes (default: 4096)\n";
        std::cout << "  --size <MB>       Backing file size in megabytes if created (default: 256)\n\n";
        std::cout << "Examples:\n";
        std::cout << "  titanntfs_mount ntfs_disk.raw N:\n";
        std::cout << "  titanntfs_mount ntfs_disk.raw N: --format --label Storage\n";
        std::cout << "  titanntfs_mount \\\\.\\PhysicalDrive1 N:\n\n";
        return 0;
    }

    std::string devicePath;
    std::string driveLetter;
    bool doFormat = false;
    std::wstring volumeLabel = L"Local Disk";
    uint32_t clusterSize = 4096;
    uint64_t diskSizeMB = 256;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--format") {
            doFormat = true;
        } else if (arg == "--label" && i + 1 < argc) {
            std::string lStr = argv[++i];
            volumeLabel = std::wstring(lStr.begin(), lStr.end());
        } else if (arg == "--cluster" && i + 1 < argc) {
            clusterSize = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--size" && i + 1 < argc) {
            diskSizeMB = std::stoull(argv[++i]);
            if (diskSizeMB < 32) diskSizeMB = 32;
        } else if (devicePath.empty()) {
            devicePath = arg;
        } else if (driveLetter.empty()) {
            driveLetter = arg;
        }
    }

    if (devicePath.empty() || driveLetter.empty()) {
        std::cerr << "[TitanNTFS Error] Both device/image path and drive letter must be specified.\n";
        return 1;
    }

    // Validate drive letter format (e.g., "N:" or "N:\")
    if (driveLetter.size() == 1) {
        driveLetter += ":";
    } else if (driveLetter.size() > 2 && driveLetter[1] == ':') {
        driveLetter = driveLetter.substr(0, 2);
    }

    auto& fsp = WinFspApi::Get();
    if (!fsp.isAvailable()) {
        std::cerr << "[TitanNTFS Error] WinFsp dynamic library (winfsp-x64.dll) was not found.\n";
        std::cerr << "Please install WinFsp on this machine (e.g. run titanntfs_setup.exe or 'winget install WinFsp.WinFsp').\n";
        return 1;
    }

    uint64_t defaultBytes = diskSizeMB * 1024ULL * 1024ULL;
    auto dev = std::make_shared<FileBlockDevice>("ntfs_dev", devicePath, 512, defaultBytes);
    if (!dev->isOnline()) {
        std::cerr << "[TitanNTFS Error] Failed to open block device or raw image: " << devicePath << "\n";
        return 1;
    }

    auto fs = std::make_shared<NtfsFileSystem>();

    if (doFormat) {
        std::cout << "[TitanNTFS] Formatting " << devicePath << " with clean-room NTFS architecture...\n";
        auto st = fs->format(*dev, clusterSize, volumeLabel);
        if (!IsSuccess(st)) {
            std::cerr << "[TitanNTFS Error] Formatting failed: " << StatusToString(st) << "\n";
            return 1;
        }
        std::cout << "[TitanNTFS] Formatting complete.\n";
    } else {
        auto st = fs->mount(dev);
        if (!IsSuccess(st)) {
            std::cout << "[TitanNTFS Notice] Device does not contain a valid NTFS signature. Initializing clean NTFS volume...\n";
            st = fs->format(*dev, clusterSize, volumeLabel);
            if (!IsSuccess(st)) {
                std::cerr << "[TitanNTFS Error] Failed to initialize volume: " << StatusToString(st) << "\n";
                return 1;
            }
        }
    }

    // Seed welcome file with Alternate Data Streams (ADS) if root is empty
    std::vector<NtfsFileRecord> rootFiles;
    if (fs->listDirectory(L"\\", rootFiles) && rootFiles.empty()) {
        const std::string welcomeText =
            "========================================================================\n"
            "   Welcome to TitanNTFS!\n"
            "   Clean-Room NTFS Engine & User-Mode Driver Bridge for Windows NT\n"
            "========================================================================\n\n"
            "- Filesystem: NT File System (NTFS)\n"
            "- Architecture: 1024-byte Master File Table (MFT) Records\n"
            "- Torn Write Protection: Update Sequence Array (USA) Fixups\n"
            "- Data Mapping: Run-Length Encoded LCN/VCN Cluster Extents\n"
            "- Alternate Data Streams: Enabled (e.g. filename:streamname)\n"
            "- Clean-Room Provenance: 17 U.S.C. 1201(f) & Google v. Oracle Interoperability\n";

        const std::string zoneText = "[ZoneTransfer]\r\nZoneId=3\r\n";

        fs->createFile(L"\\README.txt", 0x20, 0);
        size_t written = 0;
        fs->writeFile(L"\\README.txt", L"", 0,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(welcomeText.data()), welcomeText.size()),
            written);

        // Seed Alternate Data Stream to demonstrate native ADS support
        fs->writeFile(L"\\README.txt", L":Zone.Identifier", 0,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(zoneText.data()), zoneText.size()),
            written);

        fs->createDirectory(L"\\Data");
    }

    std::signal(SIGINT, SigHandler);
    std::signal(SIGTERM, SigHandler);

    std::wstring wDrive(driveLetter.begin(), driveLetter.end());
    if (!g_Session.mount(fs, wDrive)) {
        std::cerr << "[TitanNTFS Error] Failed to mount NTFS volume to drive " << driveLetter << "\n";
        return 1;
    }

    std::cout << "========================================================================\n";
    std::cout << "[TitanNTFS SUCCESS] NTFS volume is MOUNTED to " << driveLetter << "\n";
    std::cout << "========================================================================\n";
    std::cout << "Device: " << devicePath << "\n";
    std::cout << "Drive Letter: " << driveLetter << "\\\n";
    std::cout << "Cluster Size: " << clusterSize << " bytes\n";
    std::cout << "Alternate Data Streams (ADS): Enabled\n";
    std::cout << "Visible in Windows File Explorer, cmd, PowerShell, and all applications.\n";
    std::cout << "Press Ctrl+C to unmount cleanly...\n\n";

    while (g_Running) {
        Sleep(500);
    }

    g_Session.unmount();
    std::cout << "[TitanNTFS] Volume " << driveLetter << " unmounted cleanly.\n";
    return 0;
}
#else
int main() {
    std::cout << "TitanNTFS WinFsp driver bridge is a Windows NT native component.\n";
    return 0;
}
#endif
