//===- Minidump.h - Minidump constants and structures -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header constants and data structures pertaining to the Windows Minidump
// core file format.
//
// Reference:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms679293(v=vs.85).aspx
// https://chromium.googlesource.com/breakpad/breakpad/
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BINARYFORMAT_MINIDUMP_H
#define LLVM_BINARYFORMAT_MINIDUMP_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/Endian.h"

namespace llvm {
/// Constants and structures for the Windows minidump core-file format.
namespace minidump {

// Expanded (instead of the macro) so MrDocs can attach docs to each using.
/// Bring bitmask enum bitwise NOT into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator~;
/// Bring bitmask enum bitwise OR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator|;
/// Bring bitmask enum bitwise AND into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator&;
/// Bring bitmask enum bitwise XOR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator^;
/// Bring bitmask enum left-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator<<;
/// Bring bitmask enum right-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator>>;
/// Bring bitmask enum in-place OR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator|=;
/// Bring bitmask enum in-place AND into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator&=;
/// Bring bitmask enum in-place XOR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator^=;
/// Bring bitmask enum in-place left-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator<<=;
/// Bring bitmask enum in-place right-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator>>=;
/// Bring bitmask enum logical-not into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator!;
/// Bring bitmask enum any-bits-set test into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::any;

/// The minidump header is the first part of a minidump file. It identifies the
/// file as a minidump file, and gives the location of the stream directory.
struct Header {
  /// Expected value of the Signature field ("MDMP" as little-endian PMDM).
  static constexpr uint32_t MagicSignature = 0x504d444d; // PMDM
  /// Expected value of the low 16 bits of the Version field.
  static constexpr uint16_t MagicVersion = 0xa793;

  /// File signature; should equal MagicSignature.
  support::ulittle32_t Signature;
  /// Version of the minidump format.
  ///
  /// The high 16 bits of version field are implementation specific. The low 16
  /// bits should be MagicVersion.
  support::ulittle32_t Version;
  /// Number of entries in the stream directory.
  support::ulittle32_t NumberOfStreams;
  /// RVA of the stream directory (array of Directory entries).
  support::ulittle32_t StreamDirectoryRVA;
  /// Checksum of the minidump file; may be zero.
  support::ulittle32_t Checksum;
  /// Time-date stamp of when the minidump was created.
  support::ulittle32_t TimeDateStamp;
  /// Minidump type flags that control which streams are present.
  support::ulittle64_t Flags;
};
static_assert(sizeof(Header) == 32);

/// The type of a minidump stream identifies its contents. Streams numbers after
/// LastReserved are for application-defined data streams.
enum class StreamType : uint32_t {
#define HANDLE_MDMP_STREAM_TYPE(CODE, NAME) NAME = CODE,
#include "llvm/BinaryFormat/MinidumpConstants.def"
  /// Unused or unknown stream type (value 0).
  Unused = 0,
  /// Last stream type reserved for system-defined streams.
  LastReserved = 0x0000ffff,
};

/// Specifies the location (and size) of various objects in the minidump file.
/// The location is relative to the start of the file.
struct LocationDescriptor {
  /// Size in bytes of the referenced data.
  support::ulittle32_t DataSize;
  /// Relative virtual address of the data from the start of the file.
  support::ulittle32_t RVA;
};
static_assert(sizeof(LocationDescriptor) == 8);

/// Describes a single memory range (both its VM address and where to find it in
/// the file) of the process from which this minidump file was generated.
struct MemoryDescriptor {
  /// Starting virtual address of the memory range in the dumped process.
  support::ulittle64_t StartOfMemoryRange;
  /// Location of the memory contents within the minidump file.
  LocationDescriptor Memory;
};
static_assert(sizeof(MemoryDescriptor) == 16);

/// Describes a 64-bit memory range without embedding a LocationDescriptor.
struct MemoryDescriptor_64 {
  /// Starting virtual address of the memory range in the dumped process.
  support::ulittle64_t StartOfMemoryRange;
  /// Size in bytes of the memory range.
  support::ulittle64_t DataSize;
};
static_assert(sizeof(MemoryDescriptor_64) == 16);

/// Header for a MemoryList stream of MemoryDescriptor entries.
struct MemoryListHeader {
  /// Number of MemoryDescriptor entries that follow this header.
  support::ulittle32_t NumberOfMemoryRanges;
};
static_assert(sizeof(MemoryListHeader) == 4);

/// Header for a Memory64List stream of MemoryDescriptor_64 entries.
struct Memory64ListHeader {
  /// Number of MemoryDescriptor_64 entries that follow this header.
  support::ulittle64_t NumberOfMemoryRanges;
  /// RVA of the first byte of concatenated memory data for all ranges.
  support::ulittle64_t BaseRVA;
};
static_assert(sizeof(Memory64ListHeader) == 16);

/// Header for a MemoryInfoList stream of MemoryInfo entries.
struct MemoryInfoListHeader {
  /// Size in bytes of this header structure.
  support::ulittle32_t SizeOfHeader;
  /// Size in bytes of each MemoryInfo entry that follows.
  support::ulittle32_t SizeOfEntry;
  /// Number of MemoryInfo entries that follow this header.
  support::ulittle64_t NumberOfEntries;

