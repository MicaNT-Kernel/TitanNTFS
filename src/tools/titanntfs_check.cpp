#include "titanntfs/ntfs.hpp"
#include "titanntfs/device.hpp"
#include <iostream>
#include <string>
#include <vector>

void printUsage() {
    std::cout << "TitanNTFS Check & Repair Utility (Clean-Room Edition)\n"
              << "Usage: titanntfs_check <target_image_or_drive> [options]\n\n"
              << "Options:\n"
              << "  --repair, /f       Perform automated MFT repair and restore torn USA fixups\n"
              << "  --verbose, -v      Display detailed per-record verification logs\n\n"
              << "Mounts, validates NTFS boot sectors, and audits MFT records.\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string target;
    bool doRepair = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--repair" || arg == "/f" || arg == "-f") {
            doRepair = true;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (target.empty()) {
            target = arg;
        }
    }

    if (target.empty()) {
        printUsage();
        return 1;
    }

    std::cout << "[*] Inspecting NTFS device: " << target << "\n";

    auto dev = std::make_shared<titanntfs::FileBlockDevice>("NtfsDev", target, 512);
    if (!dev->isOnline()) {
        std::cerr << "[-] Error: Failed to open target block device: " << target << "\n";
        return 1;
    }

    titanntfs::NtfsFileSystem fs;
    auto status = fs.mount(dev);
    if (!titanntfs::IsSuccess(status)) {
        std::cerr << "[-] Mount failed: " << titanntfs::StatusToString(status) << "\n";
        return 1;
    }

    std::cout << "[+] NTFS Volume Mounted Successfully!\n"
              << "    Cluster Size:  " << fs.getClusterSize() << " bytes\n"
              << "    Total Records: " << fs.getRecordCount() << "\n"
              << "    Volume Serial: 0x" << std::hex << fs.getVolumeSerialNumber() << std::dec << "\n\n";

    std::cout << "[*] Auditing Master File Table (MFT) records and Update Sequence Arrays...\n";
    std::vector<uint64_t> corruptedRecords;
    fs.scrubMftRecords(corruptedRecords, doRepair);

    if (corruptedRecords.empty()) {
        std::cout << "[+] All MFT records verified. Zero torn writes or USA fixup violations detected.\n"
                  << "    Status: CLEAN\n";
    } else {
        std::cerr << "[!] Warning: Found " << corruptedRecords.size() << " record(s) with integrity anomalies:\n";
        for (uint64_t recNum : corruptedRecords) {
            std::cerr << "    - Record #" << recNum << (doRepair ? " [REPAIRED]" : " [ANOMALY DETECTED]") << "\n";
        }

        if (doRepair) {
            std::cout << "\n[+] Repair completed: Successfully rebuilt Update Sequence Arrays for all anomalous records.\n"
                      << "    Status: HEALED\n";
        } else {
            std::cout << "\n[i] Run with '--repair' or '/f' to automatically repair damaged records.\n"
                      << "    Status: NEEDS REPAIR\n";
            fs.unmount();
            return 2;
        }
    }

    fs.unmount();
    return 0;
}
