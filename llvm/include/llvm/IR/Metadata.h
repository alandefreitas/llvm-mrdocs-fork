//===- llvm/IR/Metadata.h - Metadata definitions ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file
/// This file contains the declarations for metadata subclasses.
/// They represent the different flavors of metadata that live in LLVM.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_METADATA_H
#define LLVM_IR_METADATA_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace llvm {

enum class CaptureComponents : uint8_t;
class Module;
class ModuleSlotTracker;
class raw_ostream;
class DbgVariableRecord;
template <typename T> class StringMapEntry;
template <typename ValueTy> class StringMapEntryStorage;
class Type;

/// Named constants used by LLVM IR metadata encodings.
enum LLVMConstants : uint32_t {
  /// Current debug-info metadata version number.
  DEBUG_METADATA_VERSION = 3
};

/// Magic number in the value profile metadata showing a target has been
/// promoted for the instruction and shouldn't be promoted again.
const uint64_t NOMORE_ICP_MAGICNUM = -1;

/// Root of the metadata hierarchy.
///
/// This is a root class for typeless data in the IR.
class Metadata {
  friend class ReplaceableMetadataImpl;

  /// RTTI.
  const unsigned char SubclassID;

protected:
  /// Active type of storage.
  enum StorageType {
    /// Interned metadata owned by the context's uniquing tables.
    Uniqued,
    /// Non-uniqued metadata owned by the context.
    Distinct,
    /// Transient forward-reference metadata with RAUW support.
    Temporary
  };

  /// Storage flag for non-uniqued, otherwise unowned, metadata.
  unsigned char Storage : 7;

  /// One bit of subclass-specific state packed into the metadata header.
  unsigned char SubclassData1 : 1;
  /// Sixteen bits of subclass-specific state packed into the metadata header.
  unsigned short SubclassData16 = 0;
  /// Thirty-two bits of subclass-specific state packed into the metadata header.
  unsigned SubclassData32 = 0;

public:
  /// RTTI identifiers for concrete metadata subclasses.
  enum MetadataKind {
#define HANDLE_METADATA_LEAF(CLASS) CLASS##Kind,
#include "llvm/IR/Metadata.def"
  };

protected:
  /// Construct metadata with subclass ID \p ID and storage kind \p Storage.
  /// \param ID Subclass RTTI identifier.
  /// \param Storage Ownership/uniquing storage kind.
  Metadata(unsigned ID, StorageType Storage)
      : SubclassID(ID), Storage(Storage), SubclassData1(false) {
    static_assert(sizeof(*this) == 8, "Metadata fields poorly packed");
  }

  /// Destroy metadata; subclasses manage their own teardown.
  ~Metadata() = default;

  /// Default handling of a changed operand, which asserts.
  ///
  /// If subclasses pass themselves in as owners to a tracking node reference,
  /// they must provide an implementation of this method.
  /// \param Ref Address of the tracking pointer that changed.
  /// \param MD New metadata value at \p Ref.
  void handleChangedOperand(void *Ref, Metadata *MD) {
    (void)Ref;
    (void)MD;
    llvm_unreachable("Unimplemented in Metadata subclass");
  }

public:
  /// Return the metadata kind ID for this node.
  /// \return The metadata kind ID for this node.
  unsigned getMetadataID() const { return SubclassID; }

  /// User-friendly dump.
  ///
  /// If \c M is provided, metadata nodes will be numbered canonically;
  /// otherwise, pointer addresses are substituted.
  ///
  /// Note: this uses an explicit overload instead of default arguments so that
  /// the nullptr version is easy to call from a debugger.
  ///
  /// @{
  /// Dump this metadata to stderr (for debugging).
  LLVM_ABI void dump() const;
  /// Dump this metadata to stderr using \p M for slot tracking.
  /// \param M Optional module used for canonical metadata numbering.
  LLVM_ABI void dump(const Module *M) const;
  /// @}

  /// Print.
  ///
  /// Prints definition of \c this.
  ///
  /// If \c M is provided, metadata nodes will be numbered canonically;
  /// otherwise, pointer addresses are substituted.
  /// @{
  /// Print this metadata definition to \p OS.
  /// \param OS Output stream.
  /// \param M Optional module for canonical metadata numbering.
  /// \param IsForDebug Whether to use debug-oriented formatting.
  LLVM_ABI void print(raw_ostream &OS, const Module *M = nullptr,
                      bool IsForDebug = false) const;
  /// Print this metadata using \p MST for slot numbering.
  /// \param OS Output stream.
  /// \param MST Module slot tracker for numbering.
  /// \param M Optional module for canonical metadata numbering.
  /// \param IsForDebug Whether to use debug-oriented formatting.
  LLVM_ABI void print(raw_ostream &OS, ModuleSlotTracker &MST,
                      const Module *M = nullptr, bool IsForDebug = false) const;
  /// @}

  /// Print as operand.
  ///
  /// Prints reference of \c this.
  ///
  /// If \c M is provided, metadata nodes will be numbered canonically;
  /// otherwise, pointer addresses are substituted.
  /// @{
  /// Print this metadata as an operand reference to \p OS.
  /// \param OS Output stream.
  /// \param M Optional module for canonical metadata numbering.
  LLVM_ABI void printAsOperand(raw_ostream &OS,
                               const Module *M = nullptr) const;
  /// Print as operand using an existing module slot tracker.
  /// \param OS Output stream.
  /// \param MST Module slot tracker for numbering.
  /// \param M Optional module for canonical metadata numbering.
  LLVM_ABI void printAsOperand(raw_ostream &OS, ModuleSlotTracker &MST,
                               const Module *M = nullptr) const;
  /// @}

  /// Metadata IDs that may generate poison.
  constexpr static const unsigned PoisonGeneratingIDs[] = {
      LLVMContext::MD_range, LLVMContext::MD_nonnull, LLVMContext::MD_align,
      LLVMContext::MD_nofpclass};
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Opaque C API conversions for \c Metadata (see CBindingWrapping.h).
/// \param P Opaque metadata reference.
/// \return The unwrapped \c Metadata pointer.
inline Metadata *unwrap(LLVMMetadataRef P) {
  return reinterpret_cast<Metadata *>(P);
}

/// Wrap a \c Metadata pointer as an opaque \c LLVMMetadataRef.
/// \param P Metadata to wrap.
/// \return An opaque \c LLVMMetadataRef for \p P.
inline LLVMMetadataRef wrap(const Metadata *P) {
  return reinterpret_cast<LLVMMetadataRef>(const_cast<Metadata *>(P));
}

/// Unwrap an opaque \c LLVMMetadataRef as a \c Metadata subclass.
/// \param P Opaque metadata reference.
/// \return \p P cast to subclass \c T.
template <typename T>
inline T *unwrap(LLVMMetadataRef P) {
  return cast<T>(unwrap(P));
}

// Specialized opaque metadata conversions.
/// Unwrap an array of opaque \c LLVMMetadataRef values.
/// \param MDs Array of opaque metadata references.
/// \return The array reinterpreted as \c Metadata **.
inline Metadata **unwrap(LLVMMetadataRef *MDs) {
  return reinterpret_cast<Metadata**>(MDs);
}

#define HANDLE_METADATA(CLASS) class CLASS;
#include "llvm/IR/Metadata.def"

// Provide specializations of isa so that we don't need definitions of
// subclasses to see if the metadata is a subclass.
#define HANDLE_METADATA_LEAF(CLASS)                                            \
  template <> struct isa_impl<CLASS, Metadata> {                               \
    static inline bool doit(const Metadata &MD) {                              \
      return MD.getMetadataID() == Metadata::CLASS##Kind;                      \
    }                                                                          \
  };
#include "llvm/IR/Metadata.def"

/// Write \p MD to \p OS using \c Metadata::print.
/// \param OS Output stream.
/// \param MD Metadata to print.
/// \return A reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const Metadata &MD) {
  MD.print(OS);
  return OS;
}

/// Metadata wrapper in the Value hierarchy.
///
/// A member of the \a Value hierarchy to represent a reference to metadata.
/// This allows, e.g., intrinsics to have metadata as operands.
///
/// Notably, this is the only thing in either hierarchy that is allowed to
/// reference \a LocalAsMetadata.
class MetadataAsValue : public Value {
  friend class ReplaceableMetadataImpl;
  friend class LLVMContextImpl;

  Metadata *MD;

  MetadataAsValue(Type *Ty, Metadata *MD);

  /// Drop use of metadata (during teardown).
  void dropUse() { MD = nullptr; }

public:
  /// Destroy this metadata-as-value wrapper.
  LLVM_ABI ~MetadataAsValue();

  /// Create or return a \c MetadataAsValue for \p MD in \p Context.
  /// \param Context LLVM context that owns the wrapper.
  /// \param MD Metadata to wrap as a Value.
  /// \return A \c MetadataAsValue wrapping \p MD.
  LLVM_ABI static MetadataAsValue *get(LLVMContext &Context, Metadata *MD);
  /// Return an existing \c MetadataAsValue for \p MD, or null if none.
  /// \param Context LLVM context that may own the wrapper.
  /// \param MD Metadata whose value wrapper is requested.
  /// \return Existing wrapper for \p MD, or null.
  LLVM_ABI static MetadataAsValue *getIfExists(LLVMContext &Context,
                                               Metadata *MD);

  /// Return the wrapped metadata pointer.
  /// \return The wrapped metadata pointer.
  Metadata *getMetadata() const { return MD; }

  /// Return true if \p V is a \c MetadataAsValue.
  /// \param V Value to test.
  /// \return True if \p V is a \c MetadataAsValue.
  static bool classof(const Value *V) {
    return V->getValueID() == MetadataAsValueVal;
  }

private:
  void handleChangedMetadata(Metadata *MD);
  void track();
  void untrack();
};

/// Base class for tracking ValueAsMetadata/DIArgLists with user lookups and
/// Owner callbacks outside of ValueAsMetadata.
///
/// Currently only inherited by DbgVariableRecord; if other classes need to use
/// it, then a SubclassID will need to be added (either as a new field or by
/// making DebugValue into a PointerIntUnion) to discriminate between the
/// subclasses in lookup and callback handling.
class DebugValueUser {
protected:
  // Capacity to store 3 debug values.
  // TODO: Not all DebugValueUser instances need all 3 elements, if we
  // restructure the DbgVariableRecord class then we can template parameterize
  // this array size.
  /// Tracked debug-value metadata operands (up to three).
  std::array<Metadata *, 3> DebugValues;

  /// Return the tracked debug-value metadata operands.
  /// \return The tracked debug-value metadata operands.
  ArrayRef<Metadata *> getDebugValues() const { return DebugValues; }

public:
  /// Return the owning DbgVariableRecord that holds these debug-value operands.
  /// \return The owning \c DbgVariableRecord.
  LLVM_ABI DbgVariableRecord *getUser();
  /// Return the owning DbgVariableRecord that holds these debug-value operands.
  /// \return The owning \c DbgVariableRecord.
  LLVM_ABI const DbgVariableRecord *getUser() const;
  /// Update a tracked debug-value pointer after RAUW.
  ///
  /// Called by ReplaceableMetadataImpl::replaceAllUsesWith, where \p Old is a
  /// pointer to one of the pointers in \c DebugValues (so should be type
  /// Metadata**), and \p NewDebugValue is the new Metadata* that is replacing
  /// *Old. For manually replacing elements of DebugValues,
  /// \c resetDebugValue(Idx, NewDebugValue) should be used instead.
  /// \param Old Address of the tracked debug-value pointer being replaced.
  /// \param NewDebugValue Replacement metadata.
  LLVM_ABI void handleChangedValue(void *Old, Metadata *NewDebugValue);
  /// Default-construct with no tracked debug-value operands.
  DebugValueUser() = default;
  /// Construct a debug-value user tracking the given metadata operands.
  /// \param DebugValues Debug-value metadata operands to track.
  explicit DebugValueUser(std::array<Metadata *, 3> DebugValues)
      : DebugValues(DebugValues) {
    trackDebugValues();
  }
  /// Move-construct and transfer debug-value operand tracking from \p X.
  /// \param X Source debug-value user.
  DebugValueUser(DebugValueUser &&X) {
    DebugValues = X.DebugValues;
    retrackDebugValues(X);
  }
  /// Copy-construct and register tracking for the same debug-value operands.
  /// \param X Source debug-value user.
  DebugValueUser(const DebugValueUser &X) {
    DebugValues = X.DebugValues;
    trackDebugValues();
  }

  /// Move-assign and transfer debug-value operand tracking from \p X.
  /// \param X Source debug-value user.
  /// \return A reference to this object.
  DebugValueUser &operator=(DebugValueUser &&X) {
    if (&X == this)
      return *this;

    untrackDebugValues();
    DebugValues = X.DebugValues;
    retrackDebugValues(X);
    return *this;
  }

  /// Copy-assign and re-register tracking for \p X's debug-value operands.
  /// \param X Source debug-value user.
  /// \return A reference to this object.
  DebugValueUser &operator=(const DebugValueUser &X) {
    if (&X == this)
      return *this;

    untrackDebugValues();
    DebugValues = X.DebugValues;
    trackDebugValues();
    return *this;
  }

