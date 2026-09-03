//===- MemProf.h - MemProf support ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains common definitions used in the reading and writing of
// memory profile data.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_MEMPROF_H
#define LLVM_PROFILEDATA_MEMPROF_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/ProfileData/MemProfData.inc"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"

#include <bitset>
#include <cstdint>

namespace llvm {
namespace yaml {
template <typename T> struct CustomMappingTraits;
} // namespace yaml

namespace memprof {

struct MemProfRecord;

/// Versions of the indexed MemProf profile format.
enum IndexedVersion : uint64_t {
  /// Version 2: added a call stack table.
  Version2 = 2,
  /// Version 3: added a radix tree for call stacks and linear IDs for frames
  /// and call stacks.
  Version3 = 3,
  /// Version 4: added CalleeGuids to call site info.
  Version4 = 4,
};

/// Oldest indexed MemProf format version supported by this reader/writer.
constexpr uint64_t MinimumSupportedVersion = Version2;
/// Newest indexed MemProf format version supported by this reader/writer.
constexpr uint64_t MaximumSupportedVersion = Version4;

// Verify that the minimum and maximum satisfy the obvious constraint.
static_assert(MinimumSupportedVersion <= MaximumSupportedVersion);

/// Return the Darwin linkage name of the memprof default-options string.
/// @return Darwin linkage name for the default-options string symbol.
inline llvm::StringRef getMemprofOptionsSymbolDarwinLinkageName() {
  return "___memprof_default_options_str";
}

/// Return the IR-level name of the memprof default-options string symbol.
///
/// Darwin linkage names are prefixed with an extra "_". See
/// DataLayout::getGlobalPrefix().
/// @return IR-level symbol name without the Darwin global prefix.
inline llvm::StringRef getMemprofOptionsSymbolName() {
  return getMemprofOptionsSymbolDarwinLinkageName().drop_front();
}

/// Identifiers for fields stored in a PortableMemInfoBlock schema.
enum class Meta : uint64_t {
  /// Sentinel marking the first schema field identifier.
  Start = 0,
  /// Declare one MemInfoBlock schema field as a Meta enumerator.
  /// @param NameTag Enumerator name and optional initializer value.
  /// @param Name Field name matching the MemInfoBlock member.
  /// @param Type C++ type of the field.
#define MIBEntryDef(NameTag, Name, Type) NameTag,
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
  /// One past the last schema field identifier; also the schema bitset size.
  Size
};

/// Ordered list of Meta field identifiers describing a MemInfoBlock schema.
using MemProfSchema = llvm::SmallVector<Meta, static_cast<int>(Meta::Size)>;

/// Return the full schema of MemInfoBlock fields currently in use.
/// @return Ordered list of all MemInfoBlock Meta field identifiers.
LLVM_ABI MemProfSchema getFullSchema();

/// Return the schema of fields used for hot/cold memory hinting.
/// @return Ordered list of Meta field identifiers for hot/cold hinting.
LLVM_ABI MemProfSchema getHotColdSchema();

/// Portable MemInfoBlock whose fields may be partially serialized by schema.
///
/// Contents may be read or written partially by providing an appropriate schema
/// to the serialize and deserialize methods.
struct PortableMemInfoBlock {
  /// Construct an empty block with no schema fields set.
  PortableMemInfoBlock() = default;
  /// Construct from a runtime MemInfoBlock using only fields in \p IncomingSchema.
  /// @param Block Source runtime meminfo block.
  /// @param IncomingSchema Fields to copy from \p Block.
  explicit PortableMemInfoBlock(const MemInfoBlock &Block,
                                const MemProfSchema &IncomingSchema) {
    for (const Meta Id : IncomingSchema)
      Schema.set(llvm::to_underlying(Id));
#define MIBEntryDef(NameTag, Name, Type) Name = Block.Name;
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
  }

  /// Construct by deserializing \p Schema fields from little-endian bytes at \p Ptr.
  /// @param Schema Fields to read from \p Ptr.
  /// @param Ptr Little-endian serialized field payload.
  PortableMemInfoBlock(const MemProfSchema &Schema, const unsigned char *Ptr) {
    deserialize(Schema, Ptr);
  }

  /// Populate this block by reading \p IncomingSchema fields from \p Ptr.
  /// @param IncomingSchema Fields to read and record in the schema bitset.
  /// @param Ptr Little-endian serialized field payload.
  void deserialize(const MemProfSchema &IncomingSchema,
                   const unsigned char *Ptr) {
    using namespace support;

    Schema.reset();
    for (const Meta Id : IncomingSchema) {
      switch (Id) {
#define MIBEntryDef(NameTag, Name, Type)                                       \
  case Meta::Name: {                                                           \
    Name = endian::readNext<Type, llvm::endianness::little>(Ptr);              \
  } break;
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
      default:
        llvm_unreachable("Unknown meta type id, is the profile collected from "
                         "a newer version of the runtime?");
      }

      Schema.set(llvm::to_underlying(Id));
    }
  }

  /// Write the fields selected by \p Schema to little-endian stream \p OS.
  /// @param Schema Fields to emit.
  /// @param OS Destination stream.
  void serialize(const MemProfSchema &Schema, raw_ostream &OS) const {
    using namespace support;

    endian::Writer LE(OS, llvm::endianness::little);
    for (const Meta Id : Schema) {
      switch (Id) {
#define MIBEntryDef(NameTag, Name, Type)                                       \
  case Meta::Name: {                                                           \
    LE.write<Type>(Name);                                                      \
  } break;
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
      default:
        llvm_unreachable("Unknown meta type id, invalid input?");
      }
    }
  }

