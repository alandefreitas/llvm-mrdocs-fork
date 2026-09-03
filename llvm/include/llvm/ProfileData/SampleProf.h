//===- SampleProf.h - Sampling profiling format support ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains common definitions used in the reading and writing of
// sample profile data.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_SAMPLEPROF_H
#define LLVM_PROFILEDATA_SAMPLEPROF_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Eytzinger.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SortedVectorMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/ProfileData/FunctionId.h"
#include "llvm/ProfileData/HashKeyMap.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace llvm {

class DILocation;
class raw_ostream;

/// Return the error category for sampleprof_error.
/// @return The std::error_category for sampleprof_error.
LLVM_ABI const std::error_category &sampleprof_category();

/// Error codes for sample profile reading and writing.
enum class sampleprof_error {
  /// No error.
  success = 0,
  /// Profile file has an invalid magic number.
  bad_magic,
  /// Profile format version is not supported.
  unsupported_version,
  /// Profile exceeds an implementation size limit.
  too_large,
  /// Profile stream ended before all data was read.
  truncated,
  /// Profile contents are malformed.
  malformed,
  /// Profile format could not be recognized.
  unrecognized_format,
  /// Requested profile writing format is not supported.
  unsupported_writing_format,
  /// Name table was truncated while reading.
  truncated_name_table,
  /// Requested operation is not implemented.
  not_implemented,
  /// A sample counter overflowed during accumulation.
  counter_overflow,
  /// Output stream does not support seeking required by the writer.
  ostream_seek_unsupported,
  /// Profile decompression failed.
  uncompress_failed,
  /// zlib support is unavailable in this build.
  zlib_unavailable,
  /// A profile hash did not match the expected value.
  hash_mismatch,
  /// A line offset value was illegal for the profile format.
  illegal_line_offset,
};

/// Convert \p E to a std::error_code.
/// @param E Sample profile error value to convert.
/// @return The corresponding std::error_code.
inline std::error_code make_error_code(sampleprof_error E) {
  return std::error_code(static_cast<int>(E), sampleprof_category());
}

/// Merge \p Result into \p Accumulator, preferring the first error.
/// @param Accumulator Error accumulator updated in place.
/// @param Result Newly observed error to merge.
/// @return The updated accumulator error value.
inline sampleprof_error mergeSampleProfErrors(sampleprof_error &Accumulator,
                                              sampleprof_error Result) {
  // Prefer first error encountered as later errors may be secondary effects of
  // the initial problem.
  if (Accumulator == sampleprof_error::success &&
      Result != sampleprof_error::success)
    Accumulator = Result;
  return Accumulator;
}

} // end namespace llvm

namespace std {

template <>
struct is_error_code_enum<llvm::sampleprof_error> : std::true_type {};

} // end namespace std

namespace llvm {
namespace sampleprof {

/// Prefix used when encoding vtable type profiles in name tables.
constexpr char kVTableProfPrefix[] = "vtables ";

/// On-disk sample profile format identifiers.
enum SampleProfileFormat {
  /// No format selected.
  SPF_None = 0,
  /// Text sample profile format.
  SPF_Text = 0x1,
  /// Deprecated compact binary format.
  SPF_Compact_Binary = 0x2, // Deprecated
  /// GCC sample profile format.
  SPF_GCC = 0x3,
  /// Extensible binary sample profile format.
  SPF_Ext_Binary = 0x4,
  /// Default binary sample profile format.
  SPF_Binary = 0xff
};

/// Layout of nested versus flattened sample profiles.
enum SampleProfileLayout {
  /// No layout selected.
  SPL_None = 0,
  /// Nested layout with callsite inlinee profiles.
  SPL_Nest = 0x1,
  /// Flattened layout without nested callsite profiles.
  SPL_Flat = 0x2,
};

static inline uint64_t SPMagic(SampleProfileFormat Format = SPF_Binary) {
  return uint64_t('S') << (64 - 8) | uint64_t('P') << (64 - 16) |
         uint64_t('R') << (64 - 24) | uint64_t('O') << (64 - 32) |
         uint64_t('F') << (64 - 40) | uint64_t('4') << (64 - 48) |
         uint64_t('2') << (64 - 56) | uint64_t(Format);
}

// The oldest version of the extensible binary format we support.
static constexpr uint64_t MinSupportedVersion = 103;

// The default version of the extensible binary profile format written by the
// compiler.  We default to v103 as v104 is work in progress.
static constexpr uint64_t DefaultVersion = 103;

// The latest supported version of the extensible binary profile format.
static constexpr uint64_t LatestVersion = 104;

// Query if a given format version is supported by this compiler.
static inline bool formatVersionIsSupported(uint64_t Version) {
  return Version >= MinSupportedVersion && Version <= LatestVersion;
}

// Unused.  Retained for downstream uses only.
LLVM_DEPRECATED("Use DefaultVersion or LatestVersion instead", "DefaultVersion")
static inline uint64_t SPVersion() { return 103; }

/// Section types used by extensible binary sample profile readers/writers.
///
/// Never change the existing value of enum. Only append new ones.
enum SecType {
  /// Invalid section type.
  SecInValid = 0,
  /// Profile summary section.
  SecProfSummary = 1,
  /// Name table section.
  SecNameTable = 2,
  /// Profile symbol list section.
  SecProfileSymbolList = 3,
  /// Function offset table section.
  SecFuncOffsetTable = 4,
  /// Function metadata section.
  SecFuncMetadata = 5,
  /// Context-sensitive name table section.
  SecCSNameTable = 6,
  /// Marker for the first type of profile section.
  SecFuncProfileFirst = 32,
  /// LBR function profile section.
  SecLBRProfile = SecFuncProfileFirst
};

static inline std::string getSecName(SecType Type) {
  switch (static_cast<int>(Type)) { // Avoid -Wcovered-switch-default
  case SecInValid:
    return "InvalidSection";
  case SecProfSummary:
    return "ProfileSummarySection";
  case SecNameTable:
    return "NameTableSection";
  case SecProfileSymbolList:
    return "ProfileSymbolListSection";
  case SecFuncOffsetTable:
    return "FuncOffsetTableSection";
  case SecFuncMetadata:
    return "FunctionMetadata";
  case SecCSNameTable:
    return "CSNameTableSection";
  case SecLBRProfile:
    return "LBRProfileSection";
  default:
    return "UnknownSection";
  }
}

/// One entry in the extensible binary section header table.
struct SecHdrTableEntry {
  /// Section type for this entry.
  SecType Type;
  /// Combined common and section-specific flags.
  uint64_t Flags;
  /// Byte offset of the section payload.
  uint64_t Offset;
  /// Byte size of the section payload.
  uint64_t Size;
  /// Index of this entry in the SectionHdrLayout table.
  uint64_t LayoutIndex;
};

/// Flags common to all extensible binary sample profile sections.
///
/// In SecHdrTableEntry::Flags, common flags are saved in the lower 32 bits and
/// section-specific flags are saved in the higher 32 bits.
enum class SecCommonFlags : uint32_t {
  /// No common flags set.
  SecFlagInValid = 0,
  /// Section payload is compressed.
  SecFlagCompress = (1 << 0),
  /// Section contains flat profiles (without callsite samples).
  SecFlagFlat = (1 << 1)
};

// Section specific flags are defined here.
// !!!Note: Everytime a new enum class is created here, please add
// a new check in verifySecFlag.
/// Flags specific to the name table section.
enum class SecNameTableFlags : uint32_t {
  /// No name-table flags set.
  SecFlagInValid = 0,
  /// Names are stored as MD5 hashes.
  SecFlagMD5Name = (1 << 0),
  /// Store MD5 in fixed length instead of ULEB128 so NameTable can be
  /// accessed like an array.
  SecFlagFixedLengthMD5 = (1 << 1),
  /// Profile contains ".__uniq." suffix name. Compiler shouldn't strip
  /// the suffix when doing profile matching when seeing the flag.
  SecFlagUniqSuffix = (1 << 2),
  /// Name table is stored in 3-span Eytzinger layout (Nested, Flat, Inlinees).
  SecFlagEytzinger = (1 << 3)
};

/// Span identifiers for an Eytzinger-layout name table.
enum class EytzingerSpan : size_t {
  /// Nested-context name span.
  Nested,
  /// Flat name span.
  Flat,
  /// Inlinee name span.
  Inlinee,
  /// Number of Eytzinger spans.
  NumSpans
};

/// Flags specific to the profile symbol list section.
enum class SecProfileSymbolListFlags : uint32_t {
  /// No profile-symbol-list flags set.
  SecFlagInValid = 0,
  /// Symbol list entries are MD5 hashes.
  SecFlagMD5 = (1 << 0)
};
/// Flags specific to the profile summary section.
enum class SecProfSummaryFlags : uint32_t {
  /// No profile-summary flags set.
  SecFlagInValid = 0,
  /// SecFlagPartial means the profile is for common/shared code.
  /// The common profile is usually merged from profiles collected
  /// from running other targets.
  SecFlagPartial = (1 << 0),
  /// SecFlagContext means this is context-sensitive flat profile for
  /// CSSPGO
  SecFlagFullContext = (1 << 1),
  /// SecFlagFSDiscriminator means this profile uses flow-sensitive
  /// discriminators.
  SecFlagFSDiscriminator = (1 << 2),
  /// SecFlagIsPreInlined means this profile contains ShouldBeInlined
  /// contexts thus this is CS preinliner computed.
  SecFlagIsPreInlined = (1 << 4),

