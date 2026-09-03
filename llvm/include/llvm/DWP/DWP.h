#ifndef LLVM_DWP_DWP_H
#define LLVM_DWP_DWP_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFUnitIndex.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <deque>
#include <vector>

namespace llvm {
/// Object-file formats and utilities for reading, writing, and inspecting binaries.
namespace object {
class ObjectFile;
}
}

namespace llvm {
class raw_pwrite_stream;

/// Policy for handling CU/TU index section contribution offset overflow.
enum OnCuIndexOverflow {
  HardStop, ///< Fail with an error when a contribution offset overflows.
  SoftStop, ///< Warn, stop adding further contributions, and keep partial output.
  Continue, ///< Warn and keep writing despite contribution offset overflow.
};

/// Policy for promoting .debug_str_offsets tables to DWARF64 form.
enum Dwarf64StrOffsetsPromotion {
  Disabled, ///< Don't do any conversion of .debug_str_offsets tables.
  Enabled,  ///< Convert any .debug_str_offsets tables to DWARF64 if needed.
  Always,   ///< Always emit .debug_str_offsets talbes as DWARF64 for testing.
};

/// Section identifiers for DWP output.
enum DWPSectionId : unsigned {
  DS_Info,        ///< .debug_info.dwo section.
  DS_Types,       ///< .debug_types.dwo section (pre-DWARFv5).
  DS_Abbrev,      ///< .debug_abbrev.dwo section.
  DS_Line,        ///< .debug_line.dwo section.
  DS_Loc,         ///< .debug_loc.dwo section (pre-DWARFv5).
  DS_Loclists,    ///< .debug_loclists.dwo section.
  DS_Rnglists,    ///< .debug_rnglists.dwo section.
  DS_Macro,       ///< .debug_macro.dwo / .debug_macinfo.dwo section.
  DS_Str,         ///< .debug_str.dwo section.
  DS_StrOffsets,  ///< .debug_str_offsets.dwo section.
  DS_CUIndex,     ///< .debug_cu_index section.
  DS_TUIndex,     ///< .debug_tu_index section.
  DS_NumSections  ///< Number of DWP section identifiers (sentinel).
};

/// Direct ELF writer for DWP output.
///
/// Section data is stored as zero-copy StringRef chunks pointing to the
/// mmap'd input files, plus an inline buffer for constructed data
/// (emitIntValue). This avoids copying gigabytes of debug section data
/// through the MC infrastructure (MCContext, MCAssembler, MCDataFragment
/// allocation, layout, etc.).
class LLVM_ABI DWPWriter {
  /// Per-section storage: ordered sequence of zero-copy chunks and inline
  /// data. emitBytes() adds zero-copy StringRef references, emitIntValue()
  /// appends to an inline buffer. When emitBytes() is called with pending
  /// inline data, the buffer is flushed to an owned block first to preserve
  /// the correct interleaving order in the output.
  struct SectionData {
    SmallVector<StringRef, 4> Chunks; // ordered segments (refs + flushed bufs)
    SmallVector<char, 0> Buffer;      // pending inline data (emitIntValue)
    // Heap storage for flushed buffers. Uses std::deque so that push_back
    // does not invalidate existing elements (StringRefs point into these).
    std::deque<SmallVector<char, 0>> OwnedBuffers;

    /// Flush pending Buffer data into Chunks as an owned block.
    void flushBuffer() {
      if (!Buffer.empty()) {
        OwnedBuffers.push_back(std::move(Buffer));
        auto &B = OwnedBuffers.back();
        Chunks.push_back(StringRef(B.data(), B.size()));
        Buffer = SmallVector<char, 0>();
      }
    }

    uint64_t totalSize() const {
      uint64_t Size = 0;
      for (auto &C : Chunks)
        Size += C.size();
      Size += Buffer.size();
      return Size;
    }

    bool empty() const { return Chunks.empty() && Buffer.empty(); }

    void writeTo(raw_ostream &OS) {
      for (auto &C : Chunks)
        OS.write(C.data(), C.size());
      if (!Buffer.empty())
        OS.write(Buffer.data(), Buffer.size());

      // Clear buffers to save some memory.
      Chunks = {};
      Buffer = {};
      OwnedBuffers = {};
    }
  };

