#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winternl.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cstdint>
#include "titanntfs/types.hpp"
#include "titanntfs/ntfs.hpp"

namespace titanntfs {

// ============================================================================
// NTSTATUS Codes
// ============================================================================

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS                   ((NTSTATUS)0x00000000L)
#define STATUS_OBJECT_NAME_NOT_FOUND     ((NTSTATUS)0xC0000034L)
#define STATUS_OBJECT_NAME_COLLISION     ((NTSTATUS)0xC0000035L)
#define STATUS_ACCESS_DENIED             ((NTSTATUS)0xC0000022L)
#define STATUS_END_OF_FILE               ((NTSTATUS)0xC0000011L)
#define STATUS_BUFFER_TOO_SMALL          ((NTSTATUS)0xC0000023L)
#define STATUS_MEDIA_WRITE_PROTECTED     ((NTSTATUS)0xC00000A2L)
#define STATUS_FILE_IS_A_DIRECTORY       ((NTSTATUS)0xC00000BAL)
#define STATUS_NOT_A_DIRECTORY           ((NTSTATUS)0xC0000103L)
#define STATUS_DIRECTORY_NOT_EMPTY       ((NTSTATUS)0xC0000101L)
#endif

#pragma pack(push, 1)

struct FSP_FSCTL_VOLUME_INFO {
    UINT64 TotalSize;
    UINT64 FreeSize;
    UINT16 VolumeLabelLength;
    WCHAR  VolumeLabel[32];
};

struct FSP_FSCTL_FILE_INFO {
    UINT32 FileAttributes;
    UINT32 ReparseTag;
    UINT64 AllocationSize;
    UINT64 FileSize;
    UINT64 CreationTime;
    UINT64 LastAccessTime;
    UINT64 LastWriteTime;
    UINT64 ChangeTime;
    UINT64 IndexNumber;
    UINT32 HardLinks;
    UINT32 EaSize;
};

struct FSP_FSCTL_DIR_INFO {
    UINT16 Size;
    FSP_FSCTL_FILE_INFO FileInfo;
    union {
        UINT64 NextOffset;
        UINT8  Padding[24];
    };
    WCHAR  FileNameBuf[1];
};

struct FSP_FSCTL_VOLUME_PARAMS {
    UINT16 Version;
    UINT16 SectorSize;
    UINT16 SectorsPerAllocationUnit;
    UINT16 MaxComponentLength;
    UINT64 VolumeCreationTime;
    UINT32 VolumeSerialNumber;
    UINT32 TransactTimeout;
    UINT32 IrpTimeout;
    UINT32 IrpCapacity;
    UINT32 FileInfoTimeout;
    UINT32 Flags;
    WCHAR  FileSystemName[16];
    UINT8  Reserved[432];
};
static_assert(sizeof(FSP_FSCTL_VOLUME_PARAMS) == 504, "FSP_FSCTL_VOLUME_PARAMS must be exactly 504 bytes");

#pragma pack(pop)

struct _FSP_FILE_SYSTEM {
    PVOID UserContext;
};
typedef struct _FSP_FILE_SYSTEM FSP_FILE_SYSTEM;

struct FSP_FILE_SYSTEM_INTERFACE {
    NTSTATUS (*GetVolumeInfo)(FSP_FILE_SYSTEM *FileSystem, FSP_FSCTL_VOLUME_INFO *VolumeInfo);
    NTSTATUS (*SetVolumeLabel)(FSP_FILE_SYSTEM *FileSystem, PWSTR VolumeLabel, FSP_FSCTL_VOLUME_INFO *VolumeInfo);
    NTSTATUS (*GetSecurityByName)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize);
    NTSTATUS (*Create)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize, PVOID *PFileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*Open)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, PVOID *PFileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*Overwrite)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO *FileInfo);
    VOID (*Cleanup)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, ULONG Flags);
    VOID (*Close)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext);
    NTSTATUS (*Read)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred);
    NTSTATUS (*Write)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*Flush)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*GetFileInfo)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*SetBasicInfo)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT32 FileAttributes, UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*SetFileSize)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*CanReplace)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PVOID TargetContext);
    NTSTATUS (*Rename)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists);
    NTSTATUS (*GetSecurity)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize);
    NTSTATUS (*SetSecurity)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, SECURITY_INFORMATION SecurityInformation, PSECURITY_DESCRIPTOR ModificationDescriptor);
    NTSTATUS (*ReadDirectory)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR Pattern, PWSTR Marker, PVOID Buffer, ULONG Length, PULONG PBytesTransferred);
    NTSTATUS (*ResolveReparsePoints)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 ReparsePointIndex, BOOLEAN ResolveLastPathComponent, PIO_STATUS_BLOCK PIoStatus, PVOID Buffer, PSIZE_T PSize);
    NTSTATUS (*GetReparsePointByName)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, BOOLEAN IsDirectory, PVOID Buffer, PSIZE_T PSize);
    NTSTATUS (*GetDirInfoByName)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, FSP_FSCTL_DIR_INFO *DirInfo);
    NTSTATUS (*Control)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT32 ControlCode, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength, PULONG PBytesTransferred);
    NTSTATUS (*SetDelete)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, BOOLEAN DeleteFile);
};

