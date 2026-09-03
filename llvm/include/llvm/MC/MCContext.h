//===- MCContext.h - Machine Code Context -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCCONTEXT_H
#define LLVM_MC_MCCONTEXT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCAsmMacro.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCGOFFAttributes.h"
#include "llvm/MC/MCPseudoProbe.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionGOFF.h"
#include "llvm/MC/MCSymbolTableEntry.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class CodeViewContext;
class MCAsmInfo;
class MCInst;
class MCLabel;
class MCObjectFileInfo;
class MCRegisterInfo;
class MCSection;
class MCSectionCOFF;
class MCSectionDXContainer;
class MCSectionELF;
class MCSectionMachO;
class MCSectionSPIRV;
class MCSectionWasm;
class MCSectionXCOFF;
class MCStreamer;
class MCSubtargetInfo;
class MCSymbol;
class MCSymbolELF;
class MCSymbolWasm;
class MCSymbolXCOFF;
class MCTargetOptions;
class MDNode;
template <typename T> class SmallVectorImpl;
class SMDiagnostic;
class SMLoc;
class SourceMgr;
enum class EmitDwarfUnwindType;

namespace wasm {
struct WasmSignature;
}

/// Context object for machine code objects.  This class owns all of the
/// sections that it creates.
///
class MCContext {
public:
  /// String map from symbol names to symbol table values.
  using SymbolTable = StringMap<MCSymbolTableValue, BumpPtrAllocator &>;
  /// Callback type for reporting diagnostics through this context.
  using DiagHandlerTy =
      std::function<void(const SMDiagnostic &, bool, const SourceMgr &,
                         std::vector<const MDNode *> &)>;
  /// Object file format environment for this context.
  enum Environment {
    IsMachO,       ///< Mach-O object file format.
    IsELF,         ///< ELF object file format.
    IsGOFF,        ///< GOFF object file format.
    IsCOFF,        ///< COFF object file format.
    IsSPIRV,       ///< SPIR-V binary format.
    IsWasm,        ///< WebAssembly object file format.
    IsXCOFF,       ///< XCOFF object file format.
    IsDXContainer  ///< DirectX container format.
  };

private:
  Environment Env;

  /// The name of the Segment where Swift5 Reflection Section data will be
  /// outputted
  StringRef Swift5ReflectionSegmentName;

  /// The triple for this object.
  Triple TT;

  /// The SourceMgr for this object, if any.
  const SourceMgr *SrcMgr = nullptr;

  /// The SourceMgr for inline assembly, if any.
  std::unique_ptr<SourceMgr> InlineSrcMgr;
  std::vector<const MDNode *> LocInfos;

  DiagHandlerTy DiagHandler;

  /// The MCAsmInfo for this target.
  const MCAsmInfo &MAI;

  /// The MCRegisterInfo for this target.
  const MCRegisterInfo *MRI = nullptr;

  /// The MCObjectFileInfo for this target.
  const MCObjectFileInfo *MOFI = nullptr;

  /// The MCSubtargetInfo for this target.
  const MCSubtargetInfo *MSTI = nullptr;

  std::unique_ptr<CodeViewContext> CVContext;

  /// Allocator object used for creating machine code objects.
  ///
  /// We use a bump pointer allocator to avoid the need to track all allocated
  /// objects.
  BumpPtrAllocator Allocator;

  /// For MCFragment instances.
  BumpPtrAllocator FragmentAllocator;

  SpecificBumpPtrAllocator<MCSectionCOFF> COFFAllocator;
  SpecificBumpPtrAllocator<MCSectionDXContainer> DXCAllocator;
  SpecificBumpPtrAllocator<MCSectionELF> ELFAllocator;
  SpecificBumpPtrAllocator<MCSectionMachO> MachOAllocator;
  SpecificBumpPtrAllocator<MCSectionGOFF> GOFFAllocator;
  SpecificBumpPtrAllocator<MCSectionSPIRV> SPIRVAllocator;
  SpecificBumpPtrAllocator<MCSectionWasm> WasmAllocator;
  SpecificBumpPtrAllocator<MCSectionXCOFF> XCOFFAllocator;
  SpecificBumpPtrAllocator<MCInst> MCInstAllocator;

  SpecificBumpPtrAllocator<wasm::WasmSignature> WasmSignatureAllocator;

  /// Bindings of names to symbol table values.
  SymbolTable Symbols;

  /// A mapping from a local label number and an instance count to a symbol.
  /// For example, in the assembly
  ///     1:
  ///     2:
  ///     1:
  /// We have three labels represented by the pairs (1, 0), (2, 0) and (1, 1)
  DenseMap<std::pair<unsigned, unsigned>, MCSymbol *> LocalSymbols;

  /// Keeps track of labels that are used in inline assembly.
  StringMap<MCSymbol *, BumpPtrAllocator &> InlineAsmUsedLabelNames;

  /// Instances of directional local labels.
  DenseMap<unsigned, MCLabel *> Instances;
  /// NextInstance() creates the next instance of the directional local label
  /// for the LocalLabelVal and adds it to the map if needed.
  unsigned NextInstance(unsigned LocalLabelVal);
  /// GetInstance() gets the current instance of the directional local label
  /// for the LocalLabelVal and adds it to the map if needed.
  unsigned GetInstance(unsigned LocalLabelVal);

  /// SHT_LLVM_BB_ADDR_MAP version to emit.
  uint8_t BBAddrMapVersion = 5;

  /// The file name of the log file from the environment variable
  /// AS_SECURE_LOG_FILE.  Which must be set before the .secure_log_unique
  /// directive is used or it is an error.
  std::string SecureLogFile;
  /// The stream that gets written to for the .secure_log_unique directive.
  std::unique_ptr<raw_fd_ostream> SecureLog;
  /// Boolean toggled when .secure_log_unique / .secure_log_reset is seen to
  /// catch errors if .secure_log_unique appears twice without
  /// .secure_log_reset appearing between them.
  bool SecureLogUsed = false;

  /// The compilation directory to use for DW_AT_comp_dir.
  SmallString<128> CompilationDir;

  /// Prefix replacement map for source file information.
  SmallVector<std::pair<std::string, std::string>, 0> DebugPrefixMap;

  /// The main file name if passed in explicitly.
  std::string MainFileName;

  /// The dwarf file and directory tables from the dwarf .file directive.
  /// We now emit a line table for each compile unit. To reduce the prologue
  /// size of each line table, the files and directories used by each compile
  /// unit are separated.
  std::map<unsigned, MCDwarfLineTable> MCDwarfLineTablesCUMap;

  /// The current dwarf line information from the last dwarf .loc directive.
  MCDwarfLoc CurrentDwarfLoc;
  bool DwarfLocSeen = false;

  /// Generate dwarf debugging info for assembly source files.
  bool GenDwarfForAssembly = false;

  /// The current dwarf file number when generate dwarf debugging info for
  /// assembly source files.
  unsigned GenDwarfFileNumber = 0;

  /// Sections for generating the .debug_ranges and .debug_aranges sections.
  SetVector<MCSection *> SectionsForRanges;

  /// The information gathered from labels that will have dwarf label
  /// entries when generating dwarf assembly source files.
  std::vector<MCGenDwarfLabelEntry> MCGenDwarfLabelEntries;

  /// The string to embed in the debug information for the compile unit, if
  /// non-empty.
  StringRef DwarfDebugFlags;

