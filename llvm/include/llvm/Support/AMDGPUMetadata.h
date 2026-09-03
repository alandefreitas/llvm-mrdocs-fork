//===--- AMDGPUMetadata.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// AMDGPU metadata definitions and in-memory representations.
///
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_AMDGPUMETADATA_H
#define LLVM_SUPPORT_AMDGPUMETADATA_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace llvm {
namespace AMDGPU {

//===----------------------------------------------------------------------===//
// HSA metadata.
//===----------------------------------------------------------------------===//
namespace HSAMD {

/// HSA metadata major version for code object V3.
constexpr uint32_t VersionMajorV3 = 1;
/// HSA metadata minor version for code object V3.
constexpr uint32_t VersionMinorV3 = 0;

/// HSA metadata major version for code object V4.
constexpr uint32_t VersionMajorV4 = 1;
/// HSA metadata minor version for code object V4.
constexpr uint32_t VersionMinorV4 = 1;

/// HSA metadata major version for code object V5.
constexpr uint32_t VersionMajorV5 = 1;
/// HSA metadata minor version for code object V5.
constexpr uint32_t VersionMinorV5 = 2;

/// HSA metadata major version for code object V6.
constexpr uint32_t VersionMajorV6 = 1;
/// HSA metadata minor version for code object V6.
constexpr uint32_t VersionMinorV6 = 2;

/// Old HSA metadata beginning assembler directive for V2. This is only used for
/// diagnostics now.

/// HSA metadata beginning assembler directive.
constexpr char AssemblerDirectiveBegin[] = ".amd_amdgpu_hsa_metadata";

/// Access qualifiers.
enum class AccessQualifier : uint8_t {
  /// Default access (no explicit qualifier).
  Default   = 0,
  /// Read-only access.
  ReadOnly  = 1,
  /// Write-only access.
  WriteOnly = 2,
  /// Read-write access.
  ReadWrite = 3,
  /// Unknown or unspecified access qualifier.
  Unknown   = 0xff
};

/// Address space qualifiers.
enum class AddressSpaceQualifier : uint8_t {
  /// Private address space.
  Private  = 0,
  /// Global address space.
  Global   = 1,
  /// Constant address space.
  Constant = 2,
  /// Local (shared) address space.
  Local    = 3,
  /// Generic address space.
  Generic  = 4,
  /// Region address space.
  Region   = 5,
  /// Unknown or unspecified address space.
  Unknown  = 0xff
};

/// Value kinds.
enum class ValueKind : uint8_t {
  /// Passed by value.
  ByValue                = 0,
  /// Global memory buffer pointer.
  GlobalBuffer           = 1,
  /// Dynamically sized shared-memory pointer.
  DynamicSharedPointer   = 2,
  /// Sampler object.
  Sampler                = 3,
  /// Image object.
  Image                  = 4,
  /// Pipe object.
  Pipe                   = 5,
  /// Queue object.
  Queue                  = 6,
  /// Hidden X component of the global offset.
  HiddenGlobalOffsetX    = 7,
  /// Hidden Y component of the global offset.
  HiddenGlobalOffsetY    = 8,
  /// Hidden Z component of the global offset.
  HiddenGlobalOffsetZ    = 9,
  /// Hidden unused/none argument.
  HiddenNone             = 10,
  /// Hidden printf buffer pointer.
  HiddenPrintfBuffer     = 11,
  /// Hidden default queue pointer.
  HiddenDefaultQueue     = 12,
  /// Hidden completion-action pointer.
  HiddenCompletionAction = 13,
  /// Hidden multi-grid synchronization argument.
  HiddenMultiGridSyncArg = 14,
  /// Hidden hostcall buffer pointer.
  HiddenHostcallBuffer   = 15,
  /// Unknown or unspecified value kind.
  Unknown                = 0xff
};

/// Value types. This is deprecated and only remains for compatibility parsing
/// of old metadata.
enum class ValueType : uint8_t {
  /// Structure type.
  Struct  = 0,
  /// Signed 8-bit integer.
  I8      = 1,
  /// Unsigned 8-bit integer.
  U8      = 2,
  /// Signed 16-bit integer.
  I16     = 3,
  /// Unsigned 16-bit integer.
  U16     = 4,
  /// 16-bit floating-point.
  F16     = 5,
  /// Signed 32-bit integer.
  I32     = 6,
  /// Unsigned 32-bit integer.
  U32     = 7,
  /// 32-bit floating-point.
  F32     = 8,
  /// Signed 64-bit integer.
  I64     = 9,
  /// Unsigned 64-bit integer.
  U64     = 10,
  /// 64-bit floating-point.
  F64     = 11,
  /// Unknown or unspecified value type.
  Unknown = 0xff
};

//===----------------------------------------------------------------------===//
// Kernel Metadata.
//===----------------------------------------------------------------------===//
/// Kernel metadata definitions and in-memory representations.
namespace Kernel {

//===----------------------------------------------------------------------===//
// Kernel Attributes Metadata.
//===----------------------------------------------------------------------===//
/// Kernel attributes metadata.
namespace Attrs {

/// Keys for kernel attributes metadata fields.
namespace Key {
/// Key for Kernel::Attr::Metadata::mReqdWorkGroupSize.
constexpr char ReqdWorkGroupSize[] = "ReqdWorkGroupSize";
/// Key for Kernel::Attr::Metadata::mWorkGroupSizeHint.
constexpr char WorkGroupSizeHint[] = "WorkGroupSizeHint";
/// Key for Kernel::Attr::Metadata::mVecTypeHint.
constexpr char VecTypeHint[] = "VecTypeHint";
/// Key for Kernel::Attr::Metadata::mRuntimeHandle.
constexpr char RuntimeHandle[] = "RuntimeHandle";
} // end namespace Key

/// In-memory representation of kernel attributes metadata.
struct Metadata final {
  /// 'reqd_work_group_size' attribute. Optional.
  std::vector<uint32_t> mReqdWorkGroupSize = std::vector<uint32_t>();
  /// 'work_group_size_hint' attribute. Optional.
  std::vector<uint32_t> mWorkGroupSizeHint = std::vector<uint32_t>();
  /// 'vec_type_hint' attribute. Optional.
  std::string mVecTypeHint = std::string();
  /// External symbol created by runtime to store the kernel address
  /// for enqueued blocks.
  std::string mRuntimeHandle = std::string();

