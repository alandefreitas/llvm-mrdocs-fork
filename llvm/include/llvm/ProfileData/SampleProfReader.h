//===- SampleProfReader.h - Read LLVM sample profile data -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions needed for reading sample profiles.
//
// NOTE: If you are making changes to this file format, please remember
//       to document them in the Clang documentation at
//       tools/clang/docs/UsersManual.rst.
//
// Text format
// -----------
//
// Sample profiles are written as ASCII text. The file is divided into
// sections, which correspond to each of the functions executed at runtime.
// Each section has the following format
//
//     function1:total_samples:total_head_samples
//      offset1[.discriminator]: number_of_samples [fn1:num fn2:num ... ]
//      offset2[.discriminator]: number_of_samples [fn3:num fn4:num ... ]
//      ...
//      offsetN[.discriminator]: number_of_samples [fn5:num fn6:num ... ]
//      offsetA[.discriminator]: fnA:num_of_total_samples
//       offsetA1[.discriminator]: number_of_samples [fn7:num fn8:num ... ]
//       ...
//      !CFGChecksum: num
//      !Attribute: flags
//
// This is a nested tree in which the indentation represents the nesting level
// of the inline stack. There are no blank lines in the file. And the spacing
// within a single line is fixed. Additional spaces will result in an error
// while reading the file.
//
// Any line starting with the '#' character is completely ignored.
//
// Inlined calls are represented with indentation. The Inline stack is a
// stack of source locations in which the top of the stack represents the
// leaf function, and the bottom of the stack represents the actual
// symbol to which the instruction belongs.
//
// Function names must be mangled in order for the profile loader to
// match them in the current translation unit. The two numbers in the
// function header specify how many total samples were accumulated in the
// function (first number), and the total number of samples accumulated
// in the prologue of the function (second number). This head sample
// count provides an indicator of how frequently the function is invoked.
//
// There are three types of lines in the function body.
//
// * Sampled line represents the profile information of a source location.
// * Callsite line represents the profile information of a callsite.
// * Metadata line represents extra metadata of the function.
//
// Each sampled line may contain several items. Some are optional (marked
// below):
//
// a. Source line offset. This number represents the line number
//    in the function where the sample was collected. The line number is
//    always relative to the line where symbol of the function is
//    defined. So, if the function has its header at line 280, the offset
//    13 is at line 293 in the file.
//
//    Note that this offset should never be a negative number. This could
//    happen in cases like macros. The debug machinery will register the
//    line number at the point of macro expansion. So, if the macro was
//    expanded in a line before the start of the function, the profile
//    converter should emit a 0 as the offset (this means that the optimizers
//    will not be able to associate a meaningful weight to the instructions
//    in the macro).
//
// b. [OPTIONAL] Discriminator. This is used if the sampled program
//    was compiled with DWARF discriminator support
//    (http://wiki.dwarfstd.org/index.php?title=Path_Discriminators).
//    DWARF discriminators are unsigned integer values that allow the
//    compiler to distinguish between multiple execution paths on the
//    same source line location.
//
//    For example, consider the line of code ``if (cond) foo(); else bar();``.
//    If the predicate ``cond`` is true 80% of the time, then the edge
//    into function ``foo`` should be considered to be taken most of the
//    time. But both calls to ``foo`` and ``bar`` are at the same source
//    line, so a sample count at that line is not sufficient. The
//    compiler needs to know which part of that line is taken more
//    frequently.
//
//    This is what discriminators provide. In this case, the calls to
//    ``foo`` and ``bar`` will be at the same line, but will have
//    different discriminator values. This allows the compiler to correctly
//    set edge weights into ``foo`` and ``bar``.
//
// c. Number of samples. This is an integer quantity representing the
//    number of samples collected by the profiler at this source
//    location.
//
// d. [OPTIONAL] Potential call targets and samples. If present, this
//    line contains a call instruction. This models both direct and
//    number of samples. For example,
//
//      130: 7  foo:3  bar:2  baz:7
//
//    The above means that at relative line offset 130 there is a call
//    instruction that calls one of ``foo()``, ``bar()`` and ``baz()``,
//    with ``baz()`` being the relatively more frequently called target.
//
// Each callsite line may contain several items. Some are optional.
//
// a. Source line offset. This number represents the line number of the
//    callsite that is inlined in the profiled binary.
//
// b. [OPTIONAL] Discriminator. Same as the discriminator for sampled line.
//
// c. Number of samples. This is an integer quantity representing the
//    total number of samples collected for the inlined instance at this
//    callsite
//
// Metadata line can occur in lines with one indent only, containing extra
// information for the top-level function. Furthermore, metadata can only
// occur after all the body samples and callsite samples.
// Each metadata line may contain a particular type of metadata, marked by
// the starting characters annotated with !. We process each metadata line
// independently, hence each metadata line has to form an independent piece
// of information that does not require cross-line reference.
// We support the following types of metadata:
//
// a. CFG Checksum (a.k.a. function hash):
//   !CFGChecksum: 12345
// b. CFG Checksum (see ContextAttributeMask):
//   !Atribute: 1
//
//
// Binary format
// -------------
//
// This is a more compact encoding. Numbers are encoded as ULEB128 values
// and all strings are encoded in a name table. The file is organized in
// the following sections:
//
// MAGIC (uint64_t)
//    File identifier computed by function SPMagic() (0x5350524f463432ff)
//
// VERSION (uint32_t)
//    File format version number computed by SPVersion()
//
// SUMMARY
//    TOTAL_COUNT (uint64_t)
//        Total number of samples in the profile.
//    MAX_COUNT (uint64_t)
//        Maximum value of samples on a line.
//    MAX_FUNCTION_COUNT (uint64_t)
//        Maximum number of samples at function entry (head samples).
//    NUM_COUNTS (uint64_t)
//        Number of lines with samples.
//    NUM_FUNCTIONS (uint64_t)
//        Number of functions with samples.
//    NUM_DETAILED_SUMMARY_ENTRIES (size_t)
//        Number of entries in detailed summary
//    DETAILED_SUMMARY
//        A list of detailed summary entry. Each entry consists of
//        CUTOFF (uint32_t)
//            Required percentile of total sample count expressed as a fraction
//            multiplied by 1000000.
//        MIN_COUNT (uint64_t)
//            The minimum number of samples required to reach the target
//            CUTOFF.
//        NUM_COUNTS (uint64_t)
//            Number of samples to get to the desrired percentile.
//
// NAME TABLE
//    SIZE (uint64_t)
//        Number of entries in the name table.
//    NAMES
//        A NUL-separated list of SIZE strings.
//
// FUNCTION BODY (one for each uninlined function body present in the profile)
//    HEAD_SAMPLES (uint64_t) [only for top-level functions]
//        Total number of samples collected at the head (prologue) of the
//        function.
//        NOTE: This field should only be present for top-level functions
//              (i.e., not inlined into any caller). Inlined function calls
//              have no prologue, so they don't need this.
//    NAME_IDX (uint64_t)
//        Index into the name table indicating the function name.
//    SAMPLES (uint64_t)
//        Total number of samples collected in this function.
//    NRECS (uint32_t)
//        Total number of sampling records this function's profile.
//    BODY RECORDS
//        A list of NRECS entries. Each entry contains:
//          OFFSET (uint32_t)
//            Line offset from the start of the function.
//          DISCRIMINATOR (uint32_t)
//            Discriminator value (see description of discriminators
//            in the text format documentation above).
//          SAMPLES (uint64_t)
//            Number of samples collected at this location.
//          NUM_CALLS (uint32_t)
//            Number of non-inlined function calls made at this location. In the
//            case of direct calls, this number will always be 1. For indirect
//            calls (virtual functions and function pointers) this will
//            represent all the actual functions called at runtime.
//          CALL_TARGETS
//            A list of NUM_CALLS entries for each called function:
//               NAME_IDX (uint64_t)
//                  Index into the name table with the callee name.
//               SAMPLES (uint64_t)
//                  Number of samples collected at the call site.
//    NUM_INLINED_FUNCTIONS (uint32_t)
//      Number of callees inlined into this function.
//    INLINED FUNCTION RECORDS
//      A list of NUM_INLINED_FUNCTIONS entries describing each of the inlined
//      callees.
//        OFFSET (uint32_t)
//          Line offset from the start of the function.
//        DISCRIMINATOR (uint32_t)
//          Discriminator value (see description of discriminators
//          in the text format documentation above).
//        FUNCTION BODY
//          A FUNCTION BODY entry describing the inlined function.
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_SAMPLEPROFREADER_H
#define LLVM_PROFILEDATA_SAMPLEPROFREADER_H