  /// Untrack debug-value operands on destruction.
  ~DebugValueUser() { untrackDebugValues(); }

  /// Clear and untrack all debug-value operands.
  void resetDebugValues() {
    untrackDebugValues();
    DebugValues.fill(nullptr);
  }

  /// Replace the debug-value operand at \p Idx with \p DebugValue.
  /// \param Idx Index of the debug-value operand to replace (0..2).
  /// \param DebugValue Replacement metadata, or null.
  void resetDebugValue(size_t Idx, Metadata *DebugValue) {
    assert(Idx < 3 && "Invalid debug value index.");
    untrackDebugValue(Idx);
    DebugValues[Idx] = DebugValue;
    trackDebugValue(Idx);
  }

  /// Return true if the tracked debug-value operands equal those of \p X.
  /// \param X Other debug-value user to compare against.
  /// \return True if the tracked operands equal those of \p X.
  bool operator==(const DebugValueUser &X) const {
    return DebugValues == X.DebugValues;
  }
  /// Return true if the tracked debug-value operands differ from \p X.
  /// \param X Other debug-value user to compare against.
  /// \return True if the tracked operands differ from \p X.
  bool operator!=(const DebugValueUser &X) const {
    return DebugValues != X.DebugValues;
  }

private:
  LLVM_ABI void trackDebugValue(size_t Idx);
  LLVM_ABI void trackDebugValues();

  LLVM_ABI void untrackDebugValue(size_t Idx);
  LLVM_ABI void untrackDebugValues();

  LLVM_ABI void retrackDebugValues(DebugValueUser &X);
};

/// API for tracking metadata references through RAUW and deletion.
///
/// Shared API for updating \a Metadata pointers in subclasses that support
/// RAUW.
///
/// This API is not meant to be used directly.  See \a TrackingMDRef for a
/// user-friendly tracking reference.
class MetadataTracking {
public:
  /// Track the reference to metadata.
  ///
  /// Register \c MD with \c *MD, if the subclass supports tracking.  If \c *MD
  /// gets RAUW'ed, \c MD will be updated to the new address.  If \c *MD gets
  /// deleted, \c MD will be set to \c nullptr.
  ///
  /// If tracking isn't supported, \c *MD will not change.
  ///
  /// \param MD Reference to the metadata pointer to track.
  /// \return true iff tracking is supported by \c MD.
  static bool track(Metadata *&MD) {
    return track(&MD, *MD, static_cast<Metadata *>(nullptr));
  }

  /// Track the reference to metadata for \a Metadata.
  ///
  /// As \a track(Metadata*&), but with support for calling back to \c Owner to
  /// tell it that its operand changed.  This could trigger \c Owner being
  /// re-uniqued.
  /// \param Ref Address of the tracking pointer.
  /// \param MD Metadata being tracked.
  /// \param Owner Metadata owner that receives change callbacks.
  /// \return True iff tracking is supported by \p MD.
  static bool track(void *Ref, Metadata &MD, Metadata &Owner) {
    return track(Ref, MD, &Owner);
  }

  /// Track the reference to metadata for \a MetadataAsValue.
  ///
  /// As \a track(Metadata*&), but with support for calling back to \c Owner to
  /// tell it that its operand changed.  This could trigger \c Owner being
  /// re-uniqued.
  /// \param Ref Address of the tracking pointer.
  /// \param MD Metadata being tracked.
  /// \param Owner MetadataAsValue owner that receives change callbacks.
  /// \return True iff tracking is supported by \p MD.
  static bool track(void *Ref, Metadata &MD, MetadataAsValue &Owner) {
    return track(Ref, MD, &Owner);
  }

  /// Track the reference to metadata for \a DebugValueUser.
  ///
  /// As \a track(Metadata*&), but with support for calling back to \c Owner to
  /// tell it that its operand changed.  This could trigger \c Owner being
  /// re-uniqued.
  /// \param Ref Address of the tracking pointer.
  /// \param MD Metadata being tracked.
  /// \param Owner DebugValueUser owner that receives change callbacks.
  /// \return True iff tracking is supported by \p MD.
  static bool track(void *Ref, Metadata &MD, DebugValueUser &Owner) {
    return track(Ref, MD, &Owner);
  }

  /// Stop tracking a reference to metadata.
  ///
  /// Stops \c *MD from tracking \c MD.
  /// \param MD Reference whose tracking should be stopped.
  static void untrack(Metadata *&MD) { untrack(&MD, *MD); }
  /// Stop tracking metadata referenced through \p Ref.
  /// \param Ref Address of the tracking pointer.
  /// \param MD Metadata whose tracking should be stopped.
  LLVM_ABI static void untrack(void *Ref, Metadata &MD);

  /// Move tracking from one reference to another.
  ///
  /// Semantically equivalent to \c untrack(MD) followed by \c track(New),
  /// except that ownership callbacks are maintained.
  ///
  /// Note: it is an error if \c *MD does not equal \c New.
  ///
  /// \param MD Current tracking reference.
  /// \param New Destination tracking reference.
  /// \return true iff tracking is supported by \c MD.
  static bool retrack(Metadata *&MD, Metadata *&New) {
    return retrack(&MD, *MD, &New);
  }
  /// Move tracking of \p MD from reference \p Ref to reference \p New.
  /// \param Ref Address of the current tracking pointer.
  /// \param MD Metadata being retracked.
  /// \param New Address of the new tracking pointer.
  /// \return True iff tracking is supported by \p MD.
  LLVM_ABI static bool retrack(void *Ref, Metadata &MD, void *New);

  /// Check whether metadata is replaceable.
  /// \param MD Metadata to test.
  /// \return True if \p MD supports RAUW.
  LLVM_ABI static bool isReplaceable(const Metadata &MD);

  /// Union of owners that may track a metadata reference.
  using OwnerTy = PointerUnion<MetadataAsValue *, Metadata *, DebugValueUser *>;

private:
  /// Track a reference to metadata for an owner.
  ///
  /// Generalized version of tracking.
  LLVM_ABI static bool track(void *Ref, Metadata &MD, OwnerTy Owner);
};

/// Shared implementation of use-lists for replaceable metadata.
///
/// Most metadata cannot be RAUW'ed.  This is a shared implementation of
/// use-lists and associated API for the three that support it (
/// \a ValueAsMetadata, \a TempMDNode, and \a DIArgList).
class ReplaceableMetadataImpl {
  friend class MetadataTracking;

public:
  /// Owner type shared with \c MetadataTracking.
  using OwnerTy = MetadataTracking::OwnerTy;

private:
  LLVMContext &Context;
  uint64_t NextIndex = 0;
  SmallDenseMap<void *, std::pair<OwnerTy, uint64_t>, 4> UseMap;

public:
  /// Construct replaceable-metadata support for \p Context.
  /// \param Context LLVM context associated with this replaceable metadata.
  ReplaceableMetadataImpl(LLVMContext &Context) : Context(Context) {}

  /// Destroy replaceable metadata once all uses have been dropped.
  ~ReplaceableMetadataImpl() {
    assert(UseMap.empty() && "Cannot destroy in-use replaceable metadata");
  }

  /// Return the LLVM context associated with this replaceable metadata.
  /// \return The associated \c LLVMContext.
  LLVMContext &getContext() const { return Context; }

  /// Replace all uses of this with MD.
  ///
  /// Replace all uses of this with \c MD, which is allowed to be null.
  /// \param MD Replacement metadata, or null.
  LLVM_ABI void replaceAllUsesWith(Metadata *MD);
  /// Replace uses of constant \p C with undef in debug-info metadata.
  /// \param C Constant whose debug-info uses should be salvaged.
  LLVM_ABI static void SalvageDebugInfo(const Constant &C);
  /// Returns the list of all DIArgList users of this.
  /// \return All \c DIArgList users of this metadata.
  LLVM_ABI SmallVector<Metadata *> getAllArgListUsers();
  /// Returns the list of all DbgVariableRecord users of this.
  /// \return All \c DbgVariableRecord users of this metadata.
  LLVM_ABI SmallVector<DbgVariableRecord *> getAllDbgVariableRecordUsers();

  /// Resolve all uses of this.
  ///
  /// Resolve all uses of this, turning off RAUW permanently.  If \c
  /// ResolveUsers, call \a MDNode::resolve() on any users whose last operand
  /// is resolved.
  /// \param ResolveUsers Whether to resolve users that become fully resolved.
  LLVM_ABI void resolveAllUses(bool ResolveUsers = true);

  /// Return the number of tracked uses of this replaceable metadata.
  /// \return The number of tracked uses.
  unsigned getNumUses() const { return UseMap.size(); }

private:
  void addRef(void *Ref, OwnerTy Owner);
  void dropRef(void *Ref);
  void moveRef(void *Ref, void *New, const Metadata &MD);

  /// Lazily construct RAUW support on MD.
  ///
  /// If this is an unresolved MDNode, RAUW support will be created on-demand.
  /// ValueAsMetadata always has RAUW support.
  static ReplaceableMetadataImpl *getOrCreate(Metadata &MD);

  /// Get RAUW support on MD, if it exists.
  static ReplaceableMetadataImpl *getIfExists(Metadata &MD);

  /// Check whether this node will support RAUW.
  ///
  /// Returns \c true unless getOrCreate() would return null.
  static bool isReplaceable(const Metadata &MD);
};

/// Value wrapper in the Metadata hierarchy.
///
/// This is a custom value handle that allows other metadata to refer to
/// classes in the Value hierarchy.
///
/// Because of full uniquing support, each value is only wrapped by a single \a
/// ValueAsMetadata object, so the lookup maps are far more efficient than
/// those using ValueHandleBase.
class ValueAsMetadata : public Metadata, ReplaceableMetadataImpl {
  friend class ReplaceableMetadataImpl;
  friend class LLVMContextImpl;

  Value *V;

  /// Drop users without RAUW (during teardown).
  void dropUsers() {
    ReplaceableMetadataImpl::resolveAllUses(/* ResolveUsers */ false);
  }

protected:
  /// Construct value-as-metadata wrapping \p V with subclass ID \p ID.
  /// \param ID Subclass RTTI identifier.
  /// \param V Wrapped IR value.
  ValueAsMetadata(unsigned ID, Value *V)
      : Metadata(ID, Uniqued), ReplaceableMetadataImpl(V->getContext()), V(V) {
    assert(V && "Expected valid value");
  }

  /// Destroy this value-as-metadata wrapper.
  ~ValueAsMetadata() = default;

public:
  /// Create or return value-as-metadata wrapping \p V.
  /// \param V IR value to wrap as metadata.
  /// \return Value-as-metadata wrapping \p V.
  LLVM_ABI static ValueAsMetadata *get(Value *V);

  /// Return constant-as-metadata for constant value \p C.
  /// \param C Constant value to wrap.
  /// \return Constant-as-metadata for \p C.
  static ConstantAsMetadata *getConstant(Value *C) {
    return cast<ConstantAsMetadata>(get(C));
  }

  /// Return local-as-metadata wrapping a function-local value.
  /// \param Local Function-local value to wrap.
  /// \return Local-as-metadata wrapping \p Local.
  static LocalAsMetadata *getLocal(Value *Local) {
    return cast<LocalAsMetadata>(get(Local));
  }

  /// Return existing value-as-metadata for \p V, or null if none.
  /// \param V Value whose metadata wrapper is requested.
  /// \return Existing value-as-metadata for \p V, or null.
  LLVM_ABI static ValueAsMetadata *getIfExists(Value *V);

  /// Return existing constant-as-metadata for \p C, or null if none.
  /// \param C Constant whose metadata wrapper is requested.
  /// \return Existing constant-as-metadata for \p C, or null.
  static ConstantAsMetadata *getConstantIfExists(Value *C) {
    return cast_or_null<ConstantAsMetadata>(getIfExists(C));
  }

  /// Return local-as-metadata for \p Local if it already exists, else null.
  /// \param Local Local value whose metadata wrapper is requested.
  /// \return Existing local-as-metadata for \p Local, or null.
  static LocalAsMetadata *getLocalIfExists(Value *Local) {
    return cast_or_null<LocalAsMetadata>(getIfExists(Local));
  }

  /// Return the wrapped IR value.
  /// \return The wrapped IR value.
  Value *getValue() const { return V; }
  /// Return the type of the wrapped IR value.
  /// \return The type of the wrapped IR value.
  Type *getType() const { return V->getType(); }
  /// Return the LLVM context of the wrapped value.
  /// \return The LLVM context of the wrapped value.
  LLVMContext &getContext() const { return V->getContext(); }

  /// Return all \c DIArgList users of this replaceable metadata.
  /// \return All \c DIArgList users of this replaceable metadata.
  SmallVector<Metadata *> getAllArgListUsers() {
    return ReplaceableMetadataImpl::getAllArgListUsers();
  }
  /// Return all \c DbgVariableRecord users of this replaceable metadata.
  /// \return All \c DbgVariableRecord users of this replaceable metadata.
  SmallVector<DbgVariableRecord *> getAllDbgVariableRecordUsers() {
    return ReplaceableMetadataImpl::getAllDbgVariableRecordUsers();
  }

