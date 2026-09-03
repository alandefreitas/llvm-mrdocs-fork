//= loongarch.h - Generic JITLink loongarch edge kinds, utilities -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing loongarch objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_LOONGARCH_H
#define LLVM_EXECUTIONENGINE_JITLINK_LOONGARCH_H

#include "TableManager.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/Orc/Shared/MemoryFlags.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/LEB128.h"

namespace llvm {
namespace jitlink {
/// JITLink utilities for loongarch relocatable objects.
namespace loongarch {

/// Represents loongarch fixups.
enum EdgeKind_loongarch : Edge::Kind {
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
  /// Errors:
  ///   - The target must reside in the low 32-bits of the address space,
  ///     otherwise an out-of-range error will be returned.
  ///
  Pointer32,

  /// A 16-bit PC-relative branch.
  ///
  /// Represents a PC-relative branch to a target within +/-128Kb. The target
  /// must be 4-byte aligned.
  ///
  /// Fixup expression:
  ///   Fixup <- (Target - Fixup + Addend) >> 2 : int16
  ///
  /// Notes:
  ///   The '16' in the name refers to the number operand bits and follows the
  /// naming convention used by the corresponding ELF relocations. Since the low
  /// two bits must be zero (because of the 4-byte alignment of the target) the
  /// operand is effectively a signed 18-bit number.
  ///
  /// Errors:
  ///   - The result of the unshifted part of the fixup expression must be
  ///     4-byte aligned otherwise an alignment error will be returned.
  ///   - The result of the fixup expression must fit into an int16 otherwise an
  ///     out-of-range error will be returned.
  ///
  Branch16PCRel,

  /// A 21-bit PC-relative branch.
  ///
  /// Represents a PC-relative branch to a target within +/-4Mb. The Target must
  /// be 4-byte aligned.
  ///
  /// Fixup expression:
  ///   Fixup <- (Target - Fixup + Addend) >> 2 : int21
  ///
  /// Notes:
  ///   The '21' in the name refers to the number operand bits and follows the
  /// naming convention used by the corresponding ELF relocations. Since the low
  /// two bits must be zero (because of the 4-byte alignment of the target) the
  /// operand is effectively a signed 23-bit number.
  ///
  /// Errors:
  ///   - The result of the unshifted part of the fixup expression must be
  ///     4-byte aligned otherwise an alignment error will be returned.
  ///   - The result of the fixup expression must fit into an int21 otherwise an
  ///     out-of-range error will be returned.
  ///
  Branch21PCRel,

  /// A 26-bit PC-relative branch.
  ///
  /// Represents a PC-relative call or branch to a target within +/-128Mb. The
  /// target must be 4-byte aligned.
  ///
  /// Fixup expression:
  ///   Fixup <- (Target - Fixup + Addend) >> 2 : int26
  ///
  /// Notes:
  ///   The '26' in the name refers to the number operand bits and follows the
  /// naming convention used by the corresponding ELF relocations. Since the low
  /// two bits must be zero (because of the 4-byte alignment of the target) the
  /// operand is effectively a signed 28-bit number.
  ///
  /// Errors:
  ///   - The result of the unshifted part of the fixup expression must be
  ///     4-byte aligned otherwise an alignment error will be returned.
  ///   - The result of the fixup expression must fit into an int26 otherwise an
  ///     out-of-range error will be returned.
  ///
  Branch26PCRel,

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

  /// A 64-bit delta.
  ///
  /// Delta from the fixup to the target.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int64
  ///
  Delta64,

  /// The signed 20-bit delta from the fixup page to the page containing the
  /// target.
  ///
  /// Fixup expression:
  ///   Fixup <- (((Target + Addend + ((Target + Addend) & 0x800)) & ~0xfff)
  //              - (Fixup & ~0xfff)) >> 12 : int20
  ///
  /// Notes:
  ///   For PCALAU12I fixups.
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int20 otherwise an
  ///     out-of-range error will be returned.
  ///
  Page20,