  /// The string to embed in as the dwarf AT_producer for the compile unit, if
  /// non-empty.
  StringRef DwarfDebugProducer;

  /// The maximum version of dwarf that we should emit.
  uint16_t DwarfVersion = 4;

  /// The format of dwarf that we emit.
  dwarf::DwarfFormat DwarfFormat = dwarf::DWARF32;

  /// Honor temporary labels, this is useful for debugging semantic
  /// differences between temporary and non-temporary labels (primarily on
  /// Darwin).
  bool SaveTempLabels = false;
  bool UseNamesOnTempLabels = false;

  /// The Compile Unit ID that we are currently processing.
  unsigned DwarfCompileUnitID = 0;

  /// A collection of MCPseudoProbe in the current module
  MCPseudoProbeTable PseudoProbeTable;

  struct COFFSectionKey {
    std::string SectionName;
    StringRef GroupName;
    int SelectionKey;
    unsigned UniqueID;

    COFFSectionKey(StringRef SectionName, StringRef GroupName, int SelectionKey,
                   unsigned UniqueID)
        : SectionName(SectionName), GroupName(GroupName),
          SelectionKey(SelectionKey), UniqueID(UniqueID) {}

    bool operator<(const COFFSectionKey &Other) const {
      return std::tie(SectionName, GroupName, SelectionKey, UniqueID) <
             std::tie(Other.SectionName, Other.GroupName, Other.SelectionKey,
                      Other.UniqueID);
    }
  };

  struct WasmSectionKey {
    std::string SectionName;
    StringRef GroupName;
    unsigned UniqueID;

    WasmSectionKey(StringRef SectionName, StringRef GroupName,
                   unsigned UniqueID)
        : SectionName(SectionName), GroupName(GroupName), UniqueID(UniqueID) {}

    bool operator<(const WasmSectionKey &Other) const {
      return std::tie(SectionName, GroupName, UniqueID) <
             std::tie(Other.SectionName, Other.GroupName, Other.UniqueID);
    }
  };

  struct XCOFFSectionKey {
    // Section name.
    std::string SectionName;
    // Section property.
    // For csect section, it is storage mapping class.
    // For debug section, it is section type flags.
    union {
      XCOFF::StorageMappingClass MappingClass;
      XCOFF::DwarfSectionSubtypeFlags DwarfSubtypeFlags;
    };
    bool IsCsect;

    XCOFFSectionKey(StringRef SectionName,
                    XCOFF::StorageMappingClass MappingClass)
        : SectionName(SectionName), MappingClass(MappingClass), IsCsect(true) {}

    XCOFFSectionKey(StringRef SectionName,
                    XCOFF::DwarfSectionSubtypeFlags DwarfSubtypeFlags)
        : SectionName(SectionName), DwarfSubtypeFlags(DwarfSubtypeFlags),
          IsCsect(false) {}

    bool operator<(const XCOFFSectionKey &Other) const {
      if (IsCsect && Other.IsCsect)
        return std::tie(SectionName, MappingClass) <
               std::tie(Other.SectionName, Other.MappingClass);
      if (IsCsect != Other.IsCsect)
        return IsCsect;
      return std::tie(SectionName, DwarfSubtypeFlags) <
             std::tie(Other.SectionName, Other.DwarfSubtypeFlags);
    }
  };

  StringMap<MCSectionMachO *> MachOUniquingMap;
  std::map<COFFSectionKey, MCSectionCOFF *> COFFUniquingMap;
  StringMap<MCSectionELF *> ELFUniquingMap;
  std::map<std::string, MCSectionGOFF *> GOFFUniquingMap;
  std::map<WasmSectionKey, MCSectionWasm *> WasmUniquingMap;
  std::map<XCOFFSectionKey, MCSectionXCOFF *> XCOFFUniquingMap;
  StringMap<MCSectionDXContainer *> DXCUniquingMap;
  StringMap<bool> RelSecNames;

  SpecificBumpPtrAllocator<MCSubtargetInfo> MCSubtargetAllocator;

  /// Do automatic reset in destructor
  bool AutoReset;

  bool HadError = false;

  void reportCommon(SMLoc Loc,
                    std::function<void(SMDiagnostic &, const SourceMgr *)>);

  MCSymbolTableEntry &getSymbolTableEntry(StringRef Name);

  MCSymbol *createSymbolImpl(const MCSymbolTableEntry *Name, bool IsTemporary);
  MCSymbol *createRenamableSymbol(const Twine &Name, bool AlwaysAddSuffix,
                                  bool IsTemporary);

  MCSymbol *getOrCreateDirectionalLocalSymbol(unsigned LocalLabelVal,
                                              unsigned Instance);

  template <typename Symbol>
  Symbol *getOrCreateSectionSymbol(StringRef Section);

  MCSectionELF *createELFSectionImpl(StringRef Section, unsigned Type,
                                     unsigned Flags, unsigned EntrySize,
                                     const MCSymbolELF *Group, bool IsComdat,
                                     unsigned UniqueID,
                                     const MCSymbolELF *LinkedToSym);

  MCSymbolXCOFF *createXCOFFSymbolImpl(const MCSymbolTableEntry *Name,
                                       bool IsTemporary);

  template <typename TAttr>
  MCSectionGOFF *getGOFFSection(SectionKind Kind, StringRef Name,
                                TAttr SDAttributes, MCSection *Parent,
                                bool IsVirtual);

  /// Map of currently defined macros.
  StringMap<MCAsmMacro> MacroMap;

  // Symbols must be assigned to a section with a compatible entry size and
  // flags. This map is used to assign unique IDs to sections to distinguish
  // between sections with identical names but incompatible entry sizes and/or
  // flags. This can occur when a symbol is explicitly assigned to a section,
  // e.g. via __attribute__((section("myname"))). The map key is the tuple
  // (section name, flags, entry size).
  DenseMap<std::tuple<StringRef, unsigned, unsigned>, unsigned> ELFEntrySizeMap;

  // This set is used to record the generic mergeable section names seen.
  // These are sections that are created as mergeable e.g. .debug_str. We need
  // to avoid assigning non-mergeable symbols to these sections. It is used
  // to prevent non-mergeable symbols being explicitly assigned  to mergeable
  // sections (e.g. via _attribute_((section("myname")))).
  DenseSet<StringRef> ELFSeenGenericMergeableSections;

public:
  /// Construct a machine-code context for the given target triple.
  /// \param TheTriple Target triple describing the object environment.
  /// \param MAI Target assembly information.
  /// \param MRI Target register information.
  /// \param MSTI Target subtarget information.
  /// \param Mgr Optional source manager for diagnostics and locations.
  /// \param DoAutoReset If true, reset() is called from the destructor.
  /// \param Swift5ReflSegmentName Optional Swift5 reflection segment name.
  LLVM_ABI explicit MCContext(const Triple &TheTriple, const MCAsmInfo &MAI,
                              const MCRegisterInfo &MRI,
                              const MCSubtargetInfo &MSTI,
                              const SourceMgr *Mgr = nullptr,
                              bool DoAutoReset = true,
                              StringRef Swift5ReflSegmentName = {});
  /// Deleted copy constructor; MCContext is non-copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  MCContext(const MCContext &Other) = delete;
  /// Deleted copy assignment; MCContext is non-copyable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  MCContext &operator=(const MCContext &Other) = delete;
  /// Destroy the context and release owned allocations.
  LLVM_ABI ~MCContext();