  /// Remove and delete \c ValueAsMetadata for \p V when \p V is destroyed.
  /// \param V Value being deleted.
  LLVM_ABI static void handleDeletion(Value *V);
  /// Update metadata after \p From is RAUW'd to \p To.
  /// \param From Value being replaced.
  /// \param To Replacement value.
  LLVM_ABI static void handleRAUW(Value *From, Value *To);

protected:
  /// Handle collisions after \a Value::replaceAllUsesWith().
  ///
  /// RAUW isn't supported directly for \a ValueAsMetadata, but if the wrapped
  /// \a Value gets RAUW'ed and the target already exists, this is used to
  /// merge the two metadata nodes.
  /// Replace all uses of this with \p MD.
  /// \param MD Replacement metadata.
  void replaceAllUsesWith(Metadata *MD) {
    ReplaceableMetadataImpl::replaceAllUsesWith(MD);
  }

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MD Metadata to test.
  /// \return True if \p MD is a \c ValueAsMetadata subclass.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == LocalAsMetadataKind ||
           MD->getMetadataID() == ConstantAsMetadataKind;
  }
};

/// Metadata wrapping a constant IR value.
class ConstantAsMetadata : public ValueAsMetadata {
  friend class ValueAsMetadata;

  ConstantAsMetadata(Constant *C)
      : ValueAsMetadata(ConstantAsMetadataKind, C) {}

public:
  /// Create or return constant-as-metadata for \p C.
  /// \param C Constant value to wrap.
  /// \return Constant-as-metadata for \p C.
  static ConstantAsMetadata *get(Constant *C) {
    return ValueAsMetadata::getConstant(C);
  }

  /// Return existing constant-as-metadata for \p C, or null if none.
  /// \param C Constant whose metadata wrapper is requested.
  /// \return Existing constant-as-metadata for \p C, or null.
  static ConstantAsMetadata *getIfExists(Constant *C) {
    return ValueAsMetadata::getConstantIfExists(C);
  }

  /// Return the wrapped constant value.
  /// \return The wrapped constant value.
  Constant *getValue() const {
    return cast<Constant>(ValueAsMetadata::getValue());
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MD Metadata to test.
  /// \return True if \p MD is a \c ConstantAsMetadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == ConstantAsMetadataKind;
  }
};

/// Metadata wrapping a function-local (non-constant) IR value.
class LocalAsMetadata : public ValueAsMetadata {
  friend class ValueAsMetadata;

  LocalAsMetadata(Value *Local)
      : ValueAsMetadata(LocalAsMetadataKind, Local) {
    assert(!isa<Constant>(Local) && "Expected local value");
  }

public:
  /// Create or return local-as-metadata for \p Local.
  /// \param Local Function-local value to wrap.
  /// \return Local-as-metadata for \p Local.
  static LocalAsMetadata *get(Value *Local) {
    return ValueAsMetadata::getLocal(Local);
  }

  /// Return existing local-as-metadata for \p Local, or null if none.
  /// \param Local Local value whose metadata wrapper is requested.
  /// \return Existing local-as-metadata for \p Local, or null.
  static LocalAsMetadata *getIfExists(Value *Local) {
    return ValueAsMetadata::getLocalIfExists(Local);
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MD Metadata to test.
  /// \return True if \p MD is a \c LocalAsMetadata.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == LocalAsMetadataKind;
  }
};

/// Transitional API for extracting constants from Metadata.
///
/// This namespace contains transitional functions for metadata that points to
/// \a Constants.
///
/// In prehistory -- when metadata was a subclass of \a Value -- \a MDNode
/// operands could refer to any \a Value.  There's was a lot of code like this:
///
/// \code
///     MDNode *N = ...;
///     auto *CI = dyn_cast<ConstantInt>(N->getOperand(2));
/// \endcode
///
/// Now that \a Value and \a Metadata are in separate hierarchies, maintaining
/// the semantics for \a isa(), \a cast(), \a dyn_cast() (etc.) requires three
/// steps: cast in the \a Metadata hierarchy, extraction of the \a Value, and
/// cast in the \a Value hierarchy.  Besides creating boiler-plate, this
/// requires subtle control flow changes.
///
/// The end-goal is to create a new type of metadata, called (e.g.) \a MDInt,
/// so that metadata can refer to numbers without traversing a bridge to the \a
/// Value hierarchy.  In this final state, the code above would look like this:
///
/// \code
///     MDNode *N = ...;
///     auto *MI = dyn_cast<MDInt>(N->getOperand(2));
/// \endcode
///
/// The API in this namespace supports the transition.  \a MDInt doesn't exist
/// yet, and even once it does, changing each metadata schema to use it is its
/// own mini-project.  In the meantime this API prevents us from introducing
/// complex and bug-prone control flow that will disappear in the end.  In
/// particular, the above code looks like this:
///
/// \code
///     MDNode *N = ...;
///     auto *CI = mdconst::dyn_extract<ConstantInt>(N->getOperand(2));
/// \endcode
///
/// The full set of provided functions includes:
///
///   mdconst::hasa                <=> isa
///   mdconst::extract             <=> cast
///   mdconst::extract_or_null     <=> cast_or_null
///   mdconst::dyn_extract         <=> dyn_cast
///   mdconst::dyn_extract_or_null <=> dyn_cast_or_null
///
/// The target of the cast must be a subclass of \a Constant.
namespace mdconst {

namespace detail {
template <typename U, typename V>
using check_has_dereference = decltype(static_cast<V>(*std::declval<U &>()));

template <typename U, typename V>
static constexpr bool HasDereference =
    is_detected<check_has_dereference, U, V>::value;

template <class V, class M> struct IsValidPointer {
  static const bool value = std::is_base_of<Constant, V>::value &&
                            HasDereference<M, const Metadata &>;
};
template <class V, class M> struct IsValidReference {
  static const bool value = std::is_base_of<Constant, V>::value &&
                            std::is_convertible<M, const Metadata &>::value;
};

} // end namespace detail

/// Check whether Metadata has a Value.
///
/// As an analogue to \a isa(), check whether \c MD has an \a Value inside of
/// type \c X.
/// \param MD Metadata pointer that may wrap a constant.
/// \return True if \p MD wraps a constant of type \c X.
template <class X, class Y>
inline std::enable_if_t<detail::IsValidPointer<X, Y>::value, bool>
hasa(Y &&MD) {
  assert(MD && "Null pointer sent into hasa");
  if (auto *V = dyn_cast<ConstantAsMetadata>(MD))
    return isa<X>(V->getValue());
  return false;
}
/// Return true if metadata reference \p MD wraps a constant of type \c X.
/// \param MD Metadata reference that may wrap a constant.
/// \return True if \p MD wraps a constant of type \c X.
template <class X, class Y>
inline std::enable_if_t<detail::IsValidReference<X, Y &>::value, bool>
hasa(Y &MD) {
  return hasa(&MD);
}

/// Extract a Value from Metadata.
///
/// As an analogue to \a cast(), extract the \a Value subclass \c X from \c MD.
/// \param MD Metadata pointer that wraps a constant.
/// \return The constant of type \c X extracted from \p MD.
template <class X, class Y>
inline std::enable_if_t<detail::IsValidPointer<X, Y>::value, X *>
extract(Y &&MD) {
  return cast<X>(cast<ConstantAsMetadata>(MD)->getValue());
}
/// Extract a \c Value from metadata referenced by \p MD.
/// \param MD Metadata reference that wraps a constant.
/// \return The constant of type \c X extracted from \p MD.
template <class X, class Y>
inline std::enable_if_t<detail::IsValidReference<X, Y &>::value, X *>
extract(Y &MD) {
  return extract(&MD);
}

/// Extract a Value from Metadata, allowing null.
///
/// As an analogue to \a cast_or_null(), extract the \a Value subclass \c X
/// from \c MD, allowing \c MD to be null.
/// \param MD Metadata pointer that may be null or wrap a constant.
/// \return The constant of type \c X, or null if \p MD is null.
template <class X, class Y>
inline std::enable_if_t<detail::IsValidPointer<X, Y>::value, X *>
extract_or_null(Y &&MD) {
  if (auto *V = cast_or_null<ConstantAsMetadata>(MD))
    return cast<X>(V->getValue());
  return nullptr;
}

/// Extract a Value from Metadata, if any.
///
/// As an analogue to \a dyn_cast_or_null(), extract the \a Value subclass \c X
/// from \c MD, return null if \c MD doesn't contain a \a Value or if the \a
/// Value it does contain is of the wrong subclass.
/// \param MD Metadata pointer that may wrap a constant.
/// \return The constant of type \c X, or null if absent or wrong type.
template <class X, class Y>
inline std::enable_if_t<detail::IsValidPointer<X, Y>::value, X *>
dyn_extract(Y &&MD) {
  if (auto *V = dyn_cast<ConstantAsMetadata>(MD))
    return dyn_cast<X>(V->getValue());
  return nullptr;
}

/// Extract a Value from Metadata, if any, allowing null.
///
/// As an analogue to \a dyn_cast_or_null(), extract the \a Value subclass \c X
/// from \c MD, return null if \c MD doesn't contain a \a Value or if the \a
/// Value it does contain is of the wrong subclass, allowing \c MD to be null.
/// \param MD Metadata pointer that may be null or wrap a constant.
/// \return The constant of type \c X, or null if \p MD is null/absent/wrong type.
template <class X, class Y>
inline std::enable_if_t<detail::IsValidPointer<X, Y>::value, X *>
dyn_extract_or_null(Y &&MD) {
  if (auto *V = dyn_cast_or_null<ConstantAsMetadata>(MD))
    return dyn_cast<X>(V->getValue());
  return nullptr;
}

} // end namespace mdconst

//===----------------------------------------------------------------------===//
/// A single uniqued string.
///
/// These are used to efficiently contain a byte sequence for metadata.
/// MDString is always unnamed.
class MDString : public Metadata {
  friend class StringMapEntryStorage<MDString>;

  StringMapEntry<MDString> *Entry = nullptr;

  MDString() : Metadata(MDStringKind, Uniqued) {}

public:
  /// Copy construction is deleted; MDString is uniqued and non-copyable.
  /// \param Other Unused; copy construction is deleted.
  MDString(const MDString &Other) = delete;
  /// Move assignment is deleted; MDString is uniqued and non-movable.
  /// \param Other Unused; move assignment is deleted.
  MDString &operator=(MDString &&Other) = delete;
  /// Copy assignment is deleted; MDString is non-copyable.
  /// \param Other Unused; assignment is deleted.
  MDString &operator=(const MDString &Other) = delete;

  /// Create or return uniqued string metadata for \p Str.
  /// \param Context LLVM context that owns the string.
  /// \param Str String contents to unique.
  /// \return Uniqued string metadata for \p Str.
  LLVM_ABI static MDString *get(LLVMContext &Context, StringRef Str);
  /// Create or return uniqued string metadata for a C string.
  /// \param Context LLVM context that owns the string.
  /// \param Str Null-terminated string contents, or null for empty.
  /// \return Uniqued string metadata for \p Str.
  static MDString *get(LLVMContext &Context, const char *Str) {
    return get(Context, Str ? StringRef(Str) : StringRef());
  }
  /// Return existing string metadata for \p Str, or null if none.
  /// \param Context LLVM context that owns the string.
  /// \param Str String contents to look up.
  /// \return Existing string metadata for \p Str, or null.
  LLVM_ABI static MDString *getIfExists(LLVMContext &Context, StringRef Str);

  /// Return the string contents of this metadata.
  /// \return The string contents of this metadata.
  LLVM_ABI StringRef getString() const;

  /// Return the number of bytes in this metadata string.
  /// \return The number of bytes in this metadata string.
  unsigned getLength() const { return (unsigned)getString().size(); }

  /// Iterator over the characters of this string.
  using iterator = StringRef::iterator;

  /// Pointer to the first byte of the string.
  /// \return Pointer to the first byte of the string.
  iterator begin() const { return getString().begin(); }

  /// Pointer to one byte past the end of the string.
  /// \return Pointer to one byte past the end of the string.
  iterator end() const { return getString().end(); }

  /// Return a pointer to the raw bytes of this metadata string.
  /// \return Pointer to the raw bytes of this metadata string.
  const unsigned char *bytes_begin() const { return getString().bytes_begin(); }
  /// Return a pointer one past the last byte of this metadata string.
  /// \return Pointer one past the last byte of this metadata string.
  const unsigned char *bytes_end() const { return getString().bytes_end(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MD Metadata to test.
  /// \return True if \p MD is an \c MDString.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == MDStringKind;
  }
};

/// A collection of metadata nodes that might be associated with a
/// memory access used by the alias-analysis infrastructure.
struct AAMDNodes {
  /// Construct empty alias-analysis metadata nodes.
  explicit AAMDNodes() = default;
  /// Construct from TBAA, TBAA-struct, scope, noalias, and address-space nodes.
  /// \param T TBAA metadata node.
  /// \param TS TBAA-struct metadata node.
  /// \param S Alias-scope metadata node.
  /// \param N Noalias-scope metadata node.
  /// \param NAS Noalias address-space metadata node.
  explicit AAMDNodes(MDNode *T, MDNode *TS, MDNode *S, MDNode *N, MDNode *NAS)
      : TBAA(T), TBAAStruct(TS), Scope(S), NoAlias(N), NoAliasAddrSpace(NAS) {}