  /// Default constructor.
  Metadata() = default;

  /// Returns whether kernel attributes metadata is empty.
  ///
  /// \returns True if kernel attributes metadata is empty, false otherwise.
  bool empty() const {
    return !notEmpty();
  }

  /// Returns whether kernel attributes metadata is not empty.
  ///
  /// \returns True if kernel attributes metadata is not empty, false otherwise.
  bool notEmpty() const {
    return !mReqdWorkGroupSize.empty() || !mWorkGroupSizeHint.empty() ||
           !mVecTypeHint.empty() || !mRuntimeHandle.empty();
  }
};

} // end namespace Attrs

//===----------------------------------------------------------------------===//
// Kernel Argument Metadata.
//===----------------------------------------------------------------------===//
/// Kernel argument metadata.
namespace Arg {

/// Keys for kernel argument metadata fields.
namespace Key {
/// Key for Kernel::Arg::Metadata::mName.
constexpr char Name[] = "Name";
/// Key for Kernel::Arg::Metadata::mTypeName.
constexpr char TypeName[] = "TypeName";
/// Key for Kernel::Arg::Metadata::mSize.
constexpr char Size[] = "Size";
/// Key for Kernel::Arg::Metadata::mOffset.
constexpr char Offset[] = "Offset";
/// Key for Kernel::Arg::Metadata::mAlign.
constexpr char Align[] = "Align";
/// Key for Kernel::Arg::Metadata::mValueKind.
constexpr char ValueKind[] = "ValueKind";
/// Key for Kernel::Arg::Metadata::mValueType. (deprecated)
constexpr char ValueType[] = "ValueType";
/// Key for Kernel::Arg::Metadata::mPointeeAlign.
constexpr char PointeeAlign[] = "PointeeAlign";
/// Key for Kernel::Arg::Metadata::mAddrSpaceQual.
constexpr char AddrSpaceQual[] = "AddrSpaceQual";
/// Key for Kernel::Arg::Metadata::mAccQual.
constexpr char AccQual[] = "AccQual";
/// Key for Kernel::Arg::Metadata::mActualAccQual.
constexpr char ActualAccQual[] = "ActualAccQual";
/// Key for Kernel::Arg::Metadata::mIsConst.
constexpr char IsConst[] = "IsConst";
/// Key for Kernel::Arg::Metadata::mIsRestrict.
constexpr char IsRestrict[] = "IsRestrict";
/// Key for Kernel::Arg::Metadata::mIsVolatile.
constexpr char IsVolatile[] = "IsVolatile";
/// Key for Kernel::Arg::Metadata::mIsPipe.
constexpr char IsPipe[] = "IsPipe";
} // end namespace Key

/// In-memory representation of kernel argument metadata.
struct Metadata final {
  /// Name. Optional.
  std::string mName = std::string();
  /// Type name. Optional.
  std::string mTypeName = std::string();
  /// Size in bytes. Required.
  uint32_t mSize = 0;
  /// Offset in bytes. Required for code object v3, unused for code object v2.
  uint32_t mOffset = 0;
  /// Alignment in bytes. Required.
  uint32_t mAlign = 0;
  /// Value kind. Required.
  ValueKind mValueKind = ValueKind::Unknown;
  /// Pointee alignment in bytes. Optional.
  uint32_t mPointeeAlign = 0;
  /// Address space qualifier. Optional.
  AddressSpaceQualifier mAddrSpaceQual = AddressSpaceQualifier::Unknown;
  /// Access qualifier. Optional.
  AccessQualifier mAccQual = AccessQualifier::Unknown;
  /// Actual access qualifier. Optional.
  AccessQualifier mActualAccQual = AccessQualifier::Unknown;
  /// True if 'const' qualifier is specified. Optional.
  bool mIsConst = false;
  /// True if 'restrict' qualifier is specified. Optional.
  bool mIsRestrict = false;
  /// True if 'volatile' qualifier is specified. Optional.
  bool mIsVolatile = false;
  /// True if 'pipe' qualifier is specified. Optional.
  bool mIsPipe = false;

