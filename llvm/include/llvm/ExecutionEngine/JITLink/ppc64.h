//===--- ppc64.h - Generic JITLink ppc64 edge kinds, utilities --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing 64-bit PowerPC objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_PPC64_H
#define LLVM_EXECUTIONENGINE_JITLINK_PPC64_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITLink/TableManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"

namespace llvm {
namespace jitlink {
/// JITLink utilities for ppc64 relocatable objects.
namespace ppc64 {

/// Represents ppc64 fixups and other ppc64-specific edge kinds.
enum EdgeKind_ppc64 : Edge::Kind {
  /// A plain 64-bit pointer value relocation.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : uint64
  ///
  Pointer64 = Edge::FirstRelocation,

  /// A plain 32-bit pointer value relocation.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : uint32
  ///
  Pointer32,

  /// A 16-bit pointer value relocation.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : uint16
  ///
  Pointer16,

  /// A 16-bit DS-form pointer value relocation (low 2 bits cleared).
  ///
  /// Fixup expression:
  ///   Fixup <- (Target + Addend) & ~3 : uint16
  ///
  Pointer16DS,

  /// The high-adjusted 16 bits of a pointer value (ha16).
  ///
  /// Fixup expression:
  ///   Fixup <- ha(Target + Addend) : uint16
  ///
  Pointer16HA,

  /// The high 16 bits of a pointer value (hi16).
  ///
  /// Fixup expression:
  ///   Fixup <- hi(Target + Addend) : uint16
  ///
  Pointer16HI,

  /// Bits 16..31 of a pointer value (high).
  ///
  /// Fixup expression:
  ///   Fixup <- high(Target + Addend) : uint16
  ///
  Pointer16HIGH,

  /// Bits 16..31 of a pointer value, high-adjusted (higha).
  ///
  /// Fixup expression:
  ///   Fixup <- higha(Target + Addend) : uint16
  ///
  Pointer16HIGHA,

  /// Bits 32..47 of a pointer value (higher).
  ///
  /// Fixup expression:
  ///   Fixup <- higher(Target + Addend) : uint16
  ///
  Pointer16HIGHER,

  /// Bits 32..47 of a pointer value, high-adjusted (highera).
  ///
  /// Fixup expression:
  ///   Fixup <- highera(Target + Addend) : uint16
  ///
  Pointer16HIGHERA,

  /// Bits 48..63 of a pointer value (highest).
  ///
  /// Fixup expression:
  ///   Fixup <- highest(Target + Addend) : uint16
  ///
  Pointer16HIGHEST,

  /// Bits 48..63 of a pointer value, high-adjusted (highesta).
  ///
  /// Fixup expression:
  ///   Fixup <- highesta(Target + Addend) : uint16
  ///
  Pointer16HIGHESTA,

  /// The low 16 bits of a pointer value (lo16).
  ///
  /// Fixup expression:
  ///   Fixup <- lo(Target + Addend) : uint16
  ///
  Pointer16LO,

  /// The low 16 bits of a pointer value in DS form (lo16, low 2 bits cleared).
  ///
  /// Fixup expression:
  ///   Fixup <- lo(Target + Addend) & ~3 : uint16
  ///
  Pointer16LODS,

  /// A 14-bit pointer field embedded in an instruction word.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : int16 (4-byte aligned, low 14 bits of insn)
  ///
  /// Errors:
  ///   - The result must be 4-byte aligned and fit in an int16, otherwise an
  ///     out-of-range or alignment error will be returned.
  ///
  Pointer14,

  /// A 64-bit delta.
  ///
  /// Delta from the fixup to the target.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int64
  ///
  Delta64,

  /// A 34-bit delta in a prefixed instruction.
  ///
  /// Delta from the fixup to the target, written into a PowerISA 3.1 prefixed
  /// instruction immediate field.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int34
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int34, otherwise
  ///     an out-of-range error will be returned.
  ///
  Delta34,

  /// A 32-bit delta.
  ///
  /// Delta from the fixup to the target.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///
  Delta32,

  /// A 32-bit negative delta.
  ///
  /// Delta from the target back to the fixup.
  ///
  /// Fixup expression:
  ///   Fixup <- Fixup - Target + Addend : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///
  NegDelta32,

  /// A 16-bit delta.
  ///
  /// Delta from the fixup to the target.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int16
  ///
  Delta16,