// ============================================================================
// Dynamic WinFsp API Loader
// ============================================================================

class WinFspApi {
public:
    static WinFspApi& Get() {
        static WinFspApi instance;
        return instance;
    }

    [[nodiscard]] bool isAvailable() const noexcept { return hModule_ != nullptr; }

    NTSTATUS Create(PWSTR DevicePath, const FSP_FSCTL_VOLUME_PARAMS* VolumeParams, const FSP_FILE_SYSTEM_INTERFACE* Interface, FSP_FILE_SYSTEM** PFileSystem) {
        if (!fnCreate_) return ((NTSTATUS)0xC0000001L);
        return fnCreate_(DevicePath, VolumeParams, Interface, PFileSystem);
    }

    VOID Delete(FSP_FILE_SYSTEM* FileSystem) {
        if (fnDelete_) fnDelete_(FileSystem);
    }

    NTSTATUS StartDispatcher(FSP_FILE_SYSTEM* FileSystem, ULONG ThreadCount) {
        if (!fnStartDispatcher_) return ((NTSTATUS)0xC0000001L);
        return fnStartDispatcher_(FileSystem, ThreadCount);
    }

    VOID StopDispatcher(FSP_FILE_SYSTEM* FileSystem) {
        if (fnStopDispatcher_) fnStopDispatcher_(FileSystem);
    }

    NTSTATUS SetMountPoint(FSP_FILE_SYSTEM* FileSystem, PWSTR MountPoint) {
        if (!fnSetMountPoint_) return ((NTSTATUS)0xC0000001L);
        return fnSetMountPoint_(FileSystem, MountPoint);
    }

    VOID RemoveMountPoint(FSP_FILE_SYSTEM* FileSystem) {
        if (fnRemoveMountPoint_) fnRemoveMountPoint_(FileSystem);
    }

    BOOLEAN AddDirInfo(FSP_FSCTL_DIR_INFO* DirInfo, PVOID Buffer, ULONG Length, PULONG PBytesTransferred) {
        if (!fnAddDirInfo_) return FALSE;
        return fnAddDirInfo_(DirInfo, Buffer, Length, PBytesTransferred);
    }

private:
    WinFspApi() {
        hModule_ = LoadLibraryW(L"winfsp-x64.dll");
        if (!hModule_) {
            // Check standard WinFsp install registry path
            HKEY hKey;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WinFsp", 0, KEY_READ, &hKey) == ERROR_SUCCESS ||
                RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\WinFsp", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                wchar_t installDir[MAX_PATH];
                DWORD dirLen = sizeof(installDir);
                if (RegQueryValueExW(hKey, L"InstallDir", nullptr, nullptr, reinterpret_cast<LPBYTE>(installDir), &dirLen) == ERROR_SUCCESS) {
                    std::wstring dllPath = std::wstring(installDir) + L"bin\\winfsp-x64.dll";
                    hModule_ = LoadLibraryW(dllPath.c_str());
                }
                RegCloseKey(hKey);
            }
        }

