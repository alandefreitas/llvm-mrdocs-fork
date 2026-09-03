#ifndef LLVM_PROFILEDATA_MEMPROFYAML_H_
#define LLVM_PROFILEDATA_MEMPROFYAML_H_

#include "llvm/ADT/SmallVector.h"
#include "llvm/ProfileData/DataAccessProf.h"
#include "llvm/ProfileData/MemProf.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/YAMLTraits.h"

namespace llvm {
namespace memprof {
/// Strong typedef for a 64-bit MemProf function GUID in YAML.
///
/// See ScalarTraits<memprof::GUIDHex64> for how a GUID is serialized and
/// deserialized in YAML.
struct GUIDHex64 {
  /// Construct a zero-initialized GUID.
  GUIDHex64() = default;
  /// Construct from base value \p V.
  /// \param V Value to store.
  GUIDHex64(const uint64_t V) : value(V) {}
  /// Copy-construct from another \c GUIDHex64.
  /// \param V Value to copy.
  GUIDHex64(const GUIDHex64 &V) = default;
  /// Copy-assign from another \c GUIDHex64.
  /// \param RHS Value to assign.
  /// \returns Reference to this object.
  GUIDHex64 &operator=(const GUIDHex64 &RHS) = default;
  /// Assign from base value \p RHS.
  /// \param RHS Base value to assign.
  /// \returns Reference to this object.
  GUIDHex64 &operator=(const uint64_t &RHS) {
    value = RHS;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \returns Const reference to the stored GUID value.
  operator const uint64_t &() const { return value; }
  /// Return true if this equals \p RHS.
  /// \param RHS Value to compare.
  /// \returns True if this GUID equals \p RHS.
  bool operator==(const GUIDHex64 &RHS) const { return value == RHS.value; }
  /// Return true if this equals base value \p RHS.
  /// \param RHS Base value to compare.
  /// \returns True if this GUID equals \p RHS.
  bool operator==(const uint64_t &RHS) const { return value == RHS; }
  /// Return true if this is less than \p RHS.
  /// \param RHS Value to compare.
  /// \returns True if this GUID is less than \p RHS.
  bool operator<(const GUIDHex64 &RHS) const { return value < RHS.value; }
  /// Stored 64-bit GUID value.
  uint64_t value;
  /// Underlying integral base type.
  using BaseType = uint64_t;
};

/// Pair of a function GUID and its MemProf record for YAML I/O.
///
/// In YAML, we treat the GUID and the fields within MemProfRecord at the same
/// level as if the GUID were part of MemProfRecord.
struct GUIDMemProfRecordPair {
  /// Function GUID associated with \c Record.
  GUIDHex64 GUID;
  /// MemProf profile record for \c GUID.
  MemProfRecord Record;
};

/// Owned YAML-friendly view of \c memprof::DataAccessProfData.
///
/// The struct members use owned strings. This is for simplicity and assumes
/// that most real world use cases do look-ups and regression test scale is
/// small.
struct YamlDataAccessProfData {
  /// Sampled data-access profile records.
  std::vector<memprof::DataAccessProfRecord> Records;
  /// Known-cold string hashes.
  std::vector<uint64_t> KnownColdStrHashes;
  /// Known-cold symbol names.
  std::vector<std::string> KnownColdSymbols;