  /// The 12-bit offset of the target within its page.
  ///
  /// Typically used to fix up ADDI/LD_W/LD_D immediates.
  ///
  /// Fixup expression:
  ///   Fixup <- ((Target + Addend) >> Shift) & 0xfff : int12
  ///
  PageOffset12,

  /// The upper 20 bits of the offset from the fixup to the target.
  ///
  /// Fixup expression:
  ///   Fixup <- (Target + Addend - Fixup + 0x800) >> 12 : int20
  ///
  /// Notes:
  ///   For PCADDU12I fixups.
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int20 otherwise an
  ///     out-of-range error will be returned.
  ///
  PCAddHi20,

  /// The lower 12 bits of the offset from the paired PCADDU12I (the initial
  /// target) to the final target it points to.
  ///
  /// Typically used to fix up ADDI/LD_W/LD_D immediates.
  ///
  /// Fixup expression:
  ///   Fixup <- (FinalTarget - InitialTarget) & 0xfff : int12
  ///
  PCAddLo12,

  /// A GOT entry getter/constructor, transformed to Page20 pointing at the GOT
  /// entry for the original target.
  ///
  /// Indicates that this edge should be transformed into a Page20 targeting
  /// the GOT entry for the edge's current target, maintaining the same addend.
  /// A GOT entry for the target should be created if one does not already
  /// exist.
  ///
  /// Edges of this kind are usually handled by a GOT/PLT builder pass inserted
  /// by default.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestGOTAndTransformToPage20,

  /// A GOT entry getter/constructor, transformed to Pageoffset12 pointing at
  /// the GOT entry for the original target.
  ///
  /// Indicates that this edge should be transformed into a PageOffset12
  /// targeting the GOT entry for the edge's current target, maintaining the
  /// same addend. A GOT entry for the target should be created if one does not
  /// already exist.
  ///
  /// Edges of this kind are usually handled by a GOT/PLT builder pass inserted
  /// by default.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  RequestGOTAndTransformToPageOffset12,

  /// A GOT entry getter/constructor, transformed to PCAddHi20 pointing at the
  /// GOT entry for the original target.
  ///
  /// Indicates that this edge should be transformed into a PCAddHi20 targeting
  /// the GOT entry for the edge's current target, maintaining the same addend.
  /// A GOT entry for the target should be created if one does not already
  /// exist.
  ///
  /// Edges of this kind are usually handled by a GOT/PLT builder pass inserted
  /// by default.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestGOTAndTransformToPCAddHi20,

  /// A 30-bit PC-relative call.
  ///
  /// Represents a PC-relative call to a target within [-2G, +2G)
  /// The target must be 4-byte aligned. For adjacent pcaddu12i+jirl
  /// instruction pairs.
  ///
  /// Fixup expression:
  ///   Fixup <- (Target - Fixup + Addend) >> 2 : int30
  ///
  /// Notes:
  ///   The '30' in the name refers to the number operand bits and follows the
  /// naming convention used by the corresponding ELF relocations. Since the low
  /// two bits must be zero (because of the 4-byte alignment of the target) the
  /// operand is effectively a signed 32-bit number.
  ///
  /// Errors:
  ///   - The result of the unshifted part of the fixup expression must be
  ///     4-byte aligned otherwise an alignment error will be returned.
  ///   - The result of the fixup expression must fit into an int30 otherwise an
  ///     out-of-range error will be returned.
  ///
  Call30PCRel,

  /// A 36-bit PC-relative call.
  ///
  /// Represents a PC-relative call to a target within [-128G - 0x20000, +128G
  /// - 0x20000). The target must be 4-byte aligned. For adjacent pcaddu18i+jirl
  /// instruction pairs.
  ///
  /// Fixup expression:
  ///   Fixup <- (Target - Fixup + Addend) >> 2 : int36
  ///
  /// Notes:
  ///   The '36' in the name refers to the number operand bits and follows the
  /// naming convention used by the corresponding ELF relocations. Since the low
  /// two bits must be zero (because of the 4-byte alignment of the target) the
  /// operand is effectively a signed 38-bit number.
  ///
  /// Errors:
  ///   - The result of the unshifted part of the fixup expression must be
  ///     4-byte aligned otherwise an alignment error will be returned.
  ///   - The result of the fixup expression must fit into an int36 otherwise an
  ///     out-of-range error will be returned.
  ///
  Call36PCRel,