        if (hModule_) {
            fnCreate_ = reinterpret_cast<FnCreate>(GetProcAddress(hModule_, "FspFileSystemCreate"));
            fnDelete_ = reinterpret_cast<FnDelete>(GetProcAddress(hModule_, "FspFileSystemDelete"));
            fnStartDispatcher_ = reinterpret_cast<FnStartDispatcher>(GetProcAddress(hModule_, "FspFileSystemStartDispatcher"));
            fnStopDispatcher_ = reinterpret_cast<FnStopDispatcher>(GetProcAddress(hModule_, "FspFileSystemStopDispatcher"));
            fnSetMountPoint_ = reinterpret_cast<FnSetMountPoint>(GetProcAddress(hModule_, "FspFileSystemSetMountPoint"));
            fnRemoveMountPoint_ = reinterpret_cast<FnRemoveMountPoint>(GetProcAddress(hModule_, "FspFileSystemRemoveMountPoint"));
            fnAddDirInfo_ = reinterpret_cast<FnAddDirInfo>(GetProcAddress(hModule_, "FspFileSystemAddDirInfo"));
        }
    }

    ~WinFspApi() {
        if (hModule_) {
            FreeLibrary(hModule_);
            hModule_ = nullptr;
        }
    }

    typedef NTSTATUS (*FnCreate)(PWSTR, const FSP_FSCTL_VOLUME_PARAMS*, const FSP_FILE_SYSTEM_INTERFACE*, FSP_FILE_SYSTEM**);
    typedef VOID (*FnDelete)(FSP_FILE_SYSTEM*);
    typedef NTSTATUS (*FnStartDispatcher)(FSP_FILE_SYSTEM*, ULONG);
    typedef VOID (*FnStopDispatcher)(FSP_FILE_SYSTEM*);
    typedef NTSTATUS (*FnSetMountPoint)(FSP_FILE_SYSTEM*, PWSTR);
    typedef VOID (*FnRemoveMountPoint)(FSP_FILE_SYSTEM*);
    typedef BOOLEAN (*FnAddDirInfo)(FSP_FSCTL_DIR_INFO*, PVOID, ULONG, PULONG);

    HMODULE hModule_{nullptr};
    FnCreate fnCreate_{nullptr};
    FnDelete fnDelete_{nullptr};
    FnStartDispatcher fnStartDispatcher_{nullptr};
    FnStopDispatcher fnStopDispatcher_{nullptr};
    FnSetMountPoint fnSetMountPoint_{nullptr};
    FnRemoveMountPoint fnRemoveMountPoint_{nullptr};
    FnAddDirInfo fnAddDirInfo_{nullptr};
};

// ============================================================================
// File Context for Active Handles (supports Alternate Data Streams)
// ============================================================================

struct NtfsFileContext {
    std::wstring basePath;
    std::wstring streamName; // Empty for primary :$DATA stream, e.g. ":Zone.Identifier"
    bool isDirectory{false};
    bool deleteOnClose{false};
};

// ============================================================================
// WinFspMountSession: User-Mode NTFS Filesystem Mount Driver
// ============================================================================

class WinFspMountSession {
public:
    WinFspMountSession() = default;
    ~WinFspMountSession() { unmount(); }