  /// Default constructor.
  Metadata() = default;
};

} // end namespace Arg

//===----------------------------------------------------------------------===//
// Kernel Code Properties Metadata.
//===----------------------------------------------------------------------===//
/// Kernel code properties metadata.
namespace CodeProps {

/// Keys for kernel code properties metadata fields.
namespace Key {
/// Key for Kernel::CodeProps::Metadata::mKernargSegmentSize.
constexpr char KernargSegmentSize[] = "KernargSegmentSize";
/// Key for Kernel::CodeProps::Metadata::mGroupSegmentFixedSize.
constexpr char GroupSegmentFixedSize[] = "GroupSegmentFixedSize";
/// Key for Kernel::CodeProps::Metadata::mPrivateSegmentFixedSize.
constexpr char PrivateSegmentFixedSize[] = "PrivateSegmentFixedSize";
/// Key for Kernel::CodeProps::Metadata::mKernargSegmentAlign.
constexpr char KernargSegmentAlign[] = "KernargSegmentAlign";
/// Key for Kernel::CodeProps::Metadata::mWavefrontSize.
constexpr char WavefrontSize[] = "WavefrontSize";
/// Key for Kernel::CodeProps::Metadata::mNumSGPRs.
constexpr char NumSGPRs[] = "NumSGPRs";
/// Key for Kernel::CodeProps::Metadata::mNumVGPRs.
constexpr char NumVGPRs[] = "NumVGPRs";
/// Key for Kernel::CodeProps::Metadata::mMaxFlatWorkGroupSize.
constexpr char MaxFlatWorkGroupSize[] = "MaxFlatWorkGroupSize";
/// Key for Kernel::CodeProps::Metadata::mIsDynamicCallStack.
constexpr char IsDynamicCallStack[] = "IsDynamicCallStack";
/// Key for Kernel::CodeProps::Metadata::mIsXNACKEnabled.
constexpr char IsXNACKEnabled[] = "IsXNACKEnabled";
/// Key for Kernel::CodeProps::Metadata::mNumSpilledSGPRs.
constexpr char NumSpilledSGPRs[] = "NumSpilledSGPRs";
/// Key for Kernel::CodeProps::Metadata::mNumSpilledVGPRs.
constexpr char NumSpilledVGPRs[] = "NumSpilledVGPRs";
} // end namespace Key

/// In-memory representation of kernel code properties metadata.
struct Metadata final {
  /// Size in bytes of the kernarg segment memory. Kernarg segment memory
  /// holds the values of the arguments to the kernel. Required.
  uint64_t mKernargSegmentSize = 0;
  /// Fixed group segment size in bytes for a workgroup.
  ///
  /// Size in bytes of the group segment memory required by a workgroup.
  /// This value does not include any dynamically allocated group segment memory
  /// that may be added when the kernel is dispatched. Required.
  uint32_t mGroupSegmentFixedSize = 0;
  /// Size in bytes of the private segment memory required by a workitem.
  /// Private segment memory includes arg, spill and private segments. Required.
  uint32_t mPrivateSegmentFixedSize = 0;
  /// Maximum byte alignment of variables used by the kernel in the
  /// kernarg memory segment. Required.
  uint32_t mKernargSegmentAlign = 0;
  /// Wavefront size. Required.
  uint32_t mWavefrontSize = 0;
  /// Total number of SGPRs used by a wavefront. Optional.
  uint16_t mNumSGPRs = 0;
  /// Total number of VGPRs used by a workitem. Optional.
  uint16_t mNumVGPRs = 0;
  /// Maximum flat work-group size supported by the kernel. Optional.
  uint32_t mMaxFlatWorkGroupSize = 0;
  /// True if the generated machine code is using a dynamically sized
  /// call stack. Optional.
  bool mIsDynamicCallStack = false;
  /// True if the generated machine code is capable of supporting XNACK.
  /// Optional.
  bool mIsXNACKEnabled = false;
  /// Number of SGPRs spilled by a wavefront. Optional.
  uint16_t mNumSpilledSGPRs = 0;
  /// Number of VGPRs spilled by a workitem. Optional.
  uint16_t mNumSpilledVGPRs = 0;