  /// The high-adjusted 16 bits of a delta (ha16).
  ///
  /// Fixup expression:
  ///   Fixup <- ha(Target - Fixup + Addend) : uint16
  ///
  Delta16HA,

  /// The high 16 bits of a delta (hi16).
  ///
  /// Fixup expression:
  ///   Fixup <- hi(Target - Fixup + Addend) : uint16
  ///
  Delta16HI,

  /// The low 16 bits of a delta (lo16).
  ///
  /// Fixup expression:
  ///   Fixup <- lo(Target - Fixup + Addend) : uint16
  ///
  Delta16LO,

  /// Absolute TOC base address relocation.
  ///
  /// Fixup expression:
  ///   Fixup <- TOCBase : uint64
  ///
  TOC,

  /// A 16-bit TOC-relative delta.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend - TOCBase : int16
  ///
  TOCDelta16,

  /// A 16-bit TOC-relative DS-form delta (low 2 bits cleared).
  ///
  /// Fixup expression:
  ///   Fixup <- (Target + Addend - TOCBase) & ~3 : uint16
  ///
  TOCDelta16DS,

  /// The high-adjusted 16 bits of a TOC-relative delta (ha16).
  ///
  /// Fixup expression:
  ///   Fixup <- ha(Target + Addend - TOCBase) : uint16
  ///
  TOCDelta16HA,

  /// The high 16 bits of a TOC-relative delta (hi16).
  ///
  /// Fixup expression:
  ///   Fixup <- hi(Target + Addend - TOCBase) : uint16
  ///
  TOCDelta16HI,

  /// The low 16 bits of a TOC-relative delta (lo16).
  ///
  /// Fixup expression:
  ///   Fixup <- lo(Target + Addend - TOCBase) : uint16
  ///
  TOCDelta16LO,

  /// The low 16 bits of a TOC-relative DS-form delta (lo16, low 2 bits cleared).
  ///
  /// Fixup expression:
  ///   Fixup <- lo(Target + Addend - TOCBase) & ~3 : uint16
  ///
  TOCDelta16LODS,

  /// Request a GOT entry and transform this edge to Delta34.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestGOTAndTransformToDelta34,

  /// A PC-relative call/branch delta.
  ///
  /// Represents a `bl`/`b` to a target within the signed 26-bit branch range.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int26
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int26, otherwise
  ///     an out-of-range error will be returned.
  ///
  CallBranchDelta,

  /// A PC-relative call/branch delta that also restores the TOC pointer.
  ///
  /// Need to restore r2 after the `bl`, suggesting the `bl` is followed by a
  /// nop that will be rewritten to `ld r2, 24(r1)`.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int26
  ///   Fixup+4 <- ld r2, 24(r1)
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int26, otherwise
  ///     an out-of-range error will be returned.
  ///
  CallBranchDeltaRestoreTOC,

  /// Request a call that uses the TOC calling convention.
  ///
  /// Request calling a function with TOC. Transformed by the PLT/TOC managers
  /// into CallBranchDelta or CallBranchDeltaRestoreTOC before fixup.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestCall,

  /// Request a call that does not use the TOC calling convention.
  ///
  /// Request calling a function without TOC. Transformed by the PLT manager
  /// into CallBranchDelta targeting a LongBranchNoTOC stub before fixup.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestCallNoTOC,

  /// Request a TLS descriptor GOT entry and transform to TOCDelta16HA.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestTLSDescInGOTAndTransformToTOCDelta16HA,

  /// Request a TLS descriptor GOT entry and transform to TOCDelta16LO.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestTLSDescInGOTAndTransformToTOCDelta16LO,