  /// SecFlagHasVTableTypeProf means this profile contains vtable type profiles.
  SecFlagHasVTableTypeProf = (1 << 5),
};

/// Flags specific to the function metadata section.
enum class SecFuncMetadataFlags : uint32_t {
  /// No function-metadata flags set.
  SecFlagInvalid = 0,
  /// Function profiles are probe-based.
  SecFlagIsProbeBased = (1 << 0),
  /// Function profiles carry context attributes.
  SecFlagHasAttribute = (1 << 1),
};

/// Flags specific to the function offset table section.
enum class SecFuncOffsetFlags : uint32_t {
  /// No function-offset flags set.
  SecFlagInvalid = 0,
  /// Store function offsets in an order of contexts. The order ensures that
  /// callee contexts of a given context laid out next to it.
  SecFlagOrdered = (1 << 0),
  /// Store function offsets in a parallel array aligned with Eytzinger NameTable
  /// span.
  SecFlagEytzinger = (1 << 1),
};

/// Assert that \p Flag is legal for section \p Type.
/// @param Type Section type being flagged.
/// @param Flag Section flag value to verify.
template <class SecFlagType>
static inline void verifySecFlag(SecType Type, SecFlagType Flag) {
  // No verification is needed for common flags.
  if (std::is_same<SecCommonFlags, SecFlagType>())
    return;

  // Verification starts here for section specific flag.
  bool IsFlagLegal = false;
  switch (Type) {
  case SecNameTable:
    IsFlagLegal = std::is_same<SecNameTableFlags, SecFlagType>();
    break;
  case SecProfileSymbolList:
    IsFlagLegal = std::is_same<SecProfileSymbolListFlags, SecFlagType>();
    break;
  case SecProfSummary:
    IsFlagLegal = std::is_same<SecProfSummaryFlags, SecFlagType>();
    break;
  case SecFuncMetadata:
    IsFlagLegal = std::is_same<SecFuncMetadataFlags, SecFlagType>();
    break;
  case SecFuncOffsetTable:
    IsFlagLegal = std::is_same<SecFuncOffsetFlags, SecFlagType>();
    break;
  default:
    break;
  }
  if (!IsFlagLegal)
    llvm_unreachable("Misuse of a flag in an incompatible section");
}

/// Set \p Flag on section header \p Entry.
/// @param Entry Section header entry to update.
/// @param Flag Flag bit to set.
template <class SecFlagType>
static inline void addSecFlag(SecHdrTableEntry &Entry, SecFlagType Flag) {
  verifySecFlag(Entry.Type, Flag);
  auto FVal = static_cast<uint64_t>(Flag);
  bool IsCommon = std::is_same<SecCommonFlags, SecFlagType>();
  Entry.Flags |= IsCommon ? FVal : (FVal << 32);
}

/// Clear \p Flag on section header \p Entry.
/// @param Entry Section header entry to update.
/// @param Flag Flag bit to clear.
template <class SecFlagType>
static inline void removeSecFlag(SecHdrTableEntry &Entry, SecFlagType Flag) {
  verifySecFlag(Entry.Type, Flag);
  auto FVal = static_cast<uint64_t>(Flag);
  bool IsCommon = std::is_same<SecCommonFlags, SecFlagType>();
  Entry.Flags &= ~(IsCommon ? FVal : (FVal << 32));
}

/// Return true if section header \p Entry has \p Flag set.
/// @param Entry Section header entry to query.
/// @param Flag Flag bit to test.
/// @return True if \p Flag is set on \p Entry.
template <class SecFlagType>
static inline bool hasSecFlag(const SecHdrTableEntry &Entry, SecFlagType Flag) {
  verifySecFlag(Entry.Type, Flag);
  auto FVal = static_cast<uint64_t>(Flag);
  bool IsCommon = std::is_same<SecCommonFlags, SecFlagType>();
  return Entry.Flags & (IsCommon ? FVal : (FVal << 32));
}

/// Represents the relative location of an instruction.
///
/// Instruction locations are specified by the line offset from the
/// beginning of the function (marked by the line where the function
/// header is) and the discriminator value within that line.
///
/// The discriminator value is useful to distinguish instructions
/// that are on the same line but belong to different basic blocks
/// (e.g., the two post-increment instructions in "if (p) x++; else y++;").
struct LineLocation {
  /// Construct a location at line offset \p L with discriminator \p D.
  /// @param L Line offset from the start of the function.
  /// @param D Discriminator within that line.
  LineLocation(uint32_t L, uint32_t D) : LineOffset(L), Discriminator(D) {}

  /// Print this location to \p OS.
  /// @param OS Output stream that receives the printed location.
  LLVM_ABI void print(raw_ostream &OS) const;
  /// Dump this location to the debug stream.
  LLVM_ABI void dump() const;

  /// Serialize the line location to the output stream using ULEB128 encoding.
  /// @param OS Output stream that receives the serialized location.
  LLVM_ABI void serialize(raw_ostream &OS) const;

  /// Order locations by line offset, then discriminator.
  /// @param O Location to compare against.
  /// @return True if this location is ordered before \p O.
  bool operator<(const LineLocation &O) const {
    return std::tie(LineOffset, Discriminator) <
           std::tie(O.LineOffset, O.Discriminator);
  }

  /// Return true if this location equals \p O.
  /// @param O Location to compare against.
  /// @return True if the locations are equal.
  bool operator==(const LineLocation &O) const {
    return LineOffset == O.LineOffset && Discriminator == O.Discriminator;
  }

  /// Return true if this location differs from \p O.
  /// @param O Location to compare against.
  /// @return True if the locations differ.
  bool operator!=(const LineLocation &O) const {
    return LineOffset != O.LineOffset || Discriminator != O.Discriminator;
  }

  /// Return a hash code for this location.
  /// @return Hash code combining line offset and discriminator.
  uint64_t getHashCode() const {
    return ((uint64_t)Discriminator << 32) | LineOffset;
  }

  /// Line offset from the start of the function.
  uint32_t LineOffset;
  /// Discriminator distinguishing instructions on the same line.
  uint32_t Discriminator;
};

/// Print \p Loc to \p OS.
/// @param OS Output stream that receives the printed location.
/// @param Loc Line location to print.
/// @return The output stream \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const LineLocation &Loc);

} // end namespace sampleprof

/// DenseMapInfo specialization for sampleprof::LineLocation.
template <> struct DenseMapInfo<sampleprof::LineLocation> {
  /// Hash a LineLocation for DenseMap.
  /// @param Val Location to hash.
  /// @return Hash value for \p Val.
  static unsigned getHashValue(const sampleprof::LineLocation &Val) {
    return DenseMapInfo<uint64_t>::getHashValue(Val.getHashCode());
  }

  /// Return true if two LineLocations are equal.
  /// @param LHS Left-hand location.
  /// @param RHS Right-hand location.
  /// @return True if \p LHS equals \p RHS.
  static bool isEqual(const sampleprof::LineLocation &LHS,
                      const sampleprof::LineLocation &RHS) {
    return LHS == RHS;
  }
};

namespace sampleprof {

/// Map from C++ polymorphic class type (by vtable) to its sample counter.
///
/// TODO: The class name FunctionId should be renamed to SymbolId in a refactor
/// change.
using TypeCountMap = SortedVectorMap<FunctionId, uint64_t, 0>;

/// Write \p Map to the output stream. Keys are linearized using \p NameTable
/// and written as ULEB128. Values are written as ULEB128 as well.
/// @param Map Type-count map to serialize.
/// @param NameTable Table mapping function ids to linearized name indices.
/// @param OS Output stream that receives the serialized map.
/// @return Success, or an error if serialization fails.
LLVM_ABI std::error_code
serializeTypeMap(const TypeCountMap &Map,
                 const MapVector<FunctionId, uint32_t> &NameTable,
                 raw_ostream &OS);

/// Representation of a single sample record.
///
/// A sample record is represented by a positive integer value, which
/// indicates how frequently was the associated line location executed.
///
/// Additionally, if the associated location contains a function call,
/// the record will hold a list of all the possible called targets and the types
/// for virtual table dispatches. For direct calls, this will be the exact
/// function being invoked. For indirect calls (function pointers, virtual table
/// dispatch), this will be a list of one or more functions. For virtual table
/// dispatches, this record will also hold the type of the object.
class SampleRecord {
public:
  /// Pair of a called function id and its sample count.
  using CallTarget = std::pair<FunctionId, uint64_t>;
  /// Comparator that orders call targets by descending frequency, then name.
  struct CallTargetComparator {
    /// Return true if \p LHS should be ordered before \p RHS.
    /// @param LHS Left-hand call target.
    /// @param RHS Right-hand call target.
    /// @return True if \p LHS should be ordered before \p RHS.
    bool operator()(const CallTarget &LHS, const CallTarget &RHS) const {
      if (LHS.second != RHS.second)
        return LHS.second > RHS.second;

      return LHS.first < RHS.first;
    }
  };

  /// Call targets sorted by CallTargetComparator.
  using SortedCallTargetSet = SmallVector<CallTarget>;
  /// Map from called function id to sample count.
  using CallTargetMap = SortedVectorMap<FunctionId, uint64_t, 0>;
  /// Construct an empty sample record.
  SampleRecord() = default;

  /// Increment the number of samples for this record by \p S.
  /// Optionally scale sample count \p S by \p Weight.
  ///
  /// Sample counts accumulate using saturating arithmetic, to avoid wrapping
  /// around unsigned integers.
  /// @param S Sample count to add.
  /// @param Weight Optional scale factor applied to \p S.
  /// @return Success, or counter_overflow on saturating overflow.
  sampleprof_error addSamples(uint64_t S, uint64_t Weight = 1) {
    bool Overflowed;
    NumSamples = SaturatingMultiplyAdd(S, Weight, NumSamples, &Overflowed);
    return Overflowed ? sampleprof_error::counter_overflow
                      : sampleprof_error::success;
  }

  /// Decrease the number of samples for this record by \p S. Return the amout
  /// of samples actually decreased.
  /// @param S Sample count to subtract.
  /// @return Number of samples actually decreased.
  uint64_t removeSamples(uint64_t S) {
    if (S > NumSamples)
      S = NumSamples;
    NumSamples -= S;
    return S;
  }

  /// Add called function \p F with samples \p S.
  /// Optionally scale sample count \p S by \p Weight.
  ///
  /// Sample counts accumulate using saturating arithmetic, to avoid wrapping
  /// around unsigned integers.
  /// @param F Called target function id.
  /// @param S Sample count to add for \p F.
  /// @param Weight Optional scale factor applied to \p S.
  /// @return Success, or counter_overflow on saturating overflow.
  sampleprof_error addCalledTarget(FunctionId F, uint64_t S,
                                   uint64_t Weight = 1) {
    uint64_t &TargetSamples = CallTargets[F];
    bool Overflowed;
    TargetSamples =
        SaturatingMultiplyAdd(S, Weight, TargetSamples, &Overflowed);
    return Overflowed ? sampleprof_error::counter_overflow
                      : sampleprof_error::success;
  }

  /// Remove called function from the call target map. Return the target sample
  /// count of the called function.
  /// @param F Called target function id to remove.
  /// @return Sample count of the removed target, or zero if absent.
  uint64_t removeCalledTarget(FunctionId F) {
    uint64_t Count = 0;
    auto I = CallTargets.find(F);
    if (I != CallTargets.end()) {
      Count = I->second;
      CallTargets.erase(I);
    }
    return Count;
  }

  /// Return true if this sample record contains function calls.
  /// @return True if call targets are present.
  bool hasCalls() const { return !CallTargets.empty(); }

  /// Return the number of samples for this record.
  /// @return Body sample count for this record.
  uint64_t getSamples() const { return NumSamples; }
  /// Return the call targets collected in this sample record.
  /// The returned reference may be invalidated by subsequent modifications to
  /// this SampleRecord.
  /// @return Map from called function id to sample count.
  const CallTargetMap &getCallTargets() const LLVM_LIFETIME_BOUND {
    return CallTargets;
  }
  /// Return call targets sorted by descending frequency.
  /// @return Call targets ordered by descending frequency.
  SortedCallTargetSet getSortedCallTargets() const {
    return sortCallTargets(CallTargets);
  }

  /// Return the sum of all call-target sample counts.
  /// @return Sum of sample counts across all call targets.
  uint64_t getCallTargetSum() const {
    uint64_t Sum = 0;
    for (const auto &I : CallTargets)
      Sum += I.second;
    return Sum;
  }

  /// Sort call targets in descending order of call frequency.
  /// @param Targets Call-target map to sort.
  /// @return Call targets sorted by descending frequency.
  static SortedCallTargetSet sortCallTargets(const CallTargetMap &Targets) {
    auto SortedTargets = llvm::to_vector_of<CallTarget>(Targets);
    llvm::sort(SortedTargets, CallTargetComparator());
    return SortedTargets;
  }

  /// Prorate call targets by a distribution factor.
  /// @param Targets Call-target map to scale.
  /// @param DistributionFactor Factor multiplied into each target count.
  /// @return New map with each target count scaled by \p DistributionFactor.
  static const CallTargetMap adjustCallTargets(const CallTargetMap &Targets,
                                               float DistributionFactor) {
    CallTargetMap AdjustedTargets;
    for (const auto &[Target, Frequency] : Targets) {
      AdjustedTargets[Target] = Frequency * DistributionFactor;
    }
    return AdjustedTargets;
  }

