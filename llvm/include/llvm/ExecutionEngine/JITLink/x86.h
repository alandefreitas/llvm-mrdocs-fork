//===----- x86.h - Generic JITLink x86 edge kinds, utilities ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing x86 objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_X86_H
#define LLVM_EXECUTIONENGINE_JITLINK_X86_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITLink/TableManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace jitlink {
/// JITLink utilities for x86 relocatable objects.
namespace x86 {

/// Represets x86 fixups
enum EdgeKind_x86 : Edge::Kind {

  /// A plain 32-bit pointer value relocation.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : uint32
  ///
  /// Errors:
  ///   - The target must reside in the low 32-bits of the address space,
  ///     otherwise an out-of-range error will be returned.
  ///
  Pointer32 = Edge::FirstRelocation,

  /// A 32-bit PC-relative relocation.
  ///
  /// Represents a data/control flow instruction using PC-relative addressing
  /// to a target.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///
  PCRel32,

  /// A plain 16-bit pointer value relocation.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : uint16
  ///
  /// Errors:
  ///   - The target must reside in the low 16-bits of the address space,
  ///     otherwise an out-of-range error will be returned.
  ///
  Pointer16,

  /// A 16-bit PC-relative relocation.
  ///
  /// Represents a data/control flow instruction using PC-relative addressing
  /// to a target.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int16
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int16, otherwise
  ///     an out-of-range error will be returned.
  ///
  PCRel16,

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
  Delta32,

  /// A 32-bit GOT delta.
  ///
  /// Delta from the global offset table to the target.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - GOTSymbol + Addend : int32
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to a null pointer GOTSymbol, which the GOT section
  ///     symbol was not been defined.
  Delta32FromGOT,

  /// A GOT entry offset within GOT getter/constructor, transformed to
  /// Delta32FromGOT pointing at the GOT entry for the original target.
  ///
  /// Indicates that this edge should be transformed into a Delta32FromGOT
  /// targeting the GOT entry for the edge's current target, maintaining the
  /// same addend.
  /// A GOT entry for the target should be created if one does not already
  /// exist.
  ///
  /// Edges of this kind are usually handled by a GOT builder pass inserted by
  /// default
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase
  RequestGOTAndTransformToDelta32FromGOT,

  /// A 32-bit PC-relative branch.
  ///
  /// Represents a PC-relative call or branch to a target. This can be used to
  /// identify, record, and/or patch call sites.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///
  BranchPCRel32,

  /// A 32-bit PC-relative branch to a pointer jump stub.
  ///
  /// The target of this relocation should be a pointer jump stub of the form:
  ///
  /// \code{.s}
  ///   .text
  ///   jmp *tgtptr
  ///   ; ...
  ///
  ///   .data
  ///   tgtptr:
  ///     .quad 0
  /// \endcode
  ///
  /// This edge kind has the same fixup expression as BranchPCRel32, but further
  /// identifies the call/branch as being to a pointer jump stub. For edges of
  /// this kind the jump stub should not be bypassed (use
  /// BranchPCRel32ToPtrJumpStubBypassable for that), but the pointer location
  /// target may be recorded to allow manipulation at runtime.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///
  BranchPCRel32ToPtrJumpStub,