  /// Request a TLS descriptor GOT entry and transform to Delta34.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestTLSDescInGOTAndTransformToDelta34,
};

/// Kind of PLT call stub to emit for an external call.
enum PLTCallStubKind {
  /// Setup function entry (r12) and long branch to target using TOC.
  LongBranch,
  /// Save TOC pointer, setup function entry and long branch to target using
  /// TOC.
  LongBranchSaveR2,
  /// Setup function entry (r12) and long branch to target without using TOC.
  LongBranchNoTOC,
};

/// ppc64 null pointer content (8 zero bytes).
LLVM_ABI extern const char NullPointerContent[8];
/// Big-endian ppc64 pointer jump stub that uses the TOC.
LLVM_ABI extern const char PointerJumpStubContent_big[20];
/// Little-endian ppc64 pointer jump stub that uses the TOC.
LLVM_ABI extern const char PointerJumpStubContent_little[20];
/// Big-endian ppc64 pointer jump stub that does not use the TOC.
LLVM_ABI extern const char PointerJumpStubNoTOCContent_big[32];
/// Little-endian ppc64 pointer jump stub that does not use the TOC.
LLVM_ABI extern const char PointerJumpStubNoTOCContent_little[32];

/// Relocation applied when materializing a PLT call stub.
struct PLTCallStubReloc {
  /// Edge kind used for the stub relocation.
  Edge::Kind K;
  /// Byte offset within the stub content at which to apply the relocation.
  size_t Offset;
  /// Addend for the stub relocation.
  Edge::AddendT A;
};

/// Stub bytes and relocations for a PLT call stub of a given kind.
struct PLTCallStubInfo {
  /// Raw instruction bytes for the stub.
  ArrayRef<char> Content;
  /// Relocations to apply against the stub's pointer/GOT target.
  SmallVector<PLTCallStubReloc, 2> Relocs;
};

/// Return stub content and relocations for \p StubKind in \p Endianness.
/// \param StubKind Which PLT call stub variant to select.
/// \return Stub bytes and the relocations to apply against the stub target.
template <llvm::endianness Endianness>
inline PLTCallStubInfo pickStub(PLTCallStubKind StubKind) {
  constexpr bool isLE = Endianness == llvm::endianness::little;
  switch (StubKind) {
  case LongBranch: {
    ArrayRef<char> Content =
        isLE ? PointerJumpStubContent_little : PointerJumpStubContent_big;
    // Skip save r2.
    Content = Content.slice(4);
    size_t Offset = isLE ? 0 : 2;
    return PLTCallStubInfo{
        Content,
        {{TOCDelta16HA, Offset, 0}, {TOCDelta16LO, Offset + 4, 0}},
    };
  }
  case LongBranchSaveR2: {
    ArrayRef<char> Content =
        isLE ? PointerJumpStubContent_little : PointerJumpStubContent_big;
    size_t Offset = isLE ? 4 : 6;
    return PLTCallStubInfo{
        Content,
        {{TOCDelta16HA, Offset, 0}, {TOCDelta16LO, Offset + 4, 0}},
    };
  }
  case LongBranchNoTOC: {
    ArrayRef<char> Content = isLE ? PointerJumpStubNoTOCContent_little
                                  : PointerJumpStubNoTOCContent_big;
    size_t Offset = isLE ? 16 : 18;
    Edge::AddendT Addend = isLE ? 8 : 10;
    return PLTCallStubInfo{
        Content,
        {{Delta16HA, Offset, Addend}, {Delta16LO, Offset + 4, Addend + 4}},
    };
  }
  }
  llvm_unreachable("Unknown PLTCallStubKind enum");
}

/// Create a new pointer block in the given section and return an anonymous
/// symbol pointing to it.
///
/// If InitialTarget is given then a Pointer64 relocation will be added to the
/// block pointing at InitialTarget.
/// \param G Link graph to create the pointer in.
/// \param PointerSection Section that will hold the pointer block.
/// \param InitialTarget Optional symbol for an initial Pointer64 edge.
/// \param InitialAddend Addend for the optional initial Pointer64 edge.
/// \return An anonymous symbol pointing at the new pointer block.
inline Symbol &createAnonymousPointer(LinkGraph &G, Section &PointerSection,
                                      Symbol *InitialTarget = nullptr,
                                      uint64_t InitialAddend = 0) {
  assert(G.getPointerSize() == sizeof(NullPointerContent) &&
         "LinkGraph's pointer size should be consistent with size of "
         "NullPointerContent");
  Block &B = G.createContentBlock(PointerSection, NullPointerContent,
                                  orc::ExecutorAddr(), G.getPointerSize(), 0);
  if (InitialTarget)
    B.addEdge(Pointer64, 0, *InitialTarget, InitialAddend);
  return G.addAnonymousSymbol(B, 0, G.getPointerSize(), false, false);
}

/// Create a jump stub that jumps via the pointer at the given symbol and
/// return an anonymous symbol pointing to it.
/// \param G Link graph to create the stub in.
/// \param StubSection Section that will hold the stub.
/// \param PointerSymbol Symbol of the in-memory pointer to jump through.
/// \param StubKind Which PLT call stub variant to emit.
/// \return An anonymous symbol pointing at the new jump stub.
template <llvm::endianness Endianness>
inline Symbol &createAnonymousPointerJumpStub(LinkGraph &G,
                                              Section &StubSection,
                                              Symbol &PointerSymbol,
                                              PLTCallStubKind StubKind) {
  PLTCallStubInfo StubInfo = pickStub<Endianness>(StubKind);
  Block &B = G.createContentBlock(StubSection, StubInfo.Content,
                                  orc::ExecutorAddr(), 4, 0);
  for (auto const &Reloc : StubInfo.Relocs)
    B.addEdge(Reloc.K, Reloc.Offset, PointerSymbol, Reloc.A);
  return G.addAnonymousSymbol(B, 0, StubInfo.Content.size(), true, false);
}

/// Create a default anonymous pointer jump stub for an external call.
///
/// LongBranchSaveR2 is the default for external calls: saves the TOC
/// pointer (r2) before branching, as required when the callee sets its
/// own TOC. Callers needing a different stub kind (e.g. LongBranchNoTOC)
/// should call createAnonymousPointerJumpStub directly with the desired
/// PLTCallStubKind.
/// \param G Link graph to create the stub in.
/// \param StubSection Section that will hold the stub.
/// \param PointerSymbol Symbol of the in-memory pointer to jump through.
/// \return An anonymous symbol pointing at the new jump stub.
template <llvm::endianness Endianness>
inline Symbol &createDefaultAnonymousPointerJumpStub(LinkGraph &G,
                                                     Section &StubSection,
                                                     Symbol &PointerSymbol) {
  return createAnonymousPointerJumpStub<Endianness>(
      G, StubSection, PointerSymbol, LongBranchSaveR2);
}

/// Table-of-contents / GOT builder for ppc64.
template <llvm::endianness Endianness>
class TOCTableManager : public TableManager<TOCTableManager<Endianness>> {
public:
  /// Return the name of the TOC/GOT section.
  ///
  /// FIXME: `llvm-jitlink -check` relies this name to be $__GOT.
  /// \return The section name string "$__GOT".
  static StringRef getSectionName() { return "$__GOT"; }