  /// Merge the samples in \p Other into this record.
  /// Optionally scale sample counts by \p Weight.
  /// @param Other Sample record to merge from.
  /// @param Weight Optional scale factor applied to \p Other's counts.
  /// @return Success, or counter_overflow on saturating overflow.
  LLVM_ABI sampleprof_error merge(const SampleRecord &Other,
                                  uint64_t Weight = 1);
  /// Print this sample record to \p OS with indentation \p Indent.
  /// @param OS Output stream that receives the printed record.
  /// @param Indent Indentation level in spaces.
  LLVM_ABI void print(raw_ostream &OS, unsigned Indent) const;
  /// Dump this sample record to the debug stream.
  LLVM_ABI void dump() const;
  /// Serialize the sample record to the output stream using ULEB128 encoding.
  /// The \p NameTable is used to map function names to their IDs.
  /// @param OS Output stream that receives the serialized record.
  /// @param NameTable Table mapping function ids to linearized name indices.
  /// @return Success, or an error if serialization fails.
  LLVM_ABI std::error_code
  serialize(raw_ostream &OS,
            const MapVector<FunctionId, uint32_t> &NameTable) const;

  /// Return true if this sample record equals \p Other.
  /// @param Other Sample record to compare against.
  /// @return True if the sample records are equal.
  bool operator==(const SampleRecord &Other) const {
    return NumSamples == Other.NumSamples && CallTargets == Other.CallTargets;
  }

  /// Return true if this sample record differs from \p Other.
  /// @param Other Sample record to compare against.
  /// @return True if the sample records differ.
  bool operator!=(const SampleRecord &Other) const { return !(*this == Other); }

private:
  uint64_t NumSamples = 0;
  CallTargetMap CallTargets;
};

/// Print \p Sample to \p OS.
/// @param OS Output stream that receives the printed record.
/// @param Sample Sample record to print.
/// @return The output stream \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const SampleRecord &Sample);

/// State of context associated with FunctionSamples.
enum ContextStateMask {
  /// Profile without context.
  UnknownContext = 0x0,
  /// Full context profile from input profile.
  RawContext = 0x1,
  /// Synthetic context created for context promotion.
  SyntheticContext = 0x2,
  /// Profile for context that is inlined into caller.
  InlinedContext = 0x4,
  /// Profile for context merged into base profile.
  MergedContext = 0x8
};

/// Attribute of context associated with FunctionSamples.
enum ContextAttributeMask {
  /// No context attributes set.
  ContextNone = 0x0,
  /// Leaf of context was inlined in previous build.
  ContextWasInlined = 0x1,
  /// Leaf of context should be inlined.
  ContextShouldBeInlined = 0x2,
  /// Leaf of context is duplicated into the base profile.
  ContextDuplicatedIntoBase = 0x4,
};

/// Represents a context frame with profile function and line location.
struct SampleContextFrame {
  /// Function associated with this frame.
  FunctionId Func;
  /// Callsite or leaf location within the function.
  LineLocation Location;

  /// Construct a frame with an empty function and zero location.
  SampleContextFrame() : Location(0, 0) {}

  /// Construct a frame for \p Func at \p Location.
  /// @param Func Function id for this frame.
  /// @param Location Location associated with this frame.
  SampleContextFrame(FunctionId Func, LineLocation Location)
      : Func(Func), Location(Location) {}

  /// Return true if this frame equals \p That.
  /// @param That Frame to compare against.
  /// @return True if the frames are equal.
  bool operator==(const SampleContextFrame &That) const {
    return Location == That.Location && Func == That.Func;
  }

  /// Return true if this frame differs from \p That.
  /// @param That Frame to compare against.
  /// @return True if the frames differ.
  bool operator!=(const SampleContextFrame &That) const {
    return !(*this == That);
  }

  /// Format this frame as a string, optionally including the line location.
  /// @param OutputLineLocation If true, append line offset and discriminator.
  /// @return String representation of this frame.
  std::string toString(bool OutputLineLocation) const {
    std::ostringstream OContextStr;
    OContextStr << Func.str();
    if (OutputLineLocation) {
      OContextStr << ":" << Location.LineOffset;
      if (Location.Discriminator)
        OContextStr << "." << Location.Discriminator;
    }
    return OContextStr.str();
  }

  /// Return a hash code for this frame.
  /// @return Hash code for this context frame.
  uint64_t getHashCode() const {
    // Context frame hash is heavily used in llvm-profgen context-sensitive
    // pre-inliner. Use a lightweight hashing here to avoid speed regression.
    uint64_t NameHash = 0;
    if (Func.isStringRef())
      NameHash = std::hash<std::string>{}(Func.str());
    else
      NameHash = Func.getHashCode();
    uint64_t LocId = Location.getHashCode();
    return NameHash + (LocId << 5) + LocId;
  }
};

static inline hash_code hash_value(const SampleContextFrame &arg) {
  return arg.getHashCode();
}

/// Growable vector of SampleContextFrame values.
using SampleContextFrameVector = SmallVector<SampleContextFrame, 1>;
/// Immutable view of a sequence of SampleContextFrame values.
using SampleContextFrames = ArrayRef<SampleContextFrame>;

/// Hash functor for SampleContextFrameVector.
struct SampleContextFrameHash {
  /// Hash the frames in \p S.
  /// @param S Frame vector to hash.
  /// @return Combined hash of the frames in \p S.
  uint64_t operator()(const SampleContextFrameVector &S) const {
    return hash_combine_range(S);
  }
};

/// Calling context, leaf function, and state for a FunctionSamples profile.
///
/// Internally sample context is represented using ArrayRef, which is also the
/// input for constructing a `SampleContext`. It can accept and represent both
/// full context string as well as context-less function name.
/// For a CS profile, a full context vector can look like:
///    `main:3 _Z5funcAi:1 _Z8funcLeafi`
/// For a base CS profile without calling context, the context vector should only
/// contain the leaf frame name.
/// For a non-CS profile, the context vector should be empty.
class SampleContext {
public:
  /// Construct an empty unknown context.
  SampleContext() : State(UnknownContext), Attributes(ContextNone) {}

  /// Construct a context-less profile keyed by function name \p Name.
  /// @param Name Leaf function name.
  SampleContext(StringRef Name)
      : Func(Name), State(UnknownContext), Attributes(ContextNone) {
    assert(!Name.empty() && "Name is empty");
  }

  /// Construct a context-less profile keyed by function id \p Func.
  /// @param Func Leaf function id.
  SampleContext(FunctionId Func)
      : Func(Func), State(UnknownContext), Attributes(ContextNone) {}

  /// Construct a context from frame sequence \p Context.
  /// @param Context Calling-context frames including the leaf.
  /// @param CState Initial context state mask.
  SampleContext(SampleContextFrames Context,
                ContextStateMask CState = RawContext)
      : Attributes(ContextNone) {
    assert(!Context.empty() && "Context is empty");
    setContext(Context, CState);
  }

  /// Decode \p ContextStr and populate function, frames, and state.
  ///
  /// Example of input `ContextStr`: `[main:3 @ _Z5funcAi:1 @ _Z8funcLeafi]`
  /// @param ContextStr Encoded context string or bare function name.
  /// @param CSNameTable Table that owns decoded context frame vectors.
  /// @param CState Initial context state when a full context is present.
  SampleContext(StringRef ContextStr,
                std::list<SampleContextFrameVector> &CSNameTable,
                ContextStateMask CState = RawContext)
      : Attributes(ContextNone) {
    assert(!ContextStr.empty());
    // Note that `[]` wrapped input indicates a full context string, otherwise
    // it's treated as context-less function name only.
    bool HasContext = ContextStr.starts_with("[");
    if (!HasContext) {
      State = UnknownContext;
      Func = FunctionId(ContextStr);
    } else {
      CSNameTable.emplace_back();
      SampleContextFrameVector &Context = CSNameTable.back();
      createCtxVectorFromStr(ContextStr, Context);
      setContext(Context, CState);
    }
  }

  /// Create a context vector from a given context string and save it in
  /// `Context`.
  /// @param ContextStr Encoded full context string wrapped in brackets.
  /// @param Context Destination vector that receives decoded frames.
  static void createCtxVectorFromStr(StringRef ContextStr,
                                     SampleContextFrameVector &Context) {
    // Remove encapsulating '[' and ']' if any
    ContextStr = ContextStr.substr(1, ContextStr.size() - 2);
    StringRef ContextRemain = ContextStr;
    StringRef ChildContext;
    FunctionId Callee;
    while (!ContextRemain.empty()) {
      auto ContextSplit = ContextRemain.split(" @ ");
      ChildContext = ContextSplit.first;
      ContextRemain = ContextSplit.second;
      LineLocation CallSiteLoc(0, 0);
      decodeContextString(ChildContext, Callee, CallSiteLoc);
      Context.emplace_back(Callee, CallSiteLoc);
    }
  }

  /// Decode one frame string into function name and location.
  ///
  /// `ContextStr` is in the form of `FuncName:StartLine.Discriminator`.
  /// @param ContextStr Encoded single-frame context string.
  /// @param Func Destination function id for the frame.
  /// @param LineLoc Destination location for the frame.
  static void decodeContextString(StringRef ContextStr, FunctionId &Func,
                                  LineLocation &LineLoc) {
    // Get function name
    auto EntrySplit = ContextStr.split(':');
    Func = FunctionId(EntrySplit.first);

    LineLoc = {0, 0};
    if (!EntrySplit.second.empty()) {
      // Get line offset, use signed int for getAsInteger so string will
      // be parsed as signed.
      int LineOffset = 0;
      auto LocSplit = EntrySplit.second.split('.');
      LocSplit.first.getAsInteger(10, LineOffset);
      LineLoc.LineOffset = LineOffset;

      // Get discriminator
      if (!LocSplit.second.empty())
        LocSplit.second.getAsInteger(10, LineLoc.Discriminator);
    }
  }

  /// Convert this context to its frame view.
  /// @return Frame sequence view of this context.
  operator SampleContextFrames() const { return FullContext; }
  /// Return true if attribute \p A is set.
  /// @param A Attribute bit to test.
  /// @return True if attribute \p A is set.
  bool hasAttribute(ContextAttributeMask A) { return Attributes & (uint32_t)A; }
  /// Set attribute \p A.
  /// @param A Attribute bit to set.
  void setAttribute(ContextAttributeMask A) { Attributes |= (uint32_t)A; }
  /// Return the raw attribute bitmask.
  /// @return Combined context attribute bitmask.
  uint32_t getAllAttributes() { return Attributes; }
  /// Replace all attributes with bitmask \p A.
  /// @param A New attribute bitmask.
  void setAllAttributes(uint32_t A) { Attributes = A; }
  /// Return true if state bit \p S is set.
  /// @param S State bit to test.
  /// @return True if state bit \p S is set.
  bool hasState(ContextStateMask S) { return State & (uint32_t)S; }
  /// Set state bit \p S.
  /// @param S State bit to set.
  void setState(ContextStateMask S) { State |= (uint32_t)S; }
  /// Clear state bit \p S.
  /// @param S State bit to clear.
  void clearState(ContextStateMask S) { State &= (uint32_t)~S; }
  /// Return true if this profile has a non-unknown context.
  /// @return True if the context state is not UnknownContext.
  bool hasContext() const { return State != UnknownContext; }
  /// Return true if the context contains only the leaf frame.
  /// @return True if there is exactly one context frame.
  bool isBaseContext() const { return FullContext.size() == 1; }
  /// Return the leaf function id.
  /// @return Function id of the leaf frame.
  FunctionId getFunction() const { return Func; }
  /// Return the full calling-context frame sequence.
  /// @return Full context frames including the leaf.
  SampleContextFrames getContextFrames() const { return FullContext; }

  /// Format \p Context as a printable context string.
  /// @param Context Frames to format.
  /// @param IncludeLeafLineLocation If true, include the leaf frame location.
  /// @return Printable string for \p Context.
  static std::string getContextString(SampleContextFrames Context,
                                      bool IncludeLeafLineLocation = false) {
    std::ostringstream OContextStr;
    for (uint32_t I = 0; I < Context.size(); I++) {
      if (OContextStr.str().size()) {
        OContextStr << " @ ";
      }
      OContextStr << Context[I].toString(I != Context.size() - 1 ||
                                         IncludeLeafLineLocation);
    }
    return OContextStr.str();
  }

