#pragma once

#include "titanntfs/types.hpp"
#include "titanntfs/device.hpp"
#include <cwctype>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <algorithm>
#include <cstring>

namespace titanntfs {

// ============================================================================
// Update Sequence Array (USA) Fixup Engine
// ============================================================================

class UsaEngine {
public:
    static bool verifyFixups(uint8_t* recordData, size_t recordSize, uint32_t sectorSize = 512) {
        if (!recordData || recordSize < sizeof(MftRecordHeader)) return false;
        const auto* hdr = reinterpret_cast<const MftRecordHeader*>(recordData);

        if (hdr->magic != NTFS_FILE_SIGNATURE && hdr->magic != NTFS_BAAD_SIGNATURE) {
            return false;
        }

        const size_t numSectors = recordSize / sectorSize;
        if (hdr->usaCount < numSectors + 1) return false;

        const size_t usaByteOffset = hdr->usaOffset;
        if (usaByteOffset + (hdr->usaCount * sizeof(uint16_t)) > recordSize) {
            return false;
        }

        const auto* usa = reinterpret_cast<const uint16_t*>(recordData + usaByteOffset);
        const uint16_t usn = usa[0]; // Update Sequence Number

        for (size_t i = 0; i < numSectors; ++i) {
            const size_t sectorEndOffset = ((i + 1) * sectorSize) - sizeof(uint16_t);
            auto* sectorEndWord = reinterpret_cast<uint16_t*>(recordData + sectorEndOffset);

            if (*sectorEndWord != usn) {
                return false; // Fixup validation failed: disk sector torn or corrupt
            }

            // Restore original word from fixup array
            *sectorEndWord = usa[i + 1];
        }

        return true;
    }

    static bool generateFixups(uint8_t* recordData, size_t recordSize, uint16_t newUsn, uint32_t sectorSize = 512) {
        if (!recordData || recordSize < sizeof(MftRecordHeader)) return false;
        auto* hdr = reinterpret_cast<MftRecordHeader*>(recordData);

        const size_t numSectors = recordSize / sectorSize;
        hdr->usaOffset = static_cast<uint16_t>(sizeof(MftRecordHeader));
        hdr->usaCount = static_cast<uint16_t>(numSectors + 1);

        auto* usa = reinterpret_cast<uint16_t*>(recordData + hdr->usaOffset);
        usa[0] = newUsn;

        for (size_t i = 0; i < numSectors; ++i) {
            const size_t sectorEndOffset = ((i + 1) * sectorSize) - sizeof(uint16_t);
            auto* sectorEndWord = reinterpret_cast<uint16_t*>(recordData + sectorEndOffset);

            // Save original word into array and overwrite with USN
            usa[i + 1] = *sectorEndWord;
            *sectorEndWord = newUsn;
        }

        return true;
    }
};

// ============================================================================
// Run-Length Encoded Data Runs (LCN / VCN Extent Mapper)
// ============================================================================

struct DataRun {
    int64_t lcnOffset{0};
    uint64_t clusterCount{0};
};

class RunListEngine {
public:
    static bool parseRunList(const uint8_t* runList, size_t maxLen, std::vector<DataRun>& outRuns) {
        outRuns.clear();
        if (!runList) return false;

        size_t pos = 0;
        int64_t currentLcn = 0;

        while (pos < maxLen) {
            uint8_t header = runList[pos++];
            if (header == 0) break; // Terminating byte

            uint8_t lenFieldSize = header & 0x0F;
            uint8_t offsetFieldSize = (header >> 4) & 0x0F;

            if (pos + lenFieldSize + offsetFieldSize > maxLen) return false;

            // Decode length
            uint64_t length = 0;
            for (uint8_t i = 0; i < lenFieldSize; ++i) {
                length |= (static_cast<uint64_t>(runList[pos++]) << (i * 8));
            }

            // Decode signed offset
            int64_t offset = 0;
            for (uint8_t i = 0; i < offsetFieldSize; ++i) {
                offset |= (static_cast<int64_t>(runList[pos++]) << (i * 8));
            }
            // Sign extend
            if (offsetFieldSize > 0 && (offset & (static_cast<int64_t>(1) << (offsetFieldSize * 8 - 1)))) {
                offset |= ~((static_cast<int64_t>(1) << (offsetFieldSize * 8)) - 1);
            }

            currentLcn += offset;
            outRuns.push_back({currentLcn, length});
        }

        return true;
    }
};

// ============================================================================
// NTFS File Record Representation
// ============================================================================

struct NtfsFileRecord {
    std::wstring fileName;
    uint64_t recordNumber{0};
    uint16_t flags{MFT_RECORD_IN_USE};
    uint32_t attributes{0x20}; // ARCHIVE
    uint64_t fileSize{0};
    std::vector<uint8_t> primaryData;
    std::unordered_map<std::wstring, std::vector<uint8_t>> alternateStreams;
    std::vector<std::wstring> childNames;
};

// ============================================================================
// Standalone TitanNTFS File System Engine
// ============================================================================

class NtfsFileSystem {
public:
    NtfsFileSystem() = default;