  /// low 6 bits label addition
  ///
  /// Fixup expression:
  ///   Fixup <- (*{1}Fixup + (Target + Addend) & 0x3f) : int8
  ///
  Add6,

  /// 8 bits label addition
  ///
  /// Fixup expression:
  ///   Fixup <- (*{1}Fixup + Target + Addend) : int8
  ///
  Add8,

  /// 16 bits label addition
  ///
  /// Fixup expression:
  ///   Fixup <- (*{2}Fixup + Target + Addend) : int16
  ///
  Add16,

  /// 32 bits label addition
  ///
  /// Fixup expression:
  ///   Fixup <- (*{4}Fixup + Target + Addend) : int32
  ///
  Add32,

  /// 64 bits label addition
  ///
  /// Fixup expression:
  ///   Fixup <- (*{8}Fixup + Target + Addend) : int64
  ///
  Add64,

  /// ULEB128 bits label addition
  ///
  /// Fixup expression:
  ///   Fixup <- (Fixup + Target + Addend) : uleb128
  ///
  AddUleb128,

  /// low 6 bits label subtraction
  ///
  /// Fixup expression:
  ///   Fixup <- (*{1}Fixup - (Target + Addend) & 0x3f) : int8
  ///
  Sub6,

  /// 8 bits label subtraction
  ///
  /// Fixup expression:
  ///   Fixup <- (*{1}Fixup - Target - Addend) : int8
  ///
  Sub8,

  /// 16 bits label subtraction
  ///
  /// Fixup expression:
  ///   Fixup <- (*{2}Fixup - Target - Addend) : int16
  ///
  Sub16,

  /// 32 bits label subtraction
  ///
  /// Fixup expression:
  ///   Fixup <- (*{4}Fixup - Target - Addend) : int32
  ///
  Sub32,

  /// 64 bits label subtraction
  ///
  /// Fixup expression:
  ///   Fixup <- (*{8}Fixup - Target - Addend) : int64
  ///
  Sub64,

  /// ULEB128 bits label subtraction
  ///
  /// Fixup expression:
  ///   Fixup <- (Fixup - Target - Addend) : uleb128
  ///
  SubUleb128,