  /// Format this context as a printable string.
  /// @return Printable string for this context.
  std::string toString() const {
    if (!hasContext())
      return Func.str();
    return getContextString(FullContext, false);
  }

  /// Return a hash code for this context.
  /// @return Hash code for this sample context.
  uint64_t getHashCode() const {
    if (hasContext())
      return hash_value(getContextFrames());
    return getFunction().getHashCode();
  }

  /// Set the name of the function and clear the current context.
  /// @param NewFunctionID Function id that becomes the new leaf name.
  void setFunction(FunctionId NewFunctionID) {
    Func = NewFunctionID;
    FullContext = SampleContextFrames();
    State = UnknownContext;
  }

  /// Replace the full context frames and state.
  /// @param Context New calling-context frames including the leaf.
  /// @param CState New context state; must not be UnknownContext.
  void setContext(SampleContextFrames Context,
                  ContextStateMask CState = RawContext) {
    assert(CState != UnknownContext);
    FullContext = Context;
    Func = Context.back().Func;
    State = CState;
  }

  /// Return true if this context equals \p That.
  /// @param That Context to compare against.
  /// @return True if the contexts are equal.
  bool operator==(const SampleContext &That) const {
    return State == That.State && Func == That.Func &&
           FullContext == That.FullContext;
  }

  /// Return true if this context differs from \p That.
  /// @param That Context to compare against.
  /// @return True if the contexts differ.
  bool operator!=(const SampleContext &That) const { return !(*this == That); }

  /// Order contexts for sorted containers.
  /// @param That Context to compare against.
  /// @return True if this context is ordered before \p That.
  bool operator<(const SampleContext &That) const {
    if (State != That.State)
      return State < That.State;

    if (!hasContext()) {
      return Func < That.Func;
    }

    uint64_t I = 0;
    while (I < std::min(FullContext.size(), That.FullContext.size())) {
      auto &Context1 = FullContext[I];
      auto &Context2 = That.FullContext[I];
      auto V = Context1.Func.compare(Context2.Func);
      if (V)
        return V < 0;
      if (Context1.Location != Context2.Location)
        return Context1.Location < Context2.Location;
      I++;
    }

    return FullContext.size() < That.FullContext.size();
  }

  /// Hash functor for SampleContext.
  struct Hash {
    /// Hash \p Context.
    /// @param Context Context to hash.
    /// @return Hash code for \p Context.
    uint64_t operator()(const SampleContext &Context) const {
      return Context.getHashCode();
    }
  };

  /// Return true if this context is a prefix of \p That.
  /// @param That Context that may extend this one.
  /// @return True if this context is a prefix of \p That.
  bool isPrefixOf(const SampleContext &That) const {
    auto ThisContext = FullContext;
    auto ThatContext = That.FullContext;
    if (ThatContext.size() < ThisContext.size())
      return false;
    ThatContext = ThatContext.take_front(ThisContext.size());
    // Compare Leaf frame first
    if (ThisContext.back().Func != ThatContext.back().Func)
      return false;
    // Compare leading context
    return ThisContext.drop_back() == ThatContext.drop_back();
  }

private:
  // The function associated with this context. If CS profile, this is the leaf
  // function.
  FunctionId Func;
  // Full context including calling context and leaf function name
  SampleContextFrames FullContext;
  // State of the associated sample profile
  uint32_t State;
  // Attribute of the associated sample profile
  uint32_t Attributes;
};

static inline hash_code hash_value(const SampleContext &Context) {
  return Context.getHashCode();
}

/// Print \p Context to \p OS.
/// @param OS Output stream that receives the printed context.
/// @param Context Sample context to print.
/// @return The output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const SampleContext &Context) {
  return OS << Context.toString();
}

class FunctionSamples;
class SampleProfileReaderItaniumRemapper;

/// Map from line location to body sample record.
using BodySampleMap = SortedVectorMap<LineLocation, SampleRecord, 0>;
// NOTE: Using a StringMap here makes parsed profiles consume around 17% more
// memory, which is *very* significant for large profiles.
/// Map from callee function id to nested FunctionSamples.
using FunctionSamplesMap = std::map<FunctionId, FunctionSamples>;
/// Map from callsite location to nested callee FunctionSamples.
using CallsiteSampleMap = std::map<LineLocation, FunctionSamplesMap>;
/// Map from callsite location to vtable/type sample counts.
using CallsiteTypeMap = SortedVectorMap<LineLocation, TypeCountMap, 0>;
/// Map remapping IR locations to matched profile locations.
using LocToLocMap = DenseMap<LineLocation, LineLocation>;

/// Representation of the samples collected for a function.
///
/// This data structure contains all the collected samples for the body
/// of a function. Each sample corresponds to a LineLocation instance
/// within the body of the function.
class FunctionSamples {
public:
  /// Construct an empty function sample profile.
  FunctionSamples() = default;

  /// Print this function profile to \p OS with indentation \p Indent.
  /// @param OS Output stream that receives the printed profile.
  /// @param Indent Indentation level in spaces.
  LLVM_ABI void print(raw_ostream &OS = dbgs(), unsigned Indent = 0) const;
  /// Dump this function profile to the debug stream.
  LLVM_ABI void dump() const;

  /// Add \p Num total samples, optionally scaled by \p Weight.
  /// @param Num Sample count to add to the function total.
  /// @param Weight Optional scale factor applied to \p Num.
  /// @return Success, or counter_overflow on saturating overflow.
  sampleprof_error addTotalSamples(uint64_t Num, uint64_t Weight = 1) {
    bool Overflowed;
    TotalSamples =
        SaturatingMultiplyAdd(Num, Weight, TotalSamples, &Overflowed);
    return Overflowed ? sampleprof_error::counter_overflow
                      : sampleprof_error::success;
  }

  /// Decrease total samples by \p Num, saturating at zero.
  /// @param Num Sample count to subtract from the function total.
  void removeTotalSamples(uint64_t Num) {
    if (TotalSamples < Num)
      TotalSamples = 0;
    else
      TotalSamples -= Num;
  }

  /// Set the total sample count to \p Num.
  /// @param Num New total sample count.
  void setTotalSamples(uint64_t Num) { TotalSamples = Num; }

  /// Set the head sample count to \p Num.
  /// @param Num New head sample count.
  void setHeadSamples(uint64_t Num) { TotalHeadSamples = Num; }

  /// Add \p Num head samples, optionally scaled by \p Weight.
  /// @param Num Sample count to add to the function head.
  /// @param Weight Optional scale factor applied to \p Num.
  /// @return Success, or counter_overflow on saturating overflow.
  sampleprof_error addHeadSamples(uint64_t Num, uint64_t Weight = 1) {
    bool Overflowed;
    TotalHeadSamples =
        SaturatingMultiplyAdd(Num, Weight, TotalHeadSamples, &Overflowed);
    return Overflowed ? sampleprof_error::counter_overflow
                      : sampleprof_error::success;
  }

  /// Add \p Num body samples at \p LineOffset/\p Discriminator.
  /// @param LineOffset Line offset from the start of the function.
  /// @param Discriminator Discriminator within that line.
  /// @param Num Sample count to add at the location.
  /// @param Weight Optional scale factor applied to \p Num.
  /// @return Success, or counter_overflow on saturating overflow.
  sampleprof_error addBodySamples(uint32_t LineOffset, uint32_t Discriminator,
                                  uint64_t Num, uint64_t Weight = 1) {
    return BodySamples[LineLocation(LineOffset, Discriminator)].addSamples(
        Num, Weight);
  }

  /// Add \p Num called-target samples for \p Func at a body location.
  /// @param LineOffset Line offset from the start of the function.
  /// @param Discriminator Discriminator within that line.
  /// @param Func Called target function id.
  /// @param Num Sample count to add for the target.
  /// @param Weight Optional scale factor applied to \p Num.
  /// @return Success, or counter_overflow on saturating overflow.
  sampleprof_error addCalledTargetSamples(uint32_t LineOffset,
                                          uint32_t Discriminator,
                                          FunctionId Func, uint64_t Num,
                                          uint64_t Weight = 1) {
    return BodySamples[LineLocation(LineOffset, Discriminator)].addCalledTarget(
        Func, Num, Weight);
  }

  /// Merge \p SampleRecord into the body samples at \p Location.
  /// @param Location Body location that receives the record.
  /// @param SampleRecord Sample record to merge.
  /// @param Weight Optional scale factor applied to \p SampleRecord.
  /// @return Success, or counter_overflow on saturating overflow.
  sampleprof_error addSampleRecord(LineLocation Location,
                                   const SampleRecord &SampleRecord,
                                   uint64_t Weight = 1) {
    return BodySamples[Location].merge(SampleRecord, Weight);
  }

  /// Reserve capacity for at least \p NumEntries body sample entries.
  /// @param NumEntries Number of body sample slots to reserve.
  void reserveBodySamples(size_t NumEntries) {
    BodySamples.reserve(NumEntries);
  }

  /// Reserve capacity for at least \p NumEntries callsite type entries.
  /// @param NumEntries Number of callsite type slots to reserve.
  void reserveCallsiteTypeCounts(size_t NumEntries) {
    VirtualCallsiteTypeCounts.reserve(NumEntries);
  }

  /// Remove a call target and decrease body samples correspondingly.
  ///
  /// Return the number of body samples actually decreased.
  /// @param LineOffset Line offset from the start of the function.
  /// @param Discriminator Discriminator within that line.
  /// @param Func Called target function id to remove.
  /// @return Number of body samples actually decreased.
  uint64_t removeCalledTargetAndBodySample(uint32_t LineOffset,
                                           uint32_t Discriminator,
                                           FunctionId Func) {
    uint64_t Count = 0;
    auto I = BodySamples.find(LineLocation(LineOffset, Discriminator));
    if (I != BodySamples.end()) {
      Count = I->second.removeCalledTarget(Func);
      Count = I->second.removeSamples(Count);
      if (!I->second.getSamples())
        BodySamples.erase(I);
    }
    return Count;
  }

  /// Remove all callsite samples for inlinees when flattening a nested profile.
  void removeAllCallsiteSamples() { CallsiteSamples.clear(); }

  /// Accumulate call-target samples so each body count is at least their sum.
  void updateCallsiteSamples() {
    for (auto &I : BodySamples) {
      uint64_t TargetSamples = I.second.getCallTargetSum();
      // It's possible that the body sample count can be greater than the call
      // target sum. E.g, if some call targets are external targets, they won't
      // be considered valid call targets, but the body sample count which is
      // from lbr ranges can actually include them.
      if (TargetSamples > I.second.getSamples())
        I.second.addSamples(TargetSamples - I.second.getSamples());
    }
  }

  /// Recompute total samples from body samples and nested callsite samples.
  void updateTotalSamples() {
    setTotalSamples(0);
    for (const auto &I : BodySamples)
      addTotalSamples(I.second.getSamples());

    for (auto &I : CallsiteSamples) {
      for (auto &CS : I.second) {
        CS.second.updateTotalSamples();
        addTotalSamples(CS.second.getTotalSamples());
      }
    }
  }

  /// Mark this context and all callee contexts as synthetic.
  void setContextSynthetic() {
    Context.setState(SyntheticContext);
    for (auto &I : CallsiteSamples) {
      for (auto &CS : I.second) {
        CS.second.setContextSynthetic();
      }
    }
  }