  SectionData Sections[DS_NumSections];
  DWPSectionId CurrentSection = DS_Info;
  uint16_t ELFMachine = 0;
  uint8_t ELFOSABI = 0;
  bool IsWASM = false;
  bool IsLittleEndian = true;

public:
  /// Construct an empty DWP writer with default ELF metadata.
  DWPWriter() = default;

  /// Set the ELF e_machine value for the output object.
  /// \param Machine ELF machine architecture identifier.
  void setMachine(uint16_t Machine) { ELFMachine = Machine; }
  /// Set the ELF EI_OSABI value for the output object.
  /// \param OSABI ELF OS/ABI identification byte.
  void setOSABI(uint8_t OSABI) { ELFOSABI = OSABI; }
  /// Select WASM object output instead of ELF.
  /// \param V True to write a WASM object; false for ELF.
  void setIsWASM(bool V) { IsWASM = V; }
  /// Set the endianness used when emitting integer values.
  /// \param V True for little-endian; false for big-endian.
  void setIsLittleEndian(bool V) { IsLittleEndian = V; }

  /// Return the pending inline buffer for section \p Id.
  /// \param Id DWP section whose inline buffer is requested.
  /// \return Mutable reference to the pending inline buffer for section \p Id.
  SmallVectorImpl<char> &getSectionBuffer(DWPSectionId Id) {
    return Sections[Id].Buffer;
  }

  /// Make \p Id the current section for subsequent emit calls.
  /// \param Id DWP section to switch to.
  void switchSection(DWPSectionId Id) { CurrentSection = Id; }

  /// Zero-copy: stores a reference to the input data without copying.
  /// Flushes any pending inline data first to preserve output order.
  /// \param Data Bytes to append by reference to the current section.
  void emitBytes(StringRef Data) {
    if (!Data.empty()) {
      auto &SD = Sections[CurrentSection];
      SD.flushBuffer();
      SD.Chunks.push_back(Data);
    }
  }

  /// Append \p Value encoded in \p Size bytes to the current section buffer.
  /// \param Value Integer value to emit.
  /// \param Size Number of bytes to write (honors current endianness).
  void emitIntValue(uint64_t Value, unsigned Size) {
    auto &Buf = Sections[CurrentSection].Buffer;
    if (IsLittleEndian) {
      for (unsigned I = 0; I < Size; ++I) {
        Buf.push_back(static_cast<char>(Value & 0xff));
        Value >>= 8;
      }
    } else {
      for (unsigned I = 0; I < Size; ++I) {
        Buf.push_back(
            static_cast<char>((Value >> (8 * (Size - 1 - I))) & 0xff));
      }
    }
  }

  /// Write accumulated sections as a minimal ELF relocatable object.
  /// \param OS Output stream to receive the ELF bytes.
  /// \return Success, or an error describing why the ELF write failed.
  Error writeELF(raw_pwrite_stream &OS);
  /// Write accumulated sections as a minimal WASM object with custom sections.
  /// \param OS Output stream to receive the WASM bytes.
  /// \return Success, or an error describing why the WASM write failed.
  Error writeWASM(raw_pwrite_stream &OS);
  /// Write the package using WASM or ELF format based on setIsWASM.
  /// \param OS Output stream to receive the object bytes.
  /// \return Success, or an error describing why the object write failed.
  Error write(raw_pwrite_stream &OS) {
    return IsWASM ? writeWASM(OS) : writeELF(OS);
  }
};

/// Deduplicating string pool that emits unique strings into a DWPWriter.
class DWPStringPool {
  DWPWriter &Out;
  DenseMap<StringRef, uint64_t> Pool;
  uint64_t Offset = 0;

public:
  /// Construct a string pool that emits into \p Out.
  /// \param Out Writer that receives unique string bytes.
  DWPStringPool(DWPWriter &Out) : Out(Out) {}

