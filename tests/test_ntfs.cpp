#include "titanntfs/ntfs.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <cstring>

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "[FAIL] " << msg << " (" << #a << " != " << #b << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

void Test_USA_Fixups() {
    std::cout << "[*] Running Test_USA_Fixups...\n";
    std::vector<uint8_t> record(1024, 0);
    auto* hdr = reinterpret_cast<titanntfs::MftRecordHeader*>(record.data());
    hdr->magic = titanntfs::NTFS_FILE_SIGNATURE;

    // Generate fixups
    bool ok = titanntfs::UsaEngine::generateFixups(record.data(), 1024, 0x5A5A, 512);
    ASSERT_TRUE(ok, "generateFixups must succeed");

    // Check sector end words match USN
    uint16_t end1 = *reinterpret_cast<uint16_t*>(record.data() + 510);
    uint16_t end2 = *reinterpret_cast<uint16_t*>(record.data() + 1022);
    ASSERT_EQ(end1, 0x5A5A, "Sector 1 end word must match USN");
    ASSERT_EQ(end2, 0x5A5A, "Sector 2 end word must match USN");

    // Verify and restore
    ok = titanntfs::UsaEngine::verifyFixups(record.data(), 1024, 512);
    ASSERT_TRUE(ok, "verifyFixups must succeed");
    std::cout << "    [PASS] USA Fixup generation and verification verified\n";
}

void Test_RunList_Decoding() {
    std::cout << "[*] Running Test_RunList_Decoding...\n";
    // Encoded run: header 0x21 (1 byte len, 2 bytes offset), length = 8, offset = 100
    // Header 0x00 terminating
    std::vector<uint8_t> runs = {0x21, 0x08, 0x64, 0x00, 0x00};
    std::vector<titanntfs::DataRun> decoded;
    bool ok = titanntfs::RunListEngine::parseRunList(runs.data(), runs.size(), decoded);
    ASSERT_TRUE(ok, "parseRunList must succeed");
    ASSERT_EQ(decoded.size(), 1u, "Must decode 1 run");
    ASSERT_EQ(decoded[0].clusterCount, 8u, "Cluster count must be 8");
    ASSERT_EQ(decoded[0].lcnOffset, 100, "LCN offset must be 100");
    std::cout << "    [PASS] Run-Length data run decoding verified\n";
}

void Test_NTFS_Format_And_Mount() {
    std::cout << "[*] Running Test_NTFS_Format_And_Mount...\n";
    auto dev = std::make_shared<titanntfs::MemoryBlockDevice>("RamDisk", 32768, 512); // 16 MB
    titanntfs::NtfsFileSystem fs;

    auto st = fs.format(*dev, 4096, L"TEST_NTFS");
    ASSERT_TRUE(titanntfs::IsSuccess(st), "Formatting must succeed");
    ASSERT_TRUE(fs.getRecordCount() >= 1, "Root record must exist");

    fs.unmount();
    ASSERT_TRUE(!fs.isMounted(), "Volume must be unmounted");

    // Re-mount
    st = fs.mount(dev);
    ASSERT_TRUE(titanntfs::IsSuccess(st), "Re-mounting formatted volume must succeed");
    std::cout << "    [PASS] NTFS volume formatted and remounted successfully\n";
}

void Test_NTFS_File_And_AlternateDataStreams() {
    std::cout << "[*] Running Test_NTFS_File_And_AlternateDataStreams...\n";
    auto dev = std::make_shared<titanntfs::MemoryBlockDevice>("RamDisk", 32768, 512);
    titanntfs::NtfsFileSystem fs;
    fs.format(*dev, 4096, L"ADS_TEST");

    // Write primary stream
    std::string primaryMsg = "Hello World primary stream!";
    size_t written = 0;
    auto st = fs.writeFile(L"\\test.txt", L"", 0, {reinterpret_cast<const uint8_t*>(primaryMsg.data()), primaryMsg.size()}, written);
    ASSERT_TRUE(titanntfs::IsSuccess(st), "writeFile primary stream must succeed");

    // Write Alternate Data Stream (Zone.Identifier)
    std::string adsMsg = "[ZoneTransfer]\r\nZoneId=3\r\n";
    st = fs.writeFile(L"\\test.txt", L":Zone.Identifier", 0, {reinterpret_cast<const uint8_t*>(adsMsg.data()), adsMsg.size()}, written);
    ASSERT_TRUE(titanntfs::IsSuccess(st), "writeFile ADS must succeed");

    // Read back primary stream
    std::vector<uint8_t> readPrimary(primaryMsg.size());
    size_t bytesRead = 0;
    st = fs.readFile(L"\\test.txt", L"", 0, readPrimary, bytesRead);
    ASSERT_TRUE(titanntfs::IsSuccess(st), "readFile primary stream must succeed");
    ASSERT_EQ(std::string(readPrimary.begin(), readPrimary.end()), primaryMsg, "Primary stream content mismatch");

    // Read back ADS
    std::vector<uint8_t> readAds(adsMsg.size());
    st = fs.readFile(L"\\test.txt", L":Zone.Identifier", 0, readAds, bytesRead);
    ASSERT_TRUE(titanntfs::IsSuccess(st), "readFile ADS must succeed");
    ASSERT_EQ(std::string(readAds.begin(), readAds.end()), adsMsg, "ADS stream content mismatch");
    std::cout << "    [PASS] File I/O and Alternate Data Streams (ADS) verified\n";
}

void Test_NTFS_Directory_Lifecycle() {
    std::cout << "[*] Running Test_NTFS_Directory_Lifecycle...\n";
    auto dev = std::make_shared<titanntfs::MemoryBlockDevice>("RamDisk", 32768, 512);
    titanntfs::NtfsFileSystem fs;
    fs.format(*dev, 4096, L"DIR_TEST");

    auto st = fs.createDirectory(L"\\Projects");
    ASSERT_TRUE(titanntfs::IsSuccess(st), "createDirectory must succeed");

    st = fs.createFile(L"\\Projects\\code.cpp", 0x20, 0);
    ASSERT_TRUE(titanntfs::IsSuccess(st), "createFile inside directory must succeed");

    // Non-empty directory removal must fail
    st = fs.removeDirectory(L"\\Projects");
    ASSERT_EQ(st, titanntfs::Status::DirectoryNotEmpty, "removeDirectory on non-empty must fail");

    // Recursive removal must succeed
    st = fs.removeDirectoryRecursive(L"\\Projects");
    ASSERT_TRUE(titanntfs::IsSuccess(st), "removeDirectoryRecursive must succeed");
    std::cout << "    [PASS] Directory lifecycle and recursive deletion verified\n";
}

int main() {
    std::cout << "========================================================\n"
              << "TitanNTFS Clean-Room Verification Suite\n"
              << "========================================================\n";

    Test_USA_Fixups();
    Test_RunList_Decoding();
    Test_NTFS_Format_And_Mount();
    Test_NTFS_File_And_AlternateDataStreams();
    Test_NTFS_Directory_Lifecycle();

    std::cout << "========================================================\n"
              << "[+] All TitanNTFS unit tests passed successfully (100% Green)\n"
              << "========================================================\n";
    return 0;
}