  /// Propagate \p Attr to this profile context and all callee contexts.
  /// @param Attr Context attribute to set on this profile and callees.
  void setContextAttribute(ContextAttributeMask Attr) {
    Context.setAttribute(Attr);
    for (auto &I : CallsiteSamples) {
      for (auto &CS : I.second) {
        CS.second.setContextAttribute(Attr);
      }
    }
  }

  /// Remap an IR location to its matched profile location if available.
  /// @param IRLoc IR location to remap through stale-profile matching.
  /// @return Matched profile location, or \p IRLoc if unmapped.
  const LineLocation &mapIRLocToProfileLoc(const LineLocation &IRLoc) const {
    // There is no remapping if the profile is not stale or the matching gives
    // the same location.
    if (!IRToProfileLocationMap)
      return IRLoc;
    const auto &ProfileLoc = IRToProfileLocationMap->find(IRLoc);
    if (ProfileLoc != IRToProfileLocationMap->end())
      return ProfileLoc->second;
    return IRLoc;
  }

  /// Return the number of samples collected at the given location.
  ///
  /// Each location is specified by \p LineOffset and \p Discriminator.
  /// If the location is not found in profile, return error.
  /// @param LineOffset Line offset from the start of the function.
  /// @param Discriminator Discriminator within that line.
  /// @return Sample count at the location, or an error if missing.
  ErrorOr<uint64_t> findSamplesAt(uint32_t LineOffset,
                                  uint32_t Discriminator) const {
    const auto &Ret = BodySamples.find(
        mapIRLocToProfileLoc(LineLocation(LineOffset, Discriminator)));
    if (Ret == BodySamples.end())
      return std::error_code();
    return Ret->second.getSamples();
  }

  /// Return the call target map collected at a given location.
  ///
  /// Each location is specified by \p LineOffset and \p Discriminator.
  /// If the location is not found in profile, return error.
  /// The returned reference may be invalidated by subsequent modifications to
  /// this FunctionSamples.
  /// @param LineOffset Line offset from the start of the function.
  /// @param Discriminator Discriminator within that line.
  /// @return Call-target map at the location, or an error if missing.
  ErrorOr<const SampleRecord::CallTargetMap &>
  findCallTargetMapAt(uint32_t LineOffset,
                      uint32_t Discriminator) const LLVM_LIFETIME_BOUND {
    const auto &Ret = BodySamples.find(
        mapIRLocToProfileLoc(LineLocation(LineOffset, Discriminator)));
    if (Ret == BodySamples.end())
      return std::error_code();
    return Ret->second.getCallTargets();
  }

  /// Return the call target map collected at \p CallSite.
  ///
  /// If the location is not found in profile, return error.
  /// The returned reference may be invalidated by subsequent modifications to
  /// this FunctionSamples.
  /// @param CallSite Callsite location whose targets are requested.
  /// @return Call-target map at \p CallSite, or an error if missing.
  ErrorOr<const SampleRecord::CallTargetMap &>
  findCallTargetMapAt(const LineLocation &CallSite) const LLVM_LIFETIME_BOUND {
    const auto &Ret = BodySamples.find(mapIRLocToProfileLoc(CallSite));
    if (Ret == BodySamples.end())
      return std::error_code();
    return Ret->second.getCallTargets();
  }

  /// Return the function samples at the given callsite location.
  /// @param Loc Callsite location whose inlinee samples are requested.
  /// @return Mutable map of inlinee FunctionSamples at \p Loc.
  FunctionSamplesMap &
  functionSamplesAt(const LineLocation &Loc) LLVM_LIFETIME_BOUND {
    return CallsiteSamples[mapIRLocToProfileLoc(Loc)];
  }

  /// Returns the FunctionSamplesMap at the given \p Loc.
  /// @param Loc Callsite location to look up.
  /// @return Pointer to the callsite map, or nullptr if absent.
  const FunctionSamplesMap *
  findFunctionSamplesMapAt(const LineLocation &Loc) const LLVM_LIFETIME_BOUND {
    auto Iter = CallsiteSamples.find(mapIRLocToProfileLoc(Loc));
    if (Iter == CallsiteSamples.end())
      return nullptr;
    return &Iter->second;
  }

  /// Returns the TypeCountMap for inlined callsites at the given \p Loc.
  /// The returned pointer may be invalidated by subsequent modifications to
  /// this FunctionSamples.
  /// @param Loc Callsite location whose type samples are requested.
  /// @return Pointer to type samples at \p Loc, or nullptr if absent.
  const TypeCountMap *
  findCallsiteTypeSamplesAt(const LineLocation &Loc) const LLVM_LIFETIME_BOUND {
    auto Iter = VirtualCallsiteTypeCounts.find(mapIRLocToProfileLoc(Loc));
    if (Iter == VirtualCallsiteTypeCounts.end())
      return nullptr;
    return &Iter->second;
  }

  /// Find FunctionSamples at callsite \p Loc for callee \p CalleeName.
  ///
  /// If no callsite can be found, relax the restriction to return the
  /// FunctionSamples at callsite location \p Loc with the maximum total sample
  /// count. If \p Remapper or \p FuncNameToProfNameMap is not nullptr, use them
  /// to find FunctionSamples with equivalent name as \p CalleeName.
  /// @param Loc Callsite location to search.
  /// @param CalleeName Expected callee name at the callsite.
  /// @param Remapper Optional remapper used to find equivalent function names.
  /// @param FuncNameToProfNameMap Optional map from IR names to profile names.
  /// @return Matching FunctionSamples, or nullptr if none found.
  LLVM_ABI const FunctionSamples *findFunctionSamplesAt(
      const LineLocation &Loc, StringRef CalleeName,
      SampleProfileReaderItaniumRemapper *Remapper,
      const HashKeyMap<DenseMap, FunctionId, FunctionId>
          *FuncNameToProfNameMap = nullptr) const LLVM_LIFETIME_BOUND;

  /// Return true if this function profile has no samples.
  /// @return True if the total sample count is zero.
  bool empty() const { return TotalSamples == 0; }

  /// Return the total number of samples collected inside the function.
  /// @return Total samples including inlined callees.
  uint64_t getTotalSamples() const { return TotalSamples; }

  /// Return branch samples targeting this top-level function, or zero.
  ///
  /// This is the raw data fetched from the profile. This should be equivalent
  /// to the sample of the first instruction of the symbol. But as we directly
  /// get this info for raw profile without referring to potentially inaccurate
  /// debug info, this gives more accurate profile data and is preferred for
  /// standalone symbols.
  /// @return Head sample count from the profile, or zero.
  uint64_t getHeadSamples() const { return TotalHeadSamples; }

  /// Estimate the sample count of the function entry basic block.
  ///
  /// The function can be either a standalone symbol or an inlined function.
  /// For Context-Sensitive profiles, this will prefer returning the head
  /// samples (i.e. getHeadSamples()), if non-zero. Otherwise it estimates from
  /// the function body's samples or callsite samples.
  /// @return Estimated entry basic-block sample count.
  uint64_t getHeadSamplesEstimate() const {
      if (FunctionSamples::ProfileIsCS && getHeadSamples()) {
        // For CS profile, if we already have more accurate head samples
        // counted by branch sample from caller, use them as entry samples.
        return getHeadSamples();
      }
      uint64_t Count = 0;
      // Use either BodySamples or CallsiteSamples which ever has the smaller
      // lineno.
      if (!BodySamples.empty() &&
          (CallsiteSamples.empty() ||
           BodySamples.begin()->first < CallsiteSamples.begin()->first))
        Count = BodySamples.begin()->second.getSamples();
      else if (!CallsiteSamples.empty()) {
        // An indirect callsite may be promoted to several inlined direct calls.
        // We need to get the sum of them.
        for (const auto &FuncSamples : CallsiteSamples.begin()->second)
          Count += FuncSamples.second.getHeadSamplesEstimate();
      }
      // Return at least 1 if total sample is not 0.
      return Count ? Count : TotalSamples > 0;
    }

    /// Return all the samples collected in the body of the function.
    /// The returned reference may be invalidated by subsequent modifications to
    /// this FunctionSamples.
    /// @return Map from body locations to sample records.
    const BodySampleMap &getBodySamples() const LLVM_LIFETIME_BOUND {
      return BodySamples;
    }

    /// Return all the callsite samples collected in the body of the function.
    /// @return Map from callsite locations to nested FunctionSamples.
    const CallsiteSampleMap &getCallsiteSamples() const LLVM_LIFETIME_BOUND {
      return CallsiteSamples;
    }

    /// Return whether this function profile contains callsite samples.
    /// @return True if callsite samples are present.
    bool hasCallsiteSamples() const { return !CallsiteSamples.empty(); }

  /// Return vtable access samples collected for C++ types in this function.
  ///
  /// The returned reference may be invalidated by subsequent modifications to
  /// this FunctionSamples.
  /// @return Map from callsite locations to type sample counts.
  const CallsiteTypeMap &getCallsiteTypeCounts() const LLVM_LIFETIME_BOUND {
    return VirtualCallsiteTypeCounts;
  }

  /// Return vtable access samples for C++ types at \p Loc.
  ///
  /// Under the hood, the caller-specified \p Loc will be un-drifted before the
  /// type sample lookup if possible.
  /// The returned reference may be invalidated by subsequent modifications to
  /// this FunctionSamples.
  /// @param Loc Callsite location whose type samples are requested.
  /// @return Mutable type-count map at \p Loc.
  TypeCountMap &getTypeSamplesAt(const LineLocation &Loc) LLVM_LIFETIME_BOUND {
    return VirtualCallsiteTypeCounts[mapIRLocToProfileLoc(Loc)];
  }

  /// Add a type sample for \p Type with \p Count at location \p Loc.
  ///
  /// This function uses saturating add which clamp the result to
  /// maximum uint64_t (the counter type), and inserts the saturating add result
  /// to map. Returns counter_overflow to caller if the actual result is larger
  /// than maximum uint64_t.
  /// @param Loc Location at which the type sample is recorded.
  /// @param Type Vtable/type id being sampled.
  /// @param Count Sample count to add for \p Type.
  /// @return Success, or counter_overflow on saturating overflow.
  sampleprof_error addTypeSamplesAt(const LineLocation &Loc, FunctionId Type,
                                    uint64_t Count) {
    auto &TypeCounts = getTypeSamplesAt(Loc);
    bool Overflowed = false;
    TypeCounts[Type] = SaturatingMultiplyAdd(Count, /* Weight= */ (uint64_t)1,
                                             TypeCounts[Type], &Overflowed);
    return Overflowed ? sampleprof_error::counter_overflow
                      : sampleprof_error::success;
  }

  /// Scale \p Other and add the result to type samples at \p Loc.
  ///
  /// Under the hood, the caller-provided \p Loc will be un-drifted before the
  /// type sample lookup if possible.
  /// typename T is either a std::map or a DenseMap.
  /// @param Loc Location at which type samples are accumulated.
  /// @param Other Map of type ids to sample counts to merge.
  /// @param Weight Optional scale factor applied to counts in \p Other.
  /// @return Success, or counter_overflow on saturating overflow.
  template <typename T>
  sampleprof_error addCallsiteVTableTypeProfAt(const LineLocation &Loc,
                                               const T &Other,
                                               uint64_t Weight = 1) {
      static_assert((std::is_same_v<typename T::key_type, StringRef> ||
                     std::is_same_v<typename T::key_type, FunctionId>) &&
                        std::is_same_v<typename T::mapped_type, uint64_t>,
                    "T must be a map with StringRef or FunctionId as key and "
                    "uint64_t as value");
      TypeCountMap &TypeCounts = getTypeSamplesAt(Loc);
      TypeCounts.reserve(TypeCounts.size() + Other.size());
      bool Overflowed = false;

      for (const auto &[Type, Count] : Other) {
        FunctionId TypeId(Type);
        bool RowOverflow = false;
        TypeCounts[TypeId] = SaturatingMultiplyAdd(
            Count, Weight, TypeCounts[TypeId], &RowOverflow);
        Overflowed |= RowOverflow;
      }
      return Overflowed ? sampleprof_error::counter_overflow
                        : sampleprof_error::success;
    }