#include "llvm/ADT/Eytzinger.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/ProfileData/GCOV.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/ProfileData/SymbolRemappingReader.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Discriminator.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace llvm {

class raw_ostream;
class Twine;

namespace vfs {
class FileSystem;
} // namespace vfs

namespace sampleprof {

class SampleProfileReader;

/// Remaps sample profile symbol names using Itanium component equivalences.
///
/// SampleProfileReaderItaniumRemapper remaps the profile data from a
/// sample profile data reader, by applying a provided set of equivalences
/// between components of the symbol names in the profile.
class SampleProfileReaderItaniumRemapper {
public:
  /// Construct a remapper over buffer \p B for reader \p R.
  /// @param B Memory buffer holding the remapping file contents.
  /// @param SRR Parsed symbol remapping reader.
  /// @param R Sample profile reader whose names will be remapped.
  SampleProfileReaderItaniumRemapper(std::unique_ptr<MemoryBuffer> B,
                                     std::unique_ptr<SymbolRemappingReader> SRR,
                                     SampleProfileReader &R)
      : Buffer(std::move(B)), Remappings(std::move(SRR)), Reader(R) {
    assert(Remappings && "Remappings cannot be nullptr");
  }

  /// Create a remapper from the given remapping file. The remapper will
  /// be used for profile read in by Reader.
  /// @param Filename Path to the remapping file.
  /// @param FS Virtual file system used to open \p Filename.
  /// @param Reader Sample profile reader the remapper will serve.
  /// @param C LLVM context used for diagnostics.
  /// @return A remapper for \p Filename, or an error on failure.
  LLVM_ABI static ErrorOr<std::unique_ptr<SampleProfileReaderItaniumRemapper>>
  create(StringRef Filename, vfs::FileSystem &FS, SampleProfileReader &Reader,
         LLVMContext &C);

  /// Create a remapper from the given Buffer. The remapper will
  /// be used for profile read in by Reader.
  /// @param B Memory buffer holding the remapping file contents.
  /// @param Reader Sample profile reader the remapper will serve.
  /// @param C LLVM context used for diagnostics.
  /// @return A remapper for \p B, or an error on failure.
  LLVM_ABI static ErrorOr<std::unique_ptr<SampleProfileReaderItaniumRemapper>>
  create(std::unique_ptr<MemoryBuffer> &B, SampleProfileReader &Reader,
         LLVMContext &C);

  /// Apply remappings to the profile read by Reader.
  /// @param Ctx LLVM context used for diagnostics during remapping.
  LLVM_ABI void applyRemapping(LLVMContext &Ctx);

  /// Return true if remapping has already been applied.
  /// @return True if remapping has already been applied.
  bool hasApplied() { return RemappingApplied; }

  /// Insert function name into remapper.
  /// @param FunctionName Function name to insert into the remapping table.
  void insert(StringRef FunctionName) { Remappings->insert(FunctionName); }

  /// Query whether there is equivalent in the remapper which has been
  /// inserted.
  /// @param FunctionName Function name to look up in the remapper.
  /// @return True if an equivalent name has been inserted for \p FunctionName.
  bool exist(StringRef FunctionName) {
    return Remappings->lookup(FunctionName);
  }

  /// Return the equivalent name in the profile for \p FunctionName if
  /// it exists.
  /// @param FunctionName Function name to remap into a profile name.
  /// @return The equivalent profile name, or std::nullopt if none.
  LLVM_ABI std::optional<StringRef> lookUpNameInProfile(StringRef FunctionName);

private:
  // The buffer holding the content read from remapping file.
  std::unique_ptr<MemoryBuffer> Buffer;
  std::unique_ptr<SymbolRemappingReader> Remappings;
  // Map remapping key to the name in the profile. By looking up the
  // key in the remapper, a given new name can be mapped to the
  // cannonical name using the NameMap.
  DenseMap<SymbolRemappingReader::Key, StringRef> NameMap;
  // The Reader the remapper is servicing.
  SampleProfileReader &Reader;
  // Indicate whether remapping has been applied to the profile read
  // by Reader -- by calling applyRemapping.
  bool RemappingApplied = false;
};

/// Unified read-only name table for sample profile function identifiers.
///
/// Manages the sample profile name table, supporting both an eagerly loaded
/// std::vector of FunctionId objects and lazy-loaded MD5 hashes read directly
/// from the memory-mapped buffer. It enforces the exclusivity of these
/// two formats and provides a unified read-only container interface.
class SampleProfileNameTable {
public:
  /// Input iterator over FunctionId entries in the name table.
  class iterator
      : public llvm::iterator_facade_base<iterator, std::input_iterator_tag,
                                          FunctionId, std::ptrdiff_t,
                                          const FunctionId *, FunctionId> {
  public:
    /// Construct an empty end iterator.
    iterator() = default;
    /// Construct an iterator at index \p Idx in \p Table.
    /// @param Table Name table being iterated; may be null for an end iterator.
    /// @param Idx Zero-based index into \p Table.
    iterator(const SampleProfileNameTable *Table, size_t Idx)
        : Table(Table), Idx(Idx) {}

    /// Return true if this iterator equals \p RHS.
    /// @param RHS Other iterator to compare.
    /// @return True if this iterator equals \p RHS.
    bool operator==(const iterator &RHS) const {
      return Table == RHS.Table && Idx == RHS.Idx;
    }

    /// Advance to the next name-table entry.
    /// @return Reference to this iterator after advancing.
    iterator &operator++() {
      ++Idx;
      return *this;
    }

    /// Return the FunctionId at the current position.
    /// @return The FunctionId at the current position.
    FunctionId operator*() const {
      assert(Table && Idx < Table->size() &&
             "Dereferencing invalid or out-of-bounds iterator");
      return (*Table)[Idx];
    }

  private:
    const SampleProfileNameTable *Table = nullptr;
    size_t Idx = 0;
  };

  /// Const iterator type alias for \c iterator.
  using const_iterator = iterator;

  /// Construct an empty name table base.
  SampleProfileNameTable() = default;
  /// Deleted copy constructor.
  /// @param Other Unused; copy construction is deleted.
  SampleProfileNameTable(const SampleProfileNameTable &Other) = delete;
  /// Deleted move constructor.
  /// @param Other Unused; move construction is deleted.
  SampleProfileNameTable(SampleProfileNameTable &&Other) = delete;
  /// Deleted copy assignment.
  /// @param Other Unused; copy assignment is deleted.
  SampleProfileNameTable &operator=(const SampleProfileNameTable &Other) =
      delete;
  /// Deleted move assignment.
  /// @param Other Unused; move assignment is deleted.
  SampleProfileNameTable &operator=(SampleProfileNameTable &&Other) = delete;
  /// Destroy the name table.
  virtual ~SampleProfileNameTable() = default;

  /// Return the number of entries in the name table.
  /// @return The number of entries in the name table.
  virtual size_t size() const = 0;
  /// Return true if the name table has no entries.
  /// @return True if the name table has no entries.
  bool empty() const { return size() == 0; }
  /// Return the FunctionId at index \p Idx.
  /// @param Idx Zero-based index into the name table.
  /// @return The FunctionId at index \p Idx.
  virtual FunctionId operator[](size_t Idx) const = 0;

  /// Return the Eytzinger span for nested or flat names.
  /// @param IsNested When true, return the nested-context span.
  /// @return The Eytzinger span for nested or flat names.
  virtual EytzingerTableSpan<support::ulittle64_t>
  getEytzingerSpan(bool IsNested) const {
    llvm_unreachable(
        "getEytzingerSpan is exclusively supported for Eytzinger layout");
  }
  /// Return true if the name table contains string key \p Key.
  /// @param Key Function name to look up.
  /// @return True if the name table contains string key \p Key.
  virtual bool contains(StringRef Key) const {
    return contains(FunctionId(Key).getHashCode());
  }
  /// Return true if the name table contains GUID \p GUID.
  /// @param GUID Function MD5/hash to look up.
  /// @return True if the name table contains GUID \p GUID.
  virtual bool contains(uint64_t GUID) const {
    return getOrCreateSet(GUIDSet, *this, GetFunctionIdHash).contains(GUID);
  }

  /// Return an iterator to the first name-table entry.
  /// @return An iterator to the first name-table entry.
  iterator begin() const { return iterator(this, 0); }
  /// Return an iterator past the last name-table entry.
  /// @return An iterator past the last name-table entry.
  iterator end() const { return iterator(this, size()); }

protected:
  /// Lazily built set of GUIDs for membership queries.
  mutable std::optional<DenseSet<uint64_t>> GUIDSet;