  /// Return the object file format environment for this context.
  ///
  /// \return The object file format environment for this context.
  Environment getObjectFileType() const { return Env; }
  /// Return true if this context targets the ELF object format.
  ///
  /// \return True if this context targets the ELF object format.
  bool isELF() const { return Env == IsELF; }
  /// Return true if this context targets the Mach-O object format.
  ///
  /// \return True if this context targets the Mach-O object format.
  bool isMachO() const { return Env == IsMachO; }
  /// Return true if this context targets the XCOFF object format.
  ///
  /// \return True if this context targets the XCOFF object format.
  bool isXCOFF() const { return Env == IsXCOFF; }

  /// Return the Swift5 reflection segment name configured for this context.
  ///
  /// \return The Swift5 reflection segment name configured for this context.
  const StringRef &getSwift5ReflectionSegmentName() const {
    return Swift5ReflectionSegmentName;
  }
  /// Return the target triple for this context.
  ///
  /// \return The target triple for this context.
  const Triple &getTargetTriple() const { return TT; }
  /// Return the source manager associated with this context, if any.
  ///
  /// \return The source manager associated with this context, or null.
  const SourceMgr *getSourceManager() const { return SrcMgr; }

  /// Initialize the source manager used for inline assembly.
  LLVM_ABI void initInlineSourceManager();
  /// Return the source manager used for inline assembly, if initialized.
  ///
  /// \return The inline-assembly source manager, or null if not initialized.
  SourceMgr *getInlineSourceManager() { return InlineSrcMgr.get(); }
  /// Return the list of location metadata nodes associated with diagnostics.
  ///
  /// \return The list of location metadata nodes associated with diagnostics.
  std::vector<const MDNode *> &getLocInfos() { return LocInfos; }
  /// Install a custom diagnostic handler for this context.
  /// \param DiagHandler Callback invoked when diagnostics are reported.
  void setDiagnosticHandler(DiagHandlerTy DiagHandler) {
    this->DiagHandler = DiagHandler;
  }

  /// Set the object file information for this context.
  /// \param Mofi Object file info describing target sections and symbols.
  void setObjectFileInfo(const MCObjectFileInfo *Mofi) { MOFI = Mofi; }

  /// Return the assembly information for this target.
  ///
  /// \return The assembly information for this target.
  const MCAsmInfo &getAsmInfo() const { return MAI; }

  /// Return the register information for this target.
  ///
  /// \return The register information for this target.
  const MCRegisterInfo *getRegisterInfo() const { return MRI; }

  /// Return the object file information for this target.
  ///
  /// \return The object file information for this target.
  const MCObjectFileInfo *getObjectFileInfo() const { return MOFI; }

  /// Return the subtarget information for this target.
  ///
  /// \return The subtarget information for this target.
  const MCSubtargetInfo *getSubtargetInfo() const { return MSTI; }

  /// Return the MC target options associated with this context.
  ///
  /// \return The MC target options associated with this context.
  LLVM_ABI const MCTargetOptions &getTargetOptions() const;

  /// Return the CodeView context owned by this MC context.
  ///
  /// \return The CodeView context owned by this MC context.
  LLVM_ABI CodeViewContext &getCVContext();

  /// Set whether temporary labels should retain names in the symbol table.
  /// \param Value True to emit names for temporary labels.
  void setUseNamesOnTempLabels(bool Value) { UseNamesOnTempLabels = Value; }

  /// \name Module Lifetime Management
  /// @{

  /// reset - return object to right after construction state to prepare
  /// to process a new module
  LLVM_ABI void reset();

  /// @}

  /// \name McInst Management

  /// Create and return a new MC instruction.
  ///
  /// \return A newly allocated MC instruction.
  LLVM_ABI MCInst *createMCInst();

  /// \name Symbol Management
  /// @{

  /// Create a linker-private temporary symbol with the default "tmp" name.
  ///
  /// This creates a "l"-prefixed symbol for Mach-O and is identical to
  /// createNamedTempSymbol for other object file formats.
  ///
  /// \return The newly created linker-private temporary symbol.
  LLVM_ABI MCSymbol *createLinkerPrivateTempSymbol();
  /// Create a linker-private symbol with the specified name prefix.
  /// \param Name Name prefix for the linker-private symbol.
  /// \return The newly created linker-private symbol.
  LLVM_ABI MCSymbol *createLinkerPrivateSymbol(const Twine &Name);

  /// Create a temporary symbol with a unique, unspecified name.
  ///
  /// The name will be omitted in the symbol table if UseNamesOnTempLabels is
  /// false (default except MCAsmStreamer). The overload without Name uses an
  /// unspecified name.
  ///
  /// \return The newly created temporary symbol.
  LLVM_ABI MCSymbol *createTempSymbol();
  /// Create a temporary symbol with a unique name based on \p Name.
  ///
  /// The name will be omitted in the symbol table if UseNamesOnTempLabels is
  /// false (default except MCAsmStreamer).
  /// \param Name Base name used when forming the unique temporary symbol.
  /// \param AlwaysAddSuffix If true, always append a unique suffix.
  /// \return The newly created temporary symbol.
  LLVM_ABI MCSymbol *createTempSymbol(const Twine &Name,
                                      bool AlwaysAddSuffix = true);

  /// Create a temporary symbol with a unique name whose name cannot be
  /// omitted in the symbol table. This is rarely used.
  ///
  /// \return The newly created named temporary symbol.
  LLVM_ABI MCSymbol *createNamedTempSymbol();
  /// Create a named temporary symbol based on \p Name that is always emitted.
  /// \param Name Base name used when forming the unique temporary symbol.
  /// \return The newly created named temporary symbol.
  LLVM_ABI MCSymbol *createNamedTempSymbol(const Twine &Name);

  /// Get or create a symbol for a basic block.
  ///
  /// For non-always-emit symbols, this behaves like createTempSymbol, except
  /// that it uses the InternalSymbolPrefix. When AlwaysEmit is true, behaves
  /// like getOrCreateSymbol, prefixed with InternalSymbolPrefix.
  /// \param Name Basic-block name used to form the symbol.
  /// \param AlwaysEmit If true, ensure the symbol is emitted like getOrCreateSymbol.
  /// \return The basic-block symbol.
  LLVM_ABI MCSymbol *createBlockSymbol(const Twine &Name,
                                       bool AlwaysEmit = false);

  /// Create a local, non-temporary symbol like an ELF mapping symbol. Calling
  /// the function with the same name will generate new, unique instances.
  /// \param Name Local symbol name.
  /// \return The newly created local symbol.
  LLVM_ABI MCSymbol *createLocalSymbol(StringRef Name);

  /// Create the definition of a directional local symbol for numbered label
  /// (used for "1:" definitions).
  /// \param LocalLabelVal Numeric label value (e.g. 1 in "1:").
  /// \return The newly created directional local symbol definition.
  LLVM_ABI MCSymbol *createDirectionalLocalSymbol(unsigned LocalLabelVal);

  /// Create and return a directional local symbol for numbered label (used
  /// for "1b" or 1f" references).
  /// \param LocalLabelVal Numeric label value (e.g. 1 in "1b"/"1f").
  /// \param Before True for a backward ("b") reference, false for forward ("f").
  /// \return The directional local symbol for the reference.
  LLVM_ABI MCSymbol *getDirectionalLocalSymbol(unsigned LocalLabelVal,
                                               bool Before);