  /// Return true if all alias-analysis metadata fields equal those of \p A.
  /// \param A Other AAMDNodes to compare against.
  /// \return True if all alias-analysis metadata fields equal those of \p A.
  bool operator==(const AAMDNodes &A) const {
    return TBAA == A.TBAA && TBAAStruct == A.TBAAStruct && Scope == A.Scope &&
           NoAlias == A.NoAlias && NoAliasAddrSpace == A.NoAliasAddrSpace;
  }

  /// Return true if any alias-analysis metadata field differs from \p A.
  /// \param A Other AAMDNodes to compare against.
  /// \return True if any alias-analysis metadata field differs from \p A.
  bool operator!=(const AAMDNodes &A) const { return !(*this == A); }

  /// Return true if any alias-analysis metadata field is non-null.
  /// \return True if any alias-analysis metadata field is non-null.
  explicit operator bool() const {
    return TBAA || TBAAStruct || Scope || NoAlias || NoAliasAddrSpace;
  }

  /// The tag for type-based alias analysis.
  MDNode *TBAA = nullptr;

  /// The tag for type-based alias analysis (tbaa struct).
  MDNode *TBAAStruct = nullptr;

  /// The tag for alias scope specification (used with noalias).
  MDNode *Scope = nullptr;

  /// The tag specifying the noalias scope.
  MDNode *NoAlias = nullptr;

  /// The tag specifying the noalias address spaces.
  MDNode *NoAliasAddrSpace = nullptr;

  /// Shift TBAA metadata so it starts \p off bytes later.
  /// \param M TBAA metadata to shift.
  /// \param off Byte offset to apply.
  /// \return TBAA metadata shifted by \p off bytes.
  LLVM_ABI static MDNode *shiftTBAA(MDNode *M, size_t off);

  /// Shift tbaa.struct metadata so it starts \p off bytes later.
  /// \param M TBAA-struct metadata to shift.
  /// \param off Byte offset to apply.
  /// \return TBAA-struct metadata shifted by \p off bytes.
  LLVM_ABI static MDNode *shiftTBAAStruct(MDNode *M, size_t off);

  /// Extend TBAA metadata to cover \p len bytes (-1 means unknown size).
  /// \param TBAA TBAA metadata to extend.
  /// \param len Access length in bytes, or -1 if unknown.
  /// \return TBAA metadata extended to cover \p len bytes.
  LLVM_ABI static MDNode *extendToTBAA(MDNode *TBAA, ssize_t len);

  /// Intersect two AAMDNodes sets that apply to the same pointer.
  ///
  /// Returns the best AAMDNodes compatible with both inputs (a set whose
  /// allowable aliasing conclusions are a subset of those allowable by both).
  /// For efficiency, this does not create any new MDNodes.
  /// \param Other Other AAMDNodes to intersect with.
  /// \return The best AAMDNodes compatible with both inputs.
  AAMDNodes intersect(const AAMDNodes &Other) const {
    AAMDNodes Result;
    Result.TBAA = Other.TBAA == TBAA ? TBAA : nullptr;
    Result.TBAAStruct = Other.TBAAStruct == TBAAStruct ? TBAAStruct : nullptr;
    Result.Scope = Other.Scope == Scope ? Scope : nullptr;
    Result.NoAlias = Other.NoAlias == NoAlias ? NoAlias : nullptr;
    Result.NoAliasAddrSpace =
        Other.NoAliasAddrSpace == NoAliasAddrSpace ? NoAliasAddrSpace : nullptr;
    return Result;
  }

  /// Create a new AAMDNode after applying a constant offset to the pointer.
  /// \param Offset Byte offset applied to the start of the pointer.
  /// \return AAMDNodes with TBAA fields shifted by \p Offset.
  AAMDNodes shift(size_t Offset) const {
    AAMDNodes Result;
    Result.TBAA = TBAA ? shiftTBAA(TBAA, Offset) : nullptr;
    Result.TBAAStruct =
        TBAAStruct ? shiftTBAAStruct(TBAAStruct, Offset) : nullptr;
    Result.Scope = Scope;
    Result.NoAlias = NoAlias;
    Result.NoAliasAddrSpace = NoAliasAddrSpace;
    return Result;
  }

  /// Create a new AAMDNode after extending coverage to \p Len bytes.
  ///
  /// A size of -1 denotes an unknown size.
  /// \param Len Access length in bytes, or -1 if unknown.
  /// \return AAMDNodes with TBAA coverage extended to \p Len bytes.
  AAMDNodes extendTo(ssize_t Len) const {
    AAMDNodes Result;
    Result.TBAA = TBAA ? extendToTBAA(TBAA, Len) : nullptr;
    // tbaa.struct contains (offset, size, type) triples. Extending the length
    // of the tbaa.struct doesn't require changing this (though more information
    // could be provided by adding more triples at subsequent lengths).
    Result.TBAAStruct = TBAAStruct;
    Result.Scope = Scope;
    Result.NoAlias = NoAlias;
    Result.NoAliasAddrSpace = NoAliasAddrSpace;
    return Result;
  }

  /// Merge AAMDNodes that apply to potentially different locations.
  /// \param Other Other AAMDNodes to merge with.
  /// \return Merged AAMDNodes for potentially different locations.
  LLVM_ABI AAMDNodes merge(const AAMDNodes &Other) const;

  /// Concatenate AAMDNodes for adjacent, non-overlapping locations.
  ///
  /// Unlike \c merge, where different locations should overlap each other,
  /// \c concat puts non-overlapping locations together.
  /// \param Other Other AAMDNodes to concatenate with.
  /// \return Concatenated AAMDNodes for adjacent non-overlapping locations.
  LLVM_ABI AAMDNodes concat(const AAMDNodes &Other) const;

  /// Adjust this AAMDNode for an access of \p AccessSize bytes.
  ///
  /// If this AAMDNode has !tbaa.struct and \p AccessSize matches the size of
  /// the field at offset 0, get the TBAA tag describing the accessed field.
  /// If such an AAMDNode already embeds !tbaa, the existing one is retrieved.
  /// Finally, !tbaa.struct is zeroed out.
  /// \param AccessSize Size in bytes of the accessed region.
  /// \return AAMDNodes adjusted for an \p AccessSize-byte access.
  LLVM_ABI AAMDNodes adjustForAccess(unsigned AccessSize);
  /// Adjust this AAMDNode for an access of type \p AccessTy at \p Offset.
  /// \param Offset Byte offset into the pointed-to object.
  /// \param AccessTy Type of the accessed value.
  /// \param DL Data layout used to compute the access size.
  /// \return AAMDNodes adjusted for an access of type \p AccessTy at \p Offset.
  LLVM_ABI AAMDNodes adjustForAccess(size_t Offset, Type *AccessTy,
                                     const DataLayout &DL);
  /// Adjust this AAMDNode for an \p AccessSize-byte access at \p Offset.
  /// \param Offset Byte offset into the pointed-to object.
  /// \param AccessSize Size in bytes of the accessed region.
  /// \return AAMDNodes adjusted for an \p AccessSize-byte access at \p Offset.
  LLVM_ABI AAMDNodes adjustForAccess(size_t Offset, unsigned AccessSize);
};

/// DenseMapInfo specialization for hashing and comparing \c AAMDNodes.
template <> struct DenseMapInfo<AAMDNodes> {
  /// Hash the AAMDNodes fields of \p Val.
  /// \param Val AAMDNodes whose fields are hashed.
  /// \return A hash of the AAMDNodes fields of \p Val.
  static unsigned getHashValue(const AAMDNodes &Val) {
    return DenseMapInfo<MDNode *>::getHashValue(Val.TBAA) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.TBAAStruct) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.Scope) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.NoAlias) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.NoAliasAddrSpace);
  }

  /// Return true if \p LHS and \p RHS compare equal.
  /// \param LHS First AAMDNodes to compare.
  /// \param RHS Second AAMDNodes to compare.
  /// \return True if \p LHS and \p RHS compare equal.
  static bool isEqual(const AAMDNodes &LHS, const AAMDNodes &RHS) {
    return LHS == RHS;
  }
};

/// Tracking metadata reference owned by Metadata.
///
/// Similar to \a TrackingMDRef, but it's expected to be owned by an instance
/// of \a Metadata, which has the option of registering itself for callbacks to
/// re-unique itself.
///
/// In particular, this is used by \a MDNode.
class MDOperand {
  Metadata *MD = nullptr;

public:
  /// Default-construct an empty metadata operand.
  MDOperand() = default;
  /// Copy construction is deleted; MDOperand is non-copyable.
  /// \param Other Unused; copy construction is deleted.
  MDOperand(const MDOperand &Other) = delete;
  /// Move construction transfers tracked metadata ownership.
  /// \param Op Source operand whose tracking is transferred.
  MDOperand(MDOperand &&Op) {
    MD = Op.MD;
    if (MD)
      (void)MetadataTracking::retrack(Op.MD, MD);
    Op.MD = nullptr;
  }
  /// Copy assignment is deleted; \c MDOperand tracks metadata by address.
  /// \param Other Unused; copy assignment is deleted.
  MDOperand &operator=(const MDOperand &Other) = delete;
  /// Move-assign and transfer tracked metadata ownership from \p Op.
  /// \param Op Source operand whose tracking is transferred.
  /// \return A reference to this operand.
  MDOperand &operator=(MDOperand &&Op) {
    MD = Op.MD;
    if (MD)
      (void)MetadataTracking::retrack(Op.MD, MD);
    Op.MD = nullptr;
    return *this;
  }

  /// Return true if this operand is an \c MDString equal to \p Str.
  /// \param Str String to compare against.
  /// \return True if this operand is an \c MDString equal to \p Str.
  bool equalsStr(StringRef Str) const {
    return isa_and_nonnull<MDString>(get()) &&
           cast<MDString>(get())->getString() == Str;
  }

  /// Stop tracking the referenced metadata.
  ~MDOperand() { untrack(); }

  /// Return the referenced metadata pointer.
  /// \return The referenced metadata pointer.
  Metadata *get() const { return MD; }
  /// Convert to the referenced metadata pointer.
  /// \return The referenced metadata pointer.
  operator Metadata *() const { return get(); }
  /// Access members of the referenced metadata.
  /// \return The referenced metadata pointer.
  Metadata *operator->() const { return get(); }
  /// Dereference to the referenced metadata.
  /// \return A reference to the referenced metadata.
  Metadata &operator*() const { return *get(); }

  /// Stop tracking and clear the referenced metadata.
  void reset() {
    untrack();
    MD = nullptr;
  }
  /// Reset to track \p MD on behalf of optional owner \p Owner.
  /// \param MD Metadata to track, or null.
  /// \param Owner Owning metadata that receives change callbacks, or null.
  void reset(Metadata *MD, Metadata *Owner) {
    untrack();
    this->MD = MD;
    track(Owner);
  }

private:
  void track(Metadata *Owner) {
    if (MD) {
      if (Owner)
        MetadataTracking::track(this, *MD, *Owner);
      else
        MetadataTracking::track(MD);
    }
  }

  void untrack() {
    assert(static_cast<void *>(this) == &MD && "Expected same address");
    if (MD)
      MetadataTracking::untrack(MD);
  }
};

/// simplify_type specialization exposing \c MDOperand as \c Metadata *.
template <> struct simplify_type<MDOperand> {
  /// Simplified type for casting helpers.
  using SimpleType = Metadata *;

  /// Return the metadata pointer held by \p MD.
  /// \param MD Operand whose metadata pointer is returned.
  /// \return The metadata pointer held by \p MD.
  static SimpleType getSimplifiedValue(MDOperand &MD) { return MD.get(); }
};

/// simplify_type specialization exposing const \c MDOperand as \c Metadata *.
template <> struct simplify_type<const MDOperand> {
  /// Simplified type for casting helpers.
  using SimpleType = Metadata *;

  /// Return the metadata pointer held by \p MD.
  /// \param MD Operand whose metadata pointer is returned.
  /// \return The metadata pointer held by \p MD.
  static SimpleType getSimplifiedValue(const MDOperand &MD) { return MD.get(); }
};

