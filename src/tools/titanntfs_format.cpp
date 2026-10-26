#include "titanntfs/ntfs.hpp"
#include <iostream>
#include <string>
#include <vector>

void printUsage() {
    std::cout << "TitanNTFS Format Utility (Clean-Room Edition)\n"
              << "Usage: titanntfs_format <target_image_or_drive> [options]\n\n"
              << "Options:\n"
              << "  --cluster <size>   Cluster size in bytes (default: 4096 / 4KB)\n"
              << "  --label <name>     Volume label (default: TITAN_NTFS)\n"
              << "  --size <MB>        Size in megabytes for new image files (default: 64)\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string target = argv[1];
    uint32_t clusterSize = 4096;
    std::wstring label = L"TITAN_NTFS";
    uint64_t sizeMb = 64;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cluster" && i + 1 < argc) {
            clusterSize = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--label" && i + 1 < argc) {
            std::string l = argv[++i];
            label = std::wstring(l.begin(), l.end());
        } else if (arg == "--size" && i + 1 < argc) {
            sizeMb = std::stoull(argv[++i]);
        }
    }

    std::cout << "[*] Initializing block device: " << target << " (" << sizeMb << " MB)\n";
    auto dev = std::make_shared<titanntfs::FileBlockDevice>("NtfsDev", target, 512, sizeMb * 1024 * 1024);
    if (!dev->isOnline()) {
        std::cerr << "[-] Error: Failed to open target block device: " << target << "\n";
        return 1;
    }

    std::cout << "[*] Formatting volume with clean-room NTFS architecture...\n";
    titanntfs::NtfsFileSystem fs;
    auto status = fs.format(*dev, clusterSize, label);
    if (!titanntfs::IsSuccess(status)) {
        std::cerr << "[-] Format failed: " << titanntfs::StatusToString(status) << "\n";
        return 1;
    }

    std::cout << "[+] NTFS volume successfully synthesized and formatted!\n"
              << "    Cluster Size: " << clusterSize << " bytes\n"
              << "    Records:      " << fs.getRecordCount() << "\n";

    fs.unmount();
    return 0;
}
