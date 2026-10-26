#include "titanntfs/ntfs.hpp"
#include "titanntfs/compression.hpp"
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

void Test_Extended_File_Ops() {
    std::cout << "[*] Running Test_Extended_File_Ops...\n";
    auto dev = std::make_shared<titanntfs::MemoryBlockDevice>("RamDisk", 32768, 512);
    titanntfs::NtfsFileSystem fs;
    fs.format(*dev, 4096, L"EXT_TEST");

    fs.createDirectory(L"\\FolderA");
    fs.createFile(L"\\FolderA\\doc.txt", 0x20, 128);

    // List directory
    std::vector<titanntfs::NtfsFileRecord> records;
    ASSERT_TRUE(fs.listDirectory(L"\\FolderA", records), "listDirectory must succeed");
    ASSERT_EQ(records.size(), 1u, "FolderA must have 1 file");

    // Rename file
    auto st = fs.renameFile(L"\\FolderA\\doc.txt", L"\\FolderA\\doc_renamed.txt", false);
    ASSERT_TRUE(titanntfs::IsSuccess(st), "renameFile must succeed");
    ASSERT_TRUE(fs.fileExists(L"\\FolderA\\doc_renamed.txt"), "doc_renamed.txt must exist");
    ASSERT_TRUE(!fs.fileExists(L"\\FolderA\\doc.txt"), "doc.txt must not exist");

    // Resize file
    st = fs.setFileSize(L"\\FolderA\\doc_renamed.txt", 256);
    ASSERT_TRUE(titanntfs::IsSuccess(st), "setFileSize must succeed");
    titanntfs::NtfsFileRecord rec;
    ASSERT_TRUE(fs.getFileRecord(L"\\FolderA\\doc_renamed.txt", rec), "getFileRecord must succeed");
    ASSERT_EQ(rec.fileSize, 256u, "File size must be 256");

    // Set attributes
    st = fs.setFileAttributes(L"\\FolderA\\doc_renamed.txt", 0x01 /* READONLY */);
    ASSERT_TRUE(titanntfs::IsSuccess(st), "setFileAttributes must succeed");
    ASSERT_TRUE(fs.getFileRecord(L"\\FolderA\\doc_renamed.txt", rec), "getFileRecord must succeed");
    ASSERT_EQ(rec.attributes, 0x01u, "Attributes must be READONLY");

    std::cout << "    [PASS] Extended operations (list, rename, resize, attributes) verified\n";
}

void Test_Alternate_Data_Streams_Enumeration_And_Deletion() {
    std::cout << "[*] Running Test_Alternate_Data_Streams_Enumeration_And_Deletion...\n";
    auto dev = std::make_shared<titanntfs::MemoryBlockDevice>("RamDisk", 32768, 512);
    titanntfs::NtfsFileSystem fs;
    fs.format(*dev, 4096, L"ADS_ENUM_TEST");

    fs.createFile(L"\\app.exe", 0x20, 0);

    // Add multiple ADS
    std::string zoneInfo = "[ZoneTransfer]\r\nZoneId=3\r\n";
    std::string metadata = "{\"verified\": true, \"signer\": \"Daniel Elliott\"}";
    size_t written = 0;
    fs.writeFile(L"\\app.exe", L":Zone.Identifier", 0, {reinterpret_cast<const uint8_t*>(zoneInfo.data()), zoneInfo.size()}, written);
    fs.writeFile(L"\\app.exe", L":SignatureMeta", 0, {reinterpret_cast<const uint8_t*>(metadata.data()), metadata.size()}, written);

    std::vector<std::wstring> streams;
    ASSERT_TRUE(fs.listStreams(L"\\app.exe", streams), "listStreams must succeed");
    ASSERT_EQ(streams.size(), 2u, "File must have 2 Alternate Data Streams");

    // Delete one stream
    auto st = fs.deleteStream(L"\\app.exe", L":Zone.Identifier");
    ASSERT_TRUE(titanntfs::IsSuccess(st), "deleteStream must succeed");

    streams.clear();
    ASSERT_TRUE(fs.listStreams(L"\\app.exe", streams), "listStreams must succeed");
    ASSERT_EQ(streams.size(), 1u, "File must now have 1 Alternate Data Stream");
    ASSERT_TRUE(streams[0] == L":SignatureMeta", "Remaining stream must match :SignatureMeta");

    std::cout << "    [PASS] Alternate Data Streams (ADS) enumeration and deletion verified\n";
}