    bool mount(std::shared_ptr<NtfsFileSystem> fs, const std::wstring& mountPoint) {
        if (!fs || mountPoint.empty()) return false;
        if (mounted_) return true;

        auto& fsp = WinFspApi::Get();
        if (!fsp.isAvailable()) return false;

        fs_ = std::move(fs);
        mountPoint_ = mountPoint;

        FSP_FSCTL_VOLUME_PARAMS params{};
        params.Version = sizeof(params);
        params.SectorSize = 512;
        params.SectorsPerAllocationUnit = 8; // 4096 bytes per cluster
        params.MaxComponentLength = 255;
        params.VolumeSerialNumber = static_cast<UINT32>(fs_->getVolumeSerialNumber());
        params.Flags = 0;
        const wchar_t fsName[] = L"NTFS";
        std::memcpy(params.FileSystemName, fsName, sizeof(fsName));

        FSP_FILE_SYSTEM_INTERFACE iface{};
        iface.GetVolumeInfo = &WinFspMountSession::CbGetVolumeInfo;
        iface.GetSecurityByName = &WinFspMountSession::CbGetSecurityByName;
        iface.Create = &WinFspMountSession::CbCreate;
        iface.Open = &WinFspMountSession::CbOpen;
        iface.Close = &WinFspMountSession::CbClose;
        iface.Cleanup = &WinFspMountSession::CbCleanup;
        iface.GetFileInfo = &WinFspMountSession::CbGetFileInfo;
        iface.Read = &WinFspMountSession::CbRead;
        iface.Write = &WinFspMountSession::CbWrite;
        iface.SetFileSize = &WinFspMountSession::CbSetFileSize;
        iface.SetBasicInfo = &WinFspMountSession::CbSetBasicInfo;
        iface.Rename = &WinFspMountSession::CbRename;
        iface.SetDelete = &WinFspMountSession::CbSetDelete;
        iface.ReadDirectory = &WinFspMountSession::CbReadDirectory;

        NTSTATUS status = fsp.Create(nullptr, &params, &iface, &fileSystem_);
        if (status != STATUS_SUCCESS || !fileSystem_) {
            return false;
        }

        fileSystem_->UserContext = this;

        status = fsp.SetMountPoint(fileSystem_, mountPoint_.data());
        if (status != STATUS_SUCCESS) {
            fsp.Delete(fileSystem_);
            fileSystem_ = nullptr;
            return false;
        }

        status = fsp.StartDispatcher(fileSystem_, 0);
        if (status != STATUS_SUCCESS) {
            fsp.RemoveMountPoint(fileSystem_);
            fsp.Delete(fileSystem_);
            fileSystem_ = nullptr;
            return false;
        }

        mounted_ = true;
        return true;
    }

    void unmount() {
        if (!mounted_ || !fileSystem_) return;
        auto& fsp = WinFspApi::Get();
        if (fsp.isAvailable()) {
            fsp.StopDispatcher(fileSystem_);
            fsp.RemoveMountPoint(fileSystem_);
            fsp.Delete(fileSystem_);
        }
        fileSystem_ = nullptr;
        mounted_ = false;
    }

    [[nodiscard]] bool isMounted() const noexcept { return mounted_; }
    [[nodiscard]] const std::wstring& getMountPoint() const noexcept { return mountPoint_; }
    [[nodiscard]] std::shared_ptr<NtfsFileSystem> getFileSystem() const noexcept { return fs_; }

    static void splitPathAndStream(const std::wstring& fullPath, std::wstring& basePath, std::wstring& streamName) {
        size_t colon = fullPath.find(L':');
        if (colon != std::wstring::npos) {
            basePath = fullPath.substr(0, colon);
            std::wstring s = fullPath.substr(colon);
            // Strip trailing :$DATA if present
            if (s.ends_with(L":$DATA")) {
                s = s.substr(0, s.size() - 6);
            }
            streamName = s;
        } else {
            basePath = fullPath;
            streamName = L"";
        }
    }

    void populateFileInfo(const std::wstring& basePath, const std::wstring& streamName, FSP_FSCTL_FILE_INFO* info) const {
        std::memset(info, 0, sizeof(*info));
        NtfsFileRecord rec;
        if (fs_ && fs_->getFileRecord(basePath, rec)) {
            info->FileAttributes = rec.attributes;
            if (rec.flags & MFT_RECORD_DIRECTORY) {
                info->FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            }

            if (streamName.empty()) {
                info->FileSize = rec.fileSize;
            } else {
                auto sIt = rec.alternateStreams.find(streamName);
                if (sIt != rec.alternateStreams.end()) {
                    info->FileSize = sIt->second.size();
                } else {
                    info->FileSize = 0;
                }
            }

            info->AllocationSize = ((info->FileSize + 4095) / 4096) * 4096;
            info->CreationTime = 133400000000000000ULL;
            info->LastAccessTime = 133400000000000000ULL;
            info->LastWriteTime = 133400000000000000ULL;
            info->ChangeTime = 133400000000000000ULL;
            info->IndexNumber = rec.recordNumber ? rec.recordNumber : 5;
            info->HardLinks = 1;
        } else if (basePath == L"\\") {
            info->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
            info->IndexNumber = 5;
            info->HardLinks = 1;
        }
    }

private:
    static NTSTATUS CbGetVolumeInfo(FSP_FILE_SYSTEM* FileSystem, FSP_FSCTL_VOLUME_INFO* VolumeInfo) {
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        if (!session || !session->fs_) return STATUS_ACCESS_DENIED;

        VolumeInfo->TotalSize = session->fs_->getTotalSectors() * 512ULL;
        VolumeInfo->FreeSize = VolumeInfo->TotalSize > (64ULL * 1024 * 1024) ? (VolumeInfo->TotalSize - (64ULL * 1024 * 1024)) : VolumeInfo->TotalSize;

        const std::wstring& label = session->fs_->getVolumeLabel();
        size_t cch = (std::min)(label.size(), size_t(31));
        std::memcpy(VolumeInfo->VolumeLabel, label.c_str(), cch * sizeof(WCHAR));
        VolumeInfo->VolumeLabel[cch] = 0;
        VolumeInfo->VolumeLabelLength = static_cast<UINT16>(cch * sizeof(WCHAR));

        return STATUS_SUCCESS;
    }