/// Pointer to the context, with optional RAUW support.
///
/// Either a raw (non-null) pointer to the \a LLVMContext, or an owned pointer
/// to \a ReplaceableMetadataImpl (which has a reference to \a LLVMContext).
class ContextAndReplaceableUses {
  PointerUnion<LLVMContext *, ReplaceableMetadataImpl *> Ptr;

public:
  /// Construct a handle that refers directly to \p Context.
  /// \param Context LLVM context referenced without RAUW support.
  ContextAndReplaceableUses(LLVMContext &Context) : Ptr(&Context) {}
  /// Construct a handle that owns replaceable-uses support.
  /// \param ReplaceableUses Owned RAUW support (must be non-null).
  ContextAndReplaceableUses(
      std::unique_ptr<ReplaceableMetadataImpl> ReplaceableUses)
      : Ptr(ReplaceableUses.release()) {
    assert(getReplaceableUses() && "Expected non-null replaceable uses");
  }
  /// Default construction is deleted; a context is always required.
  ContextAndReplaceableUses() = delete;
  /// Move construction is deleted; this handle is non-movable.
  /// \param Other Unused; move construction is deleted.
  ContextAndReplaceableUses(ContextAndReplaceableUses &&Other) = delete;
  /// Copy construction is deleted; this handle is non-copyable.
  /// \param Other Unused; copy construction is deleted.
  ContextAndReplaceableUses(const ContextAndReplaceableUses &Other) = delete;
  /// Move assignment is deleted; this handle is non-movable.
  /// \param Other Unused; move assignment is deleted.
  ContextAndReplaceableUses &
  operator=(ContextAndReplaceableUses &&Other) = delete;
  /// Copy assignment is deleted; this handle is non-copyable.
  /// \param Other Unused; copy assignment is deleted.
  ContextAndReplaceableUses &
  operator=(const ContextAndReplaceableUses &Other) = delete;
  /// Destroy owned replaceable-uses support, if any.
  ~ContextAndReplaceableUses() { delete getReplaceableUses(); }

  /// Convert to the associated \c LLVMContext.
  /// \return The associated \c LLVMContext.
  operator LLVMContext &() { return getContext(); }

  /// Whether this contains RAUW support.
  /// \return True if this contains RAUW support.
  bool hasReplaceableUses() const {
    return isa<ReplaceableMetadataImpl *>(Ptr);
  }

  /// Return the \c LLVMContext associated with this node.
  /// \return The \c LLVMContext associated with this node.
  LLVMContext &getContext() const {
    if (hasReplaceableUses())
      return getReplaceableUses()->getContext();
    return *cast<LLVMContext *>(Ptr);
  }

  /// Return replaceable-uses support if present, else null.
  /// \return Replaceable-uses support if present, else null.
  ReplaceableMetadataImpl *getReplaceableUses() const {
    if (hasReplaceableUses())
      return cast<ReplaceableMetadataImpl *>(Ptr);
    return nullptr;
  }

  /// Ensure that this has RAUW support, and then return it.
  /// \return Replaceable-uses support, creating it if needed.
  ReplaceableMetadataImpl *getOrCreateReplaceableUses() {
    if (!hasReplaceableUses())
      makeReplaceable(std::make_unique<ReplaceableMetadataImpl>(getContext()));
    return getReplaceableUses();
  }

  /// Assign RAUW support to this.
  ///
  /// Make this replaceable, taking ownership of \c ReplaceableUses (which must
  /// not be null).
  /// \param ReplaceableUses Owned RAUW support to install.
  void
  makeReplaceable(std::unique_ptr<ReplaceableMetadataImpl> ReplaceableUses) {
    assert(ReplaceableUses && "Expected non-null replaceable uses");
    assert(&ReplaceableUses->getContext() == &getContext() &&
           "Expected same context");
    delete getReplaceableUses();
    Ptr = ReplaceableUses.release();
  }

  /// Drop RAUW support.
  ///
  /// Cede ownership of RAUW support, returning it.
  /// \return Owned RAUW support previously held by this handle.
  std::unique_ptr<ReplaceableMetadataImpl> takeReplaceableUses() {
    assert(hasReplaceableUses() && "Expected to own replaceable uses");
    std::unique_ptr<ReplaceableMetadataImpl> ReplaceableUses(
        getReplaceableUses());
    Ptr = &ReplaceableUses->getContext();
    return ReplaceableUses;
  }
};

/// Deleter for temporary MDNodes used with \c std::unique_ptr.
struct TempMDNodeDeleter {
  /// Delete temporary metadata node \p Node.
  inline void operator()(MDNode *Node) const;
};

#define HANDLE_MDNODE_LEAF(CLASS)                                              \
  using Temp##CLASS = std::unique_ptr<CLASS, TempMDNodeDeleter>;
#define HANDLE_MDNODE_BRANCH(CLASS) HANDLE_MDNODE_LEAF(CLASS)
#include "llvm/IR/Metadata.def"

/// Metadata node.
///
/// Metadata nodes can be uniqued, like constants, or distinct.  Temporary
/// metadata nodes (with full support for RAUW) can be used to delay uniquing
/// until forward references are known.  The basic metadata node is an \a
/// MDTuple.
///
/// There is limited support for RAUW at construction time.  At construction
/// time, if any operand is a temporary node (or an unresolved uniqued node,
/// which indicates a transitive temporary operand), the node itself will be
/// unresolved.  As soon as all operands become resolved, it will drop RAUW
/// support permanently.
///
/// If an unresolved node is part of a cycle, \a resolveCycles() needs
/// to be called on some member of the cycle once all temporary nodes have been
/// replaced.
///
/// MDNodes can be large or small, as well as resizable or non-resizable.
/// Large MDNodes' operands are allocated in a separate storage vector,
/// whereas small MDNodes' operands are co-allocated. Distinct and temporary
/// MDnodes are resizable, but only MDTuples support this capability.
///
/// Clients can add operands to resizable MDNodes using push_back().
class MDNode : public Metadata {
  friend class ReplaceableMetadataImpl;
  friend class LLVMContextImpl;
  friend class DIAssignID;

  /// The header that is coallocated with an MDNode along with its "small"
  /// operands. It is located immediately before the main body of the node.
  /// The operands are in turn located immediately before the header.
  /// For resizable MDNodes, the space for the storage vector is also allocated
  /// immediately before the header, overlapping with the operands.
  /// Explicity set alignment because bitfields by default have an
  /// alignment of 1 on z/OS.
  struct alignas(alignof(size_t)) Header {
    size_t IsResizable : 1;
    size_t IsLarge : 1;
    size_t SmallSize : 4;
    size_t SmallNumOps : 4;
    size_t : sizeof(size_t) * CHAR_BIT - 10;

    unsigned NumUnresolved = 0;
    using LargeStorageVector = SmallVector<MDOperand, 0>;

    static constexpr size_t NumOpsFitInVector =
        sizeof(LargeStorageVector) / sizeof(MDOperand);
    static_assert(
        NumOpsFitInVector * sizeof(MDOperand) == sizeof(LargeStorageVector),
        "sizeof(LargeStorageVector) must be a multiple of sizeof(MDOperand)");

    static constexpr size_t MaxSmallSize = 15;

    static constexpr size_t getOpSize(unsigned NumOps) {
      return sizeof(MDOperand) * NumOps;
    }
    /// Returns the number of operands the node has space for based on its
    /// allocation characteristics.
    static size_t getSmallSize(size_t NumOps, bool IsResizable, bool IsLarge) {
      return IsLarge ? NumOpsFitInVector
                     : std::max(NumOps, NumOpsFitInVector * IsResizable);
    }
    /// Returns the number of bytes allocated for operands and header.
    static size_t getAllocSize(StorageType Storage, size_t NumOps) {
      return getOpSize(
                 getSmallSize(NumOps, isResizable(Storage), isLarge(NumOps))) +
             sizeof(Header);
    }

    /// Only temporary and distinct nodes are resizable.
    static bool isResizable(StorageType Storage) { return Storage != Uniqued; }
    static bool isLarge(size_t NumOps) { return NumOps > MaxSmallSize; }

    size_t getAllocSize() const {
      return getOpSize(SmallSize) + sizeof(Header);
    }
    void *getAllocation() {
      return reinterpret_cast<char *>(this + 1) -
             alignTo(getAllocSize(), alignof(uint64_t));
    }

    void *getLargePtr() const {
      static_assert(alignof(LargeStorageVector) <= alignof(Header),
                    "LargeStorageVector too strongly aligned");
      return reinterpret_cast<char *>(const_cast<Header *>(this)) -
             sizeof(LargeStorageVector);
    }

    LLVM_ABI void *getSmallPtr();

    LargeStorageVector &getLarge() {
      assert(IsLarge);
      return *reinterpret_cast<LargeStorageVector *>(getLargePtr());
    }

    const LargeStorageVector &getLarge() const {
      assert(IsLarge);
      return *reinterpret_cast<const LargeStorageVector *>(getLargePtr());
    }

    LLVM_ABI void resizeSmall(size_t NumOps);
    LLVM_ABI void resizeSmallToLarge(size_t NumOps);
    LLVM_ABI void resize(size_t NumOps);

    LLVM_ABI explicit Header(size_t NumOps, StorageType Storage);
    LLVM_ABI ~Header();

    MutableArrayRef<MDOperand> operands() {
      if (IsLarge)
        return getLarge();
      return MutableArrayRef(
          reinterpret_cast<MDOperand *>(this) - SmallSize, SmallNumOps);
    }

    ArrayRef<MDOperand> operands() const {
      if (IsLarge)
        return getLarge();
      return ArrayRef(reinterpret_cast<const MDOperand *>(this) - SmallSize,
                      SmallNumOps);
    }

    unsigned getNumOperands() const {
      if (!IsLarge)
        return SmallNumOps;
      return getLarge().size();
    }
  };

  Header &getHeader() { return *(reinterpret_cast<Header *>(this) - 1); }

  const Header &getHeader() const {
    return *(reinterpret_cast<const Header *>(this) - 1);
  }

  ContextAndReplaceableUses Context;

protected:
  /// Construct an MDNode in \p Context with subclass \p ID and operands.
  /// \param Context LLVM context that owns the node.
  /// \param ID Subclass RTTI identifier.
  /// \param Storage Storage/uniquing kind for the node.
  /// \param Ops1 First operand array.
  /// \param Ops2 Optional second operand array appended after \p Ops1.
  LLVM_ABI MDNode(LLVMContext &Context, unsigned ID, StorageType Storage,
                  ArrayRef<Metadata *> Ops1, ArrayRef<Metadata *> Ops2 = {});
  /// Destroy this metadata node.
  ~MDNode() = default;

  /// Allocate an \c MDNode with \p NumOps operand slots and \p Storage kind.
  /// \param Size Size of the MDNode subclass object.
  /// \param NumOps Number of operand slots to allocate.
  /// \param Storage Storage/uniquing kind for the node.
  /// \return Raw storage for an \c MDNode with \p NumOps operand slots.
  LLVM_ABI void *operator new(size_t Size, size_t NumOps, StorageType Storage);
  /// Deallocate an \c MDNode allocated with the matching placement \c new.
  /// \param Mem Memory returned by the matching placement new.
  LLVM_ABI void operator delete(void *Mem);

  /// Required by std, but never called.
  /// \param Mem Unused allocation pointer.
  /// \param NumOps Unused operand count.
  void operator delete(void *Mem, unsigned NumOps) {
    (void)Mem;
    (void)NumOps;
    llvm_unreachable("Constructor throws?");
  }

  /// Required by std, but never called.
  /// \param Mem Unused allocation pointer.
  /// \param NumOps Unused operand count.
  /// \param IsResizable Unused resizable flag.
  void operator delete(void *Mem, unsigned NumOps, bool IsResizable) {
    (void)Mem;
    (void)NumOps;
    (void)IsResizable;
    llvm_unreachable("Constructor throws?");
  }

  /// Drop all references held by this metadata node.
  LLVM_ABI void dropAllReferences();

  /// Return an iterator to the first mutable operand.
  /// \return Iterator to the first mutable operand.
  MDOperand *mutable_begin() { return getHeader().operands().begin(); }
  /// Return an iterator past the last mutable operand.
  /// \return Iterator past the last mutable operand.
  MDOperand *mutable_end() { return getHeader().operands().end(); }

  /// Range type over mutable \c MDOperand elements.
  using mutable_op_range = iterator_range<MDOperand *>;

  /// Return a mutable range over this node's operands.
  /// \return A mutable range over this node's operands.
  mutable_op_range mutable_operands() {
    return mutable_op_range(mutable_begin(), mutable_end());
  }

public:
  /// Copy construction is deleted; MDNode is non-copyable.
  /// \param Other Unused; copy construction is deleted.
  MDNode(const MDNode &Other) = delete;
  /// Copy assignment is deleted; MDNode is non-copyable.
  /// \param Other Unused; copy assignment is deleted.
  void operator=(const MDNode &Other) = delete;
  /// Ordinary allocation is deleted; use the custom MDNode allocators.
  /// \param Size Unused; ordinary new is deleted.
  void *operator new(size_t Size) = delete;

