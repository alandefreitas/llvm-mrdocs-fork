//===- ValueMapper.h - Remapping for constants and metadata -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MapValue interface which is used by various parts of
// the Transforms/Utils library to implement cloning and linking facilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_VALUEMAPPER_H
#define LLVM_TRANSFORMS_UTILS_VALUEMAPPER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/simple_ilist.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Constant;
class DIBuilder;
class DbgRecord;
class Function;
class GlobalVariable;
class Instruction;
class MDNode;
class Metadata;
class Module;
class Type;
class Value;

using ValueToValueMapTy = ValueMap<const Value *, WeakTrackingVH>;
/// Iterator over debug records in a simple intrusive list.
using DbgRecordIterator = simple_ilist<DbgRecord>::iterator;
/// Small set of metadata pointers used while remapping.
using MetadataSetTy = SmallPtrSet<const Metadata *, 16>;
/// Predicate that identifies metadata which should map onto itself.
using MetadataPredicate = std::function<bool(const Metadata *)>;

/// This is a class that can be implemented by clients to remap types when
/// cloning constants and instructions.
class LLVM_ABI ValueMapTypeRemapper {
  virtual void anchor(); // Out of line method.

public:
  /// Destroy the type remapper.
  virtual ~ValueMapTypeRemapper() = default;

  /// The client should implement this method if they want to remap types while
  /// mapping values.
  ///
  /// \param SrcTy Type to remap.
  /// \return The remapped type for \p SrcTy.
  virtual Type *remapType(Type *SrcTy) = 0;
};

/// This is a class that can be implemented by clients to materialize Values on
/// demand.
class LLVM_ABI ValueMaterializer {
  virtual void anchor(); // Out of line method.

protected:
  /// Construct a value materializer.
  ValueMaterializer() = default;
  /// Copy-construct a value materializer.
  ///
  /// \param Other Materializer to copy from.
  ValueMaterializer(const ValueMaterializer &Other) = default;
  /// Copy-assign a value materializer.
  ///
  /// \param Other Materializer to copy from.
  /// \return Reference to this materializer after the copy.
  ValueMaterializer &operator=(const ValueMaterializer &Other) = default;
  /// Destroy the value materializer.
  ~ValueMaterializer() = default;

public:
  /// This method can be implemented to generate a mapped Value on demand. For
  /// example, if linking lazily. Returns null if the value is not materialized.
  ///
  /// \param V Value to materialize.
  /// \return The materialized value, or nullptr if not materialized.
  virtual Value *materialize(Value *V) = 0;
};

/// These are flags that the value mapping APIs allow.
enum RemapFlags {
  /// No remapping flags.
  RF_None = 0,

  /// If this flag is set, the remapper knows that only local values within a
  /// function (such as an instruction or argument) are mapped, not global
  /// values like functions and global metadata.
  RF_NoModuleLevelChanges = 1,

  /// If this flag is set, the remapper ignores missing function-local entries
  /// (Argument, Instruction, BasicBlock) that are not in the value map.  If it
  /// is unset, it aborts if an operand is asked to be remapped which doesn't
  /// exist in the mapping.
  ///
  /// There are no such assertions in MapValue(), whose results are almost
  /// unchanged by this flag.  This flag mainly changes the assertion behaviour
  /// in RemapInstruction().
  ///
  /// Since an Instruction's metadata operands (even that point to SSA values)
  /// aren't guaranteed to be dominated by their definitions, MapMetadata will
  /// return "!{}" instead of "null" for \a LocalAsMetadata instances whose SSA
  /// values are unmapped when this flag is set.  Otherwise, \a MapValue()
  /// completely ignores this flag.
  ///
  /// \a MapMetadata() always ignores this flag.
  RF_IgnoreMissingLocals = 2,

  /// Instruct the remapper to reuse and mutate distinct metadata (remapping
  /// them in place) instead of cloning remapped copies. This flag has no
  /// effect when RF_NoModuleLevelChanges, since that implies an identity
  /// mapping.
  RF_ReuseAndMutateDistinctMDs = 4,