  /// Return the maximum body sample count inside this function.
  ///
  /// When SkipCallSite is false, which is the default, the return count includes
  /// samples in the inlined functions. When SkipCallSite is true, the return
  /// count only considers the body samples.
  /// @param SkipCallSite If true, ignore samples from inlined callsites.
  /// @return Maximum sample count found inside this function.
  uint64_t getMaxCountInside(bool SkipCallSite = false) const {
    uint64_t MaxCount = 0;
    for (const auto &L : getBodySamples())
      MaxCount = std::max(MaxCount, L.second.getSamples());
    if (SkipCallSite)
      return MaxCount;
    for (const auto &C : getCallsiteSamples())
      for (const FunctionSamplesMap::value_type &F : C.second)
        MaxCount = std::max(MaxCount, F.second.getMaxCountInside());
    return MaxCount;
  }

  /// Merge the samples in \p Other into this one.
  /// Optionally scale samples by \p Weight.
  /// @param Other FunctionSamples to merge from.
  /// @param Weight Optional scale factor applied to \p Other's sample counts.
  /// @return Success, or the first error encountered while merging.
  sampleprof_error merge(const FunctionSamples &Other, uint64_t Weight = 1) {
      sampleprof_error Result = sampleprof_error::success;
      if (!GUIDToFuncNameMap)
        GUIDToFuncNameMap = Other.GUIDToFuncNameMap;
      if (Context.getFunction().empty())
        Context = Other.getContext();
      if (FunctionHash == 0) {
        // Set the function hash code for the target profile.
        FunctionHash = Other.getFunctionHash();
      } else if (FunctionHash != Other.getFunctionHash()) {
        // The two profiles coming with different valid hash codes indicates
        // either:
        // 1. They are same-named static functions from different compilation
        // units (without using -unique-internal-linkage-names), or
        // 2. They are really the same function but from different compilations.
        // Let's bail out in either case for now, which means one profile is
        // dropped.
        return sampleprof_error::hash_mismatch;
      }

      mergeSampleProfErrors(Result,
                            addTotalSamples(Other.getTotalSamples(), Weight));
      mergeSampleProfErrors(Result,
                            addHeadSamples(Other.getHeadSamples(), Weight));
      BodySamples.reserve(BodySamples.size() + Other.getBodySamples().size());
      for (const auto &I : Other.getBodySamples()) {
        const LineLocation &Loc = I.first;
        const SampleRecord &Rec = I.second;
        mergeSampleProfErrors(Result, BodySamples[Loc].merge(Rec, Weight));
      }
      for (const auto &I : Other.getCallsiteSamples()) {
        const LineLocation &Loc = I.first;
        FunctionSamplesMap &FSMap = functionSamplesAt(Loc);
        for (const auto &Rec : I.second)
          mergeSampleProfErrors(Result,
                                FSMap[Rec.first].merge(Rec.second, Weight));
      }
      VirtualCallsiteTypeCounts.reserve(VirtualCallsiteTypeCounts.size() +
                                        Other.getCallsiteTypeCounts().size());
      for (const auto &[Loc, OtherTypeMap] : Other.getCallsiteTypeCounts())
        mergeSampleProfErrors(
            Result, addCallsiteVTableTypeProfAt(Loc, OtherTypeMap, Weight));

      return Result;
    }

  /// Collect GUIDs of hot inlined callees and call targets into \p S.
  ///
  /// Recursively traverses all children, if the total sample count of the
  /// corresponding function is no less than \p Threshold, add its corresponding
  /// GUID to \p S. Also traverse the BodySamples to add hot CallTarget's GUID
  /// to \p S.
  /// @param S Set that receives GUIDs of hot functions to import.
  /// @param SymbolMap Map from profile function ids to IR Function pointers.
  /// @param Threshold Minimum total samples required for a function to be hot.
  void findInlinedFunctions(
      DenseSet<GlobalValue::GUID> &S,
      const HashKeyMap<DenseMap, FunctionId, Function *> &SymbolMap,
      uint64_t Threshold) const {
      if (TotalSamples <= Threshold)
        return;
      auto IsDeclaration = [](const Function *F) {
        return !F || F->isDeclaration();
      };
      if (IsDeclaration(SymbolMap.lookup(getFunction()))) {
        // Add to the import list only when it's defined out of module.
        S.insert(getGUID());
      }
      // Import hot CallTargets, which may not be available in IR because full
      // profile annotation cannot be done until backend compilation in ThinLTO.
      for (const auto &BS : BodySamples)
        for (const auto &TS : BS.second.getCallTargets())
          if (TS.second > Threshold) {
            const Function *Callee = SymbolMap.lookup(TS.first);
            if (IsDeclaration(Callee))
              S.insert(TS.first.getHashCode());
          }
      for (const auto &CS : CallsiteSamples)
        for (const auto &NameFS : CS.second)
          NameFS.second.findInlinedFunctions(S, SymbolMap, Threshold);
    }

  /// Set the name of the function.
  /// @param NewFunctionID Function id to assign as this profile's name.
  void setFunction(FunctionId NewFunctionID) {
    Context.setFunction(NewFunctionID);
  }

  /// Return the function name.
  /// @return Function id for this profile.
  FunctionId getFunction() const { return Context.getFunction(); }

  /// Return the original function name.
  /// @return Original function name string.
  StringRef getFuncName() const { return getFuncName(getFunction()); }

  /// Set the CFG hash for this function profile.
  /// @param Hash CFG hash value to store.
  void setFunctionHash(uint64_t Hash) { FunctionHash = Hash; }

  /// Return the CFG hash for this function profile.
  /// @return Stored CFG hash value.
  uint64_t getFunctionHash() const { return FunctionHash; }

  /// Install an IR-to-profile location remapping table.
  /// @param LTLM Map from IR locations to matched profile locations.
  void setIRToProfileLocationMap(const LocToLocMap *LTLM) {
    assert(IRToProfileLocationMap == nullptr && "this should be set only once");
    IRToProfileLocationMap = LTLM;
  }

  /// Return the canonical name for a function, taking into account
  /// suffix elision policy attributes.
  /// @param F Function whose canonical sample-profile name is computed.
  /// @return Canonical function name for sample-profile matching.
  static StringRef getCanonicalFnName(const Function &F) {
    const char *AttrName = "sample-profile-suffix-elision-policy";
    auto Attr = F.getFnAttribute(AttrName).getValueAsString();
    return getCanonicalFnName(F.getName(), Attr);
  }

  /// Name suffixes which canonicalization should handle to avoid
  /// profile mismatch.
  static constexpr const char *LLVMSuffix = ".llvm.";
  /// Suffix introduced by function outlining/partial specialization.
  static constexpr const char *PartSuffix = ".part.";
  /// Suffix introduced by unique-internal-linkage renaming.
  static constexpr const char *UniqSuffix = ".__uniq.";

  /// Canonicalize \p FnName using the default known suffix list.
  /// @param FnName Function name to canonicalize.
  /// @param Attr Suffix elision policy ("selected", "all", "none", or empty).
  /// @return Canonicalized form of \p FnName.
  static StringRef getCanonicalFnName(StringRef FnName,
                                      StringRef Attr = "selected") {
    // Note the sequence of the suffixes in the knownSuffixes array matters.
    // If suffix "A" is appended after the suffix "B", "A" should be in front
    // of "B" in knownSuffixes.
    const SmallVector<StringRef> KnownSuffixes{LLVMSuffix, PartSuffix,
                                               UniqSuffix};
    return getCanonicalFnName(FnName, KnownSuffixes, Attr);
  }

  /// Canonicalize a coroutine function name, stripping coro and llvm suffixes.
  /// @param FnName Function name to canonicalize.
  /// @param Attr Suffix elision policy ("selected", "all", "none", or empty).
  /// @return Canonicalized coroutine function name.
  static StringRef getCanonicalCoroFnName(StringRef FnName,
                                          StringRef Attr = "selected") {
    // A local coroutine function from another CU can be promoted to a global
    // function during ThinLTO import. This will create a linkage name like
    // "_Zfoo.llvm.xxxx.cleanup". Remove the ".llvm." suffix after stripping all
    // the coroutine suffixes to avoid pseudo probe mismatch.
    const SmallVector<StringRef, 3> CoroSuffixes{".cleanup", ".destroy",
                                                 ".resume", LLVMSuffix};
    return getCanonicalFnName(FnName, CoroSuffixes, Attr);
  }

  /// Canonicalize \p FnName by stripping suffixes listed in \p Suffixes.
  /// @param FnName Function name to canonicalize.
  /// @param Suffixes Suffix strings considered by the elision policy.
  /// @param Attr Suffix elision policy ("selected", "all", "none", or empty).
  /// @return Canonicalized form of \p FnName.
  static StringRef getCanonicalFnName(StringRef FnName,
                                      ArrayRef<StringRef> Suffixes,
                                      StringRef Attr = "selected") {
    if (Attr == "" || Attr == "all")
      return FnName.split('.').first;
    if (Attr == "selected") {
      StringRef Cand(FnName);
      for (const auto Suffix : Suffixes) {
        // If the profile contains ".__uniq." suffix, don't strip the
        // suffix for names in the IR.
        if (Suffix == UniqSuffix && FunctionSamples::HasUniqSuffix)
          continue;
        auto It = Cand.rfind(Suffix);
        if (It == StringRef::npos)
          continue;
        auto Dit = Cand.rfind('.');
        if (Dit == It || Dit == It + Suffix.size() - 1)
          Cand = Cand.substr(0, It);
      }
      return Cand;
    }
    if (Attr == "none")
      return FnName;
    assert(false && "internal error: unknown suffix elision policy");
    return FnName;
  }

  /// Translate \p Func into its original name.
  ///
  /// When profile doesn't use MD5, \p Func needs no translation.
  /// When profile uses MD5, \p Func in current FunctionSamples
  /// is actually GUID of the original function name. getFuncName will
  /// translate \p Func in current FunctionSamples into its original name
  /// by looking up in the function map GUIDToFuncNameMap.
  /// If the original name doesn't exist in the map, return empty StringRef.
  /// @param Func Function id to translate to a source name.
  /// @return Original function name, or empty if unknown under MD5.
  StringRef getFuncName(FunctionId Func) const {
    if (!UseMD5)
      return Func.stringRef();

    assert(GUIDToFuncNameMap &&
           "GUIDToFuncNameMap needs to be populated first");
    return GUIDToFuncNameMap->lookup(Func.getHashCode());
  }

  /// Returns the line offset to the start line of the subprogram.
  /// We assume that a single function will not exceed 65535 LOC.
  /// @param DIL Debug location whose offset within its subprogram is computed.
  /// @return Line offset of \p DIL from its subprogram start.
  LLVM_ABI static unsigned getOffset(const DILocation *DIL);

  /// Return a unique callsite identifier for a call's debug location.
  ///
  /// This is wrapper of two scenarios, the probe-based profile and
  /// regular profile, to hide implementation details from the sample loader and
  /// the context tracker.
  /// @param DIL Debug location of the call instruction.
  /// @param ProfileIsFS True when the profile uses flow-sensitive discriminators.
  /// @return Line location uniquely identifying the callsite.
  LLVM_ABI static LineLocation getCallSiteIdentifier(const DILocation *DIL,
                                                     bool ProfileIsFS = false);