  /// Projection that returns the hash code of a FunctionId.
  static constexpr auto GetFunctionIdHash = [](FunctionId F) {
    return F.getHashCode();
  };
  /// Projection that returns the string view of a FunctionId.
  static constexpr auto GetFunctionIdString = [](FunctionId F) {
    return F.stringRef();
  };

  /// Return \p Set, building it from \p Range with \p Proj if needed.
  /// @param Set Optional set to populate and return.
  /// @param Range Range of items used to populate \p Set on first use.
  /// @param Proj Projection from range elements to set keys.
  /// @return \p Set, building it from \p Range with \p Proj if needed.
  template <typename SetT, typename RangeT, typename ProjT = llvm::identity>
  static const SetT &getOrCreateSet(std::optional<SetT> &Set,
                                    const RangeT &Range, ProjT Proj = ProjT()) {
    if (!Set) {
      Set.emplace();
      Set->reserve(Range.size());
      for (const auto &Item : Range)
        Set->insert(Proj(Item));
    }
    return *Set;
  }
};

/// Name table that lazily reads fixed-length MD5 hashes from a memory buffer.
class LazySampleProfileNameTable final : public SampleProfileNameTable {
  const uint8_t *Start = nullptr;
  size_t Size = 0;

public:
  /// Construct a lazy name table over \p Size MD5 values starting at \p Start.
  /// @param Start Pointer to the first little-endian MD5 value.
  /// @param Size Number of MD5 entries in the table.
  LazySampleProfileNameTable(const uint8_t *Start, size_t Size)
      : Start(Start), Size(Size) {}

  /// Return the number of MD5 entries.
  /// @return The number of MD5 entries.
  size_t size() const override { return Size; }

  /// Return the FunctionId at index \p Idx.
  /// @param Idx Zero-based index into the MD5 table.
  /// @return The FunctionId at index \p Idx.
  FunctionId operator[](size_t Idx) const override {
    assert(Idx < Size && "Index out of bounds");
    using namespace support;
    return FunctionId(endian::read<uint64_t, unaligned>(
        Start + Idx * sizeof(uint64_t), endianness::little));
  }

  /// Return true if the table contains GUID \p GUID.
  /// @param GUID Function MD5/hash to look up.
  /// @return True if the table contains GUID \p GUID.
  bool contains(uint64_t GUID) const override {
    ArrayRef<support::ulittle64_t> Table(
        reinterpret_cast<const support::ulittle64_t *>(Start), Size);
    return getOrCreateSet(GUIDSet, Table).contains(GUID);
  }
};

/// Name table backed by an eagerly loaded vector of string FunctionIds.
class StringSampleProfileNameTable final : public SampleProfileNameTable {
  std::vector<FunctionId> Vec;
  mutable std::optional<DenseSet<StringRef>> NameSet;

public:
  /// Construct a string name table by moving \p Vec.
  /// @param Vec FunctionId vector to take ownership of.
  explicit StringSampleProfileNameTable(std::vector<FunctionId> &&Vec)
      : Vec(std::move(Vec)) {}
  /// Construct a string name table by copying \p Vec.
  /// @param Vec FunctionId vector to copy.
  explicit StringSampleProfileNameTable(const std::vector<FunctionId> &Vec)
      : Vec(Vec) {}

  /// Return the number of string FunctionId entries.
  /// @return The number of string FunctionId entries.
  size_t size() const override { return Vec.size(); }

  /// Return the FunctionId at index \p Idx.
  /// @param Idx Zero-based index into the vector.
  /// @return The FunctionId at index \p Idx.
  FunctionId operator[](size_t Idx) const override {
    assert(Idx < Vec.size() && "Index out of bounds");
    return Vec[Idx];
  }

  /// Return true if the table contains string key \p Key.
  /// @param Key Function name to look up.
  /// @return True if the table contains string key \p Key.
  bool contains(StringRef Key) const override {
    return getOrCreateSet(NameSet, Vec, GetFunctionIdString).contains(Key);
  }
};

/// Name table backed by an eagerly loaded vector of MD5 FunctionIds.
class MD5SampleProfileNameTable final : public SampleProfileNameTable {
  std::vector<FunctionId> Vec;

public:
  /// Construct an MD5 name table by moving \p Vec.
  /// @param Vec FunctionId vector to take ownership of.
  explicit MD5SampleProfileNameTable(std::vector<FunctionId> &&Vec)
      : Vec(std::move(Vec)) {}
  /// Construct an MD5 name table by copying \p Vec.
  /// @param Vec FunctionId vector to copy.
  explicit MD5SampleProfileNameTable(const std::vector<FunctionId> &Vec)
      : Vec(Vec) {}

  /// Return the number of MD5 FunctionId entries.
  /// @return The number of MD5 FunctionId entries.
  size_t size() const override { return Vec.size(); }

  /// Return the FunctionId at index \p Idx.
  /// @param Idx Zero-based index into the vector.
  /// @return The FunctionId at index \p Idx.
  FunctionId operator[](size_t Idx) const override {
    assert(Idx < Vec.size() && "Index out of bounds");
    return Vec[Idx];
  }
};

/// Name table using Eytzinger-ordered MD5 spans for nested and flat names.
class EytzingerSampleProfileNameTable final : public SampleProfileNameTable {
  ArrayRef<support::ulittle64_t> Array;
  std::array<EytzingerTableSpan<support::ulittle64_t>,
             static_cast<size_t>(EytzingerSpan::NumSpans)>
      Spans;

public:
  /// Construct an Eytzinger name table over nested, flat, and inlinee spans.
  /// @param Data Pointer to the concatenated little-endian MD5 array.
  /// @param NumNested Number of nested-context entries.
  /// @param NumFlat Number of flat-context entries.
  /// @param NumInlinees Number of inlinee entries.
  EytzingerSampleProfileNameTable(const support::ulittle64_t *Data,
                                  size_t NumNested, size_t NumFlat,
                                  size_t NumInlinees)
      : Array(Data, NumNested + NumFlat + NumInlinees),
        Spans{{{Data, NumNested},
               {Data + NumNested, NumFlat},
               {Data + NumNested + NumFlat, NumInlinees}}} {}

  /// Return the total number of Eytzinger-ordered entries.
  /// @return The total number of Eytzinger-ordered entries.
  size_t size() const override { return Array.size(); }

  /// Return the FunctionId at index \p Idx.
  /// @param Idx Zero-based index into the concatenated array.
  /// @return The FunctionId at index \p Idx.
  FunctionId operator[](size_t Idx) const override {
    return FunctionId(Array[Idx]);
  }

  /// Return the Eytzinger span for nested or flat names.
  /// @param IsNested When true, return the nested-context span.
  /// @return The Eytzinger span for nested or flat names.
  EytzingerTableSpan<support::ulittle64_t>
  getEytzingerSpan(bool IsNested) const override {
    return Spans[static_cast<size_t>(IsNested ? EytzingerSpan::Nested
                                              : EytzingerSpan::Flat)];
  }

  /// Return true if any Eytzinger span contains GUID \p GUID.
  /// @param GUID Function MD5/hash to look up.
  /// @return True if any Eytzinger span contains GUID \p GUID.
  bool contains(uint64_t GUID) const override {
    return llvm::any_of(Spans,
                        [&](const auto &Span) { return Span.contains(GUID); });
  }
};

/// Sample-based profile reader.
///
/// Each profile contains sample counts for all the functions
/// executed. Inside each function, statements are annotated with the
/// collected samples on all the instructions associated with that
/// statement.
///
/// For this to produce meaningful data, the program needs to be
/// compiled with some debug information (at minimum, line numbers:
/// -gline-tables-only). Otherwise, it will be impossible to match IR
/// instructions to the line numbers collected by the profiler.
///
/// From the profile file, we are interested in collecting the
/// following information:
///
/// * A list of functions included in the profile (mangled names).
///
/// * For each function F:
///   1. The total number of samples collected in F.
///
///   2. The samples collected at each line in F. To provide some
///      protection against source code shuffling, line numbers should
///      be relative to the start of the function.
///
/// The reader supports two file formats: text and binary. The text format
/// is useful for debugging and testing, while the binary format is more
/// compact and I/O efficient. They can both be used interchangeably.
class SampleProfileReader {
public:
  /// Construct a sample profile reader for buffer \p B.
  /// @param B Memory buffer holding the profile.
  /// @param C LLVM context used for diagnostics.
  /// @param Format Profile format tag; defaults to SPF_None.
  SampleProfileReader(std::unique_ptr<MemoryBuffer> B, LLVMContext &C,
                      SampleProfileFormat Format = SPF_None)
      : Profiles(), Ctx(C), Buffer(std::move(B)), Format(Format) {}

  /// Destroy the sample profile reader.
  virtual ~SampleProfileReader() = default;