  /// Print the MemInfoBlock fields in YAML to \p OS.
  /// @param OS Destination stream.
  void printYAML(raw_ostream &OS) const {
    OS << "      MemInfoBlock:\n";
#define MIBEntryDef(NameTag, Name, Type)                                       \
  OS << "        " << #Name << ": " << Name << "\n";
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
    if (AccessHistogramSize > 0) {
      OS << "        " << "AccessHistogramValues" << ":";
      for (uint32_t I = 0; I < AccessHistogramSize; ++I) {
        OS << " " << ((uint64_t *)AccessHistogram)[I];
      }
      OS << "\n";
    }
  }

  /// Return the schema bitset; intended for unit tests.
  /// @return Bitset of Meta fields present in this block.
  std::bitset<llvm::to_underlying(Meta::Size)> getSchema() const {
    return Schema;
  }

  // Define getters for each type which can be called by analyses.
#define MIBEntryDef(NameTag, Name, Type)                                       \
  Type get##Name() const {                                                     \
    assert(Schema[llvm::to_underlying(Meta::Name)]);                           \
    return Name;                                                               \
  }
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef

  // Define setters for each type which can be called by the writer.
#define MIBEntryDef(NameTag, Name, Type)                                       \
  void set##Name(Type NewVal) {                                                \
    assert(Schema[llvm::to_underlying(Meta::Name)]);                           \
    Name = NewVal;                                                             \
  }
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef

  /// Reset this block to an empty default-constructed state.
  void clear() { *this = PortableMemInfoBlock(); }

  /// Return true if \p Other has the same schema and field values.
  /// @param Other Block to compare against.
  /// @return True if schema and all present field values match.
  bool operator==(const PortableMemInfoBlock &Other) const {
    if (Other.Schema != Schema)
      return false;

#define MIBEntryDef(NameTag, Name, Type)                                       \
  if (Schema[llvm::to_underlying(Meta::Name)] &&                               \
      Other.get##Name() != get##Name())                                        \
    return false;
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
    return true;
  }

  /// Return true if \p Other differs in schema or any field value.
  /// @param Other Block to compare against.
  /// @return True if schema or any field value differs.
  bool operator!=(const PortableMemInfoBlock &Other) const {
    return !operator==(Other);
  }

  /// Return the serialized size in bytes of the fields in \p Schema.
  /// @param Schema Fields whose sizes are summed.
  /// @return Total byte size of the selected fields.
  static size_t serializedSize(const MemProfSchema &Schema) {
    size_t Result = 0;

    for (const Meta Id : Schema) {
      switch (Id) {
#define MIBEntryDef(NameTag, Name, Type)                                       \
  case Meta::Name: {                                                           \
    Result += sizeof(Type);                                                    \
  } break;
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
      default:
        llvm_unreachable("Unknown meta type id, invalid input?");
      }
    }

    return Result;
  }

  // Give YAML access to the individual MIB fields.
  friend struct yaml::CustomMappingTraits<memprof::PortableMemInfoBlock>;

private:
  // The set of available fields, indexed by Meta::Name.
  std::bitset<llvm::to_underlying(Meta::Size)> Schema;

#define MIBEntryDef(NameTag, Name, Type) Type Name = Type();
#include "llvm/ProfileData/MIBEntryDef.inc"
#undef MIBEntryDef
};

/// Hash identifier for a unique call Frame.
using FrameId = uint64_t;
/// Dense linear index into the frame array used by newer indexed formats.
using LinearFrameId = uint32_t;
/// Call frame for a dynamic allocation context, typically from a stack depot.
///
/// The contents of the frame are populated by symbolizing the stack depot call
/// frame from the compiler runtime.
struct Frame {
  /// Function GUID from the lower 64 bits of llvm::md5(FunctionName).
  GlobalValue::GUID Function = 0;
  /// Optional symbol name; populated by the reader on request and not serialized.
  std::unique_ptr<std::string> SymbolName;
  /// Source line offset of the call from the beginning of the parent function.
  uint32_t LineOffset = 0;
  /// Source column of the call, to distinguish multiple calls on one line.
  uint32_t Column = 0;
  /// Whether this frame is an inlined call site.
  bool IsInlineFrame = false;

  /// Construct an empty frame with default field values.
  Frame() = default;
  /// Move-construct a frame, transferring ownership of SymbolName.
  /// @param Other Source frame to move from.
  Frame(Frame &&Other) = default;
  /// Move-assign a frame, transferring ownership of SymbolName.
  /// @param Other Source frame to move from.
  /// @return Reference to this frame after the move assignment.
  Frame &operator=(Frame &&Other) = default;

  /// Copy-construct a frame, deep-copying SymbolName when present.
  /// @param Other Source frame to copy.
  Frame(const Frame &Other) {
    Function = Other.Function;
    SymbolName = Other.SymbolName
                     ? std::make_unique<std::string>(*Other.SymbolName)
                     : nullptr;
    LineOffset = Other.LineOffset;
    Column = Other.Column;
    IsInlineFrame = Other.IsInlineFrame;
  }

  /// Construct a frame from function hash, line offset, column, and inline flag.
  /// @param Hash Function GUID.
  /// @param Off Line offset from the parent function start.
  /// @param Col Source column.
  /// @param Inline Whether the frame is inlined.
  Frame(GlobalValue::GUID Hash, uint32_t Off, uint32_t Col, bool Inline)
      : Function(Hash), LineOffset(Off), Column(Col), IsInlineFrame(Inline) {}

  /// Return true if \p Other matches this frame, ignoring SymbolName.
  ///
  /// Comparing the function hash serves the same purpose as comparing names.
  /// @param Other Frame to compare against.
  /// @return True if Function, LineOffset, Column, and IsInlineFrame match.
  bool operator==(const Frame &Other) const {
    return Other.Function == Function && Other.LineOffset == LineOffset &&
           Other.Column == Column && Other.IsInlineFrame == IsInlineFrame;
  }