  /// Default constructor.
  Metadata() = default;

  /// Returns whether kernel code properties metadata is empty.
  ///
  /// \returns True if kernel code properties metadata is empty, false
  /// otherwise.
  bool empty() const {
    return !notEmpty();
  }

  /// Returns whether kernel code properties metadata is not empty.
  ///
  /// \returns True if kernel code properties metadata is not empty, false
  /// otherwise.
  bool notEmpty() const {
    return true;
  }
};

} // end namespace CodeProps

//===----------------------------------------------------------------------===//
// Kernel Debug Properties Metadata.
//===----------------------------------------------------------------------===//
/// Kernel debug properties metadata.
namespace DebugProps {

/// Keys for kernel debug properties metadata fields.
namespace Key {
/// Key for Kernel::DebugProps::Metadata::mDebuggerABIVersion.
constexpr char DebuggerABIVersion[] = "DebuggerABIVersion";
/// Key for Kernel::DebugProps::Metadata::mReservedNumVGPRs.
constexpr char ReservedNumVGPRs[] = "ReservedNumVGPRs";
/// Key for Kernel::DebugProps::Metadata::mReservedFirstVGPR.
constexpr char ReservedFirstVGPR[] = "ReservedFirstVGPR";
/// Key for Kernel::DebugProps::Metadata::mPrivateSegmentBufferSGPR.
constexpr char PrivateSegmentBufferSGPR[] = "PrivateSegmentBufferSGPR";
/// Key for
///     Kernel::DebugProps::Metadata::mWavefrontPrivateSegmentOffsetSGPR.
constexpr char WavefrontPrivateSegmentOffsetSGPR[] =
    "WavefrontPrivateSegmentOffsetSGPR";
} // end namespace Key

/// In-memory representation of kernel debug properties metadata.
struct Metadata final {
  /// Debugger ABI version. Optional.
  std::vector<uint32_t> mDebuggerABIVersion = std::vector<uint32_t>();
  /// Consecutive number of VGPRs reserved for debugger use. Must be 0 if
  /// mDebuggerABIVersion is not set. Optional.
  uint16_t mReservedNumVGPRs = 0;
  /// First fixed VGPR reserved. Must be uint16_t(-1) if
  /// mDebuggerABIVersion is not set or mReservedFirstVGPR is 0. Optional.
  uint16_t mReservedFirstVGPR = uint16_t(-1);
  /// Fixed SGPR index of the scratch V# buffer.
  ///
  /// Fixed SGPR of the first of 4 SGPRs used to hold the scratch V# used
  /// for the entire kernel execution. Must be uint16_t(-1) if
  /// mDebuggerABIVersion is not set or SGPR not used or not known. Optional.
  uint16_t mPrivateSegmentBufferSGPR = uint16_t(-1);
  /// Fixed SGPR index of the wave scratch offset.
  ///
  /// Fixed SGPR used to hold the wave scratch offset for the entire
  /// kernel execution. Must be uint16_t(-1) if mDebuggerABIVersion is not set
  /// or SGPR is not used or not known. Optional.
  uint16_t mWavefrontPrivateSegmentOffsetSGPR = uint16_t(-1);

  /// Default constructor.
  Metadata() = default;

  /// Returns whether kernel debug properties metadata is empty.
  ///
  /// \returns True if kernel debug properties metadata is empty, false
  /// otherwise.
  bool empty() const {
    return !notEmpty();
  }