  /// Read and validate the file header.
  /// @return Success, or an error if the header is invalid.
  virtual std::error_code readHeader() = 0;

  /// Set the masked bit range used for FS discriminators.
  ///
  /// Parameter Pass specify the sequence number, Pass == i is for the i-th
  /// round of adding FS discriminators. Pass == 0 is for using base
  /// discriminators.
  /// @param P FS discriminator pass whose bit range should be used.
  void setDiscriminatorMaskedBitFrom(FSDiscriminatorPass P) {
    MaskedBitFrom = getFSPassBitEnd(P);
  }

  /// Get the bitmask the discriminators: For FS profiles, return the bit
  /// mask for this pass. For non FS profiles, return (unsigned) -1.
  /// @return The discriminator bitmask for this pass, or all bits for non-FS profiles.
  uint32_t getDiscriminatorMask() const {
    if (!ProfileIsFS)
      return 0xFFFFFFFF;
    assert((MaskedBitFrom != 0) && "MaskedBitFrom is not set properly");
    return getN1Bits(MaskedBitFrom);
  }

  /// The interface to read sample profiles from the associated file.
  /// @return Success, or an error if reading fails.
  std::error_code read() {
    if (std::error_code EC = readImpl())
      return EC;
    if (Remapper)
      Remapper->applyRemapping(Ctx);
    FunctionSamples::UseMD5 = useMD5();
    return sampleprof_error::success;
  }

  /// Read sample profiles for the given functions.
  /// @param FuncsToUse Functions whose profiles should be loaded.
  /// @return Success, or an error if reading fails.
  std::error_code read(const DenseSet<StringRef> &FuncsToUse) {
    DenseSet<StringRef> S;
    for (StringRef F : FuncsToUse)
      if (Profiles.find(FunctionId(F)) == Profiles.end())
        S.insert(F);
    if (std::error_code EC = read(S, Profiles))
      return EC;
    return sampleprof_error::success;
  }

  /// The implementaion to read sample profiles from the associated file.
  /// @return Success, or an error if reading fails.
  virtual std::error_code readImpl() = 0;

  /// Print the profile for \p FunctionSamples on stream \p OS.
  /// @param FS FunctionSamples instance to print.
  /// @param OS Output stream receiving the dump.
  LLVM_ABI void dumpFunctionProfile(const FunctionSamples &FS,
                                    raw_ostream &OS = dbgs());

  /// Collect functions with definitions in module \p M.
  ///
  /// For reader which support loading function profiles on demand, return true
  /// when the reader has been given a module. Always return false for reader
  /// which doesn't support loading function profiles on demand.
  /// @return True if the reader was given a module and supports on-demand loading.
  virtual bool collectFuncsFromModule() { return false; }

  /// Print all the profiles on stream \p OS.
  /// @param OS Output stream receiving the dump.
  LLVM_ABI void dump(raw_ostream &OS = dbgs());

  /// Print all the profiles on stream \p OS in the JSON format.
  /// @param OS Output stream receiving the JSON dump.
  LLVM_ABI void dumpJson(raw_ostream &OS = dbgs());

  /// Return the format version of the profile. For tests only.
  /// @return The format version of the profile.
  uint64_t getFormatVersion() const { return FormatVersion; }

  /// Return the samples collected for function \p F.
  /// @param F Function whose samples should be looked up.
  /// @return The samples for \p F, or null if none.
  FunctionSamples *getSamplesFor(const Function &F) {
    // The function name may have been updated by adding suffix. Call
    // a helper to (optionally) strip off suffixes so that we can
    // match against the original function name in the profile.
    StringRef CanonName = FunctionSamples::getCanonicalFnName(F);
    return getSamplesFor(CanonName);
  }

  /// Return the samples collected for function \p F.
  /// @param Fname Function name whose samples should be looked up.
  /// @return The samples for \p Fname, or null if none.
  FunctionSamples *getSamplesFor(StringRef Fname) {
    auto It = Profiles.find(FunctionId(Fname));
    if (It != Profiles.end())
      return &It->second;

    if (FuncNameToProfNameMap && !FuncNameToProfNameMap->empty()) {
      auto R = FuncNameToProfNameMap->find(FunctionId(Fname));
      if (R != FuncNameToProfNameMap->end()) {
        Fname = R->second.stringRef();
        auto It = Profiles.find(FunctionId(Fname));
        if (It != Profiles.end())
          return &It->second;
      }
    }

    if (Remapper) {
      if (auto NameInProfile = Remapper->lookUpNameInProfile(Fname)) {
        auto It = Profiles.find(FunctionId(*NameInProfile));
        if (It != Profiles.end())
          return &It->second;
      }
    }
    return nullptr;
  }

  /// Return all the profiles.
  /// @return All loaded function profiles.
  SampleProfileMap &getProfiles() { return Profiles; }

  /// Report a parse error message.
  /// @param LineNumber Source line associated with the diagnostic, if any.
  /// @param Msg Diagnostic message to report.
  void reportError(int64_t LineNumber, const Twine &Msg) const {
    Ctx.diagnose(DiagnosticInfoSampleProfile(Buffer->getBufferIdentifier(),
                                             LineNumber, Msg));
  }

  /// Create a sample profile reader for \p Filename.
  ///
  /// Create a remapper underlying if RemapFilename is not empty.
  /// Parameter P specifies the FSDiscriminatorPass.
  /// @param Filename Path to the sample profile file.
  /// @param C LLVM context used for diagnostics.
  /// @param FS Virtual file system used to open the profile and remap file.
  /// @param P FS discriminator pass used to configure masked bits.
  /// @param RemapFilename Optional path to a symbol remapping file.
  /// @return A sample profile reader, or an error on failure.
  LLVM_ABI static ErrorOr<std::unique_ptr<SampleProfileReader>>
  create(StringRef Filename, LLVMContext &C, vfs::FileSystem &FS,
         FSDiscriminatorPass P = FSDiscriminatorPass::Base,
         StringRef RemapFilename = "");

  /// Create a sample profile reader from memory buffer \p B.
  ///
  /// Create a remapper underlying if RemapFilename is not empty.
  /// Parameter P specifies the FSDiscriminatorPass.
  /// @param B Memory buffer holding the profile contents.
  /// @param C LLVM context used for diagnostics.
  /// @param FS Virtual file system used to open an optional remap file.
  /// @param P FS discriminator pass used to configure masked bits.
  /// @param RemapFilename Optional path to a symbol remapping file.
  /// @return A sample profile reader, or an error on failure.
  LLVM_ABI static ErrorOr<std::unique_ptr<SampleProfileReader>>
  create(std::unique_ptr<MemoryBuffer> &B, LLVMContext &C, vfs::FileSystem &FS,
         FSDiscriminatorPass P = FSDiscriminatorPass::Base,
         StringRef RemapFilename = "");

  /// Return the profile summary.
  /// @return The profile summary.
  ProfileSummary &getSummary() const { return *Summary; }

  /// Return the memory buffer holding the profile.
  /// @return The memory buffer holding the profile.
  MemoryBuffer *getBuffer() const { return Buffer.get(); }

  /// \brief Return the profile format.
  /// @return The profile format.
  SampleProfileFormat getFormat() const { return Format; }

  /// Whether input profile is based on pseudo probes.
  /// @return True if the input profile is based on pseudo probes.
  bool profileIsProbeBased() const { return ProfileIsProbeBased; }

  /// Whether input profile is fully context-sensitive.
  /// @return True if the input profile is fully context-sensitive.
  bool profileIsCS() const { return ProfileIsCS; }

  /// Whether input profile contains ShouldBeInlined contexts.
  /// @return True if the input profile contains ShouldBeInlined contexts.
  bool profileIsPreInlined() const { return ProfileIsPreInlined; }

  /// Whether input profile is flow-sensitive.
  /// @return True if the input profile is flow-sensitive.
  bool profileIsFS() const { return ProfileIsFS; }

  /// Return the profile symbol list, if this reader supports one.
  /// @return The profile symbol list, or null if unsupported.
  virtual std::unique_ptr<ProfileSymbolList> getProfileSymbolList() {
    return nullptr;
  };

  /// It includes all the names that have samples either in outline instance
  /// or inline instance.
  /// @return Range over names that have outline or inline samples.
  virtual llvm::iterator_range<SampleProfileNameTable::iterator>
  getNameTable() const {
    return {SampleProfileNameTable::iterator(),
            SampleProfileNameTable::iterator()};
  }
  /// Dump section header information to \p OS.
  /// @param OS Output stream receiving the section dump.
  /// @return True if section info was dumped.
  virtual bool dumpSectionInfo(raw_ostream &OS = dbgs()) { return false; };
  /// Return true if the profile contains string key \p Key.
  /// @param Key Function name to look up.
  /// @return True if the profile contains string key \p Key.
  virtual bool contains(StringRef Key) const { return false; }
  /// Return true if the profile contains GUID \p GUID.
  /// @param GUID Function MD5/hash to look up.
  /// @return True if the profile contains GUID \p GUID.
  virtual bool contains(uint64_t GUID) const { return false; }