  /// Return a uniqued tuple metadata node with operands \p MDs.
  /// \param Context LLVM context that owns the node.
  /// \param MDs Operand metadata.
  /// \return A uniqued tuple metadata node with operands \p MDs.
  static inline MDTuple *get(LLVMContext &Context, ArrayRef<Metadata *> MDs);
  /// Return an existing uniqued tuple for \p MDs, or null if none.
  /// \param Context LLVM context that owns the node.
  /// \param MDs Operand metadata.
  /// \return An existing uniqued tuple for \p MDs, or null.
  static inline MDTuple *getIfExists(LLVMContext &Context,
                                     ArrayRef<Metadata *> MDs);
  /// Return a distinct (non-uniqued) tuple with operands \p MDs.
  /// \param Context LLVM context that owns the node.
  /// \param MDs Operand metadata.
  /// \return A distinct (non-uniqued) tuple with operands \p MDs.
  static inline MDTuple *getDistinct(LLVMContext &Context,
                                     ArrayRef<Metadata *> MDs);
  /// Return a temporary tuple with operands \p MDs.
  /// \param Context LLVM context that owns the node.
  /// \param MDs Operand metadata.
  /// \return A temporary tuple with operands \p MDs.
  static inline TempMDTuple getTemporary(LLVMContext &Context,
                                         ArrayRef<Metadata *> MDs);

  /// Create a (temporary) clone of this.
  /// \return A temporary clone of this node.
  LLVM_ABI TempMDNode clone() const;

  /// Deallocate a node created by getTemporary.
  ///
  /// Calls \c replaceAllUsesWith(nullptr) before deleting, so any remaining
  /// references will be reset.
  /// \param N Temporary node to delete.
  LLVM_ABI static void deleteTemporary(MDNode *N);

  /// Return the LLVM context that owns this node.
  /// \return The LLVM context that owns this node.
  LLVMContext &getContext() const { return Context.getContext(); }

  /// Replace a specific operand.
  /// \param I Zero-based operand index.
  /// \param New Replacement metadata operand.
  LLVM_ABI void replaceOperandWith(unsigned I, Metadata *New);

  /// Check if node is fully resolved.
  ///
  /// If \a isTemporary(), this always returns \c false; if \a isDistinct(),
  /// this always returns \c true.
  ///
  /// If \a isUniqued(), returns \c true if this has already dropped RAUW
  /// support (because all operands are resolved).
  ///
  /// As forward declarations are resolved, their containers should get
  /// resolved automatically.  However, if this (or one of its operands) is
  /// involved in a cycle, \a resolveCycles() needs to be called explicitly.
  /// \return True if this node is fully resolved.
  bool isResolved() const { return !isTemporary() && !getNumUnresolved(); }

  /// Return true if this node is uniqued in the context.
  /// \return True if this node is uniqued in the context.
  bool isUniqued() const { return Storage == Uniqued; }
  /// Return true if this node is distinct (not uniqued).
  /// \return True if this node is distinct (not uniqued).
  bool isDistinct() const { return Storage == Distinct; }
  /// Return true if this is a temporary (forward-declaration) metadata node.
  /// \return True if this is a temporary metadata node.
  bool isTemporary() const { return Storage == Temporary; }

  /// Return true if this node supports RAUW replacement.
  /// \return True if this node supports RAUW replacement.
  bool isReplaceable() const { return isTemporary() || isAlwaysReplaceable(); }
  /// Return true if this node kind is always RAUW-replaceable (e.g. DIAssignID).
  /// \return True if this node kind is always RAUW-replaceable.
  bool isAlwaysReplaceable() const { return getMetadataID() == DIAssignIDKind; }

  /// Return the number of uses of this temporary metadata node.
  /// \return The number of uses of this temporary metadata node.
  unsigned getNumTemporaryUses() const {
    assert(isTemporary() && "Only for temporaries");
    return Context.getReplaceableUses()->getNumUses();
  }

  /// RAUW a temporary.
  ///
  /// \pre \a isTemporary() must be \c true.
  /// \param MD Replacement metadata, or null.
  void replaceAllUsesWith(Metadata *MD) {
    assert(isReplaceable() && "Expected temporary/replaceable node");
    if (Context.hasReplaceableUses())
      Context.getReplaceableUses()->replaceAllUsesWith(MD);
  }

  /// Resolve cycles.
  ///
  /// Once all forward declarations have been resolved, force cycles to be
  /// resolved.
  ///
  /// \pre No operands (or operands' operands, etc.) have \a isTemporary().
  LLVM_ABI void resolveCycles();

  /// Resolve a unique, unresolved node.
  LLVM_ABI void resolve();

  /// Replace a temporary node with a permanent one.
  ///
  /// Try to create a uniqued version of \c N -- in place, if possible -- and
  /// return it.  If \c N cannot be uniqued, return a distinct node instead.
  /// \param N Temporary node to promote to permanent storage.
  /// \return A permanent (uniqued or distinct) replacement for \p N.
  template <class T>
  static std::enable_if_t<std::is_base_of<MDNode, T>::value, T *>
  replaceWithPermanent(std::unique_ptr<T, TempMDNodeDeleter> N) {
    return cast<T>(N.release()->replaceWithPermanentImpl());
  }

  /// Replace a temporary node with a uniqued one.
  ///
  /// Create a uniqued version of \c N -- in place, if possible -- and return
  /// it.  Takes ownership of the temporary node.
  ///
  /// \pre N does not self-reference.
  /// \param N Temporary node to unique.
  /// \return A uniqued replacement for temporary node \p N.
  template <class T>
  static std::enable_if_t<std::is_base_of<MDNode, T>::value, T *>
  replaceWithUniqued(std::unique_ptr<T, TempMDNodeDeleter> N) {
    return cast<T>(N.release()->replaceWithUniquedImpl());
  }

  /// Replace a temporary node with a distinct one.
  ///
  /// Create a distinct version of \c N -- in place, if possible -- and return
  /// it.  Takes ownership of the temporary node.
  /// \param N Temporary node to make distinct.
  /// \return A distinct replacement for temporary node \p N.
  template <class T>
  static std::enable_if_t<std::is_base_of<MDNode, T>::value, T *>
  replaceWithDistinct(std::unique_ptr<T, TempMDNodeDeleter> N) {
    return cast<T>(N.release()->replaceWithDistinctImpl());
  }

  /// Print in tree shape.
  ///
  /// Prints definition of \c this in tree shape.
  ///
  /// If \c M is provided, metadata nodes will be numbered canonically;
  /// otherwise, pointer addresses are substituted.
  /// @{
  /// Print this metadata as a tree to \p OS.
  /// \param OS Output stream.
  /// \param M Optional module for canonical metadata numbering.
  LLVM_ABI void printTree(raw_ostream &OS, const Module *M = nullptr) const;
  /// Print this metadata as a tree using slot tracker \p MST.
  /// \param OS Output stream.
  /// \param MST Module slot tracker for numbering.
  /// \param M Optional module for canonical metadata numbering.
  LLVM_ABI void printTree(raw_ostream &OS, ModuleSlotTracker &MST,
                          const Module *M = nullptr) const;
  /// @}

  /// User-friendly dump in tree shape.
  ///
  /// If \c M is provided, metadata nodes will be numbered canonically;
  /// otherwise, pointer addresses are substituted.
  ///
  /// Note: this uses an explicit overload instead of default arguments so that
  /// the nullptr version is easy to call from a debugger.
  ///
  /// @{
  /// Dump this metadata as a tree to stderr.
  LLVM_ABI void dumpTree() const;
  /// Dump this metadata as a tree, optionally numbered via \p M.
  /// \param M Optional module for canonical metadata numbering.
  LLVM_ABI void dumpTree(const Module *M) const;
  /// @}

private:
  LLVM_ABI MDNode *replaceWithPermanentImpl();
  LLVM_ABI MDNode *replaceWithUniquedImpl();
  LLVM_ABI MDNode *replaceWithDistinctImpl();

protected:
  /// Set an operand.
  ///
  /// Sets the operand directly, without worrying about uniquing.
  /// \param I Zero-based operand index.
  /// \param New Replacement metadata operand.
  LLVM_ABI void setOperand(unsigned I, Metadata *New);

  /// Return the number of unresolved operands in this node.
  /// \return The number of unresolved operands in this node.
  unsigned getNumUnresolved() const { return getHeader().NumUnresolved; }

  /// Set the number of unresolved operands in this node.
  /// \param N New unresolved-operand count.
  void setNumUnresolved(unsigned N) { getHeader().NumUnresolved = N; }
  /// Register this node as a distinct MDNode owned by the LLVMContext.
  LLVM_ABI void storeDistinctInContext();
  /// Register \p N in \p Store according to its \p Storage kind.
  /// \param N Node to store.
  /// \param Storage Storage/uniquing kind.
  /// \param Store Context store receiving the node.
  /// \return \p N after registration in \p Store according to \p Storage.
  template <class T, class StoreT>
  static T *storeImpl(T *N, StorageType Storage, StoreT &Store);
  /// Finalize \p N according to its \p Storage kind when no store is needed.
  /// \param N Node to finalize.
  /// \param Storage Storage/uniquing kind.
  /// \return \p N after finalization according to \p Storage.
  template <class T> static T *storeImpl(T *N, StorageType Storage);

  /// Resize the node to hold \a NumOps operands.
  ///
  /// \pre \a isTemporary() or \a isDistinct()
  /// \pre MetadataID == MDTupleKind
  /// \param NumOps New operand count.
  void resize(size_t NumOps) {
    assert(!isUniqued() && "Resizing is not supported for uniqued nodes");
    assert(getMetadataID() == MDTupleKind &&
           "Resizing is not supported for this node kind");
    getHeader().resize(NumOps);
  }

private:
  void handleChangedOperand(void *Ref, Metadata *New);

  /// Drop RAUW support, if any.
  void dropReplaceableUses();

  void resolveAfterOperandChange(Metadata *Old, Metadata *New);
  void decrementUnresolvedOperandCount();
  void countUnresolvedOperands();

  /// Mutate this to be "uniqued".
  ///
  /// Mutate this so that \a isUniqued().
  /// \pre \a isTemporary().
  /// \pre already added to uniquing set.
  void makeUniqued();

  /// Mutate this to be "distinct".
  ///
  /// Mutate this so that \a isDistinct().
  /// \pre \a isTemporary().
  void makeDistinct();

  void deleteAsSubclass();
  MDNode *uniquify();
  void eraseFromStore();

  template <class NodeTy> struct HasCachedHash;
  template <class NodeTy> static void dispatchRecalculateHash(NodeTy *N) {
    if constexpr (HasCachedHash<NodeTy>::value)
      N->recalculateHash();
  }
  template <class NodeTy> static void dispatchResetHash(NodeTy *N) {
    if constexpr (HasCachedHash<NodeTy>::value)
      N->setHash(0);
  }

  /// Merge branch weights from two direct callsites.
  static MDNode *mergeDirectCallProfMetadata(MDNode *A, MDNode *B,
                                             const Instruction *AInstr,
                                             const Instruction *BInstr);

public:
  /// Iterator over this node's operands.
  using op_iterator = const MDOperand *;
  /// Range of this node's operands.
  using op_range = iterator_range<op_iterator>;

  /// Return an iterator to the first operand.
  /// \return Iterator to the first operand.
  op_iterator op_begin() const {
    return const_cast<MDNode *>(this)->mutable_begin();
  }

  /// Return an iterator past the last operand.
  /// \return Iterator past the last operand.
  op_iterator op_end() const {
    return const_cast<MDNode *>(this)->mutable_end();
  }

  /// Return this node's operands as an array reference.
  /// \return This node's operands as an array reference.
  ArrayRef<MDOperand> operands() const { return getHeader().operands(); }

  /// Return the operand at index \p I.
  /// \param I Zero-based operand index.
  /// \return The operand at index \p I.
  const MDOperand &getOperand(unsigned I) const {
    assert(I < getNumOperands() && "Out of range");
    return getHeader().operands()[I];
  }