  /// Default-construct an uninitialized memory-info list header.
  MemoryInfoListHeader() = default;
  /// Construct a memory-info list header with the given field values.
  ///
  /// \param SizeOfHeader Size in bytes of this header structure.
  /// \param SizeOfEntry Size in bytes of each MemoryInfo entry.
  /// \param NumberOfEntries Number of MemoryInfo entries that follow.
  MemoryInfoListHeader(uint32_t SizeOfHeader, uint32_t SizeOfEntry,
                       uint64_t NumberOfEntries)
      : SizeOfHeader(SizeOfHeader), SizeOfEntry(SizeOfEntry),
        NumberOfEntries(NumberOfEntries) {}
};
static_assert(sizeof(MemoryInfoListHeader) == 16);

/// Memory-protection flags for a region described by MemoryInfo.
enum class MemoryProtection : uint32_t {
#define HANDLE_MDMP_PROTECT(CODE, NAME, NATIVENAME) NAME = CODE,
#include "llvm/BinaryFormat/MinidumpConstants.def"
  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/0xffffffffu),
};

/// State of a memory region described by MemoryInfo (committed, reserved, or
/// free).
enum class MemoryState : uint32_t {
#define HANDLE_MDMP_MEMSTATE(CODE, NAME, NATIVENAME) NAME = CODE,
#include "llvm/BinaryFormat/MinidumpConstants.def"
  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/0xffffffffu),
};

/// Type of a memory region described by MemoryInfo (private, mapped, or image).
enum class MemoryType : uint32_t {
#define HANDLE_MDMP_MEMTYPE(CODE, NAME, NATIVENAME) NAME = CODE,
#include "llvm/BinaryFormat/MinidumpConstants.def"
  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/0xffffffffu),
};

/// Describes a contiguous region of the dumped process's virtual address space.
struct MemoryInfo {
  /// Base address of the region of pages.
  support::ulittle64_t BaseAddress;
  /// Base address of the allocation that contains this region.
  support::ulittle64_t AllocationBase;
  /// Memory protection when the region was initially allocated.
  support::little_t<MemoryProtection> AllocationProtect;
  /// Reserved; must be zero.
  support::ulittle32_t Reserved0;
  /// Size in bytes of the region starting at BaseAddress.
  support::ulittle64_t RegionSize;
  /// Current state of the pages in the region.
  support::little_t<MemoryState> State;
  /// Current access protection of the pages in the region.
  support::little_t<MemoryProtection> Protect;
  /// Type of pages in the region.
  support::little_t<MemoryType> Type;
  /// Reserved; must be zero.
  support::ulittle32_t Reserved1;
};
static_assert(sizeof(MemoryInfo) == 48);

/// Directory entry describing one stream in a minidump file.
///
/// Specifies the location and type of a single stream in the minidump file. The
/// minidump stream directory is an array of entries of this type, with its size
/// given by Header.NumberOfStreams.
struct Directory {
  /// Type of the stream identified by this directory entry.
  support::little_t<StreamType> Type;
  /// Location and size of the stream data within the file.
  LocationDescriptor Location;
};
static_assert(sizeof(Directory) == 12);

/// The processor architecture of the system that generated this minidump. Used
/// in the ProcessorArch field of the SystemInfo stream.
enum class ProcessorArchitecture : uint16_t {
#define HANDLE_MDMP_ARCH(CODE, NAME) NAME = CODE,
#include "llvm/BinaryFormat/MinidumpConstants.def"
};

/// The OS Platform of the system that generated this minidump. Used in the
/// PlatformId field of the SystemInfo stream.
enum class OSPlatform : uint32_t {
#define HANDLE_MDMP_PLATFORM(CODE, NAME) NAME = CODE,
#include "llvm/BinaryFormat/MinidumpConstants.def"
};