  /// Return true when this container holds no data-access profile content.
  /// \returns True if records, cold hashes, and cold symbols are all empty.
  bool isEmpty() const {
    return Records.empty() && KnownColdStrHashes.empty() &&
           KnownColdSymbols.empty();
  }
};

/// Top-level MemProf dataset used with YAML for now.
struct AllMemProfData {
  /// Heap allocation profile records keyed by function GUID.
  std::vector<GUIDMemProfRecordPair> HeapProfileRecords;
  /// Data-access profiles in a YAML-friendly owned representation.
  YamlDataAccessProfData YamlifiedDataAccessProfiles;
};
} // namespace memprof

namespace yaml {
/// YAML scalar traits that format \c memprof::GUIDHex64 as hexadecimal.
template <> struct ScalarTraits<memprof::GUIDHex64> {
  /// Write \p Val to \p Out as a hexadecimal GUID with a 0x prefix.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  static void output(const memprof::GUIDHex64 &Val, void *Ctx,
                     raw_ostream &Out) {
    (void)Ctx;
    // Print GUID as a hexadecimal number with 0x prefix, no padding to keep
    // test strings compact.
    Out << format("0x%" PRIx64, (uint64_t)Val);
  }
  /// Parse \p Scalar into \p Val as a hex GUID or function name.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  static StringRef input(StringRef Scalar, void *Ctx,
                         memprof::GUIDHex64 &Val) {
    (void)Ctx;
    // Reject decimal GUIDs.
    if (all_of(Scalar, [](char C) { return std::isdigit(C); }))
      return "use a hexadecimal GUID or a function instead";

    uint64_t Num;
    if (Scalar.starts_with_insensitive("0x")) {
      // Accept hexadecimal numbers starting with 0x or 0X.
      if (Scalar.getAsInteger(0, Num))
        return "invalid hex64 number";
      Val = Num;
    } else {
      // Otherwise, treat the input as a string containing a function name.
      Val = memprof::getGUID(Scalar);
    }
    return StringRef();
  }
  /// GUID scalars never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns Always \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) {
    (void)Scalar;
    return QuotingType::None;
  }
};

/// YAMLIO mapping traits for \c memprof::Frame.
template <> struct MappingTraits<memprof::Frame> {
  /// Frame fields with Function stored as \c memprof::GUIDHex64 for YAML I/O.
  ///
  /// Essentially the same as memprof::Frame except that Function is of type
  /// memprof::GUIDHex64 instead of GlobalValue::GUID. This class helps in two
  /// ways. During serialization, we print Function as a hexadecimal number.
  /// During deserialization, we accept a function name as an alternative to the
  /// usual GUID expressed as a hexadecimal number.
  class FrameWithHex64 {
  public:
    /// Construct an empty normalizer for YAML input.
    /// \param Io Unused YAML IO object.
    FrameWithHex64(IO &Io) { (void)Io; }
    /// Construct a normalizer from frame \p F for YAML output.
    /// \param Io Unused YAML IO object.
    /// \param F Source frame to normalize.
    FrameWithHex64(IO &Io, const memprof::Frame &F)
        : Function(F.Function), LineOffset(F.LineOffset), Column(F.Column),
          IsInlineFrame(F.IsInlineFrame) {
      (void)Io;
    }
    /// Rebuild a \c memprof::Frame from the normalized fields.
    /// \param Io Unused YAML IO object.
    /// \returns Frame reconstructed from the normalized fields.
    memprof::Frame denormalize(IO &Io) {
      (void)Io;
      return memprof::Frame(Function, LineOffset, Column, IsInlineFrame);
    }

    /// Function GUID, formatted as hexadecimal in YAML.
    memprof::GUIDHex64 Function = 0;
    static_assert(std::is_same_v<decltype(Function.value),
                                 decltype(memprof::Frame::Function)>);
    /// Source line offset of the call from the beginning of the parent function.
    decltype(memprof::Frame::LineOffset) LineOffset = 0;
    /// Source column of the call.
    decltype(memprof::Frame::Column) Column = 0;
    /// Whether this frame is an inlined call site.
    decltype(memprof::Frame::IsInlineFrame) IsInlineFrame = false;
  };

  /// Map frame fields to and from YAML.
  /// \param Io YAML input/output state.
  /// \param F Frame being mapped.
  static void mapping(IO &Io, memprof::Frame &F) {
    MappingNormalization<FrameWithHex64, memprof::Frame> Keys(Io, F);

    Io.mapRequired("Function", Keys->Function);
    Io.mapRequired("LineOffset", Keys->LineOffset);
    Io.mapRequired("Column", Keys->Column);
    Io.mapRequired("IsInlineFrame", Keys->IsInlineFrame);

    // Assert that the definition of Frame matches what we expect.  The
    // structured bindings below detect changes to the number of fields.
    // static_assert checks the type of each field.
    const auto &[Function, SymbolName, LineOffset, Column, IsInlineFrame] = F;
    static_assert(
        std::is_same_v<remove_cvref_t<decltype(Function)>, GlobalValue::GUID>);
    static_assert(std::is_same_v<remove_cvref_t<decltype(SymbolName)>,
                                 std::unique_ptr<std::string>>);
    static_assert(
        std::is_same_v<remove_cvref_t<decltype(LineOffset)>, uint32_t>);
    static_assert(std::is_same_v<remove_cvref_t<decltype(Column)>, uint32_t>);
    static_assert(
        std::is_same_v<remove_cvref_t<decltype(IsInlineFrame)>, bool>);

    // MSVC issues unused variable warnings despite the uses in static_assert
    // above.
    (void)Function;
    (void)SymbolName;
    (void)LineOffset;
    (void)Column;
    (void)IsInlineFrame;
  }

  /// When true, emit this mapping in flow style.
  ///
  /// Request the inline notation for brevity:
  ///   { Function: 123, LineOffset: 11, Column: 10, IsInlineFrame: true }
  static const bool flow = true;
};