  /// A relaxable version of BranchPCRel32ToPtrJumpStub.
  ///
  /// The edge kind has the same fixup expression as BranchPCRel32ToPtrJumpStub,
  /// but identifies the call/branch as being to a pointer jump stub that may be
  /// bypassed with a direct jump to the ultimate target if the ultimate target
  /// is within range of the fixup location.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///
  BranchPCRel32ToPtrJumpStubBypassable,
};

/// Returns a string name for the given x86 edge. For debugging purposes
/// only.
/// \param K Edge kind to name.
/// \return A human-readable name for \p K.
LLVM_ABI const char *getEdgeKindName(Edge::Kind K);

/// Apply fixup expression for edge to block content.
/// \param G Link graph containing the block.
/// \param B Block whose content should be fixed up.
/// \param E Edge describing the fixup to apply.
/// \param GOTSymbol Optional GOT section symbol for GOT-relative fixups.
/// \return Success, or an error if the fixup is out of range or unsupported.
inline Error applyFixup(LinkGraph &G, Block &B, const Edge &E,
                        const Symbol *GOTSymbol) {
  using namespace llvm::support;

  char *BlockWorkingMem = B.getAlreadyMutableContent().data();
  char *FixupPtr = BlockWorkingMem + E.getOffset();
  auto FixupAddress = B.getAddress() + E.getOffset();

  switch (E.getKind()) {
  case Pointer32: {
    uint32_t Value = E.getTarget().getAddress().getValue() + E.getAddend();
    *(ulittle32_t *)FixupPtr = Value;
    break;
  }

  case PCRel32: {
    int32_t Value = E.getTarget().getAddress() - FixupAddress + E.getAddend();
    *(little32_t *)FixupPtr = Value;
    break;
  }

  case Pointer16: {
    uint32_t Value = E.getTarget().getAddress().getValue() + E.getAddend();
    if (LLVM_LIKELY(isUInt<16>(Value)))
      *(ulittle16_t *)FixupPtr = Value;
    else
      return makeTargetOutOfRangeError(G, B, E);
    break;
  }

  case PCRel16: {
    int32_t Value = E.getTarget().getAddress() - FixupAddress + E.getAddend();
    if (LLVM_LIKELY(isInt<16>(Value)))
      *(little16_t *)FixupPtr = Value;
    else
      return makeTargetOutOfRangeError(G, B, E);
    break;
  }

  case Delta32: {
    int32_t Value = E.getTarget().getAddress() - FixupAddress + E.getAddend();
    *(little32_t *)FixupPtr = Value;
    break;
  }

  case Delta32FromGOT: {
    assert(GOTSymbol && "No GOT section symbol");
    int32_t Value =
        E.getTarget().getAddress() - GOTSymbol->getAddress() + E.getAddend();
    *(little32_t *)FixupPtr = Value;
    break;
  }

  case BranchPCRel32:
  case BranchPCRel32ToPtrJumpStub:
  case BranchPCRel32ToPtrJumpStubBypassable: {
    int32_t Value = E.getTarget().getAddress() - FixupAddress + E.getAddend();
    *(little32_t *)FixupPtr = Value;
    break;
  }

  default:
    return make_error<JITLinkError>(
        "In graph " + G.getName() + ", section " + B.getSection().getName() +
        " unsupported edge kind " + getEdgeKindName(E.getKind()));
  }

  return Error::success();
}

/// x86 pointer size.
constexpr uint32_t PointerSize = 4;

/// x86 null pointer content.
LLVM_ABI extern const char NullPointerContent[PointerSize];

/// x86 pointer jump stub content.
///
/// Contains the instruction sequence for an indirect jump via an in-memory
/// pointer:
///   jmpq *ptr
LLVM_ABI extern const char PointerJumpStubContent[6];

/// Creates a new pointer block in the given section and returns an anonymous
/// symbol pointing to it.
///
/// If InitialTarget is given then an Pointer32 relocation will be added to the
/// block pointing at InitialTarget.
///
/// The pointer block will have the following default values:
///   alignment: 32-bit
///   alignment-offset: 0
///   address: highest allowable (~7U)
/// \param G Link graph to create the pointer in.
/// \param PointerSection Section that will hold the pointer block.
/// \param InitialTarget Optional symbol for an initial Pointer32 edge.
/// \param InitialAddend Addend for the optional initial Pointer32 edge.
/// \return An anonymous symbol pointing at the new pointer block.
inline Symbol &createAnonymousPointer(LinkGraph &G, Section &PointerSection,
                                      Symbol *InitialTarget = nullptr,
                                      uint64_t InitialAddend = 0) {
  auto &B = G.createContentBlock(PointerSection, NullPointerContent,
                                 orc::ExecutorAddr(), 8, 0);
  if (InitialTarget)
    B.addEdge(Pointer32, 0, *InitialTarget, InitialAddend);
  return G.addAnonymousSymbol(B, 0, PointerSize, false, false);
}

/// Create a jump stub block that jumps via the pointer at the given symbol.
///
/// The stub block will have the following default values:
///   alignment: 8-bit
///   alignment-offset: 0
///   address: highest allowable: (~5U)
/// \param G Link graph to create the stub block in.
/// \param StubSection Section that will hold the stub.
/// \param PointerSymbol Symbol of the in-memory pointer to jump through.
/// \return The newly created jump stub block.
inline Block &createPointerJumpStubBlock(LinkGraph &G, Section &StubSection,
                                         Symbol &PointerSymbol) {
  auto &B = G.createContentBlock(StubSection, PointerJumpStubContent,
                                 orc::ExecutorAddr(), 8, 0);
  B.addEdge(Pointer32,
            // Offset is 2 because the the first 2 bytes of the
            // jump stub block are {0xff, 0x25} -- an indirect absolute
            // jump.
            2, PointerSymbol, 0);
  return B;
}

/// Create a jump stub that jumps via the pointer at the given symbol and
/// an anonymous symbol pointing to it. Return the anonymous symbol.
///
/// The stub block will be created by createPointerJumpStubBlock.
/// \param G Link graph to create the stub in.
/// \param StubSection Section that will hold the stub.
/// \param PointerSymbol Symbol of the in-memory pointer to jump through.
/// \return An anonymous symbol pointing at the new jump stub.
inline Symbol &createAnonymousPointerJumpStub(LinkGraph &G,
                                              Section &StubSection,
                                              Symbol &PointerSymbol) {
  return G.addAnonymousSymbol(
      createPointerJumpStubBlock(G, StubSection, PointerSymbol), 0, 6, true,
      false);
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
    case Delta32FromGOT: {
      // we need to make sure that the GOT section exists, but don't otherwise
      // need to fix up this edge
      getGOTSection(G);
      return false;
    }
    case RequestGOTAndTransformToDelta32FromGOT:
      KindToSet = Delta32FromGOT;
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
      GOTSection = &G.createSection(getSectionName(), orc::MemProt::Read);
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

  /// Visit an edge and redirect external branches through a PLT stub.
  /// \param G Link graph being processed.
  /// \param B Block containing the edge.
  /// \param E Edge that may need a PLT stub.
  /// \return True if the edge was redirected through a stub.
  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    if (E.getKind() == BranchPCRel32 && !E.getTarget().isDefined()) {
      DEBUG_WITH_TYPE("jitlink", {
        dbgs() << "  Fixing " << G.getEdgeKindName(E.getKind()) << " edge at "
               << B->getFixupAddress(E) << " (" << B->getAddress() << " + "
               << formatv("{0:x}", E.getOffset()) << ")\n";
      });
      // Set the edge kind to Branch32ToPtrJumpStubBypassable to enable it to
      // be optimized when the target is in-range.
      E.setKind(BranchPCRel32ToPtrJumpStubBypassable);
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
  /// \return The stubs section.
  Section &getStubsSection(LinkGraph &G) {
    if (!PLTSection)
      PLTSection = &G.createSection(getSectionName(),
                                    orc::MemProt::Read | orc::MemProt::Exec);
    return *PLTSection;
  }

  /// GOT table manager used to obtain GOT entries for stub targets.
  GOTTableManager &GOT;
  /// Section holding PLT stub entries, if one has been created.
  Section *PLTSection = nullptr;
};

/// Optimize GOT and stub relocations when the edge target is in range.
///
/// 1. PCRel32GOTLoadRelaxable. For this edge kind, if the target is in range,
/// then replace GOT load with lea. (THIS IS UNIMPLEMENTED RIGHT NOW!)
/// 2. BranchPCRel32ToPtrJumpStubRelaxable. For this edge kind, if the target is
/// in range, replace a indirect jump by plt stub with a direct jump to the
/// target
/// \param G Link graph whose GOT and stub edges may be relaxed.
/// \return Success, or an error if optimization fails.
LLVM_ABI Error optimizeGOTAndStubAccesses(LinkGraph &G);

} // namespace x86
} // namespace jitlink
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_X86_H