  /// Visit an edge and ensure TOC/GOT entries exist, or transform GOT requests.
  /// \param G Link graph being processed.
  /// \param B Block containing the edge.
  /// \param E Edge that may reference the TOC or request a GOT entry.
  /// \return True if the edge was transformed.
  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    Edge::Kind K = E.getKind();
    switch (K) {
    case TOCDelta16HA:
    case TOCDelta16LO:
    case TOCDelta16DS:
    case TOCDelta16LODS:
    case CallBranchDeltaRestoreTOC:
    case RequestCall:
      // Create TOC section if TOC relocation, PLT or GOT is used.
      getOrCreateTOCSection(G);
      return false;
    case RequestGOTAndTransformToDelta34:
      E.setKind(ppc64::Delta34);
      E.setTarget(createEntry(G, E.getTarget()));
      return true;
    default:
      return false;
    }
  }

  /// Create a TOC/GOT entry pointing at \p Target.
  /// \param G Link graph to create the entry in.
  /// \param Target Symbol that the TOC/GOT entry should reference.
  /// \return An anonymous symbol pointing at the new TOC/GOT entry.
  Symbol &createEntry(LinkGraph &G, Symbol &Target) {
    return createAnonymousPointer(G, getOrCreateTOCSection(G), &Target);
  }

private:
  Section &getOrCreateTOCSection(LinkGraph &G) {
    TOCSection = G.findSectionByName(getSectionName());
    if (!TOCSection)
      TOCSection = &G.createSection(getSectionName(), orc::MemProt::Read);
    return *TOCSection;
  }

  Section *TOCSection = nullptr;
};

/// Procedure linkage table builder for ppc64.
template <llvm::endianness Endianness>
class PLTTableManager : public TableManager<PLTTableManager<Endianness>> {
public:
  /// Construct a PLT table manager using \p TOC for stub pointer targets.
  /// \param TOC TOC table manager used to resolve stub pointer targets.
  PLTTableManager(TOCTableManager<Endianness> &TOC) : TOC(TOC) {}

  /// Return the name of the stubs section.
  /// \return The section name string "$__STUBS".
  static StringRef getSectionName() { return "$__STUBS"; }