/// Detailed information about the processor of the system that generated this
/// minidump. Its interpretation depends on the ProcessorArchitecture enum.
union CPUInfo {
  /// x86 / x86-64 CPUID feature information.
  struct X86Info {
    /// CPU vendor string from CPUID leaf 0 (ebx, edx, ecx).
    char VendorID[12];                        // cpuid 0: ebx, edx, ecx
    /// Processor version information from CPUID leaf 1 (eax).
    support::ulittle32_t VersionInfo;         // cpuid 1: eax
    /// Feature flags from CPUID leaf 1 (edx).
    support::ulittle32_t FeatureInfo;         // cpuid 1: edx
    /// AMD extended feature flags from CPUID leaf 0x80000001 (ebx).
    support::ulittle32_t AMDExtendedFeatures; // cpuid 0x80000001, ebx
  } X86; ///< x86 / x86-64 variant of the CPU info.
  /// ARM / ARM64 CPU identification information.
  struct ArmInfo {
    /// CPU identification register value.
    support::ulittle32_t CPUID;
    /// ELF HWCAP feature bits on Linux; zero otherwise.
    support::ulittle32_t ElfHWCaps; // linux specific, 0 otherwise
  } Arm; ///< ARM / ARM64 variant of the CPU info.
  /// CPU feature bits for architectures other than x86 and ARM.
  struct OtherInfo {
    /// Processor feature flags as a raw 16-byte bitset.
    uint8_t ProcessorFeatures[16];
  } Other; ///< Architecture-independent processor feature bits.
};
static_assert(sizeof(CPUInfo) == 24);

/// The SystemInfo stream, containing various information about the system where
/// this minidump was generated.
struct SystemInfo {
  /// Processor architecture of the dumped system.
  support::little_t<ProcessorArchitecture> ProcessorArch;
  /// Architecture-specific processor level.
  support::ulittle16_t ProcessorLevel;
  /// Architecture-specific processor revision.
  support::ulittle16_t ProcessorRevision;

  /// Number of processors in the system.
  uint8_t NumberOfProcessors;
  /// Product type (workstation, server, or domain controller).
  uint8_t ProductType;

  /// Major version number of the operating system.
  support::ulittle32_t MajorVersion;
  /// Minor version number of the operating system.
  support::ulittle32_t MinorVersion;
  /// Build number of the operating system.
  support::ulittle32_t BuildNumber;
  /// Operating-system platform identifier.
  support::little_t<OSPlatform> PlatformId;
  /// RVA of the CSD (service pack) version string.
  support::ulittle32_t CSDVersionRVA;

  /// Bit flags identifying available OS product suites.
  support::ulittle16_t SuiteMask;
  /// Reserved; must be zero.
  support::ulittle16_t Reserved;

  /// Detailed CPU information whose layout depends on ProcessorArch.
  CPUInfo CPU;
};
static_assert(sizeof(SystemInfo) == 56);

/// Fixed-file-info version record for a module (VS_FIXEDFILEINFO).
struct VSFixedFileInfo {
  /// Structure signature; expected to be 0xFEEF04BD.
  support::ulittle32_t Signature;
  /// Version of this structure.
  support::ulittle32_t StructVersion;
  /// High 32 bits of the file version number.
  support::ulittle32_t FileVersionHigh;
  /// Low 32 bits of the file version number.
  support::ulittle32_t FileVersionLow;
  /// High 32 bits of the product version number.
  support::ulittle32_t ProductVersionHigh;
  /// Low 32 bits of the product version number.
  support::ulittle32_t ProductVersionLow;
  /// Bitmask of valid bits in FileFlags.
  support::ulittle32_t FileFlagsMask;
  /// File flags (debug, prerelease, patched, and so on).
  support::ulittle32_t FileFlags;
  /// Operating system for which this file was designed.
  support::ulittle32_t FileOS;
  /// General type of file (application, DLL, driver, and so on).
  support::ulittle32_t FileType;
  /// Function of the file; meaning depends on FileType.
  support::ulittle32_t FileSubtype;
  /// High 32 bits of the file's binary creation date.
  support::ulittle32_t FileDateHigh;
  /// Low 32 bits of the file's binary creation date.
  support::ulittle32_t FileDateLow;
};
static_assert(sizeof(VSFixedFileInfo) == 52);

/// Return true if \p LHS and \p RHS have identical VSFixedFileInfo contents.
///
/// \param LHS Left-hand version-info record.
/// \param RHS Right-hand version-info record.
/// \return True if \p LHS and \p RHS have identical contents.
inline bool operator==(const VSFixedFileInfo &LHS, const VSFixedFileInfo &RHS) {
  return memcmp(&LHS, &RHS, sizeof(VSFixedFileInfo)) == 0;
}