  /// Copy-assign a frame, deep-copying SymbolName when present.
  /// @param Other Source frame to copy.
  /// @return Reference to this frame after the copy assignment.
  Frame &operator=(const Frame &Other) {
    Function = Other.Function;
    SymbolName = Other.SymbolName
                     ? std::make_unique<std::string>(*Other.SymbolName)
                     : nullptr;
    LineOffset = Other.LineOffset;
    Column = Other.Column;
    IsInlineFrame = Other.IsInlineFrame;
    return *this;
  }

  /// Return true if \p Other does not match this frame.
  /// @param Other Frame to compare against.
  /// @return True if the frames differ (ignoring SymbolName).
  bool operator!=(const Frame &Other) const { return !operator==(Other); }

  /// Return true if a symbol name has been attached to this frame.
  /// @return True when SymbolName is present.
  bool hasSymbolName() const { return !!SymbolName; }

  /// Return the attached symbol name; requires hasSymbolName().
  /// @return Attached symbol name string.
  StringRef getSymbolName() const {
    assert(hasSymbolName());
    return *SymbolName;
  }

  /// Return the symbol name, or \p Alt if none is attached.
  /// @param Alt Fallback string when SymbolName is absent.
  /// @return Symbol name, or \p Alt when none is attached.
  std::string getSymbolNameOr(StringRef Alt) const {
    return std::string(hasSymbolName() ? getSymbolName() : Alt);
  }

  /// Write the serializable frame fields to little-endian stream \p OS.
  /// @param OS Destination stream.
  void serialize(raw_ostream &OS) const {
    using namespace support;

    endian::Writer LE(OS, llvm::endianness::little);

    // If the type of the GlobalValue::GUID changes, then we need to update
    // the reader and the writer.
    static_assert(std::is_same<GlobalValue::GUID, uint64_t>::value,
                  "Expect GUID to be uint64_t.");
    LE.write<uint64_t>(Function);

    LE.write<uint32_t>(LineOffset);
    LE.write<uint32_t>(Column);
    LE.write<bool>(IsInlineFrame);
  }

  /// Deserialize a frame from little-endian bytes at \p Ptr.
  /// @param Ptr Little-endian serialized frame payload.
  /// @return Frame reconstructed from the serialized payload.
  static Frame deserialize(const unsigned char *Ptr) {
    using namespace support;

    const uint64_t F =
        endian::readNext<uint64_t, llvm::endianness::little>(Ptr);
    const uint32_t L =
        endian::readNext<uint32_t, llvm::endianness::little>(Ptr);
    const uint32_t C =
        endian::readNext<uint32_t, llvm::endianness::little>(Ptr);
    const bool I = endian::readNext<bool, llvm::endianness::little>(Ptr);
    return Frame(/*Function=*/F, /*LineOffset=*/L, /*Column=*/C,
                 /*IsInlineFrame=*/I);
  }

  /// Return the serialized size in bytes of a frame.
  /// @return Byte size of the serializable frame fields.
  static constexpr size_t serializedSize() {
    return sizeof(Frame::Function) + sizeof(Frame::LineOffset) +
           sizeof(Frame::Column) + sizeof(Frame::IsInlineFrame);
  }

  /// Print this frame in YAML to \p OS.
  /// @param OS Destination stream.
  void printYAML(raw_ostream &OS) const {
    OS << "      -\n"
       << "        Function: " << Function << "\n"
       << "        SymbolName: " << getSymbolNameOr("<None>") << "\n"
       << "        LineOffset: " << LineOffset << "\n"
       << "        Column: " << Column << "\n"
       << "        Inline: " << IsInlineFrame << "\n";
  }
};

/// Identifier indexing into the table of call stacks.
using CallStackId = uint64_t;

/// Dense linear index into the call stack array used by newer indexed formats.
using LinearCallStackId = uint32_t;

/// Call site information that stores frames by CallStackId.
struct IndexedCallSiteInfo {
  /// Call stack ID for this call site.
  CallStackId CSId = 0;
  /// GUIDs of callees observed at this call site.
  SmallVector<GlobalValue::GUID, 1> CalleeGuids;

  /// Construct an empty call site info.
  IndexedCallSiteInfo() = default;
  /// Construct call site info with call stack \p CSId and no callees.
  /// @param CSId Call stack identifier.
  IndexedCallSiteInfo(CallStackId CSId) : CSId(CSId) {}
  /// Construct call site info with call stack \p CSId and \p CalleeGuids.
  /// @param CSId Call stack identifier.
  /// @param CalleeGuids Callee function GUIDs at this site.
  IndexedCallSiteInfo(CallStackId CSId,
                      SmallVector<GlobalValue::GUID, 1> CalleeGuids)
      : CSId(CSId), CalleeGuids(std::move(CalleeGuids)) {}

  /// Return true if \p Other has the same CSId and CalleeGuids.
  /// @param Other Call site info to compare against.
  /// @return True if CSId and CalleeGuids match.
  bool operator==(const IndexedCallSiteInfo &Other) const {
    return CSId == Other.CSId && CalleeGuids == Other.CalleeGuids;
  }

  /// Return true if \p Other differs in CSId or CalleeGuids.
  /// @param Other Call site info to compare against.
  /// @return True if CSId or CalleeGuids differ.
  bool operator!=(const IndexedCallSiteInfo &Other) const {
    return !operator==(Other);
  }
};

/// Allocation site that stores its call stack as a CallStackId.
///
/// Frames are represented using unique identifiers for space efficiency.
struct IndexedAllocationInfo {
  /// Dynamic allocation context in bottom-up (leaf-to-root) order, by ID.
  CallStackId CSId = 0;
  /// Runtime statistics recorded for this allocation.
  PortableMemInfoBlock Info;