  /// Any global values not in value map are mapped to null instead of mapping
  /// to self.  Illegal if RF_IgnoreMissingLocals is also set.
  RF_NullMapMissingGlobalValues = 8,

  /// Do not remap source location atoms. Only safe if to do this if the cloned
  /// instructions being remapped are inserted into a new function, or an
  /// existing function where the inlined-at fields are updated. If in doubt,
  /// don't use this flag. It's used when remapping is known to be un-necessary
  /// to save some compile-time.
  RF_DoNotRemapAtoms = 16,

  /// Indicate that we are importing functions, specifically in the context of
  /// ThinLTO. There is some ad-hoc behavior required in this mode.
  RF_Importing = 32,
};

/// Combine two remapping flags with a bitwise OR.
///
/// \param LHS Left-hand remapping flags.
/// \param RHS Right-hand remapping flags.
/// \return The combined remapping flags.
inline RemapFlags operator|(RemapFlags LHS, RemapFlags RHS) {
  return RemapFlags(unsigned(LHS) | unsigned(RHS));
}

/// Context for (re-)mapping values (and metadata).
///
/// A shared context used for mapping and remapping of Value and Metadata
/// instances using \a ValueToValueMapTy, \a RemapFlags, \a
/// ValueMapTypeRemapper, \a ValueMaterializer, and \a IdentityMD.
///
/// There are a number of top-level entry points:
/// - \a mapValue() (and \a mapConstant());
/// - \a mapMetadata() (and \a mapMDNode());
/// - \a remapInstruction();
/// - \a remapFunction(); and
/// - \a remapGlobalObjectMetadata().
///
/// The \a ValueMaterializer can be used as a callback, but cannot invoke any
/// of these top-level functions recursively.  Instead, callbacks should use
/// one of the following to schedule work lazily in the \a ValueMapper
/// instance:
/// - \a scheduleMapGlobalInitializer()
/// - \a scheduleMapAppendingVariable()
/// - \a scheduleMapGlobalAlias()
/// - \a scheduleMapGlobalIFunc()
/// - \a scheduleRemapFunction()
///
/// Sometimes a callback needs a different mapping context.  Such a context can
/// be registered using \a registerAlternateMappingContext(), which takes an
/// alternate \a ValueToValueMapTy and \a ValueMaterializer and returns a ID to
/// pass into the schedule*() functions.
///
/// If an \a IdentityMD predicate is optionally provided, \a Metadata for which
/// the predicate returns true will be mapped onto itself in \a VM on first use.
///
/// TODO: lib/Linker really doesn't need the \a ValueHandle in the \a
/// ValueToValueMapTy.  We should template \a ValueMapper (and its
/// implementation classes), and explicitly instantiate on two concrete
/// instances of \a ValueMap (one as \a ValueToValueMap, and one with raw \a
/// Value pointers).  It may be viable to do away with \a TrackingMDRef in the
/// \a Metadata side map for the lib/Linker case as well, in which case we'll
/// need a new template parameter on \a ValueMap.
///
/// TODO: Update callers of \a RemapInstruction() and \a MapValue() (etc.) to
/// use \a ValueMapper directly.
class ValueMapper {
  void *pImpl;

public:
  /// Construct a value mapper for \p VM with the given remapping options.
  ///
  /// \param VM Mapping from original values to remapped values.
  /// \param Flags Remapping flags that control mapper behavior.
  /// \param TypeMapper Optional callback that remaps types while mapping
/// values.
  /// \param Materializer Optional callback that materializes unmapped values.
  /// \param IdentityMD Optional predicate for metadata that maps onto itself.
  LLVM_ABI ValueMapper(ValueToValueMapTy &VM, RemapFlags Flags = RF_None,
                       ValueMapTypeRemapper *TypeMapper = nullptr,
                       ValueMaterializer *Materializer = nullptr,
                       const MetadataPredicate *IdentityMD = nullptr);
  /// Deleted move constructor; ValueMapper is not movable.
  ///
  /// \param Other Unused; move construction is not allowed.
  ValueMapper(ValueMapper &&Other) = delete;
  /// Deleted copy constructor; ValueMapper is not copyable.
  ///
  /// \param Other Unused; copy construction is not allowed.
  ValueMapper(const ValueMapper &Other) = delete;
  /// Deleted move assignment; ValueMapper cannot be move-assigned.
  ///
  /// \param Other Unused; move assignment is not allowed.
  ValueMapper &operator=(ValueMapper &&Other) = delete;
  /// Deleted copy assignment; ValueMapper cannot be copy-assigned.
  ///
  /// \param Other Unused; copy assignment is not allowed.
  ValueMapper &operator=(const ValueMapper &Other) = delete;
  /// Destroy the value mapper.
  LLVM_ABI ~ValueMapper();