  /// Lookup the symbol inside with the specified \p Name.  If it exists,
  /// return it.  If not, create a forward reference and return it.
  ///
  /// \param Name - The symbol name, which must be unique across all symbols.
  /// \return The existing or newly created symbol.
  LLVM_ABI MCSymbol *getOrCreateSymbol(const Twine &Name);

  /// Variant of getOrCreateSymbol that handles backslash-escaped symbols.
  /// For example, parse "a\"b\\" as a"\.
  /// \param Name Possibly backslash-escaped symbol name to parse.
  /// \return The existing or newly created symbol.
  LLVM_ABI MCSymbol *parseSymbol(const Twine &Name);

  /// Gets a symbol that will be defined to the final stack offset of a local
  /// variable after codegen.
  ///
  /// \param FuncName - Mangled name of the function containing the local.
  /// \param Idx - The index of a local variable passed to \@llvm.localescape.
  /// \return The frame-allocation symbol for the local variable.
  LLVM_ABI MCSymbol *getOrCreateFrameAllocSymbol(const Twine &FuncName,
                                                 unsigned Idx);

  /// Get or create the symbol holding the parent frame offset for \p FuncName.
  /// \param FuncName Mangled name of the function.
  /// \return The parent frame offset symbol.
  LLVM_ABI MCSymbol *getOrCreateParentFrameOffsetSymbol(const Twine &FuncName);

  /// Get or create the LSDA (language-specific data area) symbol for \p FuncName.
  /// \param FuncName Mangled name of the function.
  /// \return The LSDA symbol for the function.
  LLVM_ABI MCSymbol *getOrCreateLSDASymbol(const Twine &FuncName);

  /// Get the symbol for \p Name, or null.
  /// \param Name Symbol name to look up.
  /// \return The symbol, or null if it does not exist.
  LLVM_ABI MCSymbol *lookupSymbol(const Twine &Name) const;

  /// Clone a symbol for the .set directive, replacing it in the symbol table.
  ///
  /// Existing references to the original symbol remain unchanged, and the
  /// original symbol is not emitted to the symbol table.
  /// \param Sym Symbol to clone and replace in the symbol table.
  /// \return The cloned symbol now present in the symbol table.
  LLVM_ABI MCSymbol *cloneSymbol(MCSymbol &Sym);

  /// Set value for a symbol.
  /// \param Streamer Streamer used to emit the symbol assignment.
  /// \param Sym Symbol name whose value is set.
  /// \param Val Absolute value assigned to the symbol.
  LLVM_ABI void setSymbolValue(MCStreamer &Streamer, const Twine &Sym,
                               uint64_t Val);

  /// Return a const reference to the symbol table for iteration and lookup.
  ///
  /// 'const' because we still want any modifications to the table itself to
  /// use the MCContext APIs.
  ///
  /// \return A const reference to the symbol table.
  const SymbolTable &getSymbols() const { return Symbols; }

  /// isInlineAsmLabel - Return true if the name is a label referenced in
  /// inline assembly.
  /// \param Name Label name to look up among inline-asm labels.
  /// \return The inline-asm label symbol, or null if not found.
  MCSymbol *getInlineAsmLabel(StringRef Name) const {
    return InlineAsmUsedLabelNames.lookup(Name);
  }

  /// registerInlineAsmLabel - Records that the name is a label referenced in
  /// inline assembly.
  /// \param Sym Symbol to register as an inline-asm label.
  LLVM_ABI void registerInlineAsmLabel(MCSymbol *Sym);

  /// Allocates and returns a new `WasmSignature` instance (with empty parameter
  /// and return type lists).
  ///
  /// \return A newly allocated empty WasmSignature.
  LLVM_ABI wasm::WasmSignature *createWasmSignature();

  /// @}

  /// \name Section Management
  /// @{

  /// Return the MCSection for the specified mach-o section.  This requires
  /// the operands to be valid.
  /// \param Segment Mach-O segment name.
  /// \param Section Mach-O section name within the segment.
  /// \param TypeAndAttributes Mach-O section type and attribute flags.
  /// \param Reserved2 Mach-O reserved2 field value.
  /// \param K Section kind classifying the section contents.
  /// \param BeginSymName Optional name for the section begin symbol.
  /// \return The Mach-O section for the given operands.
  LLVM_ABI MCSectionMachO *getMachOSection(StringRef Segment, StringRef Section,
                                           unsigned TypeAndAttributes,
                                           unsigned Reserved2, SectionKind K,
                                           const char *BeginSymName = nullptr);

  /// Return the Mach-O section with Reserved2 defaulted to zero.
  /// \param Segment Mach-O segment name.
  /// \param Section Mach-O section name within the segment.
  /// \param TypeAndAttributes Mach-O section type and attribute flags.
  /// \param K Section kind classifying the section contents.
  /// \param BeginSymName Optional name for the section begin symbol.
  /// \return The Mach-O section for the given operands.
  MCSectionMachO *getMachOSection(StringRef Segment, StringRef Section,
                                  unsigned TypeAndAttributes, SectionKind K,
                                  const char *BeginSymName = nullptr) {
    return getMachOSection(Segment, Section, TypeAndAttributes, 0, K,
                           BeginSymName);
  }

  /// Get or create an ELF section with the given name, type, and flags.
  /// \param Section Section name.
  /// \param Type ELF section type (SHT_*).
  /// \param Flags ELF section flags (SHF_*).
  /// \return The ELF section for the given name, type, and flags.
  MCSectionELF *getELFSection(const Twine &Section, unsigned Type,
                              unsigned Flags) {
    return getELFSection(Section, Type, Flags, 0, "", false);
  }

  /// Get or create an ELF section with an explicit entry size.
  /// \param Section Section name.
  /// \param Type ELF section type (SHT_*).
  /// \param Flags ELF section flags (SHF_*).
  /// \param EntrySize Entity size for mergeable sections.
  /// \return The ELF section for the given attributes.
  MCSectionELF *getELFSection(const Twine &Section, unsigned Type,
                              unsigned Flags, unsigned EntrySize) {
    return getELFSection(Section, Type, Flags, EntrySize, "", false,
                         MCSection::NonUniqueID, nullptr);
  }

  /// Get or create an ELF section belonging to a named group.
  /// \param Section Section name.
  /// \param Type ELF section type (SHT_*).
  /// \param Flags ELF section flags (SHF_*).
  /// \param EntrySize Entity size for mergeable sections.
  /// \param Group Group section name.
  /// \param IsComdat True if the group is a COMDAT group.
  /// \return The ELF section for the given attributes and group.
  MCSectionELF *getELFSection(const Twine &Section, unsigned Type,
                              unsigned Flags, unsigned EntrySize,
                              const Twine &Group, bool IsComdat) {
    return getELFSection(Section, Type, Flags, EntrySize, Group, IsComdat,
                         MCSection::NonUniqueID, nullptr);
  }

  /// Get or create an ELF section with group name, unique ID, and linked-to symbol.
  /// \param Section Section name.
  /// \param Type ELF section type (SHT_*).
  /// \param Flags ELF section flags (SHF_*).
  /// \param EntrySize Entity size for mergeable sections.
  /// \param Group Group section name.
  /// \param IsComdat True if the group is a COMDAT group.
  /// \param UniqueID Discriminator for otherwise identical sections.
  /// \param LinkedToSym Optional symbol this section is linked to.
  /// \return The ELF section for the given attributes.
  LLVM_ABI MCSectionELF *getELFSection(const Twine &Section, unsigned Type,
                                       unsigned Flags, unsigned EntrySize,
                                       const Twine &Group, bool IsComdat,
                                       unsigned UniqueID,
                                       const MCSymbolELF *LinkedToSym);