  /// Construct an empty indexed allocation info.
  IndexedAllocationInfo() = default;
  /// Construct from call stack ID and a runtime MemInfoBlock.
  /// @param CSId Call stack identifier.
  /// @param MB Runtime meminfo block to wrap.
  /// @param Schema Fields of \p MB to retain; defaults to the full schema.
  IndexedAllocationInfo(CallStackId CSId, const MemInfoBlock &MB,
                        const MemProfSchema &Schema = getFullSchema())
      : CSId(CSId), Info(MB, Schema) {}
  /// Construct from call stack ID and an existing portable meminfo block.
  /// @param CSId Call stack identifier.
  /// @param MB Portable meminfo block to store.
  IndexedAllocationInfo(CallStackId CSId, const PortableMemInfoBlock &MB)
      : CSId(CSId), Info(MB) {}

  /// Return the serialized size in bytes for \p Schema and \p Version.
  /// @param Schema Fields included in the serialized payload.
  /// @param Version Indexed format version controlling layout.
  /// @return Byte size of this allocation info in the given layout.
  LLVM_ABI size_t serializedSize(const MemProfSchema &Schema,
                                 IndexedVersion Version) const;

  /// Return true if \p Other has the same Info and CSId.
  /// @param Other Allocation info to compare against.
  /// @return True if Info and CSId match.
  bool operator==(const IndexedAllocationInfo &Other) const {
    if (Other.Info != Info)
      return false;

    if (Other.CSId != CSId)
      return false;
    return true;
  }

  /// Return true if \p Other differs in Info or CSId.
  /// @param Other Allocation info to compare against.
  /// @return True if Info or CSId differ.
  bool operator!=(const IndexedAllocationInfo &Other) const {
    return !operator==(Other);
  }
};

/// Allocation site with inline frame contents for temporary in-memory use.
struct AllocationInfo {
  /// Dynamic allocation context with frame contents stored inline.
  std::vector<Frame> CallStack;
  /// Runtime statistics recorded for this allocation.
  PortableMemInfoBlock Info;

  /// Construct an empty allocation info.
  AllocationInfo() = default;

  /// Print this allocation site in YAML to \p OS.
  /// @param OS Destination stream.
  void printYAML(raw_ostream &OS) const {
    OS << "    -\n";
    OS << "      Callstack:\n";
    // TODO: Print out the frame on one line with to make it easier for deep
    // callstacks once we have a test to check valid YAML is generated.
    for (const Frame &F : CallStack) {
      F.printYAML(OS);
    }
    Info.printYAML(OS);
  }
};

/// MemProf profile for one function, storing frames by identifier.
///
/// This representation should be used in profile conversion and manipulation
/// tools.
struct IndexedMemProfRecord {
  /// Allocation sites in this function that have memory profiling data.
  llvm::SmallVector<IndexedAllocationInfo> AllocSites;
  /// Call sites in this function that participate in some allocation context.
  ///
  /// Each entry lists inline locations in bottom-up order (leaf to root). The
  /// inline location list may include additional entries; users should pick the
  /// last entry with the same function GUID.
  llvm::SmallVector<IndexedCallSiteInfo> CallSites;

  /// Clear allocation and call-site data, resetting to a default record.
  void clear() { *this = IndexedMemProfRecord(); }

  /// Append allocation sites from \p Other into this record.
  /// @param Other Record whose AllocSites are appended.
  void merge(const IndexedMemProfRecord &Other) {
    // TODO: Filter out duplicates which may occur if multiple memprof
    // profiles are merged together using llvm-profdata.
    AllocSites.append(Other.AllocSites);
  }

  /// Return the serialized size in bytes for \p Schema and \p Version.
  /// @param Schema Fields included in each allocation's meminfo payload.
  /// @param Version Indexed format version controlling layout.
  /// @return Byte size of this record in the given layout.
  LLVM_ABI size_t serializedSize(const MemProfSchema &Schema,
                                 IndexedVersion Version) const;

  /// Return true if \p Other has the same AllocSites and CallSites.
  /// @param Other Record to compare against.
  /// @return True if AllocSites and CallSites match.
  bool operator==(const IndexedMemProfRecord &Other) const {
    if (Other.AllocSites != AllocSites)
      return false;

    if (Other.CallSites != CallSites)
      return false;
    return true;
  }

  /// Serialize this record to \p OS using \p Schema and \p Version.
  /// @param Schema Fields included in each allocation's meminfo payload.
  /// @param OS Destination stream.
  /// @param Version Indexed format version controlling layout.
  /// @param MemProfCallStackIndexes Optional map from CallStackId to linear IDs.
  LLVM_ABI void serialize(const MemProfSchema &Schema, raw_ostream &OS,
                          IndexedVersion Version,
                          llvm::DenseMap<CallStackId, LinearCallStackId>
                              *MemProfCallStackIndexes = nullptr) const;

  /// Deserialize a record from \p Buffer using \p Schema and \p Version.
  /// @param Schema Fields expected in each allocation's meminfo payload.
  /// @param Buffer Little-endian serialized record payload.
  /// @param Version Indexed format version controlling layout.
  /// @return Record reconstructed from the serialized payload.
  LLVM_ABI static IndexedMemProfRecord deserialize(const MemProfSchema &Schema,
                                                   const unsigned char *Buffer,
                                                   IndexedVersion Version);

  /// Convert to a MemProfRecord by expanding call stacks via \p Callback.
  /// @param Callback Maps each CallStackId to an inline vector of Frames.
  /// @return In-memory record with expanded frame contents.
  LLVM_ABI MemProfRecord toMemProfRecord(
      llvm::function_ref<std::vector<Frame>(const CallStackId)> Callback) const;
};

/// Return the memprof GUID for \p FunctionName after LTO suffix canonicalization.
///
/// For memprof, any .llvm suffix added by LTO is removed. MemProfRecords are
/// mapped to functions using this GUID.
/// @param FunctionName Possibly LTO-suffixed function name.
/// @return Function GUID used to map MemProfRecords.
LLVM_ABI GlobalValue::GUID getGUID(const StringRef FunctionName);

/// Call site information with frame contents stored inline.
struct CallSiteInfo {
  /// Frames in the call stack for this call site.
  std::vector<Frame> Frames;