  /// Register an alternate mapping context.
  ///
  /// Returns a MappingContextID that can be used with the various schedule*()
  /// API to switch in a different value map on-the-fly.
  ///
  /// \param VM Alternate value map to use when this context is selected.
  /// \param Materializer Optional materializer for the alternate context.
  /// \return A MappingContextID for use with the schedule*() APIs.
  LLVM_ABI unsigned
  registerAlternateMappingContext(ValueToValueMapTy &VM,
                                  ValueMaterializer *Materializer = nullptr);

  /// Add to the current \a RemapFlags.
  ///
  /// \note Like the top-level mapping functions, \a addFlags() must be called
  /// at the top level, not during a callback in a \a ValueMaterializer.
  ///
  /// \param Flags Additional remapping flags to set.
  LLVM_ABI void addFlags(RemapFlags Flags);

  /// Map \p MD through this mapper's value map.
  ///
  /// \param MD Metadata to map.
  /// \return The mapped metadata, or nullptr when mapping fails.
  LLVM_ABI Metadata *mapMetadata(const Metadata &MD);
  /// Map metadata node \p N through this mapper's value map.
  ///
  /// \param N Metadata node to map.
  /// \return The mapped metadata node.
  LLVM_ABI MDNode *mapMDNode(const MDNode &N);

  /// Map \p V through this mapper's value map.
  ///
  /// \param V Value to map.
  /// \return The mapped value, or nullptr when no mapping is available.
  LLVM_ABI Value *mapValue(const Value &V);
  /// Map constant \p C through this mapper's value map.
  ///
  /// \param C Constant to map.
  /// \return The mapped constant.
  LLVM_ABI Constant *mapConstant(const Constant &C);

  /// Remap the operands and metadata of instruction \p I in place.
  ///
  /// \param I Instruction whose operands and metadata are remapped.
  LLVM_ABI void remapInstruction(Instruction &I);
  /// Remap the values used in debug record \p V.
  ///
  /// \param M Module containing the debug record.
  /// \param V Debug record whose referenced values are remapped.
  LLVM_ABI void remapDbgRecord(Module *M, DbgRecord &V);
  /// Remap the values used in each debug record in \p Range.
  ///
  /// \param M Module containing the debug records.
  /// \param Range Range of debug records to remap.
  LLVM_ABI void remapDbgRecordRange(Module *M,
                                    iterator_range<DbgRecordIterator> Range);
  /// Remap the operands, metadata, arguments, and instructions of \p F.
  ///
  /// \param F Function to remap in place.
  LLVM_ABI void remapFunction(Function &F);
  /// Remap metadata attached to global object \p GO.
  ///
  /// \param GO Global object whose attached metadata is remapped.
  LLVM_ABI void remapGlobalObjectMetadata(GlobalObject &GO);