void Test_MFT_Scrubbing_And_Repair() {
    std::cout << "[*] Running Test_MFT_Scrubbing_And_Repair...\n";
    auto dev = std::make_shared<titanntfs::MemoryBlockDevice>("RamDisk", 32768, 512);
    titanntfs::NtfsFileSystem fs;
    fs.format(*dev, 4096, L"SCRUB_TEST");

    fs.createFile(L"\\file1.bin", 0x20, 100);
    fs.createFile(L"\\file2.bin", 0x20, 200);

    // Initially clean
    std::vector<uint64_t> corrupted;
    fs.scrubMftRecords(corrupted, false);
    ASSERT_TRUE(corrupted.empty(), "Initial volume must have 0 corrupted records");

    // Corrupt record #16
    fs.corruptRecordUSA(16);

    // Check without repair
    fs.scrubMftRecords(corrupted, false);
    ASSERT_EQ(corrupted.size(), 1u, "Must detect 1 corrupted record");
    ASSERT_EQ(corrupted[0], 16u, "Corrupted record must be #16");

    // Check with repair
    fs.scrubMftRecords(corrupted, true);
    ASSERT_EQ(corrupted.size(), 1u, "Must report record #16 repaired");

    // Verify clean after repair
    corrupted.clear();
    fs.scrubMftRecords(corrupted, false);
    ASSERT_TRUE(corrupted.empty(), "Volume must be completely clean after automated repair");

    std::cout << "    [PASS] MFT integrity scrubbing and self-healing repair verified\n";
}

void Test_Compression_LZ4_And_RLE() {
    std::cout << "[*] Running Test_Compression_LZ4_And_RLE...\n";
    // 1. Test RLE on zero block
    std::vector<uint8_t> zeros(65536, 0);
    std::vector<uint8_t> rleCompressed;
    size_t rleSz = titanntfs::CompressionEngine::CompressRLE(zeros, rleCompressed);
    ASSERT_EQ(rleSz, 9u, "All-zero RLE block must compress to 9 bytes");

    std::vector<uint8_t> rleDecomp(65536, 0xFF);
    bool ok = titanntfs::CompressionEngine::DecompressRLE(rleCompressed, rleDecomp);
    ASSERT_TRUE(ok, "RLE decompression must succeed");
    ASSERT_TRUE(titanntfs::CompressionEngine::IsAllZero(rleDecomp), "Decompressed RLE block must be all zeros");

    // 2. Test LZ4 on repetitive text data
    std::string sample = "TitanNTFS clean-room native 1024-byte MFT record architecture with USA fixups! ";
    std::vector<uint8_t> uncompressed;
    for (int i = 0; i < 200; ++i) {
        uncompressed.insert(uncompressed.end(), sample.begin(), sample.end());
    }

    std::vector<uint8_t> lz4Compressed;
    size_t cSz = titanntfs::CompressionEngine::CompressLZ4(uncompressed, lz4Compressed);
    ASSERT_TRUE(cSz > 0 && cSz < uncompressed.size() / 5, "LZ4 compression on repetitive data must achieve >80% reduction");

    std::vector<uint8_t> lz4Decomp(uncompressed.size(), 0);
    ok = titanntfs::CompressionEngine::DecompressLZ4(lz4Compressed, lz4Decomp);
    ASSERT_TRUE(ok, "LZ4 decompression must succeed");
    ASSERT_TRUE(lz4Decomp == uncompressed, "Decompressed data must match original uncompressed data exactly");

    std::cout << "    [PASS] Clean-room LZ4 and RLE compression verified\n";
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
    Test_Extended_File_Ops();
    Test_Alternate_Data_Streams_Enumeration_And_Deletion();
    Test_MFT_Scrubbing_And_Repair();
    Test_Compression_LZ4_And_RLE();

    std::cout << "========================================================\n"
              << "[+] All TitanNTFS unit tests passed successfully (100% Green)\n"
              << "========================================================\n";
    return 0;
}