    static NTSTATUS CbGetSecurityByName(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T* PSecurityDescriptorSize) {
        (void)SecurityDescriptor;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        if (!session || !session->fs_) return STATUS_ACCESS_DENIED;

        std::wstring basePath, streamName;
        splitPathAndStream(FileName, basePath, streamName);

        NtfsFileRecord rec;
        if (session->fs_->getFileRecord(basePath, rec)) {
            if (PFileAttributes) {
                *PFileAttributes = rec.attributes;
                if (rec.flags & MFT_RECORD_DIRECTORY) *PFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            }
        } else if (basePath == L"\\") {
            if (PFileAttributes) *PFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        } else {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        if (PSecurityDescriptorSize) *PSecurityDescriptorSize = 0;
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbCreate(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
        (void)GrantedAccess; (void)SecurityDescriptor; (void)AllocationSize;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        if (!session || !session->fs_) return STATUS_ACCESS_DENIED;

        std::wstring basePath, streamName;
        splitPathAndStream(FileName, basePath, streamName);

        bool isDir = (CreateOptions & 0x00000001 /* FILE_DIRECTORY_FILE */) != 0;
        if (isDir && !streamName.empty()) {
            return STATUS_NOT_A_DIRECTORY;
        }

        if (isDir) {
            auto st = session->fs_->createDirectory(basePath);
            if (!IsSuccess(st)) return STATUS_OBJECT_NAME_COLLISION;
        } else {
            if (streamName.empty()) {
                auto st = session->fs_->createFile(basePath, FileAttributes, 0);
                if (!IsSuccess(st)) return STATUS_OBJECT_NAME_COLLISION;
            } else {
                // Ensure base file exists
                if (!session->fs_->fileExists(basePath)) {
                    session->fs_->createFile(basePath, FILE_ATTRIBUTE_ARCHIVE, 0);
                }
                size_t written = 0;
                session->fs_->writeFile(basePath, streamName, 0, {}, written);
            }
        }

        auto* ctx = new NtfsFileContext();
        ctx->basePath = basePath;
        ctx->streamName = streamName;
        ctx->isDirectory = isDir;
        *PFileContext = ctx;

        session->populateFileInfo(basePath, streamName, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbOpen(FSP_FILE_SYSTEM* FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, PVOID* PFileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
        (void)CreateOptions; (void)GrantedAccess;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        if (!session || !session->fs_) return STATUS_ACCESS_DENIED;

        std::wstring basePath, streamName;
        splitPathAndStream(FileName, basePath, streamName);

        NtfsFileRecord rec;
        bool exists = session->fs_->getFileRecord(basePath, rec);
        if (!exists && basePath != L"\\") {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        bool isDir = (basePath == L"\\") || (rec.flags & MFT_RECORD_DIRECTORY);
        if (isDir && !streamName.empty()) {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        auto* ctx = new NtfsFileContext();
        ctx->basePath = basePath;
        ctx->streamName = streamName;
        ctx->isDirectory = isDir;
        *PFileContext = ctx;

        session->populateFileInfo(basePath, streamName, FileInfo);
        return STATUS_SUCCESS;
    }

    static VOID CbClose(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext) {
        (void)FileSystem;
        auto* ctx = reinterpret_cast<NtfsFileContext*>(FileContext);
        delete ctx;
    }

    static VOID CbCleanup(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PWSTR FileName, ULONG Flags) {
        (void)FileName; (void)Flags;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<NtfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return;

        if (ctx->deleteOnClose) {
            if (ctx->isDirectory) {
                session->fs_->removeDirectory(ctx->basePath);
            } else if (!ctx->streamName.empty()) {
                session->fs_->deleteStream(ctx->basePath, ctx->streamName);
            } else {
                session->fs_->deleteFile(ctx->basePath);
            }
        }
    }

    static NTSTATUS CbGetFileInfo(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, FSP_FSCTL_FILE_INFO* FileInfo) {
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<NtfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        session->populateFileInfo(ctx->basePath, ctx->streamName, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbRead(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) {
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<NtfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;
        if (ctx->isDirectory) return STATUS_FILE_IS_A_DIRECTORY;

        size_t bytesRead = 0;
        std::span<uint8_t> dst(reinterpret_cast<uint8_t*>(Buffer), Length);
        auto st = session->fs_->readFile(ctx->basePath, ctx->streamName, Offset, dst, bytesRead);

        if (!IsSuccess(st)) {
            if (st == Status::NoSuchFile) return STATUS_OBJECT_NAME_NOT_FOUND;
            return STATUS_ACCESS_DENIED;
        }

        if (PBytesTransferred) *PBytesTransferred = static_cast<ULONG>(bytesRead);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbWrite(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO* FileInfo) {
        (void)ConstrainedIo;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<NtfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;
        if (ctx->isDirectory) return STATUS_FILE_IS_A_DIRECTORY;

        uint64_t targetOffset = Offset;
        if (WriteToEndOfFile) {
            NtfsFileRecord rec;
            if (session->fs_->getFileRecord(ctx->basePath, rec)) {
                if (ctx->streamName.empty()) {
                    targetOffset = rec.fileSize;
                } else {
                    auto sIt = rec.alternateStreams.find(ctx->streamName);
                    targetOffset = (sIt != rec.alternateStreams.end()) ? sIt->second.size() : 0;
                }
            }
        }

        size_t bytesWritten = 0;
        std::span<const uint8_t> src(reinterpret_cast<const uint8_t*>(Buffer), Length);
        auto st = session->fs_->writeFile(ctx->basePath, ctx->streamName, targetOffset, src, bytesWritten);

        if (!IsSuccess(st)) return STATUS_ACCESS_DENIED;

        if (PBytesTransferred) *PBytesTransferred = static_cast<ULONG>(bytesWritten);
        session->populateFileInfo(ctx->basePath, ctx->streamName, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbSetFileSize(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO* FileInfo) {
        (void)SetAllocationSize;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<NtfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        if (ctx->streamName.empty()) {
            auto st = session->fs_->setFileSize(ctx->basePath, NewSize);
            if (!IsSuccess(st)) return STATUS_ACCESS_DENIED;
        }

        session->populateFileInfo(ctx->basePath, ctx->streamName, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbSetBasicInfo(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, UINT32 FileAttributes, UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime, FSP_FSCTL_FILE_INFO* FileInfo) {
        (void)CreationTime; (void)LastAccessTime; (void)LastWriteTime; (void)ChangeTime;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<NtfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        if (FileAttributes != 0 && FileAttributes != INVALID_FILE_ATTRIBUTES) {
            session->fs_->setFileAttributes(ctx->basePath, FileAttributes);
        }

        session->populateFileInfo(ctx->basePath, ctx->streamName, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbRename(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists) {
        (void)FileName;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<NtfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        std::wstring newBase, newStream;
        splitPathAndStream(NewFileName, newBase, newStream);

        auto st = session->fs_->renameFile(ctx->basePath, newBase, ReplaceIfExists != 0);
        if (!IsSuccess(st)) {
            if (st == Status::ObjectNameCollision) return STATUS_OBJECT_NAME_COLLISION;
            return STATUS_ACCESS_DENIED;
        }

        ctx->basePath = newBase;
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbSetDelete(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PWSTR FileName, BOOLEAN DeleteFile) {
        (void)FileSystem; (void)FileName;
        auto* ctx = reinterpret_cast<NtfsFileContext*>(FileContext);
        if (ctx) {
            ctx->deleteOnClose = (DeleteFile != 0);
        }
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbReadDirectory(FSP_FILE_SYSTEM* FileSystem, PVOID FileContext, PWSTR Pattern, PWSTR Marker, PVOID Buffer, ULONG Length, PULONG PBytesTransferred) {
        (void)Pattern;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<NtfsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;
        if (!ctx->isDirectory) return STATUS_NOT_A_DIRECTORY;

        auto& fsp = WinFspApi::Get();
        if (PBytesTransferred) *PBytesTransferred = 0;

        std::vector<NtfsFileRecord> children;
        if (!session->fs_->listDirectory(ctx->basePath, children) && ctx->basePath != L"\\") {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        struct DirEntry {
            std::wstring name;
            NtfsFileRecord rec;
        };
        std::vector<DirEntry> entries;

        // Add standard . and .. entries
        DirEntry dot;
        dot.name = L".";
        dot.rec.flags = MFT_RECORD_DIRECTORY;
        dot.rec.attributes = FILE_ATTRIBUTE_DIRECTORY;
        entries.push_back(dot);

        DirEntry dotdot;
        dotdot.name = L"..";
        dotdot.rec.flags = MFT_RECORD_DIRECTORY;
        dotdot.rec.attributes = FILE_ATTRIBUTE_DIRECTORY;
        entries.push_back(dotdot);

        for (auto& c : children) {
            size_t slash = c.fileName.find_last_of(L'\\');
            std::wstring base = (slash != std::wstring::npos) ? c.fileName.substr(slash + 1) : c.fileName;
            DirEntry de;
            de.name = base;
            de.rec = std::move(c);
            entries.push_back(std::move(de));
        }

        // Marker pagination
        size_t startIdx = 0;
        if (Marker && Marker[0]) {
            std::wstring wMarker(Marker);
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].name == wMarker) {
                    startIdx = i + 1;
                    break;
                }
            }
        }

        std::vector<uint8_t> dirBuf(sizeof(FSP_FSCTL_DIR_INFO) + 260 * sizeof(WCHAR));
        for (size_t i = startIdx; i < entries.size(); ++i) {
            const auto& ent = entries[i];
            auto* pDirInfo = reinterpret_cast<FSP_FSCTL_DIR_INFO*>(dirBuf.data());
            std::memset(pDirInfo, 0, dirBuf.size());

            pDirInfo->FileInfo.FileAttributes = ent.rec.attributes;
            if (ent.rec.flags & MFT_RECORD_DIRECTORY) pDirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            pDirInfo->FileInfo.FileSize = ent.rec.fileSize;
            pDirInfo->FileInfo.AllocationSize = ((ent.rec.fileSize + 4095) / 4096) * 4096;
            pDirInfo->FileInfo.CreationTime = 133400000000000000ULL;
            pDirInfo->FileInfo.LastAccessTime = 133400000000000000ULL;
            pDirInfo->FileInfo.LastWriteTime = 133400000000000000ULL;
            pDirInfo->FileInfo.IndexNumber = ent.rec.recordNumber ? ent.rec.recordNumber : 5;

            size_t nameBytes = ent.name.size() * sizeof(WCHAR);
            std::memcpy(pDirInfo->FileNameBuf, ent.name.c_str(), nameBytes);
            pDirInfo->Size = static_cast<UINT16>(sizeof(FSP_FSCTL_DIR_INFO) + nameBytes);

            if (!fsp.AddDirInfo(pDirInfo, Buffer, Length, PBytesTransferred)) {
                break;
            }
        }

        return STATUS_SUCCESS;
    }

    bool mounted_{false};
    std::wstring mountPoint_;
    std::shared_ptr<NtfsFileSystem> fs_;
    FSP_FILE_SYSTEM* fileSystem_{nullptr};
};

} // namespace titanntfs
#endif
