#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <array>
#include <algorithm>

namespace titanntfs {

// ============================================================================
// Status Codes (Freestanding & NT Compatible)
// ============================================================================

enum class Status : uint32_t {
    Success                  = 0x00000000,
    NoSuchDevice             = 0xC000000E,
    NoSuchFile               = 0xC000000F,
    AccessDenied             = 0xC0000022,
    BufferTooSmall           = 0xC0000023,
    ObjectNameNotFound       = 0xC0000034,
    ObjectNameCollision      = 0xC0000035,
    DataChecksumError        = 0xC00002EB,
    DeviceError              = 0xC000009C,
    DiskFull                 = 0xC000007F,
    VolumeNotMounted         = 0xC00000A1,
    MediaWriteProtected      = 0xC00000A2,
    InvalidParameter         = 0xC000000D,
    NotImplemented           = 0xC0000002,
    DirectoryNotEmpty        = 0xC0000101,
    FileIsADirectory         = 0xC00000BA,
    NotADirectory            = 0xC0000103,
    CorruptVolume            = 0xC0000102
};

[[nodiscard]] inline constexpr bool IsSuccess(Status status) noexcept {
    return status == Status::Success;
}

[[nodiscard]] inline const char* StatusToString(Status s) noexcept {
    switch (s) {
        case Status::Success:             return "STATUS_SUCCESS";
        case Status::NoSuchDevice:        return "STATUS_NO_SUCH_DEVICE";
        case Status::NoSuchFile:          return "STATUS_NO_SUCH_FILE";
        case Status::AccessDenied:        return "STATUS_ACCESS_DENIED";
        case Status::BufferTooSmall:      return "STATUS_BUFFER_TOO_SMALL";
        case Status::ObjectNameNotFound:  return "STATUS_OBJECT_NAME_NOT_FOUND";
        case Status::ObjectNameCollision: return "STATUS_OBJECT_NAME_COLLISION";
        case Status::DataChecksumError:   return "STATUS_DATA_CHECKSUM_ERROR";
        case Status::DeviceError:         return "STATUS_DEVICE_ERROR";
        case Status::DiskFull:            return "STATUS_DISK_FULL";
        case Status::VolumeNotMounted:    return "STATUS_VOLUME_NOT_MOUNTED";
        case Status::MediaWriteProtected: return "STATUS_MEDIA_WRITE_PROTECTED";
        case Status::InvalidParameter:    return "STATUS_INVALID_PARAMETER";
        case Status::DirectoryNotEmpty:   return "STATUS_DIRECTORY_NOT_EMPTY";
        case Status::FileIsADirectory:    return "STATUS_FILE_IS_A_DIRECTORY";
        case Status::NotADirectory:       return "STATUS_NOT_A_DIRECTORY";
        case Status::CorruptVolume:       return "STATUS_FILE_CORRUPT_ERROR";
        default:                          return "STATUS_UNSUCCESSFUL";
    }
}

// ============================================================================
// NTFS Constants & Signatures
// ============================================================================

inline constexpr uint32_t NTFS_FILE_SIGNATURE = 0x454C4946; // "FILE"
inline constexpr uint32_t NTFS_BAAD_SIGNATURE = 0x44414142; // "BAAD"
inline constexpr uint32_t NTFS_INDX_SIGNATURE = 0x58444E49; // "INDX"
inline constexpr uint32_t NTFS_RSTR_SIGNATURE = 0x52545352; // "RSTR"
inline constexpr uint32_t NTFS_RCRD_SIGNATURE = 0x44524352; // "RCRD"
inline constexpr uint32_t NTFS_CHKP_SIGNATURE = 0x504B4843; // "CHKP"

// NTFS Attribute Types
inline constexpr uint32_t ATTR_STANDARD_INFORMATION = 0x10;
inline constexpr uint32_t ATTR_ATTRIBUTE_LIST        = 0x20;
inline constexpr uint32_t ATTR_FILE_NAME             = 0x30;
inline constexpr uint32_t ATTR_OBJECT_ID             = 0x40;
inline constexpr uint32_t ATTR_SECURITY_DESCRIPTOR   = 0x50;
inline constexpr uint32_t ATTR_VOLUME_NAME           = 0x60;
inline constexpr uint32_t ATTR_VOLUME_INFORMATION    = 0x70;
inline constexpr uint32_t ATTR_DATA                  = 0x80;
inline constexpr uint32_t ATTR_INDEX_ROOT            = 0x90;
inline constexpr uint32_t ATTR_INDEX_ALLOCATION      = 0xA0;
inline constexpr uint32_t ATTR_BITMAP                = 0xB0;
inline constexpr uint32_t ATTR_END_MARKER            = 0xFFFFFFFF;

// MFT Record Flags
inline constexpr uint16_t MFT_RECORD_IN_USE          = 0x0001;
inline constexpr uint16_t MFT_RECORD_DIRECTORY       = 0x0002;

// Standard Dimensions
inline constexpr uint32_t MFT_RECORD_SIZE            = 1024;
inline constexpr uint32_t NTFS_SECTOR_SIZE           = 512;
inline constexpr uint32_t DEFAULT_CLUSTER_SIZE       = 4096;

// Reserved System MFT Record Numbers
inline constexpr uint64_t MFT_REC_MFT                = 0;
inline constexpr uint64_t MFT_REC_MFTMIRR            = 1;
inline constexpr uint64_t MFT_REC_LOGFILE            = 2;
inline constexpr uint64_t MFT_REC_VOLUME             = 3;
inline constexpr uint64_t MFT_REC_ATTRDEF            = 4;
inline constexpr uint64_t MFT_REC_ROOT               = 5;
inline constexpr uint64_t MFT_REC_BITMAP             = 6;
inline constexpr uint64_t MFT_REC_BOOT               = 7;
inline constexpr uint64_t MFT_REC_BADCLUST           = 8;
inline constexpr uint64_t MFT_REC_SECURE             = 9;
inline constexpr uint64_t MFT_REC_UPCASE             = 10;
inline constexpr uint64_t MFT_REC_EXTEND             = 11;
inline constexpr uint64_t MFT_REC_USER_START         = 16;

#pragma pack(push, 1)

/**
 * @brief NTFS Volume Boot Record ($Boot, Sector 0).
 */
struct NtfsBootRecord {
    uint8_t  jmpBoot[3]{0xEB, 0x52, 0x90};
    char     oemId[8]{'N', 'T', 'F', 'S', ' ', ' ', ' ', ' '};
    uint16_t bytesPerSector{512};
    uint8_t  sectorsPerCluster{8}; // 4096 bytes per cluster
    uint16_t reservedSectors{0};
    uint8_t  fats{0};
    uint16_t rootEntries{0};
    uint16_t totalSectors16{0};
    uint8_t  media{0xF8};
    uint16_t sectorsPerFat16{0};
    uint16_t sectorsPerTrack{63};
    uint16_t numHeads{255};
    uint32_t hiddenSectors{0};
    uint32_t totalSectors32{0};
    uint8_t  unused[4]{0x80, 0x00, 0x80, 0x00};
    uint64_t totalSectors64{0};
    uint64_t mftStartLcn{4};
    uint64_t mftMirrStartLcn{2};
    int8_t   clustersPerMftRecord{-10}; // 2^10 = 1024 bytes
    uint8_t  reserved1[3]{0};
    int8_t   clustersPerIndexBlock{1};
    uint8_t  reserved2[3]{0};
    uint64_t volumeSerialNumber{0x1337BEEFCAFE0001ULL};
    uint32_t checksum{0};
    uint8_t  bootCode[426]{0};
    uint16_t signature{0xAA55};
};
static_assert(sizeof(NtfsBootRecord) == 512, "NtfsBootRecord must be 512 bytes");

/**
 * @brief MFT Record Header (1024 bytes total record).
 */
struct MftRecordHeader {
    uint32_t magic{NTFS_FILE_SIGNATURE}; // "FILE"
    uint16_t usaOffset{48};
    uint16_t usaCount{3};                // 1 sequence number + 2 sector fixups
    uint64_t lsn{0};
    uint16_t sequenceNumber{1};
    uint16_t hardLinkCount{1};
    uint16_t firstAttributeOffset{56};
    uint16_t flags{MFT_RECORD_IN_USE};
    uint32_t bytesInUse{0};
    uint32_t bytesAllocated{MFT_RECORD_SIZE};
    uint64_t baseFileRecord{0};
    uint16_t nextAttributeId{0};
    uint16_t alignPad{0};
    uint32_t recordNumber{0};
};

/**
 * @brief Common Header for all NTFS Attributes.
 */
struct AttributeHeader {
    uint32_t type{0};
    uint32_t length{0};
    uint8_t  nonResident{0};
    uint8_t  nameLength{0};
    uint16_t nameOffset{0};
    uint16_t flags{0};
    uint16_t attributeId{0};
};

/**
 * @brief Resident Attribute Specifics.
 */
struct ResidentAttributeHeader : AttributeHeader {
    uint32_t valueLength{0};
    uint16_t valueOffset{0};
    uint8_t  indexedFlag{0};
    uint8_t  reserved{0};
};

/**
 * @brief Non-Resident Attribute Specifics.
 */
struct NonResidentAttributeHeader : AttributeHeader {
    uint64_t startingVcn{0};
    uint64_t endingVcn{0};
    uint16_t runListOffset{0};
    uint16_t compressionUnitSize{0};
    uint32_t reserved{0};
    uint64_t allocatedSize{0};
    uint64_t dataSize{0};
    uint64_t initializedSize{0};
};

#pragma pack(pop)

} // namespace titanntfs