/// Describes a loaded module (executable or shared library) in the minidump.
struct Module {
  /// Base virtual address of the loaded module image.
  support::ulittle64_t BaseOfImage;
  /// Size in bytes of the module image in memory.
  support::ulittle32_t SizeOfImage;
  /// Checksum of the module image; may be zero.
  support::ulittle32_t Checksum;
  /// Time-date stamp from the module's PE header.
  support::ulittle32_t TimeDateStamp;
  /// RVA of the module's UTF-16 name string.
  support::ulittle32_t ModuleNameRVA;
  /// Version information for the module.
  VSFixedFileInfo VersionInfo;
  /// Location of the CodeView debug record, if present.
  LocationDescriptor CvRecord;
  /// Location of the miscellaneous debug record, if present.
  LocationDescriptor MiscRecord;
  /// Reserved; must be zero.
  support::ulittle64_t Reserved0;
  /// Reserved; must be zero.
  support::ulittle64_t Reserved1;
};
static_assert(sizeof(Module) == 108);

/// Describes a single thread in the minidump file. Part of the ThreadList
/// stream.
struct Thread {
  /// Thread identifier assigned by the operating system.
  support::ulittle32_t ThreadId;
  /// Suspend count of the thread at dump time.
  support::ulittle32_t SuspendCount;
  /// Priority class of the thread.
  support::ulittle32_t PriorityClass;
  /// Priority value of the thread within its priority class.
  support::ulittle32_t Priority;
  /// Address of the thread environment block (TEB).
  support::ulittle64_t EnvironmentBlock;
  /// Stack memory range for this thread.
  MemoryDescriptor Stack;
  /// Location of the thread's CPU context record.
  LocationDescriptor Context;
};
static_assert(sizeof(Thread) == 48);

/// Exception record describing a fault captured in an Exception stream.
struct Exception {
  /// Maximum number of exception information parameters.
  static constexpr size_t MaxParameters = 15;
  /// Maximum size in bytes of the ExceptionInformation array.
  static constexpr size_t MaxParameterBytes = MaxParameters * sizeof(uint64_t);
  /// LLDB-specific flag value ('LLDB' in ASCII).
  static const uint32_t LLDB_FLAG = 0x4C4C4442; // ASCII for 'LLDB'

  /// Reason the exception occurred (architecture-specific code).
  support::ulittle32_t ExceptionCode;
  /// Flags describing the exception (for example, continuable).
  support::ulittle32_t ExceptionFlags;
  /// Address of a related exception record, or zero.
  support::ulittle64_t ExceptionRecord;
  /// Address where the exception occurred.
  support::ulittle64_t ExceptionAddress;
  /// Number of valid entries in ExceptionInformation.
  support::ulittle32_t NumberParameters;
  /// Reserved alignment padding; must be zero.
  support::ulittle32_t UnusedAlignment;
  /// Additional exception parameters (up to MaxParameters entries).
  support::ulittle64_t ExceptionInformation[MaxParameters];
};
static_assert(sizeof(Exception) == 152);

/// Exception stream describing the thread that caused a dump to be written.
struct ExceptionStream {
  /// Identifier of the thread on which the exception occurred.
  support::ulittle32_t ThreadId;
  /// Reserved alignment padding; must be zero.
  support::ulittle32_t UnusedAlignment;
  /// Exception record for the fault that triggered the dump.
  Exception ExceptionRecord;
  /// Location of the faulting thread's CPU context.
  LocationDescriptor ThreadContext;
};
static_assert(sizeof(ExceptionStream) == 168);

} // namespace minidump

/// DenseMapInfo specialization so StreamType can be used as a DenseMap key.
template <> struct DenseMapInfo<minidump::StreamType> {
  /// Compute a hash value for stream type \p Val.
  ///
  /// \param Val Stream type to hash.
  /// \return Hash value for \p Val.
  static unsigned getHashValue(minidump::StreamType Val) {
    return DenseMapInfo<uint32_t>::getHashValue(static_cast<uint32_t>(Val));
  }

  /// Return true if \p LHS and \p RHS denote the same stream type.
  ///
  /// \param LHS Left-hand stream type.
  /// \param RHS Right-hand stream type.
  /// \return True if \p LHS and \p RHS are the same stream type.
  static bool isEqual(minidump::StreamType LHS, minidump::StreamType RHS) {
    return LHS == RHS;
  }
};

} // namespace llvm

#endif // LLVM_BINARYFORMAT_MINIDUMP_H