  /// Return whether names in the profile are all MD5 numbers.
  /// @return True if names in the profile are all MD5 numbers.
  bool useMD5() const { return ProfileIsMD5; }

  /// Force the profile to use MD5 in Sample contexts, even if function names
  /// are present.
  virtual void setProfileUseMD5() { ProfileIsMD5 = true; }

  /// Don't read profile without context if the flag is set.
  /// @param Skip When true, skip flat (non-context) profiles.
  void setSkipFlatProf(bool Skip) { SkipFlatProf = Skip; }

  /// Return whether any name in the profile contains ".__uniq." suffix.
  /// @return True if any name in the profile contains ".__uniq." suffix.
  virtual bool hasUniqSuffix() { return false; }

  /// Return the Itanium remapper associated with this reader, if any.
  /// @return The Itanium remapper associated with this reader, or null.
  SampleProfileReaderItaniumRemapper *getRemapper() { return Remapper.get(); }

  /// Set the module currently being compiled.
  /// @param Mod Module used for on-demand profile loading; may be null.
  void setModule(const Module *Mod) { M = Mod; }

  /// Set the function-name to profile-name map used during lookup.
  /// @param FPMap Map from IR function names to matched profile names.
  void setFuncNameToProfNameMap(
      const HashKeyMap<DenseMap, FunctionId, FunctionId> &FPMap) {
    FuncNameToProfNameMap = &FPMap;
  }

protected:
  /// Map every function to its associated profile.
  ///
  /// The profile of every function executed at runtime is collected
  /// in the structure FunctionSamples. This maps function objects
  /// to their corresponding profiles.
  SampleProfileMap Profiles;

  /// LLVM context used to emit diagnostics.
  LLVMContext &Ctx;

  /// Memory buffer holding the profile file.
  std::unique_ptr<MemoryBuffer> Buffer;

  /// Profile summary information.
  std::unique_ptr<ProfileSummary> Summary;

  /// Take ownership of the summary of this reader.
  /// @param Reader Reader whose summary ownership is transferred.
  /// @return Ownership of the summary previously held by \p Reader.
  static std::unique_ptr<ProfileSummary>
  takeSummary(SampleProfileReader &Reader) {
    return std::move(Reader.Summary);
  }

  /// Compute summary for this profile.
  LLVM_ABI void computeSummary();

  /// Read sample profiles for selected functions into \p Profiles.
  ///
  /// Currently it's only used for extended binary format to load the profiles
  /// on-demand.
  /// @param FuncsToUse Functions whose profiles should be loaded.
  /// @param Profiles Map receiving the loaded FunctionSamples.
  /// @return Success, or an error if reading fails.
  virtual std::error_code read(const DenseSet<StringRef> &FuncsToUse,
                               SampleProfileMap &Profiles) {
    return sampleprof_error::not_implemented;
  }

  /// Optional remapper applied after profiles are read.
  std::unique_ptr<SampleProfileReaderItaniumRemapper> Remapper;

  /// Map from IR function names to matched profile names.
  ///
  /// Points to SampleProfileLoader::FuncNameToProfNameMap so the sample loader
  /// can look up profiles using the new IR name.
  const HashKeyMap<DenseMap, FunctionId, FunctionId> *FuncNameToProfNameMap =
      nullptr;

  /// Map from context hash to metadata section range for on-demand reads.
  DenseMap<uint64_t, std::pair<const uint8_t *, const uint8_t *>>
      FuncMetadataIndex;

  /// Byte range of the main profile section in the buffer.
  std::pair<const uint8_t *, const uint8_t *> ProfileSecRange;

  /// Whether the profile has attribute metadata.
  bool ProfileHasAttribute = false;

  /// \brief Whether samples are collected based on pseudo probes.
  bool ProfileIsProbeBased = false;

  /// Whether function profiles are context-sensitive flat profiles.
  bool ProfileIsCS = false;

  /// Whether function profile contains ShouldBeInlined contexts.
  bool ProfileIsPreInlined = false;

  /// Number of context-sensitive profiles.
  uint32_t CSProfileCount = 0;

  /// Whether the function profiles use FS discriminators.
  bool ProfileIsFS = false;

  /// Format version of the profile.
  uint64_t FormatVersion = 0;

  /// If true, the profile has vtable profiles and reader should decode them
  /// to parse profiles correctly.
  bool ReadVTableProf = false;

  /// \brief The format of sample.
  SampleProfileFormat Format = SPF_None;

  /// Module currently being compiled, if any.
  ///
  /// The current module being compiled if SampleProfileReader is used by
  /// compiler. If SampleProfileReader is used by other tools which are not
  /// compiler, M is usually nullptr.
  const Module *M = nullptr;

  /// Zero out the discriminator bits higher than bit MaskedBitFrom (0 based).
  /// The default is to keep all the bits.
  uint32_t MaskedBitFrom = 31;

  /// Whether the profile uses MD5 for Sample Contexts and function names. This
  /// can be one-way overriden by the user to force use MD5.
  bool ProfileIsMD5 = false;

  /// If SkipFlatProf is true, skip functions marked with !Flat in text mode or
  /// sections with SecFlagFlat flag in ExtBinary mode.
  bool SkipFlatProf = false;
};

/// Sample profile reader for the text profile format.
class LLVM_ABI SampleProfileReaderText : public SampleProfileReader {
public:
  /// Construct a text sample profile reader for buffer \p B.
  /// @param B Memory buffer holding the profile.
  /// @param C LLVM context used for diagnostics.
  SampleProfileReaderText(std::unique_ptr<MemoryBuffer> B, LLVMContext &C)
      : SampleProfileReader(std::move(B), C, SPF_Text) {}

  /// Read and validate the file header.
  /// @return Success.
  std::error_code readHeader() override { return sampleprof_error::success; }

  /// Read sample profiles from the associated file.
  /// @return Success, or an error if reading fails.
  std::error_code readImpl() override;

  /// Return true if \p Buffer is in the format supported by this class.
  /// @param Buffer Memory buffer to test for the text profile format.
  /// @return True if \p Buffer is in the text profile format.
  static bool hasFormat(const MemoryBuffer &Buffer);

  /// Text format sample profile does not support MD5 for now.
  void setProfileUseMD5() override {}

private:
  /// CSNameTable is used to save full context vectors. This serves as an
  /// underlying immutable buffer for all clients.
  std::list<SampleContextFrameVector> CSNameTable;
};

/// Sample profile reader for binary profile formats.
class LLVM_ABI SampleProfileReaderBinary : public SampleProfileReader {
public:
  /// Construct a binary sample profile reader for buffer \p B.
  /// @param B Memory buffer holding the profile.
  /// @param C LLVM context used for diagnostics.
  /// @param Format Profile format tag; defaults to SPF_None.
  SampleProfileReaderBinary(std::unique_ptr<MemoryBuffer> B, LLVMContext &C,
                            SampleProfileFormat Format = SPF_None)
      : SampleProfileReader(std::move(B), C, Format) {}

  /// Read and validate the file header.
  /// @return Success, or an error if the header is invalid.
  std::error_code readHeader() override;

  /// Read sample profiles from the associated file.
  /// @return Success, or an error if reading fails.
  std::error_code readImpl() override;

  /// It includes all the names that have samples either in outline instance
  /// or inline instance.
  /// @return Range over names that have outline or inline samples.
  llvm::iterator_range<SampleProfileNameTable::iterator>
  getNameTable() const override {
    if (!NameTable)
      return {SampleProfileNameTable::iterator(),
              SampleProfileNameTable::iterator()};
    return {NameTable->begin(), NameTable->end()};
  }

  /// Return true if the name table contains string key \p Key.
  /// @param Key Function name to look up.
  /// @return True if the name table contains string key \p Key.
  bool contains(StringRef Key) const override {
    assert(NameTable && "NameTable should be populated before querying");
    return NameTable->contains(Key);
  }

  /// Return true if the name table contains GUID \p GUID.
  /// @param GUID Function MD5/hash to look up.
  /// @return True if the name table contains GUID \p GUID.
  bool contains(uint64_t GUID) const override {
    assert(NameTable && "NameTable should be populated before querying");
    return NameTable->contains(GUID);
  }

protected:
  /// Read a numeric value of type T from the profile.
  ///
  /// If an error occurs during decoding, a diagnostic message is emitted and
  /// EC is set.
  ///
  /// \returns the read value.
  template <typename T> ErrorOr<T> readNumber();