  /// Get or create an ELF section with a group symbol, unique ID, and linked-to symbol.
  /// \param Section Section name.
  /// \param Type ELF section type (SHT_*).
  /// \param Flags ELF section flags (SHF_*).
  /// \param EntrySize Entity size for mergeable sections.
  /// \param Group Group section symbol.
  /// \param IsComdat True if the group is a COMDAT group.
  /// \param UniqueID Discriminator for otherwise identical sections.
  /// \param LinkedToSym Optional symbol this section is linked to.
  /// \return The ELF section for the given attributes.
  LLVM_ABI MCSectionELF *getELFSection(const Twine &Section, unsigned Type,
                                       unsigned Flags, unsigned EntrySize,
                                       const MCSymbolELF *Group, bool IsComdat,
                                       unsigned UniqueID,
                                       const MCSymbolELF *LinkedToSym);

  /// Get an ELF section named by concatenating a prefix and suffix.
  ///
  /// This section is named by concatenating \p Prefix with '.' then \p Suffix.
  /// The \p Type describes the type of the section and \p Flags are used to
  /// further configure this named section.
  /// \param Prefix Leading part of the section name.
  /// \param Suffix Trailing part of the section name after '.'.
  /// \param Type ELF section type (SHT_*).
  /// \param Flags ELF section flags (SHF_*).
  /// \param EntrySize Entity size for mergeable sections.
  /// \return The ELF section with the concatenated name.
  LLVM_ABI MCSectionELF *getELFNamedSection(const Twine &Prefix,
                                            const Twine &Suffix, unsigned Type,
                                            unsigned Flags,
                                            unsigned EntrySize = 0);

  /// Create an ELF relocation section associated with \p RelInfoSection.
  /// \param Name Relocation section name.
  /// \param Type ELF section type (SHT_*).
  /// \param Flags ELF section flags (SHF_*).
  /// \param EntrySize Size of each relocation entry.
  /// \param Group Optional group symbol for the relocation section.
  /// \param RelInfoSection Section whose relocations are described.
  /// \return The newly created ELF relocation section.
  LLVM_ABI MCSectionELF *
  createELFRelSection(const Twine &Name, unsigned Type, unsigned Flags,
                      unsigned EntrySize, const MCSymbolELF *Group,
                      const MCSectionELF *RelInfoSection);

  /// Create an ELF group section for the given group symbol.
  /// \param Group Group symbol naming the section group.
  /// \param IsComdat True if the group is a COMDAT group.
  /// \return The newly created ELF group section.
  LLVM_ABI MCSectionELF *createELFGroupSection(const MCSymbolELF *Group,
                                               bool IsComdat);

  /// Record mergeable-section metadata for ELF unique-ID assignment.
  /// \param SectionName Section name being recorded.
  /// \param Flags ELF section flags.
  /// \param UniqueID Unique ID assigned to the section.
  /// \param EntrySize Entity size for the mergeable section.
  LLVM_ABI void recordELFMergeableSectionInfo(StringRef SectionName,
                                              unsigned Flags, unsigned UniqueID,
                                              unsigned EntrySize);

  /// Return true if \p Name is a known implicit mergeable ELF section prefix.
  /// \param Name Section name to test.
  /// \return True if Name is an implicit mergeable ELF section prefix.
  LLVM_ABI bool isELFImplicitMergeableSectionNamePrefix(StringRef Name);

  /// Return true if \p Name is a generic mergeable ELF section seen so far.
  /// \param Name Section name to test.
  /// \return True if Name is a generic mergeable ELF section.
  LLVM_ABI bool isELFGenericMergeableSection(StringRef Name);

  /// Return the unique ID of the section with the given name, flags and entry
  /// size, if it exists.
  /// \param SectionName Section name to look up.
  /// \param Flags ELF section flags.
  /// \param EntrySize Entity size for the mergeable section.
  /// \return The unique ID if found, or std::nullopt.
  LLVM_ABI std::optional<unsigned>
  getELFUniqueIDForEntsize(StringRef SectionName, unsigned Flags,
                           unsigned EntrySize);

  /// Get or create a GOFF section with SD attributes.
  /// \param Kind Section kind classifying the section contents.
  /// \param Name Section name.
  /// \param SDAttributes GOFF SD (section definition) attributes.
  /// \return The GOFF section with the given SD attributes.
  LLVM_ABI MCSectionGOFF *getGOFFSection(SectionKind Kind, StringRef Name,
                                         GOFF::SDAttr SDAttributes);
  /// Get or create a GOFF section with ED attributes under a parent section.
  /// \param Kind Section kind classifying the section contents.
  /// \param Name Section name.
  /// \param EDAttributes GOFF ED (element definition) attributes.
  /// \param Parent Parent GOFF section.
  /// \return The GOFF section with the given ED attributes.
  LLVM_ABI MCSectionGOFF *getGOFFSection(SectionKind Kind, StringRef Name,
                                         GOFF::EDAttr EDAttributes,
                                         MCSection *Parent);
  /// Get or create a GOFF section with PR attributes under a parent section.
  /// \param Kind Section kind classifying the section contents.
  /// \param Name Section name.
  /// \param PRAttributes GOFF PR (part reference) attributes.
  /// \param Parent Parent GOFF section.
  /// \return The GOFF section with the given PR attributes.
  LLVM_ABI MCSectionGOFF *getGOFFSection(SectionKind Kind, StringRef Name,
                                         GOFF::PRAttr PRAttributes,
                                         MCSection *Parent);

  /// Get or create a COFF section optionally associated with a COMDAT symbol.
  /// \param Section Section name.
  /// \param Characteristics COFF section characteristic flags.
  /// \param COMDATSymName COMDAT symbol name, if any.
  /// \param Selection COMDAT selection type.
  /// \param UniqueID Discriminator for otherwise identical sections.
  /// \return The COFF section for the given attributes.
  LLVM_ABI MCSectionCOFF *
  getCOFFSection(StringRef Section, unsigned Characteristics,
                 StringRef COMDATSymName, int Selection,
                 unsigned UniqueID = MCSection::NonUniqueID);

  /// Get or create a COFF section without COMDAT association.
  /// \param Section Section name.
  /// \param Characteristics COFF section characteristic flags.
  /// \return The COFF section for the given name and characteristics.
  LLVM_ABI MCSectionCOFF *getCOFFSection(StringRef Section,
                                         unsigned Characteristics);

  /// Get or create a COFF section associated with the section containing a key symbol.
  ///
  /// For example, to create a debug info section associated with an inline
  /// function, pass the normal debug info section as Sec and the function
  /// symbol as KeySym.
  /// \param Sec Prototype section whose characteristics are copied.
  /// \param KeySym Symbol whose containing section forms the association.
  /// \param UniqueID Discriminator for otherwise identical sections.
  /// \return The associative COFF section.
  LLVM_ABI MCSectionCOFF *
  getAssociativeCOFFSection(MCSectionCOFF *Sec, const MCSymbol *KeySym,
                            unsigned UniqueID = MCSection::NonUniqueID);

  /// Get or create the single SPIR-V section used by this context.
  ///
  /// \return The SPIR-V section for this context.
  LLVM_ABI MCSectionSPIRV *getSPIRVSection();