  /// Return a unique hash for a callsite location and callee name.
  ///
  /// Guarantee MD5 and non-MD5 representation of the same function results in
  /// the same hash.
  /// @param Callee Callee function id at the callsite.
  /// @param Callsite Location of the callsite.
  /// @return Hash uniquely identifying the callsite and callee.
  static uint64_t getCallSiteHash(FunctionId Callee,
                                  const LineLocation &Callsite) {
    return SampleContextFrame(Callee, Callsite).getHashCode();
  }

  /// Get the FunctionSamples of the inline instance where DIL originates
  /// from.
  ///
  /// The FunctionSamples of the instruction (Machine or IR) associated to
  /// \p DIL is the inlined instance in which that instruction is coming from.
  /// We traverse the inline stack of that instruction, and match it with the
  /// tree nodes in the profile.
  ///
  /// \returns the FunctionSamples pointer to the inlined instance.
  /// If \p Remapper or \p FuncNameToProfNameMap is not nullptr, it will be used
  /// to find matching FunctionSamples with not exactly the same but equivalent
  /// name.
  /// @param DIL Debug location whose inline stack is matched to the profile.
  /// @param Remapper Optional remapper used to find equivalent function names.
  /// @param FuncNameToProfNameMap Optional map from IR names to profile names.
  LLVM_ABI const FunctionSamples *findFunctionSamples(
      const DILocation *DIL,
      SampleProfileReaderItaniumRemapper *Remapper = nullptr,
      const HashKeyMap<DenseMap, FunctionId, FunctionId>
          *FuncNameToProfNameMap = nullptr) const LLVM_LIFETIME_BOUND;

  /// Return the mutable sample context for this function profile.
  /// @return Mutable sample context associated with this profile.
  SampleContext &getContext() const LLVM_LIFETIME_BOUND { return Context; }

  /// Replace the sample context with \p FContext.
  /// @param FContext New context to assign to this profile.
  void setContext(const SampleContext &FContext) { Context = FContext; }

  /// True when the profile was produced from pseudo-probe instrumentation.
  ///
  /// These boolean variables are atomic so that parallel in-process ThinLTO
  /// backends writing the same value do not race.
  LLVM_ABI static std::atomic<bool> ProfileIsProbeBased;

  /// True when the profile is context-sensitive.
  LLVM_ABI static std::atomic<bool> ProfileIsCS;

  /// True when the profile contains CS pre-inliner contexts.
  LLVM_ABI static std::atomic<bool> ProfileIsPreInlined;

  /// Whether the profile uses MD5 to represent string.
  LLVM_ABI static std::atomic<bool> UseMD5;

  /// Whether the profile contains any ".__uniq." suffix in a name.
  LLVM_ABI static std::atomic<bool> HasUniqSuffix;

  /// If this profile uses flow sensitive discriminators.
  LLVM_ABI static std::atomic<bool> ProfileIsFS;

  /// GUIDToFuncNameMap saves the mapping from GUID to the symbol name, for
  /// all the function symbols defined or declared in current module.
  DenseMap<uint64_t, StringRef> *GUIDToFuncNameMap = nullptr;

  /// Return the GUID of the context's name. If the context is already using
  /// MD5, don't hash it again.
  /// @return GUID derived from the context function name.
  uint64_t getGUID() const { return getFunction().getHashCode(); }

  /// Collect all function names in this profile including inlinees and targets.
  /// @param NameSet Set that receives discovered function names.
  LLVM_ABI void findAllNames(DenseSet<FunctionId> &NameSet) const;

  /// Return true if this FunctionSamples equals \p Other.
  /// @param Other Profile to compare against.
  /// @return True if the function samples are equal.
  bool operator==(const FunctionSamples &Other) const {
    return (GUIDToFuncNameMap == Other.GUIDToFuncNameMap ||
            (GUIDToFuncNameMap && Other.GUIDToFuncNameMap &&
             *GUIDToFuncNameMap == *Other.GUIDToFuncNameMap)) &&
           FunctionHash == Other.FunctionHash && Context == Other.Context &&
           TotalSamples == Other.TotalSamples &&
           TotalHeadSamples == Other.TotalHeadSamples &&
           BodySamples == Other.BodySamples &&
           CallsiteSamples == Other.CallsiteSamples;
  }

  /// Return true if this FunctionSamples differs from \p Other.
  /// @param Other Profile to compare against.
  /// @return True if the function samples differ.
  bool operator!=(const FunctionSamples &Other) const {
    return !(*this == Other);
  }

private:
  /// CFG hash value for the function.
  uint64_t FunctionHash = 0;

  /// Calling context for function profile
  mutable SampleContext Context;

  /// Total number of samples collected inside this function.
  ///
  /// Samples are cumulative, they include all the samples collected
  /// inside this function and all its inlined callees.
  uint64_t TotalSamples = 0;

  /// Total number of samples collected at the head of the function.
  /// This is an approximation of the number of calls made to this function
  /// at runtime.
  uint64_t TotalHeadSamples = 0;

  /// Map instruction locations to collected samples.
  ///
  /// Each entry in this map contains the number of samples
  /// collected at the corresponding line offset. All line locations
  /// are an offset from the start of the function.
  BodySampleMap BodySamples;

  /// Map call sites to collected samples for the called function.
  ///
  /// Each entry in this map corresponds to all the samples
  /// collected for the inlined function call at the given
  /// location. For example, given:
  ///
  ///     void foo() {
  ///  1    bar();
  ///  ...
  ///  8    baz();
  ///     }
  ///
  /// If the bar() and baz() calls were inlined inside foo(), this
  /// map will contain two entries.  One for all the samples collected
  /// in the call to bar() at line offset 1, the other for all the samples
  /// collected in the call to baz() at line offset 8.
  CallsiteSampleMap CallsiteSamples;

  /// Map a virtual callsite to the list of accessed vtables and vtable counts.
  /// The callsite is referenced by its source location.
  ///
  /// For example, given:
  ///
  ///     void foo() {
  ///       ...
  ///  5    inlined_vcall_bar();
  ///       ...
  ///  5    inlined_vcall_baz();
  ///       ...
  ///  200  inlined_vcall_qux();
  ///     }
  /// This map will contain two entries. One with two types for line offset 5
  /// and one with one type for line offset 200.
  CallsiteTypeMap VirtualCallsiteTypeCounts;

  /// IR to profile location map generated by stale profile matching.
  ///
  /// Each entry is a mapping from the location on current build to the matched
  /// location in the "stale" profile. For example:
  ///   Profiled source code:
  ///      void foo() {
  ///   1    bar();
  ///      }
  ///
  ///   Current source code:
  ///      void foo() {
  ///   1    // Code change
  ///   2    bar();
  ///      }
  /// Supposing the stale profile matching algorithm generated the mapping [2 ->
  /// 1], the profile query using the location of bar on the IR which is 2 will
  /// be remapped to 1 and find the location of bar in the profile.
  const LocToLocMap *IRToProfileLocationMap = nullptr;
};

/// Get the proper representation of a string according to whether the
/// current Format uses MD5 to represent the string.
static inline FunctionId getRepInFormat(StringRef Name) {
  if (Name.empty() || !FunctionSamples::UseMD5)
    return FunctionId(Name);
  return FunctionId(Function::getGUIDAssumingExternalLinkage(Name));
}

/// Print \p FS to \p OS.
/// @param OS Output stream that receives the printed profile.
/// @param FS Function samples to print.
/// @return The output stream \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const FunctionSamples &FS);

/// Map from MD5 context hash to FunctionSamples with SampleContext-friendly API.
///
/// This class provides operator overloads to the map container using MD5 as the
/// key type, so that existing code can still work in most cases using
/// SampleContext as key.
/// Note: when populating container, make sure to assign the SampleContext to
/// the mapped value immediately because the key no longer holds it.
class SampleProfileMap
    : public HashKeyMap<std::unordered_map, SampleContext, FunctionSamples> {
public:
  /// Create or look up FunctionSamples for \p Ctx and set its context if new.
  /// @param Ctx Sample context used as the map key.
  /// @return Reference to the FunctionSamples for \p Ctx.
  mapped_type &create(const SampleContext &Ctx) {
    auto Ret = try_emplace(Ctx, FunctionSamples());
    if (Ret.second)
      Ret.first->second.setContext(Ctx);
    return Ret.first->second;
  }

  /// Find the entry for \p Ctx.
  /// @param Ctx Sample context to look up.
  /// @return Iterator to the entry, or end() if not found.
  iterator find(const SampleContext &Ctx) {
    return HashKeyMap<std::unordered_map, SampleContext, FunctionSamples>::find(
        Ctx);
  }

  /// Find the entry for \p Ctx.
  /// @param Ctx Sample context to look up.
  /// @return Const iterator to the entry, or end() if not found.
  const_iterator find(const SampleContext &Ctx) const {
    return HashKeyMap<std::unordered_map, SampleContext, FunctionSamples>::find(
        Ctx);
  }

  /// Erase the entry for \p Ctx.
  /// @param Ctx Sample context whose entry should be removed.
  /// @return Number of entries erased.
  size_t erase(const SampleContext &Ctx) {
    return HashKeyMap<std::unordered_map, SampleContext,
                      FunctionSamples>::erase(Ctx);
  }

  /// Erase the entry for hashed key \p Key.
  /// @param Key MD5 hash key whose entry should be removed.
  /// @return Number of entries erased.
  size_t erase(const key_type &Key) { return base_type::erase(Key); }

  /// Erase the entry at iterator \p It.
  /// @param It Iterator to the entry to erase.
  /// @return Iterator following the erased entry.
  iterator erase(iterator It) { return base_type::erase(It); }
};

/// Pair of a profile key hash and a FunctionSamples pointer.
using NameFunctionSamples = std::pair<hash_code, const FunctionSamples *>;

/// Sort profiles from \p ProfileMap into \p SortedProfiles by name hash.
/// @param ProfileMap Source profile map to sort.
/// @param SortedProfiles Destination vector receiving sorted name/profile pairs.
LLVM_ABI void
sortFuncProfiles(const SampleProfileMap &ProfileMap,
                 std::vector<NameFunctionSamples> &SortedProfiles);

/// Helper that trims and merges cold context-sensitive profiles.
///
/// SampleContextTrimmer implements helper functions to trim, merge cold
/// context profiles. It also supports context profile canonicalization to make
/// sure ProfileMap's key is consistent with FunctionSample's name/context.
class SampleContextTrimmer {
public:
  /// Construct a trimmer over \p Profiles.
  /// @param Profiles Profile map to trim and merge.
  SampleContextTrimmer(SampleProfileMap &Profiles) : ProfileMap(Profiles) {};
  /// Trim and merge cold context profiles when requested.
  ///
  /// TrimBaseProfileOnly should only be effective when TrimColdContext is true.
  /// On top of TrimColdContext, TrimBaseProfileOnly can be used to specify to
  /// trim all cold profiles or only cold base profiles. Trimming base profiles
  /// only is mainly to honor the preinliner decision. Note that when
  /// MergeColdContext is true, preinliner decision is not honored anyway so
  /// TrimBaseProfileOnly will be ignored.
  /// @param ColdCountThreshold Sample count at or below which a context is cold.
  /// @param TrimColdContext If true, remove cold context profiles.
  /// @param MergeColdContext If true, merge cold contexts into base profiles.
  /// @param ColdContextFrameLength Max frames kept when merging cold contexts.
  /// @param TrimBaseProfileOnly If true with TrimColdContext, trim only base profiles.
  LLVM_ABI void trimAndMergeColdContextProfiles(uint64_t ColdCountThreshold,
                                                bool TrimColdContext,
                                                bool MergeColdContext,
                                                uint32_t ColdContextFrameLength,
                                                bool TrimBaseProfileOnly);

private:
  SampleProfileMap &ProfileMap;
};

/// Helper class for converting between sample profile layouts.
///
/// It supports full context-sensitive profile to nested profile conversion,
/// nested profile to flatten profile conversion, etc.
class ProfileConverter {
public:
  /// Construct a converter over \p Profiles.
  /// @param Profiles Profile map to convert in place for CS nesting.
  LLVM_ABI ProfileConverter(SampleProfileMap &Profiles);
  /// Convert full context-sensitive flat profiles into nested profiles.
  LLVM_ABI void convertCSProfiles();
  /// Tree node representing one frame while nesting CS profiles.
  struct FrameNode {
    /// Construct a frame node for \p FName.
    /// @param FName Function name for this frame.
    /// @param FSamples Optional samples associated with this frame.
    /// @param CallLoc Callsite location in the parent frame.
    FrameNode(FunctionId FName = FunctionId(),
              FunctionSamples *FSamples = nullptr,
              LineLocation CallLoc = {0, 0})
        : FuncName(FName), FuncSamples(FSamples), CallSiteLoc(CallLoc) {};