  /// Alignment requirement used by linker relaxation.
  ///
  /// Linker relaxation will use this to ensure all code sequences are properly
  /// aligned and then remove these edges from the graph.
  ///
  AlignRelaxable,
};

/// Returns a string name for the given loongarch edge. For debugging purposes
/// only.
/// \param K Edge kind to name.
/// \return A human-readable name for \p K.
LLVM_ABI const char *getEdgeKindName(Edge::Kind K);

/// Extract bits \p Val[\p Hi:\p Lo] as an unsigned value.
/// \param Val Value to extract bits from.
/// \param Hi Inclusive high bit index (0–63).
/// \param Lo Inclusive low bit index (0–63).
/// \return The extracted bit field, right-shifted to bit 0.
inline uint32_t extractBits(uint64_t Val, unsigned Hi, unsigned Lo) {
  return Hi == 63 ? Val >> Lo : (Val & ((((uint64_t)1 << (Hi + 1)) - 1))) >> Lo;
}

/// loongarch null pointer content.
LLVM_ABI extern const char NullPointerContent[8];
/// Return null-pointer bytes sized for a GOT entry in \p G.
/// \param G Link graph whose pointer size selects the GOT entry width.
/// \return Null-pointer content spanning \p G's pointer size.
inline ArrayRef<char> getGOTEntryBlockContent(LinkGraph &G) {
  return {reinterpret_cast<const char *>(NullPointerContent),
          G.getPointerSize()};
}

/// Size in bytes of a loongarch pointer-jump stub.
constexpr size_t StubEntrySize = 12;

/// LA64 stub content for an indirect jump via an in-memory pointer.
///
/// Contains the instruction sequence:
///   pcalau12i $t8, %page20(ptr)
///   ld.d      $t8, %pageoff12(ptr)
///   jr        $t8
LLVM_ABI extern const uint8_t LA64StubContent[StubEntrySize];

/// LA32 stub content for an indirect jump via an in-memory pointer.
///
/// Contains the instruction sequence:
///   pcalau12i $t8, %page20(ptr)
///   ld.w      $t8, %pageoff12(ptr)
///   jr        $t8
LLVM_ABI extern const uint8_t LA32StubContent[StubEntrySize];

/// Return the pointer-jump stub content for the architecture of \p G.
/// \param G Link graph whose pointer size selects LA64 or LA32 stub content.
/// \return LA64 or LA32 stub bytes for \p G's pointer size.
inline ArrayRef<char> getStubBlockContent(LinkGraph &G) {
  auto StubContent =
      G.getPointerSize() == 8 ? LA64StubContent : LA32StubContent;
  return {reinterpret_cast<const char *>(StubContent), StubEntrySize};
}

/// Creates a new pointer block in the given section and returns an
/// Anonymous symbol pointing to it.
///
/// If InitialTarget is given then an Pointer64 relocation will be added to the
/// block pointing at InitialTarget.
///
/// The pointer block will have the following default values:
///   alignment: PointerSize
///   alignment-offset: 0
/// \param G Link graph to create the pointer in.
/// \param PointerSection Section that will hold the pointer block.
/// \param InitialTarget Optional symbol for an initial pointer edge.
/// \param InitialAddend Addend for the optional initial pointer edge.
/// \return An anonymous symbol pointing at the new pointer block.
inline Symbol &createAnonymousPointer(LinkGraph &G, Section &PointerSection,
                                      Symbol *InitialTarget = nullptr,
                                      uint64_t InitialAddend = 0) {
  auto &B = G.createContentBlock(PointerSection, getGOTEntryBlockContent(G),
                                 orc::ExecutorAddr(), G.getPointerSize(), 0);
  if (InitialTarget)
    B.addEdge(G.getPointerSize() == 8 ? Pointer64 : Pointer32, 0,
              *InitialTarget, InitialAddend);
  return G.addAnonymousSymbol(B, 0, G.getPointerSize(), false, false);
}

/// Create a jump stub that jumps via the pointer at the given symbol and
/// an anonymous symbol pointing to it. Return the anonymous symbol.
/// \param G Link graph to create the stub in.
/// \param StubSection Section that will hold the stub.
/// \param PointerSymbol Symbol of the in-memory pointer to jump through.
/// \return An anonymous symbol pointing at the new jump stub.
inline Symbol &createAnonymousPointerJumpStub(LinkGraph &G,
                                              Section &StubSection,
                                              Symbol &PointerSymbol) {
  Block &StubContentBlock = G.createContentBlock(
      StubSection, getStubBlockContent(G), orc::ExecutorAddr(), 4, 0);
  Symbol &StubSymbol =
      G.addAnonymousSymbol(StubContentBlock, 0, StubEntrySize, true, false);
  StubContentBlock.addEdge(G.getPointerSize() == 8 ? Page20 : PCAddHi20, 0,
                           PointerSymbol, 0);
  StubContentBlock.addEdge(
      G.getPointerSize() == 8 ? PageOffset12 : PCAddLo12, 4,
      G.getPointerSize() == 8 ? PointerSymbol : StubSymbol, 0);
  return StubSymbol;
}

/// Global Offset Table Builder.
class GOTTableManager : public TableManager<GOTTableManager> {
public:
  /// Return the name of the GOT section.
  /// \return The section name string "$__GOT".
  static StringRef getSectionName() { return "$__GOT"; }