  /// Visit an edge and redirect external calls through a PLT stub.
  ///
  /// FIXME: One external symbol can only have one PLT stub in a object file.
  /// This is a limitation when we need different PLT stubs for the same symbol.
  /// For example, we need two different PLT stubs for `bl __tls_get_addr` and
  /// `bl __tls_get_addr@notoc`.
  /// \param G Link graph being processed.
  /// \param B Block containing the edge.
  /// \param E Edge that may need a PLT stub.
  /// \return True if the edge was redirected through a stub.
  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    bool isExternal = E.getTarget().isExternal();
    Edge::Kind K = E.getKind();
    if (K == ppc64::RequestCall) {
      if (isExternal) {
        E.setKind(ppc64::CallBranchDeltaRestoreTOC);
        this->StubKind = LongBranchSaveR2;
        // FIXME: We assume the addend to the external target is zero. It's
        // quite unusual that the addend of an external target to be non-zero as
        // if we have known the layout of the external object.
        E.setTarget(this->getEntryForTarget(G, E.getTarget()));
        // Addend to the stub is zero.
        E.setAddend(0);
      } else
        // TODO: There are cases a local function call need a call stub.
        // 1. Caller uses TOC, the callee doesn't, need a r2 save stub.
        // 2. Caller doesn't use TOC, the callee does, need a r12 setup stub.
        // 3. Branching target is out of range.
        E.setKind(ppc64::CallBranchDelta);
      return true;
    }
    if (K == ppc64::RequestCallNoTOC) {
      E.setKind(ppc64::CallBranchDelta);
      this->StubKind = LongBranchNoTOC;
      E.setTarget(this->getEntryForTarget(G, E.getTarget()));
      return true;
    }
    return false;
  }

  /// Create a PLT stub that jumps to \p Target via a TOC/GOT entry.
  /// \param G Link graph to create the stub in.
  /// \param Target External symbol that the stub should reach.
  /// \return An anonymous symbol pointing at the new stub.
  Symbol &createEntry(LinkGraph &G, Symbol &Target) {
    return createAnonymousPointerJumpStub<Endianness>(
        G, getOrCreateStubsSection(G), TOC.getEntryForTarget(G, Target),
        this->StubKind);
  }

private:
  Section &getOrCreateStubsSection(LinkGraph &G) {
    PLTSection = G.findSectionByName(getSectionName());
    if (!PLTSection)
      PLTSection = &G.createSection(getSectionName(),
                                    orc::MemProt::Read | orc::MemProt::Exec);
    return *PLTSection;
  }