    /// Child frames keyed by callsite location hash.
    std::map<uint64_t, FrameNode> AllChildFrames;
    /// Function name for the current frame.
    FunctionId FuncName;
    /// Function samples for the current frame.
    FunctionSamples *FuncSamples;
    /// Callsite location in the parent context.
    LineLocation CallSiteLoc;

    /// Return the child frame for \p CalleeName at \p CallSite, creating it.
    /// @param CallSite Callsite location in this frame.
    /// @param CalleeName Callee function name for the child frame.
    /// @return Pointer to the existing or newly created child frame.
    LLVM_ABI FrameNode *getOrCreateChildFrame(const LineLocation &CallSite,
                                              FunctionId CalleeName);
  };

  /// Flatten nested or CS profiles in \p ProfileMap in place.
  /// @param ProfileMap Profile map to flatten.
  /// @param ProfileIsCS True if \p ProfileMap holds context-sensitive profiles.
  static void flattenProfile(SampleProfileMap &ProfileMap,
                             bool ProfileIsCS = false) {
    SampleProfileMap TmpProfiles;
    flattenProfile(ProfileMap, TmpProfiles, ProfileIsCS);
    ProfileMap = std::move(TmpProfiles);
  }

  /// Flatten \p InputProfiles into \p OutputProfiles.
  /// @param InputProfiles Source profiles to flatten.
  /// @param OutputProfiles Destination map receiving flattened profiles.
  /// @param ProfileIsCS True if \p InputProfiles holds context-sensitive profiles.
  static void flattenProfile(const SampleProfileMap &InputProfiles,
                             SampleProfileMap &OutputProfiles,
                             bool ProfileIsCS = false) {
    if (ProfileIsCS) {
      for (const auto &I : InputProfiles) {
        // Retain the profile name and clear the full context for each function
        // profile.
        FunctionSamples &FS = OutputProfiles.create(I.second.getFunction());
        FS.merge(I.second);
      }
    } else {
      for (const auto &I : InputProfiles)
        flattenNestedProfile(OutputProfiles, I.second);
    }
  }

private:
  static void flattenNestedProfile(SampleProfileMap &OutputProfiles,
                                   const FunctionSamples &FS) {
    // To retain the context, checksum, attributes of the original profile, make
    // a copy of it if no profile is found.
    SampleContext &Context = FS.getContext();
    auto Ret = OutputProfiles.try_emplace(Context, FS);
    FunctionSamples &Profile = Ret.first->second;
    if (Ret.second) {
      // Clear nested inlinees' samples for the flattened copy. These inlinees
      // will have their own top-level entries after flattening.
      Profile.removeAllCallsiteSamples();
      // We recompute TotalSamples later, so here set to zero.
      Profile.setTotalSamples(0);
    } else {
      Profile.reserveBodySamples(FS.getBodySamples().size());
      for (const auto &[LineLocation, SampleRecord] : FS.getBodySamples()) {
        Profile.addSampleRecord(LineLocation, SampleRecord);
      }
    }

    assert(Profile.getCallsiteSamples().empty() &&
           "There should be no inlinees' profiles after flattening.");

    // TotalSamples might not be equal to the sum of all samples from
    // BodySamples and CallsiteSamples. So here we use "TotalSamples =
    // Original_TotalSamples - All_of_Callsite_TotalSamples +
    // All_of_Callsite_HeadSamples" to compute the new TotalSamples.
    uint64_t TotalSamples = FS.getTotalSamples();

    for (const auto &I : FS.getCallsiteSamples()) {
      for (const auto &Callee : I.second) {
        const auto &CalleeProfile = Callee.second;
        // Add body sample.
        Profile.addBodySamples(I.first.LineOffset, I.first.Discriminator,
                               CalleeProfile.getHeadSamplesEstimate());
        // Add callsite sample.
        Profile.addCalledTargetSamples(I.first.LineOffset,
                                       I.first.Discriminator,
                                       CalleeProfile.getFunction(),
                                       CalleeProfile.getHeadSamplesEstimate());
        // Update total samples.
        TotalSamples = TotalSamples >= CalleeProfile.getTotalSamples()
                           ? TotalSamples - CalleeProfile.getTotalSamples()
                           : 0;
        TotalSamples += CalleeProfile.getHeadSamplesEstimate();
        // Recursively convert callee profile.
        flattenNestedProfile(OutputProfiles, CalleeProfile);
      }
    }
    Profile.addTotalSamples(TotalSamples);

    Profile.setHeadSamples(Profile.getHeadSamplesEstimate());
  }

  // Nest all children profiles into the profile of Node.
  void convertCSProfiles(FrameNode &Node);
  FrameNode *getOrCreateContextPath(const SampleContext &Context);

  SampleProfileMap &ProfileMap;
  FrameNode RootFrame;
};

/// Records function symbols present in the binary that produced the profile.
///
/// ProfileSymbolList records the list of function symbols shown up in the
/// binary used to generate the profile. It is useful to discriminate a
/// function being so cold as not to shown up in the profile and a function
/// newly added.
class ProfileSymbolList {
public:
  /// Add \p Name to the symbol list.
  /// @param Name Symbol name to insert.
  /// @param Copy If true, copy the underlying memory for \p Name.
  void add(StringRef Name, bool Copy = false) {
    if (!Copy) {
      Syms.insert(Name);
      return;
    }
    Syms.insert(Name.copy(Allocator));
  }

  /// Return true if \p Name is present in the symbol list.
  /// @param Name Symbol name to look up.
  /// @return True if \p Name is in the list.
  bool contains(StringRef Name) const {
    return IsMD5 ? ColdGUIDTable.contains(llvm::MD5Hash(Name))
                 : Syms.count(Name);
  }

  /// Merge symbols from \p List into this list.
  /// @param List Source symbol list to merge from.
  void merge(const ProfileSymbolList &List) {
    assert(!List.IsMD5 &&
           "Merging pre-hashed MD5 ProfileSymbolList not yet implemented");
    for (auto Sym : List.Syms)
      add(Sym, true);
  }

  /// Return the number of symbols in the list.
  /// @return Number of symbols stored in this list.
  unsigned size() const { return IsMD5 ? ColdGUIDTable.size() : Syms.size(); }
  /// Reserve capacity for at least \p Size string symbols.
  /// @param Size Number of entries to reserve.
  void reserve(size_t Size) { Syms.reserve(Size); }

  /// Collect sorted unique MD5 GUIDs for all string symbols.
  /// @return Sorted unique MD5 hashes of the string symbols.
  std::vector<uint64_t> collectGUIDs() const {
    assert(!IsMD5 &&
           "Collecting GUIDs from existing MD5 table not yet implemented");
    std::vector<uint64_t> Keys;
    Keys.reserve(Syms.size());
    llvm::append_range(Keys, llvm::map_range(Syms, llvm::MD5Hash));
    llvm::sort(Keys);
    Keys.erase(llvm::unique(Keys), Keys.end());
    return Keys;
  }

  /// Install a pre-hashed cold GUID table and switch to MD5 mode.
  /// @param Table Eytzinger-layout table of cold function GUIDs.
  void setColdGUIDTable(EytzingerTableSpan<support::ulittle64_t> Table) {
    assert(Syms.empty() &&
           "Setting ColdGUIDTable shadows existing strings in Syms");
    ColdGUIDTable = Table;
    IsMD5 = true;
  }
  /// Return the cold GUID table when this list is in MD5 mode.
  /// @return Eytzinger-layout table of cold function GUIDs.
  EytzingerTableSpan<support::ulittle64_t> getColdGUIDTable() const {
    assert(IsMD5 && "Retrieving ColdGUIDTable from non-MD5 ProfileSymbolList");
    return ColdGUIDTable;
  }
  /// Return true if symbols are stored as MD5 GUIDs.
  /// @return True if this list stores MD5 GUIDs.
  bool isMD5() const { return IsMD5; }

  /// Read a symbol list from a binary buffer.
  /// @param Data Pointer to the serialized symbol list bytes.
  /// @param ListSize Size in bytes of the serialized list.
  /// @return Success, or an error if reading fails.
  LLVM_ABI std::error_code read(const uint8_t *Data, uint64_t ListSize);
  /// Write the symbol list to \p OS.
  /// @param OS Output stream that receives the serialized list.
  /// @return Success, or an error if writing fails.
  LLVM_ABI std::error_code write(raw_ostream &OS);
  /// Dump the symbol list for debugging.
  /// @param OS Output stream that receives the dump.
  LLVM_ABI void dump(raw_ostream &OS = dbgs()) const;

private:
  bool IsMD5 = false;
  DenseSet<StringRef> Syms;
  EytzingerTableSpan<support::ulittle64_t> ColdGUIDTable;
  BumpPtrAllocator Allocator;
};

} // end namespace sampleprof

using namespace sampleprof;
/// DenseMapInfo specialization for SampleContext.
template <> struct DenseMapInfo<SampleContext> {
  /// Hash a SampleContext for DenseMap.
  /// @param Val Context to hash.
  /// @return Hash value for \p Val.
  static unsigned getHashValue(const SampleContext &Val) {
    return Val.getHashCode();
  }

  /// Return true if two SampleContexts are equal.
  /// @param LHS Left-hand context.
  /// @param RHS Right-hand context.
  /// @return True if \p LHS equals \p RHS.
  static bool isEqual(const SampleContext &LHS, const SampleContext &RHS) {
    return LHS == RHS;
  }
};

/// Build a unique-internal-linkage post-fix hash for \p FName.
///
/// Prepends "__uniq" before the hash for tools like profilers to understand
/// that this symbol is of internal linkage type. The "__uniq" is the
/// pre-determined prefix that is used to tell tools that this symbol was
/// created with -funique-internal-linkage-symbols and the tools can strip or
/// keep the prefix as needed.
/// @param FName Function name to hash into a unique-internal-linkage suffix.
/// @return Decimal MD5 hash string with the unique-internal-linkage prefix.
inline std::string getUniqueInternalLinkagePostfix(const StringRef &FName) {
  llvm::MD5 Md5;
  Md5.update(FName);
  llvm::MD5::MD5Result R;
  Md5.final(R);
  SmallString<32> Str;
  llvm::MD5::stringifyResult(R, Str);
  // Convert MD5hash to Decimal. Demangler suffixes can either contain
  // numbers or characters but not both.
  llvm::APInt IntHash(128, Str.str(), 16);
  return toString(IntHash, /* Radix = */ 10, /* Signed = */ false)
      .insert(0, FunctionSamples::UniqSuffix);
}

} // end namespace llvm

#endif // LLVM_PROFILEDATA_SAMPLEPROF_H