  /// Visit an edge and transform GOT request edges into real fixups.
  /// \param G Link graph being processed.
  /// \param B Block containing the edge.
  /// \param E Edge that may request a GOT entry.
  /// \return True if the edge was transformed.
  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    Edge::Kind KindToSet = Edge::Invalid;
    switch (E.getKind()) {
    case RequestGOTAndTransformToPage20:
      KindToSet = Page20;
      break;
    case RequestGOTAndTransformToPageOffset12:
      KindToSet = PageOffset12;
      break;
    case RequestGOTAndTransformToPCAddHi20:
      KindToSet = PCAddHi20;
      break;
    default:
      return false;
    }
    assert(KindToSet != Edge::Invalid &&
           "Fell through switch, but no new kind to set");
    DEBUG_WITH_TYPE("jitlink", {
      dbgs() << "  Fixing " << G.getEdgeKindName(E.getKind()) << " edge at "
             << B->getFixupAddress(E) << " (" << B->getAddress() << " + "
             << formatv("{0:x}", E.getOffset()) << ")\n";
    });
    E.setKind(KindToSet);
    E.setTarget(getEntryForTarget(G, E.getTarget()));
    return true;
  }

  /// Create a GOT entry pointing at \p Target.
  /// \param G Link graph to create the entry in.
  /// \param Target Symbol that the GOT entry should reference.
  /// \return An anonymous symbol pointing at the new GOT entry.
  Symbol &createEntry(LinkGraph &G, Symbol &Target) {
    return createAnonymousPointer(G, getGOTSection(G), &Target);
  }

private:
  Section &getGOTSection(LinkGraph &G) {
    if (!GOTSection)
      GOTSection = &G.createSection(getSectionName(),
                                    orc::MemProt::Read | orc::MemProt::Exec);
    return *GOTSection;
  }

  Section *GOTSection = nullptr;
};

/// Procedure Linkage Table Builder.
class PLTTableManager : public TableManager<PLTTableManager> {
public:
  /// Construct a PLT table manager using \p GOT for stub targets.
  /// \param GOT GOT table manager used to resolve stub pointer targets.
  PLTTableManager(GOTTableManager &GOT) : GOT(GOT) {}

  /// Return the name of the stubs section.
  /// \return The section name string "$__STUBS".
  static StringRef getSectionName() { return "$__STUBS"; }

  /// Visit an edge and redirect external branches/calls through a PLT stub.
  /// \param G Link graph being processed.
  /// \param B Block containing the edge.
  /// \param E Edge that may need a PLT stub.
  /// \return True if the edge was redirected through a stub.
  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    if ((E.getKind() == Branch26PCRel || E.getKind() == Call36PCRel ||
         E.getKind() == Call30PCRel) &&
        !E.getTarget().isDefined()) {
      DEBUG_WITH_TYPE("jitlink", {
        dbgs() << "  Fixing " << G.getEdgeKindName(E.getKind()) << " edge at "
               << B->getFixupAddress(E) << " (" << B->getAddress() << " + "
               << formatv("{0:x}", E.getOffset()) << ")\n";
      });
      E.setTarget(getEntryForTarget(G, E.getTarget()));
      return true;
    }
    return false;
  }

  /// Create a PLT stub that jumps to \p Target via a GOT entry.
  /// \param G Link graph to create the stub in.
  /// \param Target External symbol that the stub should reach.
  /// \return An anonymous symbol pointing at the new stub.
  Symbol &createEntry(LinkGraph &G, Symbol &Target) {
    return createAnonymousPointerJumpStub(G, getStubsSection(G),
                                          GOT.getEntryForTarget(G, Target));
  }

public:
  /// Return the stubs section, creating it if it does not already exist.
  /// \param G Link graph that owns the stubs section.
  /// \return The stubs section for \p G.
  Section &getStubsSection(LinkGraph &G) {
    if (!StubsSection)
      StubsSection = &G.createSection(getSectionName(),
                                      orc::MemProt::Read | orc::MemProt::Exec);
    return *StubsSection;
  }

  /// GOT table manager used to obtain GOT entries for stub targets.
  GOTTableManager &GOT;
  /// Section holding PLT stub entries, if one has been created.
  Section *StubsSection = nullptr;
};

} // namespace loongarch
} // namespace jitlink
} // namespace llvm

#endif