  /// Schedule mapping of global variable \p GV's initializer to \p Init.
  ///
  /// \param GV Global variable whose initializer will be mapped.
  /// \param Init Initializer constant to map.
  /// \param MappingContextID Alternate mapping context, or 0 for the default.
  LLVM_ABI void scheduleMapGlobalInitializer(GlobalVariable &GV, Constant &Init,
                                             unsigned MappingContextID = 0);
  /// Schedule mapping of appending variable \p GV.
  ///
  /// \param GV Destination appending global variable.
  /// \param OldGV Previous appending global to map members from, or null.
  /// \param IsOldCtorDtor True if \p OldGV is a constructor or destructor list.
  /// \param NewMembers Additional members to append after mapping.
  /// \param MappingContextID Alternate mapping context, or 0 for the default.
  LLVM_ABI void scheduleMapAppendingVariable(GlobalVariable &GV,
                                             GlobalVariable *OldGV,
                                             bool IsOldCtorDtor,
                                             ArrayRef<Constant *> NewMembers,
                                             unsigned MappingContextID = 0);
  /// Schedule mapping of alias \p GA to aliasee \p Aliasee.
  ///
  /// \param GA Global alias whose aliasee will be mapped.
  /// \param Aliasee Aliasee constant to map.
  /// \param MappingContextID Alternate mapping context, or 0 for the default.
  LLVM_ABI void scheduleMapGlobalAlias(GlobalAlias &GA, Constant &Aliasee,
                                       unsigned MappingContextID = 0);
  /// Schedule mapping of ifunc \p GI to resolver \p Resolver.
  ///
  /// \param GI Global ifunc whose resolver will be mapped.
  /// \param Resolver Resolver constant to map.
  /// \param MappingContextID Alternate mapping context, or 0 for the default.
  LLVM_ABI void scheduleMapGlobalIFunc(GlobalIFunc &GI, Constant &Resolver,
                                       unsigned MappingContextID = 0);
  /// Schedule remapping of function \p F.
  ///
  /// \param F Function to remap.
  /// \param MappingContextID Alternate mapping context, or 0 for the default.
  LLVM_ABI void scheduleRemapFunction(Function &F,
                                      unsigned MappingContextID = 0);
};

/// Look up or compute a value in the value map.
///
/// Return a mapped value for a function-local value (Argument, Instruction,
/// BasicBlock), or compute and memoize a value for a Constant.
///
///  1. If \c V is in VM, return the result.
///  2. Else if \c V can be materialized with \c Materializer, do so, memoize
///     it in \c VM, and return it.
///  3. Else if \c V is a function-local value, return nullptr.
///  4. Else if \c V is a \a GlobalValue, return \c nullptr or \c V depending
///     on \a RF_NullMapMissingGlobalValues.
///  5. Else if \c V is a \a MetadataAsValue wrapping a LocalAsMetadata,
///     recurse on the local SSA value, and return nullptr or "metadata !{}" on
///     missing depending on RF_IgnoreMissingValues.
///  6. Else if \c V is a \a MetadataAsValue, rewrap the return of \a
///     MapMetadata().
///  7. Else, compute the equivalent constant, and return it.
///
/// \param V Value to look up or map.
/// \param VM Mapping from original values to remapped values.
/// \param Flags Remapping flags that control mapper behavior.
/// \param TypeMapper Optional callback that remaps types while mapping
/// values.
/// \param Materializer Optional callback that materializes unmapped values.
/// \param IdentityMD Optional predicate for metadata that maps onto itself.
/// \return The mapped value, or nullptr when no mapping is available.
inline Value *MapValue(const Value *V, ValueToValueMapTy &VM,
                       RemapFlags Flags = RF_None,
                       ValueMapTypeRemapper *TypeMapper = nullptr,
                       ValueMaterializer *Materializer = nullptr,
                       const MetadataPredicate *IdentityMD = nullptr) {
  return ValueMapper(VM, Flags, TypeMapper, Materializer, IdentityMD)
      .mapValue(*V);
}