  /// Return the offset of \p Str in the pool, emitting it if new.
  /// \param Str Null-terminated string contents to intern.
  /// \param Length Byte length of \p Str including the trailing null.
  /// \return Byte offset of \p Str within the emitted string pool.
  uint64_t getOffset(const char *Str, unsigned Length) {
    assert(strlen(Str) + 1 == Length && "Ensure length hint is correct");

    StringRef Key(Str, Length);
    auto Pair = Pool.insert(std::make_pair(Key, Offset));
    if (Pair.second) {
      Out.emitBytes(Key);
      Offset += Length;
    }

    return Pair.first->second;
  }

  /// Discard all interned string entries from the pool map.
  void clear() { Pool = DenseMap<StringRef, uint64_t>(); }
};

/// Per-unit contribution offsets and identifying names for a DWP index entry.
struct UnitIndexEntry {
  /// Section contribution offsets and lengths for this unit.
  DWARFUnitIndex::Entry::SectionContribution Contributions[8];
  /// Compile-unit or type-unit name from the DIE.
  std::string Name;
  /// DWO name attribute value for this unit, if present.
  std::string DWOName;
  /// Name of the enclosing .dwp file when this entry came from a package.
  StringRef DWPName;
};

/// Parsed header fields from a .debug_info(.dwo) unit.
///
/// Holds data for Skeleton, Split Compilation, and Type Unit Headers (only in
/// v5) as defined in Dwarf 5 specification, 7.5.1.2, 7.5.1.3 and Dwarf 4
/// specification 7.5.1.1.
struct InfoSectionUnitHeader {
  /// Unit length field; uint64_t even in 32-bit DWARF.
  uint64_t Length = 0;

  /// DWARF version field from the unit header.
  uint16_t Version = 0;

  /// Unit type field; initialized only if Version >= 5.
  uint8_t UnitType = 0;

  /// Address size field from the unit header.
  uint8_t AddrSize = 0;

  /// Debug abbrev offset field; uint64_t even in 32-bit DWARF (assumed 0).
  uint64_t DebugAbbrevOffset = 0;

  /// DWO id / signature; in the header only if Version >= 5, else from
  /// DW_AT_GNU_dwo_id.
  std::optional<uint64_t> Signature;

  /// DWARF format derived from the length of the Length field.
  dwarf::DwarfFormat Format = dwarf::DwarfFormat::DWARF32;

  /// Size of the parsed header in bytes, stored as a convenience.
  uint8_t HeaderSize = 0;
};

/// Identifying attributes extracted from a compile or type unit.
struct CompileUnitIdentifiers {
  /// DWO id / unit signature for this compile or type unit.
  uint64_t Signature = 0;
  /// Compile-unit or type-unit name string.
  const char *Name = "";
  /// DWO name attribute string, if present.
  const char *DWOName = "";
};

/// Merge split DWARF inputs into \p Out, optionally writing the package.
/// \param Out Writer that accumulates section contributions.
/// \param Inputs Paths to input .dwo / .dwp object files.
/// \param OverflowOptValue Policy when CU/TU index offsets overflow 4 GiB.
/// \param StrOffsetsOptValue Policy for promoting .debug_str_offsets to DWARF64.
/// \param OS Optional stream; when non-null, write the finished package to it.
/// \return Success, or an error describing why the merge or write failed.
LLVM_ABI Error write(DWPWriter &Out, ArrayRef<std::string> Inputs,
                     OnCuIndexOverflow OverflowOptValue,
                     Dwarf64StrOffsetsPromotion StrOffsetsOptValue,
                     raw_pwrite_stream *OS = nullptr);

/// Mapping from DWARF section kind to its contribution length in bytes.
typedef std::vector<std::pair<DWARFSectionKind, uint32_t>> SectionLengths;

/// Parse the unit header at the start of a .debug_info(.dwo) section.
/// \param Info Contents of the info section to parse.
/// \param IsLittleEndian True if the section data is little-endian.
/// \return The parsed header fields, or an error if parsing fails.
LLVM_ABI Expected<InfoSectionUnitHeader>
parseInfoSectionUnitHeader(StringRef Info, bool IsLittleEndian);

} // namespace llvm
#endif // LLVM_DWP_DWP_H