  /// Returns whether kernel debug properties metadata is not empty.
  ///
  /// \returns True if kernel debug properties metadata is not empty, false
  /// otherwise.
  bool notEmpty() const {
    return !mDebuggerABIVersion.empty();
  }
};

} // end namespace DebugProps

/// Keys for kernel metadata fields.
namespace Key {
/// Key for Kernel::Metadata::mName.
constexpr char Name[] = "Name";
/// Key for Kernel::Metadata::mSymbolName.
constexpr char SymbolName[] = "SymbolName";
/// Key for Kernel::Metadata::mLanguage.
constexpr char Language[] = "Language";
/// Key for Kernel::Metadata::mLanguageVersion.
constexpr char LanguageVersion[] = "LanguageVersion";
/// Key for Kernel::Metadata::mAttrs.
constexpr char Attrs[] = "Attrs";
/// Key for Kernel::Metadata::mArgs.
constexpr char Args[] = "Args";
/// Key for Kernel::Metadata::mCodeProps.
constexpr char CodeProps[] = "CodeProps";
/// Key for Kernel::Metadata::mDebugProps.
constexpr char DebugProps[] = "DebugProps";
} // end namespace Key

/// In-memory representation of kernel metadata.
struct Metadata final {
  /// Kernel source name. Required.
  std::string mName = std::string();
  /// Kernel descriptor name. Required.
  std::string mSymbolName = std::string();
  /// Language. Optional.
  std::string mLanguage = std::string();
  /// Language version. Optional.
  std::vector<uint32_t> mLanguageVersion = std::vector<uint32_t>();
  /// Attributes metadata. Optional.
  Attrs::Metadata mAttrs = Attrs::Metadata();
  /// Arguments metadata. Optional.
  std::vector<Arg::Metadata> mArgs = std::vector<Arg::Metadata>();
  /// Code properties metadata. Optional.
  CodeProps::Metadata mCodeProps = CodeProps::Metadata();
  /// Debug properties metadata. Optional.
  DebugProps::Metadata mDebugProps = DebugProps::Metadata();

  /// Default constructor.
  Metadata() = default;
};

} // end namespace Kernel

/// Keys for top-level HSA metadata fields.
namespace Key {
/// Key for HSA::Metadata::mVersion.
constexpr char Version[] = "Version";
/// Key for HSA::Metadata::mPrintf.
constexpr char Printf[] = "Printf";
/// Key for HSA::Metadata::mKernels.
constexpr char Kernels[] = "Kernels";
} // end namespace Key

/// In-memory representation of HSA metadata.
struct Metadata final {
  /// HSA metadata version. Required.
  std::vector<uint32_t> mVersion = std::vector<uint32_t>();
  /// Printf metadata. Optional.
  std::vector<std::string> mPrintf = std::vector<std::string>();
  /// Kernels metadata. Required.
  std::vector<Kernel::Metadata> mKernels = std::vector<Kernel::Metadata>();