  /// Get or create a Wasm section with the given name and kind.
  /// \param Section Section name.
  /// \param K Section kind classifying the section contents.
  /// \param Flags Wasm section flags.
  /// \return The Wasm section for the given name and kind.
  MCSectionWasm *getWasmSection(const Twine &Section, SectionKind K,
                                unsigned Flags = 0) {
    return getWasmSection(Section, K, Flags, "", ~0);
  }

  /// Get or create a Wasm section belonging to a named group.
  /// \param Section Section name.
  /// \param K Section kind classifying the section contents.
  /// \param Flags Wasm section flags.
  /// \param Group Group name.
  /// \param UniqueID Discriminator for otherwise identical sections.
  /// \return The Wasm section for the given attributes and group.
  LLVM_ABI MCSectionWasm *getWasmSection(const Twine &Section, SectionKind K,
                                         unsigned Flags, const Twine &Group,
                                         unsigned UniqueID);

  /// Get or create a Wasm section belonging to a group symbol.
  /// \param Section Section name.
  /// \param K Section kind classifying the section contents.
  /// \param Flags Wasm section flags.
  /// \param Group Group symbol.
  /// \param UniqueID Discriminator for otherwise identical sections.
  /// \return The Wasm section for the given attributes and group symbol.
  LLVM_ABI MCSectionWasm *getWasmSection(const Twine &Section, SectionKind K,
                                         unsigned Flags,
                                         const MCSymbolWasm *Group,
                                         unsigned UniqueID);

  /// Get or create the DXContainer section for the provided section name.
  /// \param Section Section name.
  /// \param K Section kind classifying the section contents.
  /// \return The DXContainer section for the given name and kind.
  LLVM_ABI MCSectionDXContainer *getDXContainerSection(StringRef Section,
                                                       SectionKind K);

  /// Return true if an XCOFF section with the given name and csect properties exists.
  /// \param Section Section name.
  /// \param CsectProp Csect properties identifying the section.
  /// \return True if the XCOFF section already exists.
  LLVM_ABI bool hasXCOFFSection(StringRef Section,
                                XCOFF::CsectProperties CsectProp) const;

  /// Get or create an XCOFF section with optional csect or dwarf subtype properties.
  /// \param Section Section name.
  /// \param K Section kind classifying the section contents.
  /// \param CsectProp Optional csect properties.
  /// \param MultiSymbolsAllowed Whether multiple symbols may share the section.
  /// \param DwarfSubtypeFlags Optional DWARF section subtype flags.
  /// \return The XCOFF section for the given attributes.
  LLVM_ABI MCSectionXCOFF *getXCOFFSection(
      StringRef Section, SectionKind K,
      std::optional<XCOFF::CsectProperties> CsectProp = std::nullopt,
      bool MultiSymbolsAllowed = false,
      std::optional<XCOFF::DwarfSectionSubtypeFlags> DwarfSubtypeFlags =
          std::nullopt);

  /// Create and save a copy of \p STI and return a reference to the copy.
  /// \param STI Subtarget info to copy into context-owned storage.
  /// \return A reference to the context-owned subtarget info copy.
  LLVM_ABI MCSubtargetInfo &getSubtargetCopy(const MCSubtargetInfo &STI);

  /// Return the SHT_LLVM_BB_ADDR_MAP version that will be emitted.
  ///
  /// \return The SHT_LLVM_BB_ADDR_MAP version that will be emitted.
  uint8_t getBBAddrMapVersion() const { return BBAddrMapVersion; }

  /// @}

  /// \name Dwarf Management
  /// @{

  /// Return the compilation directory used for DW_AT_comp_dir.
  ///
  /// The compilation directory should be set with \c setCompilationDir before
  /// calling this function. If it is unset, an empty string will be returned.
  ///
  /// \return The compilation directory, or an empty string if unset.
  StringRef getCompilationDir() const { return CompilationDir; }

  /// Set the compilation directory for DW_AT_comp_dir.
  /// \param S Compilation directory path.
  void setCompilationDir(StringRef S) { CompilationDir = S.str(); }

  /// Add an entry to the debug prefix map.
  /// \param From Path prefix to replace.
  /// \param To Replacement path prefix.
  LLVM_ABI void addDebugPrefixMapEntry(const std::string &From,
                                       const std::string &To);

  /// Remap one path in-place as per the debug prefix map.
  /// \param Path Path buffer to rewrite using the prefix map.
  LLVM_ABI void remapDebugPath(SmallVectorImpl<char> &Path);

  /// Remap all debug directory paths in-place as per the debug prefix map.
  LLVM_ABI void RemapDebugPaths();

  /// Return the main file name for error messages and debug info.
  ///
  /// This can be set to ensure we've got the correct file name after
  /// preprocessing or for -save-temps.
  ///
  /// \return The main file name for error messages and debug info.
  const std::string &getMainFileName() const { return MainFileName; }

  /// Set the main file name and override the default.
  /// \param S Main source file name.
  void setMainFileName(StringRef S) { MainFileName = std::string(S); }

  /// Creates an entry in the dwarf file and directory tables.
  /// \param Directory Directory component of the file path.
  /// \param FileName File name component of the path.
  /// \param FileNumber DWARF file number to assign or look up.
  /// \param Checksum Optional MD5 checksum of the file contents.
  /// \param Source Optional source text embedded for the file.
  /// \param CUID Compile unit ID whose line table is updated.
  /// \return The assigned file number, or an error on failure.
  LLVM_ABI Expected<unsigned>
  getDwarfFile(StringRef Directory, StringRef FileName, unsigned FileNumber,
               std::optional<MD5::MD5Result> Checksum,
               std::optional<StringRef> Source, unsigned CUID);

  /// Return true if \p FileNumber is a valid DWARF file number for \p CUID.
  /// \param FileNumber File number to validate.
  /// \param CUID Compile unit ID whose line table is checked.
  /// \return True if FileNumber is valid for the given compile unit.
  LLVM_ABI bool isValidDwarfFileNumber(unsigned FileNumber, unsigned CUID = 0);

  /// Return the map of DWARF line tables keyed by compile unit ID.
  ///
  /// \return The map of DWARF line tables keyed by compile unit ID.
  const std::map<unsigned, MCDwarfLineTable> &getMCDwarfLineTables() const {
    return MCDwarfLineTablesCUMap;
  }

  /// Return a mutable reference to the DWARF line table for \p CUID.
  /// \param CUID Compile unit ID of the line table to access.
  /// \return A mutable reference to the DWARF line table.
  MCDwarfLineTable &getMCDwarfLineTable(unsigned CUID) {
    return MCDwarfLineTablesCUMap[CUID];
  }

  /// Return a const reference to the DWARF line table for \p CUID.
  /// \param CUID Compile unit ID of the line table to access.
  /// \return A const reference to the DWARF line table.
  const MCDwarfLineTable &getMCDwarfLineTable(unsigned CUID) const {
    auto I = MCDwarfLineTablesCUMap.find(CUID);
    assert(I != MCDwarfLineTablesCUMap.end());
    return I->second;
  }

  /// Return the DWARF file table for the given compile unit.
  /// \param CUID Compile unit ID whose file table is returned.
  /// \return The DWARF file table for the compile unit.
  const SmallVectorImpl<MCDwarfFile> &getMCDwarfFiles(unsigned CUID = 0) {
    return getMCDwarfLineTable(CUID).getMCDwarfFiles();
  }