  /// Read a numeric value of type T from the profile. The value is saved
  /// without encoded.
  /// @return The read value, or an error on failure.
  template <typename T> ErrorOr<T> readUnencodedNumber();

  /// Read a string from the profile.
  ///
  /// If an error occurs during decoding, a diagnostic message is emitted and
  /// EC is set.
  ///
  /// \returns the read value.
  ErrorOr<StringRef> readString();

  /// Read the string index and check whether it overflows the table.
  /// @param Table Name or context table used to validate the index range.
  /// @return The string index, or an error if it overflows \p Table.
  template <typename T> inline ErrorOr<size_t> readStringIndex(T &Table);

  /// Read the next function profile instance starting at \p Start.
  /// @param Start Pointer to the start of the function profile record.
  /// @return Success, or an error if reading fails.
  std::error_code readFuncProfile(const uint8_t *Start);
  /// Read the next function profile instance starting at \p Start into \p Profiles.
  /// @param Start Pointer to the start of the function profile record.
  /// @param Profiles Map receiving the loaded FunctionSamples.
  /// @return Success, or an error if reading fails.
  std::error_code readFuncProfile(const uint8_t *Start,
                                  SampleProfileMap &Profiles);

  /// Read the contents of the given profile instance.
  /// @param FProfile FunctionSamples instance to populate.
  /// @return Success, or an error if reading fails.
  std::error_code readProfile(FunctionSamples &FProfile);

  /// Read the contents of Magic number and Version number.
  /// @return Success, or an error if reading fails.
  std::error_code readMagicIdent();

  /// Read profile summary.
  /// @return Success, or an error if reading fails.
  std::error_code readSummary();

  /// Read the whole name table.
  /// @return Success, or an error if reading fails.
  std::error_code readNameTable();

  /// Read a string indirectly via the name table. Optionally return the index.
  /// @param RetIdx Optional out-parameter set to the name-table index.
  /// @return The FunctionId from the name table, or an error on failure.
  ErrorOr<FunctionId> readStringFromTable(size_t *RetIdx = nullptr);

  /// Read a context indirectly via the CSNameTable. Optionally return the
  /// index.
  /// @param RetIdx Optional out-parameter set to the CS name-table index.
  /// @return The context frames, or an error on failure.
  ErrorOr<SampleContextFrames> readContextFromTable(size_t *RetIdx = nullptr);

  /// Read a context indirectly via the CSNameTable if the profile has context,
  /// otherwise same as readStringFromTable, also return its hash value.
  /// @return The sample context and its hash, or an error on failure.
  ErrorOr<std::pair<SampleContext, uint64_t>> readSampleContextFromTable();

  /// Read all virtual functions' vtable access counts for \p FProfile.
  /// @param FProfile FunctionSamples whose callsite vtable profiles are read.
  /// @return Success, or an error if reading fails.
  std::error_code readCallsiteVTableProf(FunctionSamples &FProfile);

  /// Decode a vtable type-count map from the input buffer into \p M.
  ///
  /// Read bytes from the input buffer pointed by `Data` and decode them into
  /// \p M. `Data` will be advanced to the end of the read bytes when this
  /// function returns. Returns error if any.
  /// @param M Map receiving vtable type hashes and their access counts.
  /// @return Success, or an error if reading fails.
  std::error_code readVTableTypeCountMap(TypeCountMap &M);

  /// Points to the current location in the buffer.
  const uint8_t *Data = nullptr;

  /// Points to the end of the buffer.
  const uint8_t *End = nullptr;

  /// Function name table.
  std::unique_ptr<SampleProfileNameTable> NameTable;

  /// CSNameTable is used to save full context vectors. It is the backing buffer
  /// for SampleContextFrames.
  std::vector<SampleContextFrameVector> CSNameTable;

  /// Table to cache MD5 values of sample contexts corresponding to
  /// readSampleContextFromTable(), used to index into Profiles or
  /// FuncOffsetTable.
  std::vector<uint64_t> MD5SampleContextTable;

  /// Starting address of the MD5 sample-context table.
  ///
  /// The starting address of the table of MD5 values of sample contexts. For
  /// fixed length MD5 non-CS profile it is same as MD5NameMemStart because
  /// hashes of non-CS contexts are already in the profile. Otherwise it points
  /// to the start of MD5SampleContextTable.
  const uint64_t *MD5SampleContextStart = nullptr;

private:
  std::error_code readSummaryEntry(std::vector<ProfileSummaryEntry> &Entries);
  virtual std::error_code verifySPMagic(uint64_t Magic) = 0;
};

/// Sample profile reader for the raw (non-extensible) binary format.
class LLVM_ABI SampleProfileReaderRawBinary : public SampleProfileReaderBinary {
private:
  std::error_code verifySPMagic(uint64_t Magic) override;

public:
  /// Construct a raw-binary sample profile reader for buffer \p B.
  /// @param B Memory buffer holding the profile.
  /// @param C LLVM context used for diagnostics.
  /// @param Format Profile format tag; defaults to SPF_Binary.
  SampleProfileReaderRawBinary(std::unique_ptr<MemoryBuffer> B, LLVMContext &C,
                               SampleProfileFormat Format = SPF_Binary)
      : SampleProfileReaderBinary(std::move(B), C, Format) {}

  /// Return true if \p Buffer is in the format supported by this class.
  /// @param Buffer Memory buffer to test for the raw binary format.
  /// @return True if \p Buffer is in the raw binary format.
  static bool hasFormat(const MemoryBuffer &Buffer);
};

/// Tag type selecting in-memory DenseMap storage for SampleProfileFuncOffsetTable.
struct InMemoryModeT {};
/// Tag type selecting Eytzinger parallel-array storage for SampleProfileFuncOffsetTable.
struct EytzingerModeT {};

/// Tag value selecting in-memory mode for SampleProfileFuncOffsetTable.
inline constexpr InMemoryModeT InMemoryMode{};
/// Tag value selecting Eytzinger mode for SampleProfileFuncOffsetTable.
inline constexpr EytzingerModeT EytzingerMode{};

/// A unified wrapper representing the function offset table.
///
/// This class abstracts away the physical representation of the offset table,
/// which can either be:
///
/// - An llvm::DenseMap mapping function GUIDs (or context hashes) to their
///   profile offsets, populated when reading the array of offsets in
///   context-sensitive (CS) profiles or version 103 profiles.
///
/// - A raw slice of 32-bit relative offsets for Eytzinger parallel lookups.
///
/// It exposes a single, type-agnostic lookup interface, shielding the reader
/// from the underlying container types. To prevent hybrid-state corruption, the
/// table's mode is locked at construction time.
class SampleProfileFuncOffsetTable {
public:
  /// Storage mode for the function offset table.
  enum class TableMode {
    /// Offsets stored in an in-memory DenseMap keyed by GUID.
    InMemory,
    /// Offsets stored as a parallel array for Eytzinger name lookups.
    Eytzinger
  };

  /// Deleted default constructor; a mode must be selected.
  SampleProfileFuncOffsetTable() = delete;
  /// Deleted copy constructor.
  /// @param Other Unused; copy construction is deleted.
  SampleProfileFuncOffsetTable(const SampleProfileFuncOffsetTable &Other) =
      delete;
  /// Deleted copy assignment.
  /// @param Other Unused; copy assignment is deleted.
  SampleProfileFuncOffsetTable &
  operator=(const SampleProfileFuncOffsetTable &Other) = delete;
  /// Deleted move constructor.
  /// @param Other Unused; move construction is deleted.
  SampleProfileFuncOffsetTable(SampleProfileFuncOffsetTable &&Other) = delete;
  /// Deleted move assignment.
  /// @param Other Unused; move assignment is deleted.
  SampleProfileFuncOffsetTable &
  operator=(SampleProfileFuncOffsetTable &&Other) = delete;

  /// Construct an in-memory offset table with optional \p InitialCapacity.
  /// @param Tag Tag selecting in-memory DenseMap storage.
  /// @param InitialCapacity Optional reserved capacity for the DenseMap.
  explicit SampleProfileFuncOffsetTable([[maybe_unused]] InMemoryModeT Tag,
                                        size_t InitialCapacity = 0)
      : Mode(TableMode::InMemory) {
    InMemoryTable.reserve(InitialCapacity);
  }

