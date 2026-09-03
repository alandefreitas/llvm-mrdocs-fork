//===-- llvm/GlobalObject.h - Class to represent global objects -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This represents an independent object. That is, a function or a global
// variable, but not an alias.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GLOBALOBJECT_H
#define LLVM_IR_GLOBALOBJECT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Comdat;
class Metadata;

/// Base class for independently defined globals such as functions and variables.
///
/// A global object is a function or a global variable, but not an alias.
class GlobalObject : public GlobalValue {
public:
  /// Values for visibility metadata attached to vtables.
  ///
  /// Describes the scope in which a virtual call could end up being dispatched
  /// through this vtable.
  enum VCallVisibility {
    /// Type is potentially visible to external code.
    VCallVisibilityPublic = 0,
    /// Type is only visible to code which will be in the current module after
    /// LTO internalization.
    VCallVisibilityLinkageUnit = 1,
    /// Type is only visible to code in the current module.
    VCallVisibilityTranslationUnit = 2,
  };

protected:
  /// Construct a global object with the given type, linkage, and name.
  /// \param Ty The type of the global object's value.
  /// \param VTy The ValueTy enumerator for the concrete subclass.
  /// \param AllocInfo Operand allocation info for the User base.
  /// \param Linkage The linkage type for the global object.
  /// \param Name The name of the global object.
  /// \param AddressSpace The address space of the global object's pointer.
  GlobalObject(Type *Ty, ValueTy VTy, AllocInfo AllocInfo, LinkageTypes Linkage,
               const Twine &Name, unsigned AddressSpace = 0)
      : GlobalValue(Ty, VTy, AllocInfo, Linkage, Name, AddressSpace) {
    setGlobalValueSubClassData(0);
  }
  /// Destroy this global object.
  LLVM_ABI ~GlobalObject();

  /// The COMDAT group for this global, or null if none.
  Comdat *ObjComdat = nullptr;

  friend class Value;
  /// Index of first metadata attachment in context, or zero.
  unsigned MetadataIndex = 0;

  /// Bit indices for data packed into \c GlobalValueSubClassData.
  enum {
    LastAlignmentBit = 5,   ///< Last bit index used for alignment.
    LastCodeModelBit = 8,   ///< Last bit index used for code model (subclasses).
    HasSectionHashEntryBit, ///< Bit indicating a custom section name is stored.
    GlobalObjectBits,       ///< Total bits reserved by \c GlobalObject.
  };
  /// Number of subclass data bits remaining after GlobalObject's reserved bits.
  static const unsigned GlobalObjectSubClassDataBits =
      GlobalValueSubClassDataBits - GlobalObjectBits;

private:
  static const unsigned AlignmentBits = LastAlignmentBit + 1;
  static const unsigned AlignmentMask = (1 << AlignmentBits) - 1;
  static const unsigned GlobalObjectMask = (1 << GlobalObjectBits) - 1;

public:
  /// Copy construction is deleted; GlobalObject owns module-level state.
  /// \param Other The global object that would be copied (deleted).
  GlobalObject(const GlobalObject &Other) = delete;

protected:
  /// Returns the alignment of the given variable or function.
  ///
  /// Note that for functions this is the alignment of the code, not the
  /// alignment of a function pointer.
  /// \return The optional alignment of this global object.
  MaybeAlign getAlign() const {
    unsigned Data = getGlobalValueSubClassData();
    unsigned AlignmentData = Data & AlignmentMask;
    return decodeMaybeAlign(AlignmentData);
  }

  /// Sets the alignment attribute of the GlobalObject.
  /// \param Align The required alignment for this global object.
  LLVM_ABI void setAlignment(Align Align);

  /// Sets the alignment attribute of the GlobalObject.
  /// This method will be deprecated as the alignment property should always be
  /// defined.
  /// \param Align The optional alignment, or empty to clear it.
  LLVM_ABI void setAlignment(MaybeAlign Align);