/// Lookup or compute a mapping for a piece of metadata.
///
/// Compute and memoize a mapping for \c MD.
///
///  1. If \c MD is mapped, return it.
///  2. Else if \a RF_NoModuleLevelChanges or \c MD is an \a MDString, return
///     \c MD.
///  3. Else if \c MD is a \a ConstantAsMetadata, call \a MapValue() and
///     re-wrap its return (returning nullptr on nullptr).
///  4. Else if \c IdentityMD predicate returns true for \c MD then add an
///     identity mapping for it and return it.
///  5. Else, \c MD is an \a MDNode.  These are remapped, along with their
///     transitive operands.  Distinct nodes are duplicated or moved depending
///     on \a RF_MoveDistinctNodes.  Uniqued nodes are remapped like constants.
///
/// \note \a LocalAsMetadata is completely unsupported by \a MapMetadata.
/// Instead, use \a MapValue() with its wrapping \a MetadataAsValue instance.
///
/// \param MD Metadata to look up or map.
/// \param VM Mapping from original values to remapped values.
/// \param Flags Remapping flags that control mapper behavior.
/// \param TypeMapper Optional callback that remaps types while mapping
/// values.
/// \param Materializer Optional callback that materializes unmapped values.
/// \param IdentityMD Optional predicate for metadata that maps onto itself.
/// \return The mapped metadata, or nullptr when mapping fails.
inline Metadata *MapMetadata(const Metadata *MD, ValueToValueMapTy &VM,
                             RemapFlags Flags = RF_None,
                             ValueMapTypeRemapper *TypeMapper = nullptr,
                             ValueMaterializer *Materializer = nullptr,
                             const MetadataPredicate *IdentityMD = nullptr) {
  return ValueMapper(VM, Flags, TypeMapper, Materializer, IdentityMD)
      .mapMetadata(*MD);
}

/// Version of MapMetadata with type safety for MDNode.
///
/// \param MD Metadata node to look up or map.
/// \param VM Mapping from original values to remapped values.
/// \param Flags Remapping flags that control mapper behavior.
/// \param TypeMapper Optional callback that remaps types while mapping
/// values.
/// \param Materializer Optional callback that materializes unmapped values.
/// \param IdentityMD Optional predicate for metadata that maps onto itself.
/// \return The mapped metadata node.
inline MDNode *MapMetadata(const MDNode *MD, ValueToValueMapTy &VM,
                           RemapFlags Flags = RF_None,
                           ValueMapTypeRemapper *TypeMapper = nullptr,
                           ValueMaterializer *Materializer = nullptr,
                           const MetadataPredicate *IdentityMD = nullptr) {
  return ValueMapper(VM, Flags, TypeMapper, Materializer, IdentityMD)
      .mapMDNode(*MD);
}

/// Convert the instruction operands from referencing the current values into
/// those specified by VM.
///
/// If \a RF_IgnoreMissingLocals is set and an operand can't be found via \a
/// MapValue(), use the old value.  Otherwise assert that this doesn't happen.
///
/// Note that \a MapValue() only returns \c nullptr for SSA values missing from
/// \c VM.
///
/// \param I Instruction whose operands and metadata are remapped.
/// \param VM Mapping from original values to remapped values.
/// \param Flags Remapping flags that control mapper behavior.
/// \param TypeMapper Optional callback that remaps types while mapping
/// values.
/// \param Materializer Optional callback that materializes unmapped values.
/// \param IdentityMD Optional predicate for metadata that maps onto itself.
inline void RemapInstruction(Instruction *I, ValueToValueMapTy &VM,
                             RemapFlags Flags = RF_None,
                             ValueMapTypeRemapper *TypeMapper = nullptr,
                             ValueMaterializer *Materializer = nullptr,
                             const MetadataPredicate *IdentityMD = nullptr) {
  ValueMapper(VM, Flags, TypeMapper, Materializer, IdentityMD)
      .remapInstruction(*I);
}

/// Remap the source-location atom of instruction \p I.
///
/// Called by RemapInstruction. This updates the instruction's atom group
/// number if it has been mapped (e.g. with llvm::mapAtomInstance), which is
/// necessary to distinguish source code atoms on duplicated code paths.
///
/// \param I Instruction whose debug-location atom may be remapped.
/// \param VM Value map that may contain a mapped atom instance.
LLVM_ABI void RemapSourceAtom(Instruction *I, ValueToValueMapTy &VM);