  /// Construct an Eytzinger offset table over \p NameSpan and \p FuncOffsetSpan.
  /// @param Tag Tag selecting Eytzinger parallel-array storage.
  /// @param NameSpan Eytzinger-ordered GUID/name span used for lookups.
  /// @param FuncOffsetSpan Parallel array of relative function offsets.
  SampleProfileFuncOffsetTable(
      [[maybe_unused]] EytzingerModeT Tag,
      EytzingerTableSpan<support::ulittle64_t> NameSpan,
      ArrayRef<support::ulittle32_t> FuncOffsetSpan)
      : Mode(TableMode::Eytzinger), NameSpan(NameSpan),
        FuncOffsetSpan(FuncOffsetSpan) {}

  /// Insert a function GUID and its profile offset into the in-memory map.
  /// @param GUID Function or context hash to insert.
  /// @param Offset File offset of the corresponding FunctionSamples.
  void insert(uint64_t GUID, uint64_t Offset) {
    assert(Mode == TableMode::InMemory &&
           "Cannot insert into a non-in-memory offset table");
    InMemoryTable[GUID] = Offset;
  }

  /// Query the offset table for the profile offset associated with the given
  /// GUID. Returns the offset if found, or std::nullopt if the key is missing.
  /// @param GUID Function or context hash to look up.
  /// @return The profile offset for \p GUID, or std::nullopt if missing.
  std::optional<uint64_t> lookup(uint64_t GUID) const {
    if (isEytzinger()) {
      if (std::optional<size_t> Idx = NameSpan.findIndex(GUID)) {
        uint32_t RelOffset = FuncOffsetSpan[*Idx];
        if (RelOffset != UINT32_MAX)
          return RelOffset;
      }
      return std::nullopt;
    }
    auto Iter = InMemoryTable.find(GUID);
    if (Iter != InMemoryTable.end())
      return Iter->second;
    return std::nullopt;
  }

  /// Direct read-only array (`ArrayRef`) of function offsets aligned parallel
  /// to the corresponding Eytzinger name span.
  /// @return Parallel array of function offsets for the Eytzinger name span.
  ArrayRef<support::ulittle32_t> getFuncOffsets() const {
    assert(isEytzinger() &&
           "Cannot call getFuncOffsets() on non-Eytzinger table");
    return FuncOffsetSpan;
  }

  /// Return the expected number of entries in the Eytzinger name span.
  /// @return The expected number of entries in the Eytzinger name span.
  size_t getExpectedSize() const {
    assert(isEytzinger() &&
           "Cannot call getExpectedSize() on non-Eytzinger table");
    return NameSpan.size();
  }

  /// Return true if this table uses Eytzinger parallel-array storage.
  /// @return True if this table uses Eytzinger parallel-array storage.
  bool isEytzinger() const { return Mode == TableMode::Eytzinger; }

private:
  TableMode Mode;
  llvm::DenseMap<hash_code, uint64_t> InMemoryTable;
  EytzingerTableSpan<support::ulittle64_t> NameSpan;
  ArrayRef<support::ulittle32_t> FuncOffsetSpan;
};

/// Base reader for sample profiles in the extensible binary format.
///
/// SampleProfileReaderExtBinaryBase/SampleProfileWriterExtBinaryBase defines
/// the basic structure of the extensible binary format.
/// The format is organized in sections except the magic and version number
/// at the beginning. There is a section table before all the sections, and
/// each entry in the table describes the entry type, start, size and
/// attributes. The format in each section is defined by the section itself.
///
/// It is easy to add a new section while maintaining the backward
/// compatibility of the profile. Nothing extra needs to be done. If we want
/// to extend an existing section, like add cache misses information in
/// addition to the sample count in the profile body, we can add a new section
/// with the extension and retire the existing section, and we could choose
/// to keep the parser of the old section if we want the reader to be able
/// to read both new and old format profile.
///
/// SampleProfileReaderExtBinary/SampleProfileWriterExtBinary define the
/// commonly used sections of a profile in extensible binary format. It is
/// possible to define other types of profile inherited from
/// SampleProfileReaderExtBinaryBase/SampleProfileWriterExtBinaryBase.
class LLVM_ABI SampleProfileReaderExtBinaryBase
    : public SampleProfileReaderBinary {
private:
  std::error_code decompressSection(const uint8_t *SecStart,
                                    const uint64_t SecSize,
                                    const uint8_t *&DecompressBuf,
                                    uint64_t &DecompressBufSize);

  BumpPtrAllocator Allocator;

protected:
  /// Section header table describing each profile section.
  std::vector<SecHdrTableEntry> SecHdrTable;
  /// Read the section header table entry at index \p Idx.
  /// @param Idx Index into SecHdrTable / the on-disk section header table.
  /// @return Success, or an error if reading fails.
  std::error_code readSecHdrTableEntry(uint64_t Idx);
  /// Read the full section header table.
  /// @return Success, or an error if reading fails.
  std::error_code readSecHdrTable();

  /// Read function metadata for the profiles in \p Profiles.
  /// @param Profiles Set of FunctionSamples instances to attach metadata to.
  /// @return Success, or an error if reading fails.
  std::error_code readFuncMetadata(DenseSet<FunctionSamples *> &Profiles);
  /// Read function metadata for all loaded profiles.
  /// @return Success, or an error if reading fails.
  std::error_code readFuncMetadata();
  /// Read function metadata for a single profile \p FProfile.
  /// @param FProfile FunctionSamples instance to update; may be null.
  /// @return Success, or an error if reading fails.
  std::error_code readFuncMetadata(FunctionSamples *FProfile);
  /// Read the function offset table in Eytzinger or legacy layout.
  /// @param IsEytzinger Whether the table uses Eytzinger layout.
  /// @param IsNested Whether the table covers nested (CS) contexts.
  /// @return Success, or an error if reading fails.
  std::error_code readFuncOffsetTable(bool IsEytzinger, bool IsNested);
  /// Read an Eytzinger-layout function offset table.
  /// @param IsNested Whether the table covers nested (CS) contexts.
  /// @return Success, or an error if reading fails.
  std::error_code readEytzingerFuncOffsetTable(bool IsNested);
  /// Read a legacy (non-Eytzinger) function offset table.
  /// @return Success, or an error if reading fails.
  std::error_code readLegacyFuncOffsetTable();
  /// Read all function profiles from the profile body section.
  /// @return Success, or an error if reading fails.
  std::error_code readFuncProfiles();
  /// Read function profiles for \p FuncsToUse into \p Profiles.
  /// @param FuncsToUse Functions whose profiles should be loaded.
  /// @param Profiles Map receiving the loaded FunctionSamples.
  /// @return Success, or an error if reading fails.
  std::error_code readFuncProfiles(const DenseSet<StringRef> &FuncsToUse,
                                   SampleProfileMap &Profiles);
  /// Read the name table section.
  /// @param IsMD5 Whether names are stored as MD5 hashes.
  /// @param FixedLengthMD5 Whether MD5 hashes use fixed-length encoding.
  /// @param IsEytzinger Whether the name table uses Eytzinger layout.
  /// @return Success, or an error if reading fails.
  std::error_code readNameTableSec(bool IsMD5, bool FixedLengthMD5,
                                   bool IsEytzinger = false);
  /// Read an Eytzinger-layout name table section.
  /// @param IsMD5 Whether names are stored as MD5 hashes.
  /// @param FixedLengthMD5 Whether MD5 hashes use fixed-length encoding.
  /// @return Success, or an error if reading fails.
  std::error_code readNameTableSecEytzinger(bool IsMD5, bool FixedLengthMD5);
  /// Read a legacy (non-Eytzinger) name table section.
  /// @param IsMD5 Whether names are stored as MD5 hashes.
  /// @param FixedLengthMD5 Whether MD5 hashes use fixed-length encoding.
  /// @return Success, or an error if reading fails.
  std::error_code readNameTableSecLegacy(bool IsMD5, bool FixedLengthMD5);
  /// Read the context-sensitive name table section.
  /// @return Success, or an error if reading fails.
  std::error_code readCSNameTableSec();
  /// Read the profile symbol list section.
  /// @param IsMD5 Whether symbols are stored as MD5 hashes.
  /// @return Success, or an error if reading fails.
  std::error_code readProfileSymbolList(bool IsMD5);
  /// Read a string-based profile symbol list.
  /// @return Success, or an error if reading fails.
  std::error_code readStringBasedProfileSymbolList();
  /// Read an MD5-based profile symbol list.
  /// @return Success, or an error if reading fails.
  std::error_code readMD5ProfileSymbolList();

  /// Read and validate the extensible-binary file header.
  /// @return Success, or an error if the header is invalid.
  std::error_code readHeader() override;
  /// Verify that \p Magic is a valid extensible-binary sample profile magic.
  /// @param Magic Magic number read from the profile header.
  /// @return Success, or an error if \p Magic is invalid.
  std::error_code verifySPMagic(uint64_t Magic) override = 0;
  /// Read one section described by \p Entry starting at \p Start.
  /// @param Start Pointer to the first byte of the section payload.
  /// @param Size Size in bytes of the section payload.
  /// @param Entry Section header table entry for this section.
  /// @return Success, or an error if reading fails.
  virtual std::error_code readOneSection(const uint8_t *Start, uint64_t Size,
                                         const SecHdrTableEntry &Entry);
  /// Dispatch reading of a custom section described by \p Entry.
  ///
  /// Placeholder for subclasses to dispatch their own section readers.
  /// @param Entry Section header table entry for the custom section.
  /// @return Success, or an error if reading fails.
  virtual std::error_code readCustomSection(const SecHdrTableEntry &Entry) = 0;

  /// Determine which container readFuncOffsetTable() should populate, the list
  /// FuncOffsetList or the map FuncOffsetTable.
  /// @return True if FuncOffsetList should be populated instead of FuncOffsetTable.
  bool useFuncOffsetList() const;

  /// Profile symbol list loaded from the profile, if present.
  std::unique_ptr<ProfileSymbolList> ProfSymList;

  /// Map from a function context MD5 to its FunctionSamples file offset.
  ///
  /// The table mapping from a function context's MD5 to the offset of its
  /// FunctionSample towards file start.
  /// At most one of FuncOffsetTable and FuncOffsetList is populated.
  std::optional<SampleProfileFuncOffsetTable> FuncOffsetTable;

  /// The list version of FuncOffsetTable. This is used if every entry is
  /// being accessed.
  std::vector<std::pair<SampleContext, uint64_t>> FuncOffsetList;

  /// The set containing the functions to use when compiling a module.
  DenseSet<StringRef> FuncsToUse;

public:
  /// Construct an extensible-binary base reader for buffer \p B.
  /// @param B Memory buffer holding the profile.
  /// @param C LLVM context used for diagnostics.
  /// @param Format Profile format tag for this reader.
  SampleProfileReaderExtBinaryBase(std::unique_ptr<MemoryBuffer> B,
                                   LLVMContext &C, SampleProfileFormat Format)
      : SampleProfileReaderBinary(std::move(B), C, Format) {
    FuncOffsetTable.emplace(InMemoryMode);
  }

  /// Read sample profiles in extensible format from the associated file.
  /// @return Success, or an error if reading fails.
  std::error_code readImpl() override;

  /// Get the total size of all \p Type sections.
  /// @param Type Section type whose sizes should be summed.
  /// @return Combined size in bytes of all sections of \p Type.
  uint64_t getSectionSize(SecType Type);
  /// Get the total size of header and all sections.
  /// @return Total size in bytes of the profile file contents.
  uint64_t getFileSize();
  /// Dump section header information to \p OS.
  /// @param OS Output stream receiving the section dump.
  /// @return True if section info was dumped.
  bool dumpSectionInfo(raw_ostream &OS = dbgs()) override;

  /// Collect functions with definitions in Module M. Return true if
  /// the reader has been given a module.
  /// @return True if the reader has been given a module.
  bool collectFuncsFromModule() override;

  /// Take ownership of the loaded profile symbol list, if any.
  /// @return Ownership of the profile symbol list, or null if none was loaded.
  std::unique_ptr<ProfileSymbolList> getProfileSymbolList() override {
    return std::move(ProfSymList);
  };

private:
  /// Read the profiles on-demand for the given functions. This is used after
  /// stale call graph matching finds new functions whose profiles aren't loaded
  /// at the beginning and we need to loaded the profiles explicitly for
  /// potential matching.
  std::error_code read(const DenseSet<StringRef> &FuncsToUse,
                       SampleProfileMap &Profiles) override;
};

/// Sample profile reader for the common extensible binary format sections.
class LLVM_ABI SampleProfileReaderExtBinary
    : public SampleProfileReaderExtBinaryBase {
private:
  std::error_code verifySPMagic(uint64_t Magic) override;
  std::error_code readCustomSection(const SecHdrTableEntry &Entry) override {
    // Update the data reader pointer to the end of the section.
    Data = End;
    return sampleprof_error::success;
  };

public:
  /// Construct an extensible-binary sample profile reader for buffer \p B.
  /// @param B Memory buffer holding the profile.
  /// @param C LLVM context used for diagnostics.
  /// @param Format Profile format tag; defaults to SPF_Ext_Binary.
  SampleProfileReaderExtBinary(std::unique_ptr<MemoryBuffer> B, LLVMContext &C,
                               SampleProfileFormat Format = SPF_Ext_Binary)
      : SampleProfileReaderExtBinaryBase(std::move(B), C, Format) {}

  /// Return true if \p Buffer is in the format supported by this class.
  /// @param Buffer Memory buffer to test for the extensible binary format.
  /// @return True if \p Buffer is in the extensible binary format.
  static bool hasFormat(const MemoryBuffer &Buffer);
};

/// Call stack of nested FunctionSamples used while reading GCC AFDO profiles.
using InlineCallStack = SmallVector<FunctionSamples *, 10>;

/// GCC histogram kinds; only call-target histograms are needed here.
enum HistType {
  /// Interval histogram.
  HIST_TYPE_INTERVAL,
  /// Power-of-two histogram.
  HIST_TYPE_POW2,
  /// Single-value histogram.
  HIST_TYPE_SINGLE_VALUE,
  /// Constant-delta histogram.
  HIST_TYPE_CONST_DELTA,
  /// Indirect-call target histogram.
  HIST_TYPE_INDIR_CALL,
  /// Average-value histogram.
  HIST_TYPE_AVERAGE,
  /// Inclusive-or histogram.
  HIST_TYPE_IOR,
  /// Top-N indirect-call target histogram.
  HIST_TYPE_INDIR_CALL_TOPN
};

/// Sample profile reader for GCC AutoFDO (GCOV-based) profiles.
class LLVM_ABI SampleProfileReaderGCC : public SampleProfileReader {
public:
  /// Construct a GCC sample profile reader for buffer \p B.
  /// @param B Memory buffer holding the GCC AFDO profile.
  /// @param C LLVM context used for diagnostics.
  SampleProfileReaderGCC(std::unique_ptr<MemoryBuffer> B, LLVMContext &C)
      : SampleProfileReader(std::move(B), C, SPF_GCC),
        GcovBuffer(Buffer.get()) {}