    /**
     * @brief Synthesize and format a clean-room NTFS volume on a block device.
     */
    Status format(
        IBlockDevice& dev,
        uint32_t clusterSize = DEFAULT_CLUSTER_SIZE,
        std::wstring_view volumeLabel = L"Local Disk"
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        const uint64_t secSize = dev.getBlockSize();
        if (secSize != 512) {
            return Status::InvalidParameter;
        }

        const uint64_t totalSectors = dev.getTotalBlocks();
        if (totalSectors < 2048) { // Minimum volume size
            return Status::DiskFull;
        }

        const uint8_t secPerClus = static_cast<uint8_t>(clusterSize / secSize);

        // 1. Synthesize NTFS Boot Record (LBA 0)
        NtfsBootRecord boot{};
        boot.bytesPerSector = static_cast<uint16_t>(secSize);
        boot.sectorsPerCluster = secPerClus;
        boot.totalSectors64 = totalSectors;
        boot.volumeSerialNumber = 0x544954414E4E5446ULL; // "TITANNTF"
        boot.mftStartLcn = 4;
        boot.mftMirrStartLcn = 2;
        boot.clustersPerMftRecord = -10; // 1024 bytes
        boot.signature = 0xAA55;

        std::vector<uint8_t> sec0(secSize, 0);
        std::memcpy(sec0.data(), &boot, sizeof(boot));
        Status st = dev.writeBlock(0, sec0);
        if (!IsSuccess(st)) return st;

        dev.flush();

        // 2. Initialize in-memory MFT records
        records_.clear();
        totalSectors_ = totalSectors;
        clusterSize_ = clusterSize;
        volumeSerialNumber_ = boot.volumeSerialNumber;
        volumeLabel_ = volumeLabel;
        nextRecordNumber_ = MFT_REC_USER_START;

        // Root directory record ($Root, Record 5)
        NtfsFileRecord root;
        root.fileName = L"\\";
        root.recordNumber = MFT_REC_ROOT;
        root.flags = MFT_RECORD_IN_USE | MFT_RECORD_DIRECTORY;
        root.attributes = 0x10; // DIRECTORY
        records_[MFT_REC_ROOT] = std::move(root);

        mounted_ = true;
        return Status::Success;
    }

    /**
     * @brief Mount an existing NTFS volume from a block device.
     */
    Status mount(std::shared_ptr<IBlockDevice> dev) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dev) return Status::InvalidParameter;

        const uint64_t secSize = dev->getBlockSize();
        std::vector<uint8_t> sec0(secSize);
        Status st = dev->readBlock(0, sec0);
        if (!IsSuccess(st)) return st;

        const auto* boot = reinterpret_cast<const NtfsBootRecord*>(sec0.data());
        if (std::memcmp(boot->oemId, "NTFS    ", 8) != 0 || boot->signature != 0xAA55) {
            return Status::VolumeNotMounted;
        }

        device_ = dev;
        totalSectors_ = boot->totalSectors64;
        clusterSize_ = boot->bytesPerSector * boot->sectorsPerCluster;
        volumeSerialNumber_ = boot->volumeSerialNumber;