  /// Return the DWARF directory table for the given compile unit.
  /// \param CUID Compile unit ID whose directory table is returned.
  /// \return The DWARF directory table for the compile unit.
  const SmallVectorImpl<std::string> &getMCDwarfDirs(unsigned CUID = 0) {
    return getMCDwarfLineTable(CUID).getMCDwarfDirs();
  }

  /// Return the compile unit ID currently being processed.
  ///
  /// \return The compile unit ID currently being processed.
  unsigned getDwarfCompileUnitID() { return DwarfCompileUnitID; }

  /// Set the compile unit ID currently being processed.
  /// \param CUIndex New compile unit ID.
  void setDwarfCompileUnitID(unsigned CUIndex) { DwarfCompileUnitID = CUIndex; }

  /// Specifies the "root" file and directory of the compilation unit.
  /// These are "file 0" and "directory 0" in DWARF v5.
  /// \param CUID Compile unit ID whose root file is set.
  /// \param CompilationDir Root compilation directory.
  /// \param Filename Root source file name.
  /// \param Checksum Optional MD5 checksum of the root file.
  /// \param Source Optional embedded source text for the root file.
  void setMCLineTableRootFile(unsigned CUID, StringRef CompilationDir,
                              StringRef Filename,
                              std::optional<MD5::MD5Result> Checksum,
                              std::optional<StringRef> Source) {
    getMCDwarfLineTable(CUID).setRootFile(CompilationDir, Filename, Checksum,
                                          Source);
  }

  /// Reports whether MD5 checksum usage is consistent (all-or-none).
  /// \param CUID Compile unit ID whose MD5 usage is checked.
  /// \return True if MD5 checksum usage is consistent for the compile unit.
  bool isDwarfMD5UsageConsistent(unsigned CUID) const {
    return getMCDwarfLineTable(CUID).isMD5UsageConsistent();
  }

  /// Record the current dwarf .loc directive for the next instruction.
  ///
  /// Saves the information from the currently parsed dwarf .loc directive
  /// and sets DwarfLocSeen. When the next instruction is assembled an entry
  /// in the line number table with this information and the address of the
  /// instruction will be created.
  /// \param FileNum DWARF file number from the .loc directive.
  /// \param Line Source line number.
  /// \param Column Source column number.
  /// \param Flags DWARF location flags (is_stmt, basic_block, etc.).
  /// \param Isa Instruction set architecture encoding.
  /// \param Discriminator DWARF discriminator value.
  void setCurrentDwarfLoc(unsigned FileNum, unsigned Line, unsigned Column,
                          unsigned Flags, unsigned Isa,
                          unsigned Discriminator) {
    CurrentDwarfLoc.setFileNum(FileNum);
    CurrentDwarfLoc.setLine(Line);
    CurrentDwarfLoc.setColumn(Column);
    CurrentDwarfLoc.setFlags(Flags);
    CurrentDwarfLoc.setIsa(Isa);
    CurrentDwarfLoc.setDiscriminator(Discriminator);
    DwarfLocSeen = true;
  }

  /// Clear the flag indicating a dwarf .loc directive was seen.
  void clearDwarfLocSeen() { DwarfLocSeen = false; }

  /// Return true if a dwarf .loc directive has been seen since the last clear.
  ///
  /// \return True if a dwarf .loc directive has been seen since the last clear.
  bool getDwarfLocSeen() { return DwarfLocSeen; }
  /// Return the dwarf location information from the last .loc directive.
  ///
  /// \return The dwarf location information from the last .loc directive.
  const MCDwarfLoc &getCurrentDwarfLoc() { return CurrentDwarfLoc; }

  /// Return whether dwarf debug info should be generated for assembly sources.
  ///
  /// \return True if dwarf debug info should be generated for assembly sources.
  bool getGenDwarfForAssembly() { return GenDwarfForAssembly; }
  /// Set whether dwarf debug info should be generated for assembly sources.
  /// \param Value True to generate dwarf for assembly.
  void setGenDwarfForAssembly(bool Value) { GenDwarfForAssembly = Value; }
  /// Return the current dwarf file number used when generating assembly dwarf.
  ///
  /// \return The current dwarf file number used when generating assembly dwarf.
  unsigned getGenDwarfFileNumber() { return GenDwarfFileNumber; }
  /// Return how dwarf unwind information should be emitted.
  ///
  /// \return How dwarf unwind information should be emitted.
  LLVM_ABI EmitDwarfUnwindType emitDwarfUnwindInfo() const;
  /// Return whether compact unwind should emit the non-canonical form.
  ///
  /// \return True if compact unwind should emit the non-canonical form.
  LLVM_ABI bool emitCompactUnwindNonCanonical() const;

  /// Set the current dwarf file number used when generating assembly dwarf.
  /// \param FileNumber New dwarf file number.
  void setGenDwarfFileNumber(unsigned FileNumber) {
    GenDwarfFileNumber = FileNumber;
  }

  /// Specifies information about the "root file" for assembler clients
  /// (e.g., llvm-mc). Assumes compilation dir etc. have been set up.
  /// \param FileName Root assembly source file name.
  /// \param Buffer Contents of the root assembly source file.
  LLVM_ABI void setGenDwarfRootFile(StringRef FileName, StringRef Buffer);

  /// Return the set of sections used when generating dwarf ranges and aranges.
  ///
  /// \return The set of sections used when generating dwarf ranges and aranges.
  const SetVector<MCSection *> &getGenDwarfSectionSyms() {
    return SectionsForRanges;
  }

  /// Add a section to the set used for generating dwarf ranges and aranges.
  /// \param Sec Section to record.
  /// \return True if the section was newly inserted.
  bool addGenDwarfSection(MCSection *Sec) {
    return SectionsForRanges.insert(Sec);
  }

  /// Finalize dwarf sections by emitting any deferred dwarf data via \p MCOS.
  /// \param MCOS Streamer used to emit finalized dwarf sections.
  LLVM_ABI void finalizeDwarfSections(MCStreamer &MCOS);

  /// Return the collected dwarf label entries for assembly dwarf generation.
  ///
  /// \return The collected dwarf label entries for assembly dwarf generation.
  const std::vector<MCGenDwarfLabelEntry> &getMCGenDwarfLabelEntries() const {
    return MCGenDwarfLabelEntries;
  }

  /// Append a dwarf label entry for assembly dwarf generation.
  /// \param E Label entry to record.
  void addMCGenDwarfLabelEntry(const MCGenDwarfLabelEntry &E) {
    MCGenDwarfLabelEntries.push_back(E);
  }

  /// Set the string embedded in dwarf debug info for the compile unit flags.
  /// \param S Debug flags string.
  void setDwarfDebugFlags(StringRef S) { DwarfDebugFlags = S; }
  /// Return the string embedded in dwarf debug info for the compile unit flags.
  ///
  /// \return The dwarf compile unit flags string.
  StringRef getDwarfDebugFlags() { return DwarfDebugFlags; }

  /// Set the dwarf AT_producer string for the compile unit.
  /// \param S Producer string.
  void setDwarfDebugProducer(StringRef S) { DwarfDebugProducer = S; }
  /// Return the dwarf AT_producer string for the compile unit.
  ///
  /// \return The dwarf AT_producer string for the compile unit.
  StringRef getDwarfDebugProducer() { return DwarfDebugProducer; }