  /// GUIDs of callees observed at this call site.
  SmallVector<GlobalValue::GUID, 1> CalleeGuids;

  /// Construct an empty call site info.
  CallSiteInfo() = default;
  /// Construct call site info with inline \p Frames and no callees.
  /// @param Frames Call stack frames, leaf to root.
  CallSiteInfo(std::vector<Frame> Frames) : Frames(std::move(Frames)) {}
  /// Construct call site info with inline \p Frames and \p CalleeGuids.
  /// @param Frames Call stack frames, leaf to root.
  /// @param CalleeGuids Callee function GUIDs at this site.
  CallSiteInfo(std::vector<Frame> Frames,
               SmallVector<GlobalValue::GUID, 1> CalleeGuids)
      : Frames(std::move(Frames)), CalleeGuids(std::move(CalleeGuids)) {}

  /// Return true if \p Other has the same Frames and CalleeGuids.
  /// @param Other Call site info to compare against.
  /// @return True if Frames and CalleeGuids match.
  bool operator==(const CallSiteInfo &Other) const {
    return Frames == Other.Frames && CalleeGuids == Other.CalleeGuids;
  }

  /// Return true if \p Other differs in Frames or CalleeGuids.
  /// @param Other Call site info to compare against.
  /// @return True if Frames or CalleeGuids differ.
  bool operator!=(const CallSiteInfo &Other) const {
    return !operator==(Other);
  }
};

/// MemProf profile for one function with frame contents stored inline.
///
/// Prefer this representation for small temporary in-memory instances.
struct MemProfRecord {
  /// Allocation sites in this function with inline frame contents.
  llvm::SmallVector<AllocationInfo> AllocSites;
  /// Call sites in this function with inline frame contents.
  llvm::SmallVector<CallSiteInfo> CallSites;

  /// Construct an empty memprof record.
  MemProfRecord() = default;

  /// Print this record in YAML to \p OS.
  /// @param OS Destination stream.
  void print(llvm::raw_ostream &OS) const {
    if (!AllocSites.empty()) {
      OS << "    AllocSites:\n";
      for (const AllocationInfo &N : AllocSites)
        N.printYAML(OS);
    }

    if (!CallSites.empty()) {
      OS << "    CallSites:\n";
      for (const CallSiteInfo &CS : CallSites) {
        for (const Frame &F : CS.Frames) {
          OS << "    -\n";
          F.printYAML(OS);
        }
      }
    }
  }
};

/// Read a MemProfSchema from \p Buffer, advancing it past the schema bytes.
///
/// All entries in the buffer are interpreted as uint64_t. The first entry
/// denotes the number of ids in the schema. Subsequent entries are integers
/// which map to memprof::Meta enum class entries. After successfully reading
/// the schema, the pointer is one byte past the schema contents.
/// @param Buffer Pointer to the schema payload; advanced on success.
/// @return Parsed schema, or an error if the payload is invalid.
LLVM_ABI Expected<MemProfSchema>
readMemProfSchema(const unsigned char *&Buffer);

/// OnDiskChainedHashTable lookup trait for IndexedMemProfRecord entries.
class RecordLookupTrait {
public:
  /// Record type returned by ReadData.
  using data_type = const IndexedMemProfRecord &;
  /// Key type stored in the hash table.
  using internal_key_type = uint64_t;
  /// Key type supplied by lookup clients.
  using external_key_type = uint64_t;
  /// Hash value type for table keys.
  using hash_value_type = uint64_t;
  /// Byte offset / length type used by the on-disk format.
  using offset_type = uint64_t;

  /// Deleted; a version and schema are required to deserialize records.
  RecordLookupTrait() = delete;
  /// Construct a lookup trait for indexed format \p V using schema \p S.
  /// @param V Indexed MemProf format version.
  /// @param S Schema used when deserializing records.
  RecordLookupTrait(IndexedVersion V, const MemProfSchema &S)
      : Version(V), Schema(S) {}

  /// Return true if keys \p A and \p B are equal.
  /// @param A First key.
  /// @param B Second key.
  /// @return True if the keys are equal.
  static bool EqualKey(uint64_t A, uint64_t B) { return A == B; }
  /// Return the internal key corresponding to external key \p K.
  /// @param K External lookup key.
  /// @return Internal key for \p K.
  static uint64_t GetInternalKey(uint64_t K) { return K; }
  /// Return the external key corresponding to internal key \p K.
  /// @param K Internal table key.
  /// @return External key for \p K.
  static uint64_t GetExternalKey(uint64_t K) { return K; }

  /// Return the hash of key \p K (the key itself for GUID keys).
  /// @param K Key to hash.
  /// @return Hash value for \p K.
  hash_value_type ComputeHash(uint64_t K) { return K; }

  /// Read key and data byte lengths from little-endian bytes at \p D.
  /// @param D Pointer advanced past the length fields.
  /// @return Pair of key length and data length in bytes.
  static std::pair<offset_type, offset_type>
  ReadKeyDataLength(const unsigned char *&D) {
    using namespace support;

    offset_type KeyLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    offset_type DataLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    return std::make_pair(KeyLen, DataLen);
  }

  /// Read the lookup key from little-endian bytes at \p D.
  /// @param D Key payload.
  /// @param Unused Key length from the on-disk format; unused for fixed keys.
  /// @return Deserialized external key.
  uint64_t ReadKey(const unsigned char *D, offset_type Unused) {
    using namespace support;
    return endian::readNext<external_key_type, llvm::endianness::little>(D);
  }