/// Remap the Values used in the DbgRecord \a DR using the value map \a
/// VM.
///
/// \param M Module containing the debug record.
/// \param DR Debug record whose referenced values are remapped.
/// \param VM Mapping from original values to remapped values.
/// \param Flags Remapping flags that control mapper behavior.
/// \param TypeMapper Optional callback that remaps types while mapping
/// values.
/// \param Materializer Optional callback that materializes unmapped values.
/// \param IdentityMD Optional predicate for metadata that maps onto itself.
inline void RemapDbgRecord(Module *M, DbgRecord *DR, ValueToValueMapTy &VM,
                           RemapFlags Flags = RF_None,
                           ValueMapTypeRemapper *TypeMapper = nullptr,
                           ValueMaterializer *Materializer = nullptr,
                           const MetadataPredicate *IdentityMD = nullptr) {
  ValueMapper(VM, Flags, TypeMapper, Materializer, IdentityMD)
      .remapDbgRecord(M, *DR);
}

/// Remap the Values used in the DbgRecords \a Range using the value map \a
/// VM.
///
/// \param M Module containing the debug records.
/// \param Range Range of debug records to remap.
/// \param VM Mapping from original values to remapped values.
/// \param Flags Remapping flags that control mapper behavior.
/// \param TypeMapper Optional callback that remaps types while mapping
/// values.
/// \param Materializer Optional callback that materializes unmapped values.
/// \param IdentityMD Optional predicate for metadata that maps onto itself.
inline void RemapDbgRecordRange(Module *M,
                                iterator_range<DbgRecordIterator> Range,
                                ValueToValueMapTy &VM,
                                RemapFlags Flags = RF_None,
                                ValueMapTypeRemapper *TypeMapper = nullptr,
                                ValueMaterializer *Materializer = nullptr,
                                const MetadataPredicate *IdentityMD = nullptr) {
  ValueMapper(VM, Flags, TypeMapper, Materializer, IdentityMD)
      .remapDbgRecordRange(M, Range);
}

/// Remap the operands, metadata, arguments, and instructions of a function.
///
/// Calls \a MapValue() on prefix data, prologue data, and personality
/// function; calls \a MapMetadata() on each attached MDNode; remaps the
/// argument types using the provided \c TypeMapper; and calls \a
/// RemapInstruction() on every instruction.
///
/// \param F Function to remap in place.
/// \param VM Mapping from original values to remapped values.
/// \param Flags Remapping flags that control mapper behavior.
/// \param TypeMapper Optional callback that remaps types while mapping
/// values.
/// \param Materializer Optional callback that materializes unmapped values.
/// \param IdentityMD Optional predicate for metadata that maps onto itself.
inline void RemapFunction(Function &F, ValueToValueMapTy &VM,
                          RemapFlags Flags = RF_None,
                          ValueMapTypeRemapper *TypeMapper = nullptr,
                          ValueMaterializer *Materializer = nullptr,
                          const MetadataPredicate *IdentityMD = nullptr) {
  ValueMapper(VM, Flags, TypeMapper, Materializer, IdentityMD).remapFunction(F);
}

/// Version of MapValue with type safety for Constant.
///
/// \param V Constant to look up or map.
/// \param VM Mapping from original values to remapped values.
/// \param Flags Remapping flags that control mapper behavior.
/// \param TypeMapper Optional callback that remaps types while mapping
/// values.
/// \param Materializer Optional callback that materializes unmapped values.
/// \param IdentityMD Optional predicate for metadata that maps onto itself.
/// \return The mapped constant.
inline Constant *MapValue(const Constant *V, ValueToValueMapTy &VM,
                          RemapFlags Flags = RF_None,
                          ValueMapTypeRemapper *TypeMapper = nullptr,
                          ValueMaterializer *Materializer = nullptr,
                          const MetadataPredicate *IdentityMD = nullptr) {
  return ValueMapper(VM, Flags, TypeMapper, Materializer, IdentityMD)
      .mapConstant(*V);
}

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_VALUEMAPPER_H
