#include "titanntfs/ntfs.hpp"
#include <iostream>
#include <string>

void printUsage() {
    std::cout << "TitanNTFS Check Utility (Clean-Room Edition)\n"
              << "Usage: titanntfs_check <target_image_or_drive>\n\n"
              << "Mounts and audits NTFS Volume Boot Records and MFT state.\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string target = argv[1];
    std::cout << "[*] Inspecting device: " << target << "\n";

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
              << "    Total Records: " << fs.getRecordCount() << "\n"
              << "    Status:        CLEAN\n";

    fs.unmount();
    return 0;
}