  /// Deserialize the record for key \p K from little-endian bytes at \p D.
  /// @param K Lookup key associated with the record.
  /// @param D Record payload.
  /// @param Unused Data length from the on-disk format; unused here.
  /// @return Reference to the deserialized IndexedMemProfRecord.
  data_type ReadData(uint64_t K, const unsigned char *D, offset_type Unused) {
    Record = IndexedMemProfRecord::deserialize(Schema, D, Version);
    return Record;
  }

private:
  // Holds the MemProf version.
  IndexedVersion Version;
  // Holds the memprof schema used to deserialize records.
  MemProfSchema Schema;
  // Holds the records from one function deserialized from the indexed format.
  IndexedMemProfRecord Record;
};

/// OnDiskChainedHashTable writer trait for IndexedMemProfRecord entries.
class RecordWriterTrait {
public:
  /// Key type stored in the hash table (typically a function GUID).
  using key_type = uint64_t;
  /// Reference type passed for keys during emission.
  using key_type_ref = uint64_t;

  /// Record type stored as the table value.
  using data_type = IndexedMemProfRecord;
  /// Mutable reference to a record during emission.
  using data_type_ref = IndexedMemProfRecord &;

  /// Hash value type for table keys.
  using hash_value_type = uint64_t;
  /// Byte offset / length type used by the on-disk format.
  using offset_type = uint64_t;

private:
  // Pointer to the memprof schema to use for the generator.
  const MemProfSchema *Schema;
  // The MemProf version to use for the serialization.
  IndexedVersion Version;

  // Mappings from CallStackId to the indexes into the call stack array.
  llvm::DenseMap<CallStackId, LinearCallStackId> *MemProfCallStackIndexes;

public:
  /// Deleted; Version and Schema must be provided.
  RecordWriterTrait() = delete;
  /// Construct a writer trait with schema, version, and optional CS index map.
  /// @param Schema Schema used when serializing records.
  /// @param V Indexed MemProf format version.
  /// @param MemProfCallStackIndexes Optional CallStackId to linear-ID map.
  RecordWriterTrait(
      const MemProfSchema *Schema, IndexedVersion V,
      llvm::DenseMap<CallStackId, LinearCallStackId> *MemProfCallStackIndexes)
      : Schema(Schema), Version(V),
        MemProfCallStackIndexes(MemProfCallStackIndexes) {}

  /// Return the hash of key \p K (the key itself for GUID keys).
  /// @param K Key to hash.
  /// @return Hash value for \p K.
  static hash_value_type ComputeHash(key_type_ref K) { return K; }

  /// Emit key and data lengths for \p K / \p V and return those lengths.
  /// @param Out Destination stream.
  /// @param K Key whose size is emitted.
  /// @param V Record whose serialized size is emitted.
  /// @return Pair of key length and data length in bytes.
  std::pair<offset_type, offset_type>
  EmitKeyDataLength(raw_ostream &Out, key_type_ref K, data_type_ref V) {
    using namespace support;

    endian::Writer LE(Out, llvm::endianness::little);
    offset_type N = sizeof(K);
    LE.write<offset_type>(N);
    offset_type M = V.serializedSize(*Schema, Version);
    LE.write<offset_type>(M);
    return std::make_pair(N, M);
  }

  /// Emit key \p K to \p Out.
  /// @param Out Destination stream.
  /// @param K Key to emit.
  /// @param Unused Key length from EmitKeyDataLength; unused for fixed keys.
  void EmitKey(raw_ostream &Out, key_type_ref K, offset_type Unused) {
    using namespace support;
    endian::Writer LE(Out, llvm::endianness::little);
    LE.write<uint64_t>(K);
  }

  /// Emit record \p V to \p Out, then clear \p V to free retained vectors.
  /// @param Out Destination stream.
  /// @param UnusedKey Key associated with \p V; unused during emission.
  /// @param V Record to serialize and then clear.
  /// @param UnusedLen Data length from EmitKeyDataLength; unused here.
  void EmitData(raw_ostream &Out, key_type_ref UnusedKey, data_type_ref V,
                offset_type UnusedLen) {
    assert(Schema != nullptr && "MemProf schema is not initialized!");
    V.serialize(*Schema, Out, Version, MemProfCallStackIndexes);
    // Clear the IndexedMemProfRecord which results in clearing/freeing its
    // vectors of allocs and callsites. This is owned by the associated on-disk
    // hash table, but unused after this point. See also the comment added to
    // the client which constructs the on-disk hash table for this trait.
    V.clear();
  }
};

/// OnDiskChainedHashTable writer trait for FrameId to Frame mappings.
class FrameWriterTrait {
public:
  /// Key type identifying a unique frame.
  using key_type = FrameId;
  /// Reference type passed for frame keys during emission.
  using key_type_ref = FrameId;

  /// Frame value stored in the table.
  using data_type = Frame;
  /// Mutable reference to a frame during emission.
  using data_type_ref = Frame &;

  /// Hash value type for frame keys.
  using hash_value_type = FrameId;
  /// Byte offset / length type used by the on-disk format.
  using offset_type = uint64_t;

  /// Return the hash of key \p K (the key itself).
  /// @param K Frame ID to hash.
  /// @return Hash value for \p K.
  static hash_value_type ComputeHash(key_type_ref K) { return K; }

  /// Emit key and data lengths for \p K / \p V and return those lengths.
  /// @param Out Destination stream.
  /// @param K Frame ID whose size is emitted.
  /// @param V Frame whose serialized size is emitted.
  /// @return Pair of key length and data length in bytes.
  static std::pair<offset_type, offset_type>
  EmitKeyDataLength(raw_ostream &Out, key_type_ref K, data_type_ref V) {
    using namespace support;
    endian::Writer LE(Out, llvm::endianness::little);
    offset_type N = sizeof(K);
    LE.write<offset_type>(N);
    offset_type M = V.serializedSize();
    LE.write<offset_type>(M);
    return std::make_pair(N, M);
  }