  /// Read and validate the file header.
  /// @return Success, or an error if the header is invalid.
  std::error_code readHeader() override;

  /// Read sample profiles from the associated file.
  /// @return Success, or an error if reading fails.
  std::error_code readImpl() override;

  /// Return true if \p Buffer is in the format supported by this class.
  /// @param Buffer Memory buffer to test for the GCC AFDO format.
  /// @return True if \p Buffer is in the GCC AFDO format.
  static bool hasFormat(const MemoryBuffer &Buffer);

protected:
  /// Read the function name table from the GCC profile.
  /// @return Success, or an error if reading fails.
  std::error_code readNameTable();
  /// Read one function profile, possibly nested via \p InlineStack.
  /// @param InlineStack Stack of enclosing FunctionSamples for inlined sites.
  /// @param Update Whether to merge into an existing profile instance.
  /// @param Offset Offset of this function record in the GCOV buffer.
  /// @return Success, or an error if reading fails.
  std::error_code readOneFunctionProfile(const InlineCallStack &InlineStack,
                                         bool Update, uint32_t Offset);
  /// Read all function profiles from the GCC AFDO section.
  /// @return Success, or an error if reading fails.
  std::error_code readFunctionProfiles();
  /// Skip the next word in the GCOV buffer.
  /// @return Success, or an error if skipping fails.
  std::error_code skipNextWord();
  /// Read a numeric value of type T from the GCOV buffer.
  /// @return The read value, or an error on failure.
  template <typename T> ErrorOr<T> readNumber();
  /// Read a string from the GCOV buffer.
  /// @return The read string, or an error on failure.
  ErrorOr<StringRef> readString();

  /// Read the section tag and check that it's the same as \p Expected.
  /// @param Expected Expected GCOV section tag value.
  /// @return Success, or an error if the tag does not match \p Expected.
  std::error_code readSectionTag(uint32_t Expected);

  /// GCOV buffer containing the profile.
  GCOVBuffer GcovBuffer;

  /// Function names in this profile.
  std::vector<std::string> Names;

  /// GCOV tags used to separate sections in the profile file.
  static const uint32_t GCOVTagAFDOFileNames = 0xaa000000;
  /// GCOV tag marking the AFDO function profile section.
  static const uint32_t GCOVTagAFDOFunction = 0xac000000;
};

} // end namespace sampleprof

} // end namespace llvm

#endif // LLVM_PROFILEDATA_SAMPLEPROFREADER_H