  TOCTableManager<Endianness> &TOC;
  Section *PLTSection = nullptr;
  PLTCallStubKind StubKind;
};

/// Returns a string name for the given ppc64 edge. For debugging purposes
/// only.
/// \param K Edge kind to name.
/// \return A human-readable name for \p K.
LLVM_ABI const char *getEdgeKindName(Edge::Kind K);

inline static uint16_t ha(uint64_t x) { return (x + 0x8000) >> 16; }
inline static uint64_t lo(uint64_t x) { return x & 0xffff; }
inline static uint16_t hi(uint64_t x) { return x >> 16; }
inline static uint64_t high(uint64_t x) { return (x >> 16) & 0xffff; }
inline static uint64_t higha(uint64_t x) {
  return ((x + 0x8000) >> 16) & 0xffff;
}
inline static uint64_t higher(uint64_t x) { return (x >> 32) & 0xffff; }
inline static uint64_t highera(uint64_t x) {
  return ((x + 0x8000) >> 32) & 0xffff;
}
inline static uint16_t highest(uint64_t x) { return x >> 48; }
inline static uint16_t highesta(uint64_t x) { return (x + 0x8000) >> 48; }

/// Read a PowerISA 3.1 prefixed instruction from \p Loc.
///
/// Prefixed instruction introduced in ISAv3.1 consists of two 32-bit words,
/// prefix word and suffix word, i.e., prefixed_instruction =
/// concat(prefix_word, suffix_word). That's to say, for a prefixed instruction
/// encoded in uint64_t, the most significant 32 bits belong to the prefix word.
/// The prefix word is at low address for both big/little endian. Byte order in
/// each word still follows its endian.
/// \param Loc Address of the prefixed instruction in object memory.
/// \return The prefixed instruction as concat(prefix_word, suffix_word).
template <llvm::endianness Endianness>
inline static uint64_t readPrefixedInstruction(const char *Loc) {
  constexpr bool isLE = Endianness == llvm::endianness::little;
  uint64_t Inst = support::endian::read64<Endianness>(Loc);
  return isLE ? (Inst << 32) | (Inst >> 32) : Inst;
}

/// Write a PowerISA 3.1 prefixed instruction \p Inst to \p Loc.
/// \param Loc Address at which to store the prefixed instruction.
/// \param Inst Prefixed instruction encoded as concat(prefix, suffix).
template <llvm::endianness Endianness>
inline static void writePrefixedInstruction(char *Loc, uint64_t Inst) {
  constexpr bool isLE = Endianness == llvm::endianness::little;
  Inst = isLE ? (Inst << 32) | (Inst >> 32) : Inst;
  support::endian::write64<Endianness>(Loc, Inst);
}

/// Write a 16-bit half16 relocation field for edge kind \p K.
/// \param FixupPtr Address of the half16 field to update.
/// \param Value Relocated value before half16 extraction.
/// \param K Edge kind selecting which half16 extraction to apply.
/// \return Success, or an error if \p K does not write a half16 field.
template <llvm::endianness Endianness>
inline Error relocateHalf16(char *FixupPtr, int64_t Value, Edge::Kind K) {
  switch (K) {
  case Delta16:
  case Pointer16:
  case TOCDelta16:
    support::endian::write16<Endianness>(FixupPtr, Value);
    break;
  case Pointer16DS:
  case TOCDelta16DS:
    support::endian::write16<Endianness>(FixupPtr, Value & ~3);
    break;
  case Delta16HA:
  case Pointer16HA:
  case TOCDelta16HA:
    support::endian::write16<Endianness>(FixupPtr, ha(Value));
    break;
  case Delta16HI:
  case Pointer16HI:
  case TOCDelta16HI:
    support::endian::write16<Endianness>(FixupPtr, hi(Value));
    break;
  case Pointer16HIGH:
    support::endian::write16<Endianness>(FixupPtr, high(Value));
    break;
  case Pointer16HIGHA:
    support::endian::write16<Endianness>(FixupPtr, higha(Value));
    break;
  case Pointer16HIGHER:
    support::endian::write16<Endianness>(FixupPtr, higher(Value));
    break;
  case Pointer16HIGHERA:
    support::endian::write16<Endianness>(FixupPtr, highera(Value));
    break;
  case Pointer16HIGHEST:
    support::endian::write16<Endianness>(FixupPtr, highest(Value));
    break;
  case Pointer16HIGHESTA:
    support::endian::write16<Endianness>(FixupPtr, highesta(Value));
    break;
  case Delta16LO:
  case Pointer16LO:
  case TOCDelta16LO:
    support::endian::write16<Endianness>(FixupPtr, lo(Value));
    break;
  case Pointer16LODS:
  case TOCDelta16LODS:
    support::endian::write16<Endianness>(FixupPtr, lo(Value) & ~3);
    break;
  default:
    return make_error<JITLinkError>(
        StringRef(getEdgeKindName(K)) +
        " relocation does not write at half16 field");
  }
  return Error::success();
}

/// Apply fixup expression for edge to block content.
/// \param G Link graph containing the block.
/// \param B Block whose content should be fixed up.
/// \param E Edge describing the fixup to apply.
/// \param TOCSymbol Optional TOC section symbol for TOC-relative fixups.
/// \return Success, or an error if the fixup is out of range or unsupported.
template <llvm::endianness Endianness>
inline Error applyFixup(LinkGraph &G, Block &B, const Edge &E,
                        const Symbol *TOCSymbol) {
  char *BlockWorkingMem = B.getAlreadyMutableContent().data();
  char *FixupPtr = BlockWorkingMem + E.getOffset();
  orc::ExecutorAddr FixupAddress = B.getAddress() + E.getOffset();
  int64_t S = E.getTarget().getAddress().getValue();
  int64_t A = E.getAddend();
  int64_t P = FixupAddress.getValue();
  int64_t TOCBase = TOCSymbol ? TOCSymbol->getAddress().getValue() : 0;
  Edge::Kind K = E.getKind();

  DEBUG_WITH_TYPE("jitlink", {
    dbgs() << "    Applying fixup on " << G.getEdgeKindName(K)
           << " edge, (S, A, P, .TOC.) = (" << formatv("{0:x}", S) << ", "
           << formatv("{0:x}", A) << ", " << formatv("{0:x}", P) << ", "
           << formatv("{0:x}", TOCBase) << ")\n";
  });

  switch (K) {
  case Pointer64: {
    uint64_t Value = S + A;
    support::endian::write64<Endianness>(FixupPtr, Value);
    break;
  }
  case Delta16:
  case Delta16HA:
  case Delta16HI:
  case Delta16LO: {
    int64_t Value = S + A - P;
    if (LLVM_UNLIKELY(!isInt<32>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    return relocateHalf16<Endianness>(FixupPtr, Value, K);
  }
  case TOC:
    support::endian::write64<Endianness>(FixupPtr, TOCBase);
    break;
  case Pointer16:
  case Pointer16DS:
  case Pointer16HA:
  case Pointer16HI:
  case Pointer16HIGH:
  case Pointer16HIGHA:
  case Pointer16HIGHER:
  case Pointer16HIGHERA:
  case Pointer16HIGHEST:
  case Pointer16HIGHESTA:
  case Pointer16LO:
  case Pointer16LODS: {
    uint64_t Value = S + A;
    if (LLVM_UNLIKELY(!isInt<32>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    return relocateHalf16<Endianness>(FixupPtr, Value, K);
  }
  case Pointer14: {
    static const uint32_t Low14Mask = 0xfffc;
    uint64_t Value = S + A;
    assert((Value & 3) == 0 && "Pointer14 requires 4-byte alignment");
    if (LLVM_UNLIKELY(!isInt<16>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    uint32_t Inst = support::endian::read32<Endianness>(FixupPtr);
    support::endian::write32<Endianness>(FixupPtr, (Inst & ~Low14Mask) |
                                                       (Value & Low14Mask));
    break;
  }
  case TOCDelta16:
  case TOCDelta16DS:
  case TOCDelta16HA:
  case TOCDelta16HI:
  case TOCDelta16LO:
  case TOCDelta16LODS: {
    int64_t Value = S + A - TOCBase;
    if (LLVM_UNLIKELY(!isInt<32>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    return relocateHalf16<Endianness>(FixupPtr, Value, K);
  }
  case CallBranchDeltaRestoreTOC:
  case CallBranchDelta: {
    int64_t Value = S + A - P;
    if (LLVM_UNLIKELY(!isInt<26>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    uint32_t Inst = support::endian::read32<Endianness>(FixupPtr);
    support::endian::write32<Endianness>(FixupPtr, (Inst & 0xfc000003) |
                                                       (Value & 0x03fffffc));
    if (K == CallBranchDeltaRestoreTOC) {
      uint32_t NopInst = support::endian::read32<Endianness>(FixupPtr + 4);
      assert(NopInst == 0x60000000 &&
             "NOP should be placed here for restoring r2");
      (void)NopInst;
      // Restore r2 by instruction 0xe8410018 which is `ld r2, 24(r1)`.
      support::endian::write32<Endianness>(FixupPtr + 4, 0xe8410018);
    }
    break;
  }
  case Delta64: {
    int64_t Value = S + A - P;
    support::endian::write64<Endianness>(FixupPtr, Value);
    break;
  }
  case Delta34: {
    int64_t Value = S + A - P;
    if (!LLVM_UNLIKELY(isInt<34>(Value)))
      return makeTargetOutOfRangeError(G, B, E);
    static const uint64_t SI0Mask = 0x00000003ffff0000;
    static const uint64_t SI1Mask = 0x000000000000ffff;
    static const uint64_t FullMask = 0x0003ffff0000ffff;
    uint64_t Inst = readPrefixedInstruction<Endianness>(FixupPtr) & ~FullMask;
    writePrefixedInstruction<Endianness>(
        FixupPtr, Inst | ((Value & SI0Mask) << 16) | (Value & SI1Mask));
    break;
  }
  case Delta32: {
    int64_t Value = S + A - P;
    if (LLVM_UNLIKELY(!isInt<32>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    support::endian::write32<Endianness>(FixupPtr, Value);
    break;
  }
  case NegDelta32: {
    int64_t Value = P - S + A;
    if (LLVM_UNLIKELY(!isInt<32>(Value))) {
      return makeTargetOutOfRangeError(G, B, E);
    }
    support::endian::write32<Endianness>(FixupPtr, Value);
    break;
  }
  default:
    return make_error<JITLinkError>(
        "In graph " + G.getName() + ", section " + B.getSection().getName() +
        " unsupported edge kind " + getEdgeKindName(E.getKind()));
  }
  return Error::success();
}

} // namespace ppc64
} // namespace jitlink
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_PPC64_H