  /// Return number of MDNode operands.
  /// \return The number of MDNode operands.
  unsigned getNumOperands() const { return getHeader().getNumOperands(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// \param MD Metadata to test.
  /// \return True if \p MD is an \c MDNode subclass.
  static bool classof(const Metadata *MD) {
    switch (MD->getMetadataID()) {
    default:
      return false;
#define HANDLE_MDNODE_LEAF(CLASS)                                              \
  case CLASS##Kind:                                                            \
    return true;
#include "llvm/IR/Metadata.def"
    }
  }

  /// Check whether MDNode is a vtable access.
  /// \return True if this MDNode is a vtable access.
  LLVM_ABI bool isTBAAVtableAccess() const;

  /// Methods for metadata merging.
  /// Concatenate metadata nodes \p A and \p B.
  /// \param A First metadata node.
  /// \param B Second metadata node.
  /// \return The concatenation of metadata nodes \p A and \p B.
  LLVM_ABI static MDNode *concatenate(MDNode *A, MDNode *B);
  /// Intersect metadata nodes \p A and \p B.
  /// \param A First metadata node.
  /// \param B Second metadata node.
  /// \return The intersection of metadata nodes \p A and \p B.
  LLVM_ABI static MDNode *intersect(MDNode *A, MDNode *B);
  /// Merge TBAA metadata, returning the most generic common tag.
  /// \param A First TBAA metadata node.
  /// \param B Second TBAA metadata node.
  /// \return The most generic common TBAA tag for \p A and \p B.
  LLVM_ABI static MDNode *getMostGenericTBAA(MDNode *A, MDNode *B);
  /// Merge FP-math metadata, returning the most generic accuracy.
  /// \param A First FP-math metadata node.
  /// \param B Second FP-math metadata node.
  /// \return The most generic FP-math accuracy for \p A and \p B.
  LLVM_ABI static MDNode *getMostGenericFPMath(MDNode *A, MDNode *B);
  /// Merge !range metadata by taking the union of the integer intervals.
  /// \param A First range metadata node.
  /// \param B Second range metadata node.
  /// \return The union of the integer intervals in \p A and \p B.
  LLVM_ABI static MDNode *getMostGenericRange(MDNode *A, MDNode *B);
  /// Merge noalias address-space metadata, returning the most generic set.
  /// \param A First noalias address-space metadata node.
  /// \param B Second noalias address-space metadata node.
  /// \return The most generic noalias address-space set for \p A and \p B.
  LLVM_ABI static MDNode *getMostGenericNoaliasAddrspace(MDNode *A, MDNode *B);
  /// Merge alias-scope metadata, keeping scopes present in both.
  /// \param A First alias-scope metadata node.
  /// \param B Second alias-scope metadata node.
  /// \return Alias-scope metadata keeping scopes present in both \p A and \p B.
  LLVM_ABI static MDNode *getMostGenericAliasScope(MDNode *A, MDNode *B);
  /// Merge align/dereferenceable metadata, returning the most generic bound.
  /// \param A First align/dereferenceable metadata node.
  /// \param B Second align/dereferenceable metadata node.
  /// \return The most generic align/dereferenceable bound for \p A and \p B.
  LLVM_ABI static MDNode *getMostGenericAlignmentOrDereferenceable(MDNode *A,
                                                                   MDNode *B);
  /// Merge \c !nofpclass metadata by intersecting the allowed FP classes.
  /// \param A First nofpclass metadata node.
  /// \param B Second nofpclass metadata node.
  /// \return The intersection of allowed FP classes from \p A and \p B.
  LLVM_ABI static MDNode *getMostGenericNoFPClass(MDNode *A, MDNode *B);
  /// Merge !prof metadata from two instructions.
  /// Currently only implemented with direct callsites with branch weights.
  /// \param A First prof metadata node.
  /// \param B Second prof metadata node.
  /// \param AInstr Instruction associated with \p A.
  /// \param BInstr Instruction associated with \p B.
  /// \return Merged !prof metadata from \p A and \p B.
  LLVM_ABI static MDNode *getMergedProfMetadata(MDNode *A, MDNode *B,
                                                const Instruction *AInstr,
                                                const Instruction *BInstr);
  /// Merge memprof metadata from two instructions.
  /// \param A First memprof metadata node.
  /// \param B Second memprof metadata node.
  /// \return Merged memprof metadata from \p A and \p B.
  LLVM_ABI static MDNode *getMergedMemProfMetadata(MDNode *A, MDNode *B);
  /// Merge callsite metadata from two instructions.
  /// \param A First callsite metadata node.
  /// \param B Second callsite metadata node.
  /// \return Merged callsite metadata from \p A and \p B.
  LLVM_ABI static MDNode *getMergedCallsiteMetadata(MDNode *A, MDNode *B);
  /// Merge callee-type metadata from two instructions.
  /// \param A First callee-type metadata node.
  /// \param B Second callee-type metadata node.
  /// \return Merged callee-type metadata from \p A and \p B.
  LLVM_ABI static MDNode *getMergedCalleeTypeMetadata(const MDNode *A,
                                                      const MDNode *B);
  /// Merge alloc-token metadata from two instructions.
  /// \param A First alloc-token metadata node.
  /// \param B Second alloc-token metadata node.
  /// \return Merged alloc-token metadata from \p A and \p B.
  LLVM_ABI static MDNode *getMergedAllocTokenMetadata(const MDNode *A,
                                                      const MDNode *B);

  /// Convert !captures metadata to CaptureComponents. MD may be nullptr.
  /// \param MD Captures metadata node, or null.
  /// \return Capture components decoded from \p MD (or empty if null).
  LLVM_ABI static CaptureComponents toCaptureComponents(const MDNode *MD);
  /// Convert CaptureComponents to !captures metadata. The return value may be
  /// nullptr.
  /// \param Ctx LLVM context that owns the result.
  /// \param CC Capture components to encode.
  /// \return !captures metadata encoding \p CC, or null.
  LLVM_ABI static MDNode *fromCaptureComponents(LLVMContext &Ctx,
                                                CaptureComponents CC);
};

/// Tuple of metadata.
///
/// This is the simple \a MDNode arbitrary tuple.  Nodes are uniqued by
/// default based on their operands.
class MDTuple : public MDNode {
  friend class LLVMContextImpl;
  friend class MDNode;

  MDTuple(LLVMContext &C, StorageType Storage, unsigned Hash,
          ArrayRef<Metadata *> Vals)
      : MDNode(C, MDTupleKind, Storage, Vals) {
    setHash(Hash);
  }

  ~MDTuple() { dropAllReferences(); }

  void setHash(unsigned Hash) { SubclassData32 = Hash; }
  void recalculateHash();

  LLVM_ABI static MDTuple *getImpl(LLVMContext &Context,
                                   ArrayRef<Metadata *> MDs,
                                   StorageType Storage,
                                   bool ShouldCreate = true);

  TempMDTuple cloneImpl() const {
    ArrayRef<MDOperand> Operands = operands();
    return getTemporary(getContext(), SmallVector<Metadata *, 4>(Operands));
  }

public:
  /// Get the hash, if any.
  /// \return The cached hash, if any.
  unsigned getHash() const { return SubclassData32; }

  /// Create or return a uniqued tuple of the given metadata operands.
  /// \param Context LLVM context that owns the tuple.
  /// \param MDs Operand metadata.
  /// \return A uniqued tuple of the given metadata operands.
  static MDTuple *get(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
    return getImpl(Context, MDs, Uniqued);
  }

  /// Return an existing uniqued tuple for \p MDs, or null if none.
  /// \param Context LLVM context that owns the tuple.
  /// \param MDs Operand metadata.
  /// \return An existing uniqued tuple for \p MDs, or null.
  static MDTuple *getIfExists(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
    return getImpl(Context, MDs, Uniqued, /* ShouldCreate */ false);
  }

  /// Return a distinct node.
  ///
  /// Return a distinct node -- i.e., a node that is not uniqued.
  /// \param Context LLVM context that owns the tuple.
  /// \param MDs Operand metadata.
  /// \return A distinct (non-uniqued) tuple with operands \p MDs.
  static MDTuple *getDistinct(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
    return getImpl(Context, MDs, Distinct);
  }

  /// Return a temporary node.
  ///
  /// For use in constructing cyclic MDNode structures. A temporary MDNode is
  /// not uniqued, may be RAUW'd, and must be manually deleted with
  /// deleteTemporary.
  /// \param Context LLVM context that owns the tuple.
  /// \param MDs Operand metadata.
  /// \return A temporary tuple with operands \p MDs.
  static TempMDTuple getTemporary(LLVMContext &Context,
                                  ArrayRef<Metadata *> MDs) {
    return TempMDTuple(getImpl(Context, MDs, Temporary));
  }

  /// Return a (temporary) clone of this.
  /// \return A temporary clone of this tuple.
  TempMDTuple clone() const { return cloneImpl(); }

  /// Append an element to the tuple. This will resize the node.
  /// \param MD Metadata operand to append.
  void push_back(Metadata *MD) {
    size_t NumOps = getNumOperands();
    resize(NumOps + 1);
    setOperand(NumOps, MD);
  }

  /// Shrink the operands by 1.
  void pop_back() { resize(getNumOperands() - 1); }

  /// Filter out tuple elements that do not satisfy predicate.
  /// Return this if no elements should be filtered out (without re-uniquing).
  /// \param Pred Predicate returning true for operands to keep.
  /// \return This tuple, or a filtered uniqued tuple keeping operands that satisfy \p Pred.
  template <typename T> MDTuple *filter(T &&Pred) {
    ArrayRef<MDOperand> Ops = operands();
    // Exit if no nodes should be removed.
    if (llvm::all_of(Ops, Pred))
      return this;
    return get(getContext(),
               to_vector_of<Metadata *>(llvm::make_filter_range(Ops, Pred)));
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MD Metadata to test.
  /// \return True if \p MD is an \c MDTuple.
  static bool classof(const Metadata *MD) {
    return MD->getMetadataID() == MDTupleKind;
  }
};

/// Return a uniqued MDTuple with operands \p MDs.
/// \param Context LLVM context that owns the tuple.
/// \param MDs Operand metadata.
/// \return A uniqued MDTuple with operands \p MDs.
MDTuple *MDNode::get(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
  return MDTuple::get(Context, MDs);
}

/// Return an existing uniqued MDTuple for \p MDs, or null if none.
/// \param Context LLVM context that owns the tuple.
/// \param MDs Operand metadata.
/// \return An existing uniqued MDTuple for \p MDs, or null.
MDTuple *MDNode::getIfExists(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
  return MDTuple::getIfExists(Context, MDs);
}

/// Return a distinct MDTuple with operands \p MDs.
/// \param Context LLVM context that owns the tuple.
/// \param MDs Operand metadata.
/// \return A distinct MDTuple with operands \p MDs.
MDTuple *MDNode::getDistinct(LLVMContext &Context, ArrayRef<Metadata *> MDs) {
  return MDTuple::getDistinct(Context, MDs);
}

/// Return a temporary MDTuple with operands \p MDs.
/// \param Context LLVM context that owns the tuple.
/// \param MDs Operand metadata.
/// \return A temporary MDTuple with operands \p MDs.
TempMDTuple MDNode::getTemporary(LLVMContext &Context,
                                 ArrayRef<Metadata *> MDs) {
  return MDTuple::getTemporary(Context, MDs);
}

/// Delete temporary metadata node \p Node.
/// \param Node Temporary metadata node to delete.
void TempMDNodeDeleter::operator()(MDNode *Node) const {
  MDNode::deleteTemporary(Node);
}

/// Wrapper around an MDNode that exposes alias-scope metadata.
///
/// This is a simple wrapper around an MDNode which provides a higher-level
/// interface by hiding the details of how alias analysis information is encoded
/// in its operands.
class AliasScopeNode {
  const MDNode *Node = nullptr;

public:
  /// Construct an empty alias-scope wrapper.
  AliasScopeNode() = default;
  /// Construct a wrapper around alias-scope metadata \p N.
  /// \param N Alias-scope metadata node, or null.
  explicit AliasScopeNode(const MDNode *N) : Node(N) {}

  /// Get the MDNode for this AliasScopeNode.
  /// \return The underlying alias-scope \c MDNode.
  const MDNode *getNode() const { return Node; }

  /// Get the MDNode for this AliasScopeNode's domain.
  /// \return The domain \c MDNode for this alias scope, or null.
  const MDNode *getDomain() const {
    if (Node->getNumOperands() < 2)
      return nullptr;
    return dyn_cast_or_null<MDNode>(Node->getOperand(1));
  }
  /// Return the optional name string of this alias scope.
  /// \return The optional name string of this alias scope.
  StringRef getName() const {
    if (Node->getNumOperands() > 2)
      if (MDString *N = dyn_cast_or_null<MDString>(Node->getOperand(2)))
        return N->getString();
    return StringRef();
  }
};

/// Typed iterator through MDNode operands.
///
/// An iterator that transforms an \a MDNode::iterator into an iterator over a
/// particular Metadata subclass.
template <class T> class TypedMDOperandIterator {
  MDNode::op_iterator I = nullptr;

public:
  /// Forward-iterator category tag.
  using iterator_category = std::forward_iterator_tag;
  /// Pointed-to metadata subclass pointer type.
  using value_type = T *;
  /// Distance between iterators.
  using difference_type = std::ptrdiff_t;
  /// Unused pointer typedef required by iterator traits.
  using pointer = void;
  /// Reference type produced by dereference.
  using reference = T *;

  /// Default-construct an end iterator.
  TypedMDOperandIterator() = default;
  /// Construct an iterator at operand position \p I.
  /// \param I Underlying MDNode operand iterator.
  explicit TypedMDOperandIterator(MDNode::op_iterator I) : I(I) {}

  /// Dereference to the typed metadata operand.
  /// \return The typed metadata operand.
  T *operator*() const { return cast_or_null<T>(*I); }

  /// Advance to the next operand (preincrement).
  /// \return A reference to this iterator after advancing.
  TypedMDOperandIterator &operator++() {
    ++I;
    return *this;
  }

  /// Advance to the next operand (postincrement).
  /// \param Unused Distinguishes postincrement from preincrement.
  /// \return A copy of the iterator before advancing.
  TypedMDOperandIterator operator++(int Unused) {
    TypedMDOperandIterator Temp(*this);
    ++I;
    return Temp;
  }

  /// Compare equality of the underlying operand iterators.
  /// \param X Other iterator to compare against.
  /// \return True if the underlying operand iterators are equal.
  bool operator==(const TypedMDOperandIterator &X) const { return I == X.I; }
  /// Return true if the underlying operand iterators differ.
  /// \param X Other iterator to compare against.
  /// \return True if the underlying operand iterators differ.
  bool operator!=(const TypedMDOperandIterator &X) const { return I != X.I; }
};

/// Typed, array-like tuple of metadata.
///
/// This is a wrapper for \a MDTuple that makes it act like an array holding a
/// particular type of metadata.
template <class T> class MDTupleTypedArrayWrapper {
  const MDTuple *N = nullptr;

public:
  /// Construct an empty typed array wrapper.
  MDTupleTypedArrayWrapper() = default;
  /// Construct a wrapper around typed tuple \p N.
  /// \param N Underlying metadata tuple, or null.
  MDTupleTypedArrayWrapper(const MDTuple *N) : N(N) {}

  /// Construct from a compatible wrapper when \c U* converts to \c T*.
  /// \param Other Source wrapper whose underlying tuple is reused.
  /// \param Enable SFINAE enabler; unused at runtime.
  template <class U>
  MDTupleTypedArrayWrapper(
      const MDTupleTypedArrayWrapper<U> &Other,
      std::enable_if_t<std::is_convertible<U *, T *>::value> *Enable = nullptr)
      : N(Other.get()) {
    (void)Enable;
  }

  /// Construct from a differently typed wrapper when \c U* is not convertible to \c T*.
  /// \param Other Source wrapper whose underlying tuple is reused.
  /// \param Enable SFINAE enabler; unused at runtime.
  template <class U>
  explicit MDTupleTypedArrayWrapper(
      const MDTupleTypedArrayWrapper<U> &Other,
      std::enable_if_t<!std::is_convertible<U *, T *>::value> *Enable = nullptr)
      : N(Other.get()) {
    (void)Enable;
  }

  /// Return true if this wrapper holds a non-null tuple.
  /// \return True if this wrapper holds a non-null tuple.
  explicit operator bool() const { return get(); }
  /// Convert to the underlying \c MDTuple pointer.
  /// \return The underlying \c MDTuple pointer.
  explicit operator MDTuple *() const { return get(); }

  /// Return the underlying \c MDTuple pointer.
  /// \return The underlying \c MDTuple pointer.
  MDTuple *get() const { return const_cast<MDTuple *>(N); }
  /// Access members of the underlying tuple.
  /// \return The underlying \c MDTuple pointer.
  MDTuple *operator->() const { return get(); }
  /// Dereference to the underlying tuple.
  /// \return A reference to the underlying tuple.
  MDTuple &operator*() const { return *get(); }

  // FIXME: Fix callers and remove condition on N.
  /// Return the number of operands, or zero if the tuple is null.
  /// \return The number of operands, or zero if the tuple is null.
  unsigned size() const { return N ? N->getNumOperands() : 0u; }
  /// Return true if the wrapper holds no tuple or the tuple has no operands.
  /// \return True if the wrapper holds no tuple or the tuple has no operands.
  bool empty() const { return N ? N->getNumOperands() == 0 : true; }
  /// Return the typed metadata at index \p I, or null if that operand is null.
  /// \param I Zero-based operand index.
  /// \return The typed metadata at index \p I, or null.
  T *operator[](unsigned I) const { return cast_or_null<T>(N->getOperand(I)); }

  // FIXME: Fix callers and remove condition on N.
  /// Iterator over typed metadata operands.
  using iterator = TypedMDOperandIterator<T>;

  /// Return an iterator to the first typed operand.
  /// \return Iterator to the first typed operand.
  iterator begin() const { return N ? iterator(N->op_begin()) : iterator(); }
  /// Return an iterator past the last typed operand.
  /// \return Iterator past the last typed operand.
  iterator end() const { return N ? iterator(N->op_end()) : iterator(); }
};

#define HANDLE_METADATA(CLASS)                                                 \
  using CLASS##Array = MDTupleTypedArrayWrapper<CLASS>;
#include "llvm/IR/Metadata.def"

/// Placeholder metadata for operands of distinct MDNodes.
///
/// This is a lightweight placeholder for an operand of a distinct node.  It's
/// purpose is to help track forward references when creating a distinct node.
/// This allows distinct nodes involved in a cycle to be constructed before
/// their operands without requiring a heavyweight temporary node with
/// full-blown RAUW support.
///
/// Each placeholder supports only a single MDNode user.  Clients should pass
/// an ID, retrieved via \a getID(), to indicate the "real" operand that this
/// should be replaced with.
///
/// While it would be possible to implement move operators, they would be
/// fairly expensive.  Leave them unimplemented to discourage their use
/// (clients can use std::deque, std::list, BumpPtrAllocator, etc.).
class DistinctMDOperandPlaceholder : public Metadata {
  friend class MetadataTracking;

  Metadata **Use = nullptr;

public:
  /// Construct a distinct operand placeholder with the given ID.
  /// \param ID Identifier for the eventual real operand.
  explicit DistinctMDOperandPlaceholder(unsigned ID)
      : Metadata(DistinctMDOperandPlaceholderKind, Distinct) {
    SubclassData32 = ID;
  }

  /// Default construction is deleted; an ID is required.
  DistinctMDOperandPlaceholder() = delete;
  /// Move construction is deleted; placeholders track a single use by address.
  /// \param Other Unused; move construction is deleted.
  DistinctMDOperandPlaceholder(DistinctMDOperandPlaceholder &&Other) = delete;
  /// Copy construction is deleted; placeholders track a single use by address.
  /// \param Other Unused; copy construction is deleted.
  DistinctMDOperandPlaceholder(const DistinctMDOperandPlaceholder &Other) = delete;

  /// Clear the tracked use pointer if this placeholder is still referenced.
  ~DistinctMDOperandPlaceholder() {
    if (Use)
      *Use = nullptr;
  }

  /// Return the placeholder ID for the eventual real operand.
  /// \return The placeholder ID for the eventual real operand.
  unsigned getID() const { return SubclassData32; }

  /// Replace the use of this with MD.
  /// \param MD Metadata that replaces this placeholder use.
  void replaceUseWith(Metadata *MD) {
    if (!Use)
      return;
    *Use = MD;

    if (*Use)
      MetadataTracking::track(*Use);

    Metadata *T = cast<Metadata>(this);
    MetadataTracking::untrack(T);
    assert(!Use && "Use is still being tracked despite being untracked!");
  }
};

//===----------------------------------------------------------------------===//
/// A tuple of MDNodes.
///
/// Despite its name, a NamedMDNode isn't itself an MDNode.
///
/// NamedMDNodes are named module-level entities that contain lists of MDNodes.
///
/// It is illegal for a NamedMDNode to appear as an operand of an MDNode.
class NamedMDNode : public ilist_node<NamedMDNode> {
  friend class LLVMContextImpl;
  friend class Module;

  std::string Name;
  Module *Parent = nullptr;
  void *Operands; // SmallVector<TrackingMDRef, 4>

  void setParent(Module *M) { Parent = M; }

  explicit NamedMDNode(const Twine &N);

  template <class T1> class op_iterator_impl {
    friend class NamedMDNode;

    const NamedMDNode *Node = nullptr;
    unsigned Idx = 0;

    op_iterator_impl(const NamedMDNode *N, unsigned i) : Node(N), Idx(i) {}

  public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = T1;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type;

    op_iterator_impl() = default;

    bool operator==(const op_iterator_impl &o) const { return Idx == o.Idx; }
    bool operator!=(const op_iterator_impl &o) const { return Idx != o.Idx; }

    op_iterator_impl &operator++() {
      ++Idx;
      return *this;
    }

    op_iterator_impl operator++(int) {
      op_iterator_impl tmp(*this);
      operator++();
      return tmp;
    }

    op_iterator_impl &operator--() {
      --Idx;
      return *this;
    }

    op_iterator_impl operator--(int) {
      op_iterator_impl tmp(*this);
      operator--();
      return tmp;
    }

    T1 operator*() const { return Node->getOperand(Idx); }
  };

public:
  /// Named metadata nodes are not copyable.
  /// \param Other Unused; copy construction is deleted.
  NamedMDNode(const NamedMDNode &Other) = delete;
  /// Destroy this named metadata node and drop all operand references.
  LLVM_ABI ~NamedMDNode();

  /// Drop all references and remove the node from parent module.
  LLVM_ABI void eraseFromParent();

  /// Remove all uses and clear node vector.
  void dropAllReferences() { clearOperands(); }
  /// Drop all references to this node's operands.
  LLVM_ABI void clearOperands();

  /// Get the module that holds this named metadata collection.
  /// \return The module that holds this named metadata collection.
  inline Module *getParent() { return Parent; }
  /// Get the module that holds this named metadata collection.
  /// \return The module that holds this named metadata collection.
  inline const Module *getParent() const { return Parent; }

  /// Return the operand at index \p i.
  /// \param i Zero-based operand index.
  /// \return The operand at index \p i.
  LLVM_ABI MDNode *getOperand(unsigned i) const;
  /// Return the number of operands.
  /// \return The number of operands.
  LLVM_ABI unsigned getNumOperands() const;
  /// Append \p M as an operand of this named metadata.
  /// \param M Metadata node to append.
  LLVM_ABI void addOperand(MDNode *M);
  /// Set the operand at index \p I to \p New.
  /// \param I Zero-based operand index.
  /// \param New Replacement metadata node.
  LLVM_ABI void setOperand(unsigned I, MDNode *New);
  /// Return the name of this named metadata.
  /// \return The name of this named metadata.
  LLVM_ABI StringRef getName() const;
  /// Print this named metadata to \p ROS.
  /// \param ROS Output stream.
  /// \param IsForDebug Whether to use debug-oriented formatting.
  LLVM_ABI void print(raw_ostream &ROS, bool IsForDebug = false) const;
  /// Print this named metadata using slot tracker \p MST.
  /// \param ROS Output stream.
  /// \param MST Module slot tracker for numbering.
  /// \param IsForDebug Whether to use debug-oriented formatting.
  LLVM_ABI void print(raw_ostream &ROS, ModuleSlotTracker &MST,
                      bool IsForDebug = false) const;
  /// Dump this named metadata to stderr.
  LLVM_ABI void dump() const;

  // ---------------------------------------------------------------------------
  // Operand Iterator interface...
  //
  /// Iterator over this named metadata's MDNode operands.
  using op_iterator = op_iterator_impl<MDNode *>;

  /// Return an iterator to the first operand.
  /// \return Iterator to the first operand.
  op_iterator op_begin() { return op_iterator(this, 0); }
  /// Return an iterator past the last operand.
  /// \return Iterator past the last operand.
  op_iterator op_end()   { return op_iterator(this, getNumOperands()); }

  /// Const iterator over this named metadata's MDNode operands.
  using const_op_iterator = op_iterator_impl<const MDNode *>;

  /// Return a const iterator to the first operand.
  /// \return Const iterator to the first operand.
  const_op_iterator op_begin() const { return const_op_iterator(this, 0); }
  /// Return a const iterator past the last operand.
  /// \return Const iterator past the last operand.
  const_op_iterator op_end()   const { return const_op_iterator(this, getNumOperands()); }

  /// Return a mutable range over this named metadata's operands.
  /// \return A mutable range over this named metadata's operands.
  inline iterator_range<op_iterator>  operands() {
    return make_range(op_begin(), op_end());
  }
  /// Return a const range over this named metadata's operands.
  /// \return A const range over this named metadata's operands.
  inline iterator_range<const_op_iterator> operands() const {
    return make_range(op_begin(), op_end());
  }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Opaque C API conversions for \c NamedMDNode (see CBindingWrapping.h).
/// \param P Opaque named-metadata reference.
/// \return The unwrapped \c NamedMDNode pointer.
inline NamedMDNode *unwrap(LLVMNamedMDNodeRef P) {
  return reinterpret_cast<NamedMDNode *>(P);
}

/// Wrap a \c NamedMDNode as an opaque \c LLVMNamedMDNodeRef.
/// \param P Named metadata to wrap.
/// \return An opaque \c LLVMNamedMDNodeRef for \p P.
inline LLVMNamedMDNodeRef wrap(const NamedMDNode *P) {
  return reinterpret_cast<LLVMNamedMDNodeRef>(const_cast<NamedMDNode *>(P));
}

/// Unwrap an opaque \c LLVMNamedMDNodeRef as a \c NamedMDNode subclass.
/// \param P Opaque named-metadata reference.
/// \return \p P cast to subclass \c T.
template <typename T>
inline T *unwrap(LLVMNamedMDNodeRef P) {
  return cast<T>(unwrap(P));
}

} // end namespace llvm

#endif // LLVM_IR_METADATA_H