  /// Default constructor.
  Metadata() = default;
};

/// Converts \p String to \p HSAMetadata.
///
/// \param String Input HSA metadata string.
/// \param HSAMetadata Output in-memory HSA metadata.
/// \returns A std::error_code indicating success or failure.
LLVM_ABI std::error_code fromString(StringRef String, Metadata &HSAMetadata);

/// Converts \p HSAMetadata to \p String.
///
/// \param HSAMetadata Input in-memory HSA metadata.
/// \param String Output HSA metadata string.
/// \returns A std::error_code indicating success or failure.
LLVM_ABI std::error_code toString(Metadata HSAMetadata, std::string &String);

//===----------------------------------------------------------------------===//
// HSA metadata for v3 code object.
//===----------------------------------------------------------------------===//
namespace V3 {
/// HSA metadata major version.
constexpr uint32_t VersionMajor = 1;
/// HSA metadata minor version.
constexpr uint32_t VersionMinor = 0;

/// HSA metadata beginning assembler directive.
constexpr char AssemblerDirectiveBegin[] = ".amdgpu_metadata";
/// HSA metadata ending assembler directive.
constexpr char AssemblerDirectiveEnd[] = ".end_amdgpu_metadata";
} // end namespace V3

} // end namespace HSAMD

//===----------------------------------------------------------------------===//
// PAL metadata.
//===----------------------------------------------------------------------===//
/// PAL (Platform Abstraction Layer) metadata definitions.
namespace PALMD {

/// PAL metadata (old linear format) assembler directive.
constexpr char AssemblerDirective[] = ".amd_amdgpu_pal_metadata";

/// PAL metadata (new MsgPack format) beginning assembler directive.
constexpr char AssemblerDirectiveBegin[] = ".amdgpu_pal_metadata";

/// PAL metadata (new MsgPack format) ending assembler directive.
constexpr char AssemblerDirectiveEnd[] = ".end_amdgpu_pal_metadata";

/// PAL metadata keys.
enum Key : uint32_t {
  /// Compute program resource register 1.
  R_2E12_COMPUTE_PGM_RSRC1 = 0x2e12,
  /// LS shader program resource register 1.
  R_2D4A_SPI_SHADER_PGM_RSRC1_LS = 0x2d4a,
  /// HS shader program resource register 1.
  R_2D0A_SPI_SHADER_PGM_RSRC1_HS = 0x2d0a,
  /// ES shader program resource register 1.
  R_2CCA_SPI_SHADER_PGM_RSRC1_ES = 0x2cca,
  /// GS shader program resource register 1.
  R_2C8A_SPI_SHADER_PGM_RSRC1_GS = 0x2c8a,
  /// VS shader program resource register 1.
  R_2C4A_SPI_SHADER_PGM_RSRC1_VS = 0x2c4a,
  /// PS shader program resource register 1.
  R_2C0A_SPI_SHADER_PGM_RSRC1_PS = 0x2c0a,
  /// Compute dispatch initiator register.
  R_2E00_COMPUTE_DISPATCH_INITIATOR = 0x2e00,
  /// PS input enable register.
  R_A1B3_SPI_PS_INPUT_ENA = 0xa1b3,
  /// PS input address register.
  R_A1B4_SPI_PS_INPUT_ADDR = 0xa1b4,
  /// PS input control register.
  R_A1B6_SPI_PS_IN_CONTROL = 0xa1b6,
  /// VGT shader stages enable register.
  R_A2D5_VGT_SHADER_STAGES_EN = 0xa2d5,

  /// Number of VGPRs used by the LS shader.
  LS_NUM_USED_VGPRS = 0x10000021,
  /// Number of VGPRs used by the HS shader.
  HS_NUM_USED_VGPRS = 0x10000022,
  /// Number of VGPRs used by the ES shader.
  ES_NUM_USED_VGPRS = 0x10000023,
  /// Number of VGPRs used by the GS shader.
  GS_NUM_USED_VGPRS = 0x10000024,
  /// Number of VGPRs used by the VS shader.
  VS_NUM_USED_VGPRS = 0x10000025,
  /// Number of VGPRs used by the PS shader.
  PS_NUM_USED_VGPRS = 0x10000026,
  /// Number of VGPRs used by the CS shader.
  CS_NUM_USED_VGPRS = 0x10000027,

  /// Number of SGPRs used by the LS shader.
  LS_NUM_USED_SGPRS = 0x10000028,
  /// Number of SGPRs used by the HS shader.
  HS_NUM_USED_SGPRS = 0x10000029,
  /// Number of SGPRs used by the ES shader.
  ES_NUM_USED_SGPRS = 0x1000002a,
  /// Number of SGPRs used by the GS shader.
  GS_NUM_USED_SGPRS = 0x1000002b,
  /// Number of SGPRs used by the VS shader.
  VS_NUM_USED_SGPRS = 0x1000002c,
  /// Number of SGPRs used by the PS shader.
  PS_NUM_USED_SGPRS = 0x1000002d,
  /// Number of SGPRs used by the CS shader.
  CS_NUM_USED_SGPRS = 0x1000002e,

  /// Scratch size in bytes for the LS shader.
  LS_SCRATCH_SIZE = 0x10000044,
  /// Scratch size in bytes for the HS shader.
  HS_SCRATCH_SIZE = 0x10000045,
  /// Scratch size in bytes for the ES shader.
  ES_SCRATCH_SIZE = 0x10000046,
  /// Scratch size in bytes for the GS shader.
  GS_SCRATCH_SIZE = 0x10000047,
  /// Scratch size in bytes for the VS shader.
  VS_SCRATCH_SIZE = 0x10000048,
  /// Scratch size in bytes for the PS shader.
  PS_SCRATCH_SIZE = 0x10000049,
  /// Scratch size in bytes for the CS shader.
  CS_SCRATCH_SIZE = 0x1000004a
};

} // end namespace PALMD
} // end namespace AMDGPU
} // end namespace llvm

#endif // LLVM_SUPPORT_AMDGPUMETADATA_H