  /// Return subclass-specific data packed above \c GlobalObject bits.
  /// \return The subclass data bits stored above \c GlobalObject's reserved bits.
  unsigned getGlobalObjectSubClassData() const {
    unsigned ValueData = getGlobalValueSubClassData();
    return ValueData >> GlobalObjectBits;
  }

  /// Set subclass-specific data packed above \c GlobalObject bits.
  /// \param Val The subclass data value to store.
  void setGlobalObjectSubClassData(unsigned Val) {
    unsigned OldData = getGlobalValueSubClassData();
    setGlobalValueSubClassData((OldData & GlobalObjectMask) |
                               (Val << GlobalObjectBits));
    assert(getGlobalObjectSubClassData() == Val && "representation error");
  }

public:
  /// Check if this global has a custom object file section.
  ///
  /// This is more efficient than calling getSection() and checking for an empty
  /// string.
  /// \return True if a custom section name is stored for this global.
  bool hasSection() const {
    return getGlobalValueSubClassData() & (1 << HasSectionHashEntryBit);
  }

  /// Get the custom section of this global if it has one.
  ///
  /// If this global does not have a custom section, this will be empty and the
  /// default object file section (.text, .data, etc) will be used.
  /// \return The custom section name, or empty if none is set.
  StringRef getSection() const {
    return hasSection() ? getSectionImpl() : StringRef();
  }

  /// Change the section for this global.
  ///
  /// Setting the section to the empty string tells LLVM to choose an
  /// appropriate default object file section.
  /// \param S The section name, or empty for the default section.
  LLVM_ABI void setSection(StringRef S);

  /// Update the section prefix metadata if it differs from \p Prefix.
  ///
  /// If existing prefix is different from \p Prefix, set it to \p Prefix. If \p
  /// Prefix is empty, the set clears the existing metadata. Returns true if
  /// section prefix changed and false otherwise.
  /// \param Prefix The new section prefix, or empty to clear it.
  /// \return True if the section prefix changed, false otherwise.
  LLVM_ABI bool setSectionPrefix(StringRef Prefix);

  /// Get the section prefix for this global object.
  /// \return The section prefix, or \c std::nullopt if none is set.
  LLVM_ABI std::optional<StringRef> getSectionPrefix() const;

  /// Return true if this global object belongs to a COMDAT group.
  /// \return True if this global object has a COMDAT group.
  bool hasComdat() const { return getComdat() != nullptr; }
  /// Return the Comdat object for this global object, or null if none.
  /// \return The Comdat for this global object, or null if none.
  const Comdat *getComdat() const { return ObjComdat; }
  /// Return the Comdat object for this global object, or null if none.
  /// \return The Comdat for this global object, or null if none.
  Comdat *getComdat() { return ObjComdat; }
  /// Set the COMDAT group for this global object, or clear it if \p C is null.
  /// \param C The Comdat to assign, or null to clear.
  LLVM_ABI void setComdat(Comdat *C);

  /// Add a metadata attachment to this global object.
  using Value::addMetadata;
  /// Erase all metadata attached to this global object.
  using Value::clearMetadata;
  /// Erase all metadata attachments with the given kind.
  using Value::eraseMetadata;
  /// Erase all metadata attachments matching the given predicate.
  using Value::eraseMetadataIf;
  /// Appends all metadata attached to this global object to \c MDs.
  using Value::getAllMetadata;
  /// Set a particular kind of metadata attachment on this global object.
  using Value::setMetadata;

  /// Return true if this GlobalObject has any metadata attached to it.
  /// \return True if any metadata is attached to this global object.
  bool hasMetadata() const { return MetadataIndex != 0; }

  /// Return true if this instruction has the given type of metadata attached.
  /// \param KindID The metadata kind ID to look up.
  /// \return True if metadata of kind \p KindID is attached.
  bool hasMetadata(unsigned KindID) const {
    return getMetadata(KindID) != nullptr;
  }

  /// Return true if this instruction has the given type of metadata attached.
  /// \param Kind The metadata kind name to look up.
  /// \return True if metadata of kind \p Kind is attached.
  bool hasMetadata(StringRef Kind) const {
    return getMetadata(Kind) != nullptr;
  }