  /// Set the dwarf format (DWARF32 or DWARF64) to emit.
  /// \param f Dwarf format to use.
  void setDwarfFormat(dwarf::DwarfFormat f) { DwarfFormat = f; }
  /// Return the dwarf format (DWARF32 or DWARF64) that will be emitted.
  ///
  /// \return The dwarf format (DWARF32 or DWARF64) that will be emitted.
  dwarf::DwarfFormat getDwarfFormat() const { return DwarfFormat; }

  /// Set the maximum dwarf version that should be emitted.
  /// \param v Dwarf version number.
  void setDwarfVersion(uint16_t v) { DwarfVersion = v; }
  /// Return the maximum dwarf version that should be emitted.
  ///
  /// \return The maximum dwarf version that should be emitted.
  uint16_t getDwarfVersion() const { return DwarfVersion; }

  /// @}

  /// Return the secure log file path from AS_SECURE_LOG_FILE.
  ///
  /// \return The secure log file path from AS_SECURE_LOG_FILE.
  StringRef getSecureLogFile() { return SecureLogFile; }
  /// Return the stream written by .secure_log_unique, if any.
  ///
  /// \return The secure log stream, or null if none is set.
  raw_fd_ostream *getSecureLog() { return SecureLog.get(); }

  /// Take ownership of the secure log output stream.
  /// \param Value Stream to use for .secure_log_unique output.
  void setSecureLog(std::unique_ptr<raw_fd_ostream> Value) {
    SecureLog = std::move(Value);
  }

  /// Return whether .secure_log_unique has been used since the last reset.
  ///
  /// \return True if .secure_log_unique has been used since the last reset.
  bool getSecureLogUsed() { return SecureLogUsed; }
  /// Set whether .secure_log_unique has been used since the last reset.
  /// \param Value New secure-log-used flag.
  void setSecureLogUsed(bool Value) { SecureLogUsed = Value; }

  /// Allocate \p Size bytes from the context bump allocator.
  /// \param Size Number of bytes to allocate.
  /// \param Align Required alignment in bytes.
  /// \return A pointer to the allocated memory.
  void *allocate(unsigned Size, unsigned Align = 8) {
    return Allocator.Allocate(Size, Align);
  }

  /// No-op deallocation; memory is released when the context is destroyed.
  /// \param Ptr Pointer previously returned by allocate (unused).
  void deallocate(void *Ptr) {}

  /// Allocates a copy of the given string on the allocator managed by this
  /// context and returns the result.
  /// \param s String to copy into context-owned storage.
  /// \return A StringRef referring to the context-owned copy.
  StringRef allocateString(StringRef s) {
    return StringSaver(Allocator).save(s);
  }

  /// Return true if any error has been reported through this context.
  ///
  /// \return True if any error has been reported through this context.
  bool hadError() { return HadError; }
  /// Forward a source diagnostic through the configured diagnostic handler.
  /// \param SMD Diagnostic to report.
  LLVM_ABI void diagnose(const SMDiagnostic &SMD);
  /// Report an error at \p L with message \p Msg and mark that an error occurred.
  /// \param L Source location of the error.
  /// \param Msg Error message text.
  LLVM_ABI void reportError(SMLoc L, const Twine &Msg);
  /// Report a warning at \p L with message \p Msg.
  /// \param L Source location of the warning.
  /// \param Msg Warning message text.
  LLVM_ABI void reportWarning(SMLoc L, const Twine &Msg);

  /// Look up a previously defined assembler macro by name.
  /// \param Name Macro name to find.
  /// \return Pointer to the macro, or nullptr if not defined.
  MCAsmMacro *lookupMacro(StringRef Name) {
    StringMap<MCAsmMacro>::iterator I = MacroMap.find(Name);
    return (I == MacroMap.end()) ? nullptr : &I->getValue();
  }

  /// Define or replace an assembler macro with the given name.
  /// \param Name Macro name to define.
  /// \param Macro Macro body and parameters to store.
  void defineMacro(StringRef Name, MCAsmMacro Macro) {
    MacroMap.insert(std::make_pair(Name, std::move(Macro)));
  }

  /// Remove a previously defined assembler macro.
  /// \param Name Macro name to undefine.
  void undefineMacro(StringRef Name) { MacroMap.erase(Name); }

  /// Return the pseudo-probe table for the current module.
  ///
  /// \return The pseudo-probe table for the current module.
  MCPseudoProbeTable &getMCPseudoProbeTable() { return PseudoProbeTable; }
};

} // end namespace llvm

// operator new and delete aren't allowed inside namespaces.
// The throw specifications are mandated by the standard.
/// Placement new for using the MCContext's allocator.
///
/// This placement form of operator new uses the MCContext's allocator for
/// obtaining memory. It is a non-throwing new, which means that it returns
/// null on error. (If that is what the allocator does. The current does, so if
/// this ever changes, this operator will have to be changed, too.)
/// Usage looks like this (assuming there's an MCContext 'Context' in scope):
/// \code
/// // Default alignment (8)
/// IntegerLiteral *Ex = new (Context) IntegerLiteral(arguments);
/// // Specific alignment
/// IntegerLiteral *Ex2 = new (Context, 4) IntegerLiteral(arguments);
/// \endcode
/// Please note that you cannot use delete on the pointer; it must be
/// deallocated using an explicit destructor call followed by
/// \c Context.Deallocate(Ptr).
///
/// \param Bytes The number of bytes to allocate. Calculated by the compiler.
/// \param C The MCContext that provides the allocator.
/// \param Alignment The alignment of the allocated memory (if the underlying
///                  allocator supports it).
/// \return The allocated memory. Could be NULL.
inline void *operator new(size_t Bytes, llvm::MCContext &C,
                          size_t Alignment = 8) noexcept {
  return C.allocate(Bytes, Alignment);
}
/// Placement delete companion to the new above.
///
/// This operator is just a companion to the new above. There is no way of
/// invoking it directly; see the new operator for more details. This operator
/// is called implicitly by the compiler if a placement new expression using
/// the MCContext throws in the object constructor.
inline void operator delete(void *Ptr, llvm::MCContext &C, size_t) noexcept {
  C.deallocate(Ptr);
}

/// This placement form of operator new[] uses the MCContext's allocator for
/// obtaining memory. It is a non-throwing new[], which means that it returns
/// null on error.
/// Usage looks like this (assuming there's an MCContext 'Context' in scope):
/// \code
/// // Default alignment (8)
/// char *data = new (Context) char[10];
/// // Specific alignment
/// char *data = new (Context, 4) char[10];
/// \endcode
/// Please note that you cannot use delete on the pointer; it must be
/// deallocated using an explicit destructor call followed by
/// \c Context.Deallocate(Ptr).
///
/// \param Bytes The number of bytes to allocate. Calculated by the compiler.
/// \param C The MCContext that provides the allocator.
/// \param Alignment The alignment of the allocated memory (if the underlying
///                  allocator supports it).
/// \return The allocated memory. Could be NULL.
inline void *operator new[](size_t Bytes, llvm::MCContext &C,
                            size_t Alignment = 8) noexcept {
  return C.allocate(Bytes, Alignment);
}

/// Placement delete[] companion to the new[] above.
///
/// This operator is just a companion to the new[] above. There is no way of
/// invoking it directly; see the new[] operator for more details. This operator
/// is called implicitly by the compiler if a placement new[] expression using
/// the MCContext throws in the object constructor.
inline void operator delete[](void *Ptr, llvm::MCContext &C) noexcept {
  C.deallocate(Ptr);
}

#endif // LLVM_MC_MCCONTEXT_H