  /// Emit frame key \p K to \p Out.
  /// @param Out Destination stream.
  /// @param K Frame ID to emit.
  /// @param Unused Key length from EmitKeyDataLength; unused for fixed keys.
  void EmitKey(raw_ostream &Out, key_type_ref K, offset_type Unused) {
    using namespace support;
    endian::Writer LE(Out, llvm::endianness::little);
    LE.write<key_type>(K);
  }

  /// Emit frame \p V to \p Out.
  /// @param Out Destination stream.
  /// @param UnusedKey Key associated with \p V; unused during emission.
  /// @param V Frame to serialize.
  /// @param UnusedLen Data length from EmitKeyDataLength; unused here.
  void EmitData(raw_ostream &Out, key_type_ref UnusedKey, data_type_ref V,
                offset_type UnusedLen) {
    V.serialize(Out);
  }
};

/// OnDiskChainedHashTable lookup trait for FrameId to Frame mappings.
class FrameLookupTrait {
public:
  /// Frame type returned by ReadData.
  using data_type = const Frame;
  /// Key type stored in the hash table.
  using internal_key_type = FrameId;
  /// Key type supplied by lookup clients.
  using external_key_type = FrameId;
  /// Hash value type for frame keys.
  using hash_value_type = FrameId;
  /// Byte offset / length type used by the on-disk format.
  using offset_type = uint64_t;

  /// Return true if keys \p A and \p B are equal.
  /// @param A First frame ID.
  /// @param B Second frame ID.
  /// @return True if the keys are equal.
  static bool EqualKey(internal_key_type A, internal_key_type B) {
    return A == B;
  }
  /// Return the internal key corresponding to \p K.
  /// @param K External frame ID.
  /// @return Internal key for \p K.
  static uint64_t GetInternalKey(internal_key_type K) { return K; }
  /// Return the external key corresponding to \p K.
  /// @param K Internal frame ID.
  /// @return External key for \p K.
  static uint64_t GetExternalKey(external_key_type K) { return K; }

  /// Return the hash of key \p K (the key itself).
  /// @param K Frame ID to hash.
  /// @return Hash value for \p K.
  hash_value_type ComputeHash(internal_key_type K) { return K; }

  /// Read key and data byte lengths from little-endian bytes at \p D.
  /// @param D Pointer advanced past the length fields.
  /// @return Pair of key length and data length in bytes.
  static std::pair<offset_type, offset_type>
  ReadKeyDataLength(const unsigned char *&D) {
    using namespace support;

    offset_type KeyLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    offset_type DataLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    return std::make_pair(KeyLen, DataLen);
  }

  /// Read the frame key from little-endian bytes at \p D.
  /// @param D Key payload.
  /// @param Unused Key length from the on-disk format; unused for fixed keys.
  /// @return Deserialized frame ID key.
  uint64_t ReadKey(const unsigned char *D, offset_type Unused) {
    using namespace support;
    return endian::readNext<external_key_type, llvm::endianness::little>(D);
  }

  /// Deserialize the frame for key \p K from little-endian bytes at \p D.
  /// @param K Lookup key associated with the frame.
  /// @param D Frame payload.
  /// @param Unused Data length from the on-disk format; unused here.
  /// @return Frame reconstructed from the payload.
  data_type ReadData(uint64_t K, const unsigned char *D, offset_type Unused) {
    return Frame::deserialize(D);
  }
};

/// OnDiskChainedHashTable writer trait for CallStackId to FrameId sequences.
class CallStackWriterTrait {
public:
  /// Key type identifying a unique call stack.
  using key_type = CallStackId;
  /// Reference type passed for call stack keys during emission.
  using key_type_ref = CallStackId;

  /// Sequence of frame IDs forming a call stack.
  using data_type = llvm::SmallVector<FrameId>;
  /// Mutable reference to a frame-ID sequence during emission.
  using data_type_ref = llvm::SmallVector<FrameId> &;

  /// Hash value type for call stack keys.
  using hash_value_type = CallStackId;
  /// Byte offset / length type used by the on-disk format.
  using offset_type = uint64_t;

  /// Return the hash of key \p K (the key itself).
  /// @param K Call stack ID to hash.
  /// @return Hash value for \p K.
  static hash_value_type ComputeHash(key_type_ref K) { return K; }

  /// Emit the data length for \p V and return key/data lengths.
  ///
  /// The key length is constant and is not written explicitly.
  /// @param Out Destination stream.
  /// @param K Call stack ID whose size contributes the key length.
  /// @param V Frame-ID sequence whose byte size is emitted.
  /// @return Pair of key length and data length in bytes.
  static std::pair<offset_type, offset_type>
  EmitKeyDataLength(raw_ostream &Out, key_type_ref K, data_type_ref V) {
    using namespace support;
    endian::Writer LE(Out, llvm::endianness::little);
    // We do not explicitly emit the key length because it is a constant.
    offset_type N = sizeof(K);
    offset_type M = sizeof(FrameId) * V.size();
    LE.write<offset_type>(M);
    return std::make_pair(N, M);
  }

  /// Emit call stack key \p K to \p Out.
  /// @param Out Destination stream.
  /// @param K Call stack ID to emit.
  /// @param Unused Key length from EmitKeyDataLength; unused for fixed keys.
  void EmitKey(raw_ostream &Out, key_type_ref K, offset_type Unused) {
    using namespace support;
    endian::Writer LE(Out, llvm::endianness::little);
    LE.write<key_type>(K);
  }