  /// Get the metadata of given kind attached to this GlobalObject.
  /// If the metadata is not found then return null.
  /// \param KindID The metadata kind ID to look up.
  /// \return The metadata node of kind \p KindID, or null if none.
  MDNode *getMetadata(unsigned KindID) const {
    return hasMetadata() ? getMetadataImpl(KindID) : nullptr;
  }

  /// Get the metadata of given kind attached to this GlobalObject.
  /// If the metadata is not found then return null.
  /// \param Kind The metadata kind name to look up.
  /// \return The metadata node of kind \p Kind, or null if none.
  MDNode *getMetadata(StringRef Kind) const {
    return hasMetadata() ? Value::getMetadata(Kind) : nullptr;
  }

  /// Append all metadata attachments of a given kind to a vector.
  ///
  /// Appends all attachments with the given ID to \c MDs in insertion order.
  /// If the Value has no attachments with the given ID, or if ID is invalid,
  /// leaves MDs unchanged.
  /// @{
  /// \param KindID The metadata kind ID to collect.
  /// \param MDs The vector that receives matching metadata nodes.
  LLVM_ABI void getMetadata(unsigned KindID,
                            SmallVectorImpl<MDNode *> &MDs) const;
  /// Append all metadata attachments of a given kind name to a vector.
  /// \param Kind The metadata kind name to collect.
  /// \param MDs The vector that receives matching metadata nodes.
  LLVM_ABI void getMetadata(StringRef Kind,
                            SmallVectorImpl<MDNode *> &MDs) const;
  /// @}

  /// Return true if this global has metadata other than debug location and GUID.
  /// \return True if non-debug, non-GUID metadata is attached.
  LLVM_ABI bool hasMetadataOtherThanDebugLocAndGuid() const;

  /// Copy metadata from \p Src, adjusting offsets by \p Offset.
  /// \param Src The global object whose metadata is copied.
  /// \param Offset Byte offset applied to metadata that encodes offsets.
  LLVM_ABI void copyMetadata(const GlobalObject *Src, unsigned Offset);

  /// Attach type metadata at the given offset for this global object.
  /// \param Offset The byte offset within the global for the type metadata.
  /// \param TypeID The type identifier metadata to attach.
  LLVM_ABI void addTypeMetadata(unsigned Offset, Metadata *TypeID);
  /// Set the vcall visibility metadata for this global object.
  /// \param Visibility The vcall visibility scope to record.
  LLVM_ABI void setVCallVisibilityMetadata(VCallVisibility Visibility);
  /// Return the vcall visibility recorded on this global object.
  /// \return The vcall visibility scope for this global object.
  LLVM_ABI VCallVisibility getVCallVisibility() const;

  /// Returns true if the alignment of the value can be unilaterally
  /// increased.
  ///
  /// Note that for functions this is the alignment of the code, not the
  /// alignment of a function pointer.
  /// \return True if this global object's alignment can be increased.
  LLVM_ABI bool canIncreaseAlignment() const;

protected:
  /// Copy all additional attributes from \p Src to this global object.
  /// \param Src The global object whose attributes are copied.
  LLVM_ABI void copyAttributesFrom(const GlobalObject *Src);

public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V The value to test.
  /// \return True if \p V is a Function, GlobalVariable, or GlobalIFunc.
  static bool classof(const Value *V) {
    return V->getValueID() == Value::FunctionVal ||
           V->getValueID() == Value::GlobalVariableVal ||
           V->getValueID() == Value::GlobalIFuncVal;
  }

private:
  void setGlobalObjectFlag(unsigned Bit, bool Val) {
    unsigned Mask = 1 << Bit;
    setGlobalValueSubClassData((~Mask & getGlobalValueSubClassData()) |
                               (Val ? Mask : 0u));
  }

  LLVM_ABI StringRef getSectionImpl() const;
};

} // end namespace llvm

#endif // LLVM_IR_GLOBALOBJECT_H