/// YAMLIO custom mapping traits for \c memprof::PortableMemInfoBlock.
template <> struct CustomMappingTraits<memprof::PortableMemInfoBlock> {
  /// Read one schema field named \p KeyStr into \p MIB.
  /// \param Io YAML input/output state.
  /// \param KeyStr Field name from the YAML mapping.
  /// \param MIB Destination MemInfoBlock being populated.
  static void inputOne(IO &Io, StringRef KeyStr,
                       memprof::PortableMemInfoBlock &MIB) {
    // PortableMemInfoBlock keeps track of the set of fields that actually have
    // values.  We update the set here as we receive a key-value pair from the
    // YAML document.
    //
    // We set MIB.Name via a temporary variable because ScalarTraits<uintptr_t>
    // isn't available on macOS.
#define MIBEntryDef(NameTag, Name, Type)                                       \
  if (KeyStr == #Name) {                                                       \
    uint64_t Value;                                                            \
    Io.mapRequired(KeyStr, Value);                                             \
    MIB.Name = static_cast<Type>(Value);                                       \
    MIB.Schema.set(llvm::to_underlying(memprof::Meta::Name));                  \
    return;                                                                    \
  }
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
    Io.setError("Key is not a valid validation event");
  }

  /// Write schema-present fields of \p MIB as YAML mapping entries.
  /// \param Io YAML input/output state.
  /// \param MIB Source MemInfoBlock being written.
  static void output(IO &Io, memprof::PortableMemInfoBlock &MIB) {
    auto Schema = MIB.getSchema();
#define MIBEntryDef(NameTag, Name, Type)                                       \
  if (Schema.test(llvm::to_underlying(memprof::Meta::Name))) {                 \
    uint64_t Value = MIB.Name;                                                 \
    Io.mapRequired(#Name, Value);                                              \
  }
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
  }
};

/// YAMLIO mapping traits for \c memprof::AllocationInfo.
template <> struct MappingTraits<memprof::AllocationInfo> {
  /// Map allocation-info fields to and from YAML.
  /// \param Io YAML input/output state.
  /// \param AI Allocation info being mapped.
  static void mapping(IO &Io, memprof::AllocationInfo &AI) {
    Io.mapRequired("Callstack", AI.CallStack);
    Io.mapRequired("MemInfoBlock", AI.Info);
  }
};

/// YAMLIO mapping traits for \c memprof::CallSiteInfo.
template <> struct MappingTraits<memprof::CallSiteInfo> {
  /// Call-site fields with callee GUIDs stored as \c GUIDHex64 for YAML I/O.
  class CallSiteInfoWithHex64Guids {
  public:
    /// Construct an empty normalizer for YAML input.
    /// \param Io Unused YAML IO object.
    CallSiteInfoWithHex64Guids(IO &Io) { (void)Io; }
    /// Construct a normalizer from call-site info \p CS for YAML output.
    /// \param Io Unused YAML IO object.
    /// \param CS Source call-site info to normalize.
    CallSiteInfoWithHex64Guids(IO &Io, const memprof::CallSiteInfo &CS)
        : Frames(CS.Frames) {
      (void)Io;
      // Convert uint64_t GUIDs to GUIDHex64 for serialization.
      CalleeGuids.reserve(CS.CalleeGuids.size());
      for (uint64_t Guid : CS.CalleeGuids)
        CalleeGuids.push_back(memprof::GUIDHex64(Guid));
    }

    /// Rebuild a \c memprof::CallSiteInfo from the normalized fields.
    /// \param Io Unused YAML IO object.
    /// \returns Call-site info reconstructed from the normalized fields.
    memprof::CallSiteInfo denormalize(IO &Io) {
      (void)Io;
      memprof::CallSiteInfo CS;
      CS.Frames = Frames;
      // Convert GUIDHex64 back to uint64_t GUIDs after deserialization.
      CS.CalleeGuids.reserve(CalleeGuids.size());
      for (memprof::GUIDHex64 HexGuid : CalleeGuids)
        CS.CalleeGuids.push_back(HexGuid.value);
      return CS;
    }

    /// Call-stack frames; Function GUIDs use \c MappingTraits<memprof::Frame>.
    decltype(memprof::CallSiteInfo::Frames) Frames;
    /// Callee GUIDs normalized to \c GUIDHex64 for YAML scalar traits.
    SmallVector<memprof::GUIDHex64> CalleeGuids;
  };

  /// Map call-site info fields to and from YAML.
  /// \param Io YAML input/output state.
  /// \param CS Call-site info being mapped.
  static void mapping(IO &Io, memprof::CallSiteInfo &CS) {
    // Use MappingNormalization to handle the conversion between
    // memprof::CallSiteInfo and CallSiteInfoWithHex64Guids.
    MappingNormalization<CallSiteInfoWithHex64Guids, memprof::CallSiteInfo>
        Keys(Io, CS);
    Io.mapRequired("Frames", Keys->Frames);
    // Map the normalized CalleeGuids (which are now GUIDHex64).
    Io.mapOptional("CalleeGuids", Keys->CalleeGuids);
  }
};

/// YAMLIO mapping traits for \c memprof::GUIDMemProfRecordPair.
///
/// In YAML, we use GUIDMemProfRecordPair instead of MemProfRecord so that we
/// can treat the GUID and the fields within MemProfRecord at the same level as
/// if the GUID were part of MemProfRecord.
template <> struct MappingTraits<memprof::GUIDMemProfRecordPair> {
  /// Map GUID and MemProf record fields to and from YAML.
  /// \param Io YAML input/output state.
  /// \param Pair GUID/record pair being mapped.
  static void mapping(IO &Io, memprof::GUIDMemProfRecordPair &Pair) {
    Io.mapRequired("GUID", Pair.GUID);
    Io.mapOptional("AllocSites", Pair.Record.AllocSites);
    Io.mapOptional("CallSites", Pair.Record.CallSites);
  }
};

/// YAMLIO mapping traits for \c memprof::SourceLocation.
template <> struct MappingTraits<memprof::SourceLocation> {
  /// Map source-location fields to and from YAML.
  /// \param Io YAML input/output state.
  /// \param Loc Source location being mapped.
  static void mapping(IO &Io, memprof::SourceLocation &Loc) {
    Io.mapOptional("FileName", Loc.FileName);
    Io.mapOptional("Line", Loc.Line);
  }
};

/// YAMLIO mapping traits for \c memprof::DataAccessProfRecord.
template <> struct MappingTraits<memprof::DataAccessProfRecord> {
  /// Map data-access profile record fields to and from YAML.
  /// \param Io YAML input/output state.
  /// \param Rec Data-access profile record being mapped.
  static void mapping(IO &Io, memprof::DataAccessProfRecord &Rec) {
    if (Io.outputting()) {
      if (std::holds_alternative<std::string>(Rec.SymHandle)) {
        Io.mapOptional("Symbol", std::get<std::string>(Rec.SymHandle));
      } else {
        Io.mapOptional("Hash", std::get<uint64_t>(Rec.SymHandle));
      }
    } else {
      std::string SymName;
      uint64_t Hash = 0;
      Io.mapOptional("Symbol", SymName);
      Io.mapOptional("Hash", Hash);
      if (!SymName.empty()) {
        Rec.SymHandle = SymName;
      } else {
        Rec.SymHandle = Hash;
      }
    }
    Io.mapRequired("AccessCount", Rec.AccessCount);
    Io.mapOptional("Locations", Rec.Locations);
  }
};

/// YAMLIO mapping traits for \c memprof::YamlDataAccessProfData.
template <> struct MappingTraits<memprof::YamlDataAccessProfData> {
  /// Map YAML data-access profile fields to and from YAML.
  /// \param Io YAML input/output state.
  /// \param Data Data-access profile container being mapped.
  static void mapping(IO &Io, memprof::YamlDataAccessProfData &Data) {
    Io.mapOptional("SampledRecords", Data.Records);
    Io.mapOptional("KnownColdSymbols", Data.KnownColdSymbols);
    Io.mapOptional("KnownColdStrHashes", Data.KnownColdStrHashes);
  }
};

/// YAMLIO mapping traits for \c memprof::AllMemProfData.
template <> struct MappingTraits<memprof::AllMemProfData> {
  /// Map top-level MemProf YAML fields to and from YAML.
  /// \param Io YAML input/output state.
  /// \param Data Top-level MemProf dataset being mapped.
  static void mapping(IO &Io, memprof::AllMemProfData &Data) {
    if (!Io.outputting() || !Data.HeapProfileRecords.empty())
      Io.mapOptional("HeapProfileRecords", Data.HeapProfileRecords);
    // Map data access profiles if reading input, or if writing output &&
    // the struct is populated.
    if (!Io.outputting() || !Data.YamlifiedDataAccessProfiles.isEmpty())
      Io.mapOptional("DataAccessProfiles", Data.YamlifiedDataAccessProfiles);
  }
};

/// YAMLIO sequence traits for \c SmallVector of \c memprof::GUIDHex64.
template <> struct SequenceTraits<SmallVector<memprof::GUIDHex64>> {
  /// Return the number of elements in \p Seq.
  /// \param Io Unused YAML IO object.
  /// \param Seq Sequence container.
  /// \returns Number of elements in \p Seq.
  static size_t size(IO &Io, SmallVector<memprof::GUIDHex64> &Seq) {
    (void)Io;
    return Seq.size();
  }
  /// Return a reference to element \p Index of \p Seq, growing it if needed.
  /// \param Io Unused YAML IO object.
  /// \param Seq Sequence container.
  /// \param Index Zero-based element index.
  /// \returns Reference to the element at \p Index.
  static memprof::GUIDHex64 &
  element(IO &Io, SmallVector<memprof::GUIDHex64> &Seq, size_t Index) {
    (void)Io;
    if (Index >= Seq.size())
      Seq.resize(Index + 1);
    return Seq[Index];
  }
  /// When true, emit this sequence in flow style.
  static const bool flow = true;
};

/// Sequences of MemProf frames use block formatting.
template <> struct SequenceElementTraits<memprof::Frame> {
  /// Emit sequences of MemProf frames in block style.
  static const bool flow = false;
};

/// Sequences of frame vectors use block formatting.
template <> struct SequenceElementTraits<std::vector<memprof::Frame>> {
  /// Emit sequences of frame vectors in block style.
  static const bool flow = false;
};

/// Sequences of allocation infos use block formatting.
template <> struct SequenceElementTraits<memprof::AllocationInfo> {
  /// Emit sequences of allocation infos in block style.
  static const bool flow = false;
};

/// Sequences of call-site infos use block formatting.
template <> struct SequenceElementTraits<memprof::CallSiteInfo> {
  /// Emit sequences of call-site infos in block style.
  static const bool flow = false;
};

/// Sequences of GUID/record pairs use block formatting.
template <> struct SequenceElementTraits<memprof::GUIDMemProfRecordPair> {
  /// Emit sequences of GUID/record pairs in block style.
  static const bool flow = false;
};

/// Sequences of GUIDHex64 values use block formatting.
///
/// Used for CalleeGuids.
template <> struct SequenceElementTraits<memprof::GUIDHex64> {
  /// Emit sequences of GUIDHex64 values in block style.
  static const bool flow = false;
};

/// Sequences of data-access profile records use block formatting.
template <> struct SequenceElementTraits<memprof::DataAccessProfRecord> {
  /// Emit sequences of data-access profile records in block style.
  static const bool flow = false;
};

/// Sequences of source locations use block formatting.
template <> struct SequenceElementTraits<memprof::SourceLocation> {
  /// Emit sequences of source locations in block style.
  static const bool flow = false;
};

} // namespace yaml
} // namespace llvm

#endif // LLVM_PROFILEDATA_MEMPROFYAML_H_