  /// Emit the FrameId sequence \p V to \p Out.
  ///
  /// The vector length is not written; it is inferred from the data length.
  /// @param Out Destination stream.
  /// @param UnusedKey Key associated with \p V; unused during emission.
  /// @param V Frame-ID sequence to serialize.
  /// @param UnusedLen Data length from EmitKeyDataLength; unused here.
  void EmitData(raw_ostream &Out, key_type_ref UnusedKey, data_type_ref V,
                offset_type UnusedLen) {
    using namespace support;
    endian::Writer LE(Out, llvm::endianness::little);
    // Emit the frames.  We do not explicitly emit the length of the vector
    // because it can be inferred from the data length.
    for (FrameId F : V)
      LE.write<FrameId>(F);
  }
};

/// OnDiskChainedHashTable lookup trait for CallStackId to FrameId sequences.
class CallStackLookupTrait {
public:
  /// Frame-ID sequence type returned by ReadData.
  using data_type = const llvm::SmallVector<FrameId>;
  /// Key type stored in the hash table.
  using internal_key_type = CallStackId;
  /// Key type supplied by lookup clients.
  using external_key_type = CallStackId;
  /// Hash value type for call stack keys.
  using hash_value_type = CallStackId;
  /// Byte offset / length type used by the on-disk format.
  using offset_type = uint64_t;

  /// Return true if keys \p A and \p B are equal.
  /// @param A First call stack ID.
  /// @param B Second call stack ID.
  /// @return True if the keys are equal.
  static bool EqualKey(internal_key_type A, internal_key_type B) {
    return A == B;
  }
  /// Return the internal key corresponding to \p K.
  /// @param K External call stack ID.
  /// @return Internal key for \p K.
  static uint64_t GetInternalKey(internal_key_type K) { return K; }
  /// Return the external key corresponding to \p K.
  /// @param K Internal call stack ID.
  /// @return External key for \p K.
  static uint64_t GetExternalKey(external_key_type K) { return K; }

  /// Return the hash of key \p K (the key itself).
  /// @param K Call stack ID to hash.
  /// @return Hash value for \p K.
  hash_value_type ComputeHash(internal_key_type K) { return K; }

  /// Read the data length from \p D and return constant key length plus it.
  ///
  /// The key length is not stored on disk because it is fixed-size.
  /// @param D Pointer advanced past the data-length field.
  /// @return Pair of constant key length and data length in bytes.
  static std::pair<offset_type, offset_type>
  ReadKeyDataLength(const unsigned char *&D) {
    using namespace support;

    // We do not explicitly read the key length because it is a constant.
    offset_type KeyLen = sizeof(external_key_type);
    offset_type DataLen =
        endian::readNext<offset_type, llvm::endianness::little>(D);
    return std::make_pair(KeyLen, DataLen);
  }

  /// Read the call stack key from little-endian bytes at \p D.
  /// @param D Key payload.
  /// @param Unused Key length from the on-disk format; unused for fixed keys.
  /// @return Deserialized call stack ID key.
  uint64_t ReadKey(const unsigned char *D, offset_type Unused) {
    using namespace support;
    return endian::readNext<external_key_type, llvm::endianness::little>(D);
  }

  /// Deserialize the FrameId sequence for key \p K from \p Length bytes at \p D.
  /// @param K Lookup key associated with the call stack.
  /// @param D Frame-ID payload.
  /// @param Length Byte length of the payload; must be a multiple of FrameId.
  /// @return Frame-ID sequence reconstructed from the payload.
  data_type ReadData(uint64_t K, const unsigned char *D, offset_type Length) {
    using namespace support;
    llvm::SmallVector<FrameId> CS;
    // Derive the number of frames from the data length.
    uint64_t NumFrames = Length / sizeof(FrameId);
    assert(Length % sizeof(FrameId) == 0);
    CS.reserve(NumFrames);
    for (size_t I = 0; I != NumFrames; ++I) {
      FrameId F = endian::readNext<FrameId, llvm::endianness::little>(D);
      CS.push_back(F);
    }
    return CS;
  }
};

/// Source location of a call expressed as line offset plus column.
struct LineLocation {
  /// Construct a location from line offset \p L and column \p D.
  /// @param L Line offset from the enclosing function.
  /// @param D Source column.
  LineLocation(uint32_t L, uint32_t D) : LineOffset(L), Column(D) {}

  /// Return true if this location sorts before \p O.
  /// @param O Other location to compare against.
  /// @return True if this location is ordered before \p O.
  bool operator<(const LineLocation &O) const {
    return std::tie(LineOffset, Column) < std::tie(O.LineOffset, O.Column);
  }

  /// Return true if this location equals \p O.
  /// @param O Other location to compare against.
  /// @return True if LineOffset and Column match.
  bool operator==(const LineLocation &O) const {
    return LineOffset == O.LineOffset && Column == O.Column;
  }

  /// Return true if this location differs from \p O.
  /// @param O Other location to compare against.
  /// @return True if LineOffset or Column differ.
  bool operator!=(const LineLocation &O) const {
    return LineOffset != O.LineOffset || Column != O.Column;
  }

  /// Return a combined hash of LineOffset and Column.
  /// @return Combined 64-bit hash of LineOffset and Column.
  uint64_t getHashCode() const { return ((uint64_t)Column << 32) | LineOffset; }

  /// Line offset from the beginning of the enclosing function.
  uint32_t LineOffset;
  /// Source column within the line.
  uint32_t Column;
};

/// Pair of a call-site LineLocation and the callee function GUID.
using CallEdgeTy = std::pair<LineLocation, uint64_t>;
} // namespace memprof

/// DenseMapInfo specialization for memprof::LineLocation keys.
template <> struct DenseMapInfo<memprof::LineLocation> {
  /// Return a hash for \p Val suitable for DenseMap.
  /// @param Val Location to hash.
  /// @return Hash value for \p Val.
  static unsigned getHashValue(const memprof::LineLocation &Val) {
    return DenseMapInfo<uint64_t>::getHashValue(Val.getHashCode());
  }

  /// Return true if \p LHS and \p RHS are equal.
  /// @param LHS First location.
  /// @param RHS Second location.
  /// @return True if the locations are equal.
  static bool isEqual(const memprof::LineLocation &LHS,
                      const memprof::LineLocation &RHS) {
    return LHS == RHS;
  }
};

} // namespace llvm
#endif // LLVM_PROFILEDATA_MEMPROF_H