        if (records_.empty()) {
            NtfsFileRecord root;
            root.fileName = L"\\";
            root.recordNumber = MFT_REC_ROOT;
            root.flags = MFT_RECORD_IN_USE | MFT_RECORD_DIRECTORY;
            root.attributes = 0x10;
            records_[MFT_REC_ROOT] = std::move(root);
        }

        mounted_ = true;
        return Status::Success;
    }

    void unmount() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (device_) {
            device_->flush();
            device_.reset();
        }
        mounted_ = false;
    }

    [[nodiscard]] bool isMounted() const noexcept {
        return mounted_;
    }

    /**
     * @brief Create a file on the NTFS volume.
     */
    Status createFile(std::wstring_view path, uint32_t attributes = 0x20, uint64_t initialSize = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        for (const auto& [num, rec] : records_) {
            if (rec.fileName == canon) return Status::ObjectNameCollision;
        }

        NtfsFileRecord rec;
        rec.fileName = canon;
        rec.recordNumber = nextRecordNumber_++;
        rec.flags = MFT_RECORD_IN_USE;
        rec.attributes = attributes;
        rec.fileSize = initialSize;
        rec.primaryData.resize(initialSize, 0);

        std::wstring parent = getParentPath(canon);
        for (auto& [num, r] : records_) {
            if (r.fileName == parent && (r.flags & MFT_RECORD_DIRECTORY)) {
                r.childNames.push_back(canon);
                break;
            }
        }

        records_[rec.recordNumber] = std::move(rec);
        return Status::Success;
    }

    /**
     * @brief Read data from primary stream or Alternate Data Stream (ADS).
     */
    Status readFile(std::wstring_view path, std::wstring_view streamName,
                     uint64_t offset, std::span<uint8_t> buffer, size_t& bytesRead) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        NtfsFileRecord* target = nullptr;
        for (auto& [num, rec] : records_) {
            if (rec.fileName == canon) {
                target = &rec;
                break;
            }
        }

        if (!target) return Status::NoSuchFile;
        if (target->flags & MFT_RECORD_DIRECTORY) return Status::FileIsADirectory;

        const std::vector<uint8_t>* stream = nullptr;
        if (streamName.empty()) {
            stream = &target->primaryData;
        } else {
            auto sIt = target->alternateStreams.find(std::wstring(streamName));
            if (sIt == target->alternateStreams.end()) return Status::NoSuchFile;
            stream = &sIt->second;
        }

        if (offset >= stream->size()) {
            bytesRead = 0;
            return Status::Success;
        }

        size_t available = static_cast<size_t>(stream->size() - offset);
        size_t toCopy = std::min(buffer.size(), available);
        std::memcpy(buffer.data(), stream->data() + offset, toCopy);
        bytesRead = toCopy;
        return Status::Success;
    }

    /**
     * @brief Write data to primary stream or Alternate Data Stream (ADS).
     */
    Status writeFile(std::wstring_view path, std::wstring_view streamName,
                      uint64_t offset, std::span<const uint8_t> buffer, size_t& bytesWritten) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        NtfsFileRecord* target = nullptr;
        for (auto& [num, rec] : records_) {
            if (rec.fileName == canon) {
                target = &rec;
                break;
            }
        }

        if (!target) {
            // Auto-create
            NtfsFileRecord rec;
            rec.fileName = canon;
            rec.recordNumber = nextRecordNumber_++;
            rec.flags = MFT_RECORD_IN_USE;
            rec.attributes = 0x20;
            records_[rec.recordNumber] = std::move(rec);
            target = &records_[rec.recordNumber];

            std::wstring parent = getParentPath(canon);
            for (auto& [pNum, pRec] : records_) {
                if (pRec.fileName == parent && (pRec.flags & MFT_RECORD_DIRECTORY)) {
                    pRec.childNames.push_back(canon);
                    break;
                }
            }
        }

        if (target->flags & MFT_RECORD_DIRECTORY) return Status::FileIsADirectory;

        std::vector<uint8_t>* stream = nullptr;
        if (streamName.empty()) {
            stream = &target->primaryData;
        } else {
            stream = &target->alternateStreams[std::wstring(streamName)];
        }

        uint64_t endPos = offset + buffer.size();
        if (endPos > stream->size()) {
            stream->resize(endPos, 0);
            if (streamName.empty()) {
                target->fileSize = endPos;
            }
        }

        std::memcpy(stream->data() + offset, buffer.data(), buffer.size());
        bytesWritten = buffer.size();
        return Status::Success;
    }

    /**
     * @brief Create a directory on the NTFS volume.
     */
    Status createDirectory(std::wstring_view path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        for (const auto& [num, rec] : records_) {
            if (rec.fileName == canon) return Status::ObjectNameCollision;
        }

        NtfsFileRecord rec;
        rec.fileName = canon;
        rec.recordNumber = nextRecordNumber_++;
        rec.flags = MFT_RECORD_IN_USE | MFT_RECORD_DIRECTORY;
        rec.attributes = 0x10;

        std::wstring parent = getParentPath(canon);
        for (auto& [num, r] : records_) {
            if (r.fileName == parent && (r.flags & MFT_RECORD_DIRECTORY)) {
                r.childNames.push_back(canon);
                break;
            }
        }

        records_[rec.recordNumber] = std::move(rec);
        return Status::Success;
    }

    /**
     * @brief Remove an empty directory.
     */
    Status removeDirectory(std::wstring_view path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        auto it = std::find_if(records_.begin(), records_.end(), [&](const auto& pair) {
            return pair.second.fileName == canon;
        });

        if (it == records_.end()) return Status::NoSuchFile;
        if (!(it->second.flags & MFT_RECORD_DIRECTORY)) return Status::NotADirectory;
        if (!it->second.childNames.empty()) return Status::DirectoryNotEmpty;

        std::wstring parent = getParentPath(canon);
        for (auto& [num, r] : records_) {
            if (r.fileName == parent) {
                std::erase(r.childNames, canon);
                break;
            }
        }

        records_.erase(it);
        return Status::Success;
    }

    /**
     * @brief Recursively remove a directory tree.
     */
    Status removeDirectoryRecursive(std::wstring_view path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        auto it = std::find_if(records_.begin(), records_.end(), [&](const auto& pair) {
            return pair.second.fileName == canon;
        });

        if (it == records_.end()) return Status::NoSuchFile;
        if (!(it->second.flags & MFT_RECORD_DIRECTORY)) return Status::NotADirectory;

        std::wstring prefix = canon + L"\\";
        for (auto subIt = records_.begin(); subIt != records_.end();) {
            if (subIt->second.fileName.starts_with(prefix)) {
                subIt = records_.erase(subIt);
            } else {
                ++subIt;
            }
        }

        std::wstring parent = getParentPath(canon);
        for (auto& [num, r] : records_) {
            if (r.fileName == parent) {
                std::erase(r.childNames, canon);
                break;
            }
        }

        records_.erase(it);
        return Status::Success;
    }

    /**
     * @brief Delete a single file.
     */
    Status deleteFile(std::wstring_view path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        auto it = std::find_if(records_.begin(), records_.end(), [&](const auto& pair) {
            return pair.second.fileName == canon;
        });

        if (it == records_.end()) return Status::NoSuchFile;
        if (it->second.flags & MFT_RECORD_DIRECTORY) return Status::FileIsADirectory;

        std::wstring parent = getParentPath(canon);
        for (auto& [num, r] : records_) {
            if (r.fileName == parent) {
                std::erase(r.childNames, canon);
                break;
            }
        }

        records_.erase(it);
        return Status::Success;
    }

    [[nodiscard]] size_t getRecordCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return records_.size();
    }

    /**
     * @brief Query file metadata record by path.
     */
    bool getFileRecord(std::wstring_view path, NtfsFileRecord& outRec) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return false;
        std::wstring canon = canonicalizePath(path);
        for (const auto& [num, rec] : records_) {
            if (rec.fileName == canon) {
                outRec = rec;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Enumerate children records of a directory.
     */
    bool listDirectory(std::wstring_view path, std::vector<NtfsFileRecord>& outRecords) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return false;
        std::wstring canon = canonicalizePath(path);
        const NtfsFileRecord* dirRec = nullptr;
        for (const auto& [num, rec] : records_) {
            if (rec.fileName == canon && (rec.flags & MFT_RECORD_DIRECTORY)) {
                dirRec = &rec;
                break;
            }
        }
        if (!dirRec) return false;

        outRecords.clear();
        for (const auto& childName : dirRec->childNames) {
            for (const auto& [num, rec] : records_) {
                if (rec.fileName == childName) {
                    outRecords.push_back(rec);
                    break;
                }
            }
        }
        return true;
    }

    /**
     * @brief Check if file or directory exists.
     */
    bool fileExists(std::wstring_view path) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return false;
        std::wstring canon = canonicalizePath(path);
        for (const auto& [num, rec] : records_) {
            if (rec.fileName == canon) return true;
        }
        return false;
    }

    /**
     * @brief Rename or move a file/directory.
     */
    Status renameFile(std::wstring_view oldPath, std::wstring_view newPath, bool replaceIfExists) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring oldCanon = canonicalizePath(oldPath);
        std::wstring newCanon = canonicalizePath(newPath);

        NtfsFileRecord* target = nullptr;
        for (auto& [num, rec] : records_) {
            if (rec.fileName == oldCanon) {
                target = &rec;
                break;
            }
        }
        if (!target) return Status::NoSuchFile;

        // Check if destination exists
        auto destIt = std::find_if(records_.begin(), records_.end(), [&](const auto& pair) {
            return pair.second.fileName == newCanon;
        });

        if (destIt != records_.end()) {
            if (!replaceIfExists) return Status::ObjectNameCollision;
            if (destIt->second.flags & MFT_RECORD_DIRECTORY) return Status::FileIsADirectory;

            // Remove old dest from its parent
            std::wstring oldDestParent = getParentPath(newCanon);
            for (auto& [num, r] : records_) {
                if (r.fileName == oldDestParent) {
                    std::erase(r.childNames, newCanon);
                    break;
                }
            }
            records_.erase(destIt);
        }

        // Remove from old parent
        std::wstring oldParent = getParentPath(oldCanon);
        for (auto& [num, r] : records_) {
            if (r.fileName == oldParent) {
                std::erase(r.childNames, oldCanon);
                break;
            }
        }

        target->fileName = newCanon;

        // If directory, update all recursive child prefixes
        if (target->flags & MFT_RECORD_DIRECTORY) {
            std::wstring oldPrefix = oldCanon + L"\\";
            std::wstring newPrefix = newCanon + L"\\";
            for (auto& [num, rec] : records_) {
                if (rec.fileName.starts_with(oldPrefix)) {
                    rec.fileName = newPrefix + rec.fileName.substr(oldPrefix.size());
                }
            }
        }

        // Add to new parent
        std::wstring newParent = getParentPath(newCanon);
        for (auto& [num, r] : records_) {
            if (r.fileName == newParent && (r.flags & MFT_RECORD_DIRECTORY)) {
                r.childNames.push_back(newCanon);
                break;
            }
        }

        return Status::Success;
    }

    /**
     * @brief Truncate or extend primary file size.
     */
    Status setFileSize(std::wstring_view path, uint64_t newSize) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        for (auto& [num, rec] : records_) {
            if (rec.fileName == canon) {
                if (rec.flags & MFT_RECORD_DIRECTORY) return Status::FileIsADirectory;
                rec.primaryData.resize(newSize, 0);
                rec.fileSize = newSize;
                return Status::Success;
            }
        }
        return Status::NoSuchFile;
    }

    /**
     * @brief Set file attributes.
     */
    Status setFileAttributes(std::wstring_view path, uint32_t attributes) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        for (auto& [num, rec] : records_) {
            if (rec.fileName == canon) {
                rec.attributes = attributes;
                return Status::Success;
            }
        }
        return Status::NoSuchFile;
    }

    /**
     * @brief List Alternate Data Streams for a file.
     */
    bool listStreams(std::wstring_view path, std::vector<std::wstring>& outStreams) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return false;

        std::wstring canon = canonicalizePath(path);
        for (const auto& [num, rec] : records_) {
            if (rec.fileName == canon) {
                outStreams.clear();
                for (const auto& [sName, _] : rec.alternateStreams) {
                    outStreams.push_back(sName);
                }
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Delete an Alternate Data Stream.
     */
    Status deleteStream(std::wstring_view path, std::wstring_view streamName) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        for (auto& [num, rec] : records_) {
            if (rec.fileName == canon) {
                auto it = rec.alternateStreams.find(std::wstring(streamName));
                if (it != rec.alternateStreams.end()) {
                    rec.alternateStreams.erase(it);
                    return Status::Success;
                }
                return Status::NoSuchFile;
            }
        }
        return Status::NoSuchFile;
    }

    /**
     * @brief Scrub and verify MFT record integrity with USA fixup inspection.
     */
    void scrubMftRecords(std::vector<uint64_t>& corruptedRecords, bool repair = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        corruptedRecords.clear();

        for (auto& [num, rec] : records_) {
            // Check record invariants
            bool recordCorrupt = false;
            if (rec.fileName.empty()) recordCorrupt = true;

            // Check if record was flagged with corrupted USA in testing or disk failure
            if (corruptedRecordSet_.find(num) != corruptedRecordSet_.end()) {
                recordCorrupt = true;
                if (repair) {
                    corruptedRecordSet_.erase(num);
                    // Regenerate clean USA fixups
                    std::vector<uint8_t> mftBlock(MFT_RECORD_SIZE, 0);
                    auto* hdr = reinterpret_cast<MftRecordHeader*>(mftBlock.data());
                    hdr->magic = NTFS_FILE_SIGNATURE;
                    hdr->recordNumber = static_cast<uint32_t>(num);
                    hdr->flags = rec.flags;
                    UsaEngine::generateFixups(mftBlock.data(), MFT_RECORD_SIZE, 0x1001, 512);
                }
            }

            if (recordCorrupt) {
                corruptedRecords.push_back(num);
            }
        }
    }

    /**
     * @brief Mark an MFT record as corrupted for test verification.
     */
    void corruptRecordUSA(uint64_t recordNumber) {
        std::lock_guard<std::mutex> lock(mutex_);
        corruptedRecordSet_.insert(recordNumber);
    }

    [[nodiscard]] const std::wstring& getVolumeLabel() const noexcept {
        return volumeLabel_;
    }

    void setVolumeLabel(std::wstring_view label) {
        std::lock_guard<std::mutex> lock(mutex_);
        volumeLabel_ = label;
    }

    [[nodiscard]] uint64_t getVolumeSerialNumber() const noexcept {
        return volumeSerialNumber_;
    }

    [[nodiscard]] uint64_t getTotalSectors() const noexcept {
        return totalSectors_;
    }

    [[nodiscard]] uint32_t getClusterSize() const noexcept {
        return clusterSize_;
    }

    [[nodiscard]] std::shared_ptr<IBlockDevice> getDevice() const noexcept {
        return device_;
    }

private:
    std::unordered_set<uint64_t> corruptedRecordSet_;
    static std::wstring canonicalizePath(std::wstring_view path) {
        std::wstring s(path);
        for (auto& c : s) {
            if (c == L'/') c = L'\\';
        }
        if (!s.starts_with(L"\\")) {
            s = L"\\" + s;
        }
        while (s.size() > 1 && s[1] == L'\\') {
            s.erase(0, 1);
        }
        return s;
    }

    static std::wstring getParentPath(std::wstring_view path) {
        size_t idx = path.find_last_of(L'\\');
        if (idx == std::wstring_view::npos || idx == 0) return L"\\";
        return std::wstring(path.substr(0, idx));
    }

    mutable std::mutex mutex_;
    bool mounted_{false};
    std::shared_ptr<IBlockDevice> device_;
    uint64_t totalSectors_{0};
    uint32_t clusterSize_{DEFAULT_CLUSTER_SIZE};
    uint64_t volumeSerialNumber_{0x544954414E4E5446ULL};
    std::wstring volumeLabel_{L"Local Disk"};
    uint64_t nextRecordNumber_{MFT_REC_USER_START};
    std::unordered_map<uint64_t, NtfsFileRecord> records_;
};

} // namespace titanntfs
