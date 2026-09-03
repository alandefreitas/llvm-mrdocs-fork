//===- llvm/TextAPI/Record.h - TAPI Record ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the TAPI Record Types.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_RECORD_H
#define LLVM_TEXTAPI_RECORD_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TextAPI/Symbol.h"
#include <string>

namespace llvm {
namespace MachO {

// Expanded (instead of the macro) so MrDocs can attach docs to each using.
/// Bring bitmask enum bitwise NOT into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator~;
/// Bring bitmask enum bitwise OR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator|;
/// Bring bitmask enum bitwise AND into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator&;
/// Bring bitmask enum bitwise XOR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator^;
/// Bring bitmask enum left-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator<<;
/// Bring bitmask enum right-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator>>;
/// Bring bitmask enum in-place OR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator|=;
/// Bring bitmask enum in-place AND into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator&=;
/// Bring bitmask enum in-place XOR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator^=;
/// Bring bitmask enum in-place left-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator<<=;
/// Bring bitmask enum in-place right-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator>>=;
/// Bring bitmask enum logical-not into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator!;
/// Bring bitmask enum any-bits-set test into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::any;

class RecordsSlice;

/// Lightweight source location for a record.
struct RecordLoc {
  /// Construct an empty, invalid source location.
  RecordLoc() = default;
  /// Construct a source location from a file path and line number.
  ///
  /// \param File The source file path.
  /// \param Line The 1-based line number in \p File.
  RecordLoc(std::string File, unsigned Line)
      : File(std::move(File)), Line(Line) {}

  /// Whether there is source location tied to the RecordLoc object.
  ///
  /// \return True if a source file path is set.
  bool isValid() const { return !File.empty(); }

  /// Compare two source locations for equality.
  ///
  /// \param O The other location to compare against.
  /// \return True if both locations have the same file and line.
  bool operator==(const RecordLoc &O) const {
    return std::tie(File, Line) == std::tie(O.File, O.Line);
  }

  /// Source file path associated with the record, or empty if unknown.
  const std::string File;
  /// Line number in \c File, or zero if unknown.
  const unsigned Line = 0;
};

/// Linkage of a TAPI record relative to the library interface.
enum class RecordLinkage : uint8_t {
  /// Unknown linkage.
  Unknown = 0,

  /// Local, hidden or private extern linkage.
  Internal = 1,

  /// Undefined linkage, it represents usage of external interface.
  Undefined = 2,

  /// Re-exported linkage, record is defined in external interface.
  Rexported = 3,

  /// Exported linkage.
  Exported = 4,
};

/// Define Record. They represent API's in binaries that could be linkable
/// symbols.
class Record {
public:
  /// Construct an empty record with default attributes.
  Record() = default;
  /// Construct a record with a name, linkage, and symbol flags.
  ///
  /// \param Name The symbol name.
  /// \param Linkage The linkage of the symbol.
  /// \param Flags The flags that describe attributes of the symbol.
  Record(StringRef Name, RecordLinkage Linkage, SymbolFlags Flags)
      : Name(Name), Linkage(Linkage), Flags(mergeFlags(Flags, Linkage)),
        Verified(false) {}

  /// Whether the symbol is weakly defined.
  ///
  /// \return True if the symbol is weakly defined.
  bool isWeakDefined() const {
    return (Flags & SymbolFlags::WeakDefined) == SymbolFlags::WeakDefined;
  }

  /// Whether the symbol is weakly referenced.
  ///
  /// \return True if the symbol is weakly referenced.
  bool isWeakReferenced() const {
    return (Flags & SymbolFlags::WeakReferenced) == SymbolFlags::WeakReferenced;
  }

  /// Whether the symbol is a thread-local value.
  ///
  /// \return True if the symbol is a thread-local value.
  bool isThreadLocalValue() const {
    return (Flags & SymbolFlags::ThreadLocalValue) ==
           SymbolFlags::ThreadLocalValue;
  }

  /// Whether the symbol lives in a data segment.
  ///
  /// \return True if the symbol lives in a data segment.
  bool isData() const {
    return (Flags & SymbolFlags::Data) == SymbolFlags::Data;
  }

  /// Whether the symbol lives in a text segment.
  ///
  /// \return True if the symbol lives in a text segment.
  bool isText() const {
    return (Flags & SymbolFlags::Text) == SymbolFlags::Text;
  }

  /// Whether the record has internal linkage.
  ///
  /// \return True if the record has internal linkage.
  bool isInternal() const { return Linkage == RecordLinkage::Internal; }
  /// Whether the record has undefined linkage.
  ///
  /// \return True if the record has undefined linkage.
  bool isUndefined() const { return Linkage == RecordLinkage::Undefined; }
  /// Whether the record is exported or re-exported.
  ///
  /// \return True if the record is exported or re-exported.
  bool isExported() const { return Linkage >= RecordLinkage::Rexported; }
  /// Whether the record is re-exported from another interface.
  ///
  /// \return True if the record is re-exported from another interface.
  bool isRexported() const { return Linkage == RecordLinkage::Rexported; }

  /// Whether the record has been verified against the interface.
  ///
  /// \return True if the record has been verified against the interface.
  bool isVerified() const { return Verified; }
  /// Set whether the record has been verified.
  ///
  /// \param V True if the record is verified.
  void setVerify(bool V = true) { Verified = V; }

  /// Get the symbol name.
  ///
  /// \return The symbol name.
  StringRef getName() const { return Name; }
  /// Get the symbol flags.
  ///
  /// \return The flags that describe attributes of the symbol.
  SymbolFlags getFlags() const { return Flags; }

private:
  LLVM_ABI SymbolFlags mergeFlags(SymbolFlags Flags, RecordLinkage Linkage);

protected:
  /// Symbol name of the record.
  StringRef Name;
  /// Linkage of the record.
  RecordLinkage Linkage;
  /// Flags that describe attributes of the symbol.
  SymbolFlags Flags;
  /// Whether the record has been verified against the interface.
  bool Verified;

  friend class RecordsSlice;
};

/// Non-ObjC record representing a global variable or function.
class GlobalRecord : public Record {
public:
  /// Kind of global symbol represented by this record.
  enum class Kind : uint8_t {
    /// Unknown global kind.
    Unknown = 0,
    /// Global variable.
    Variable = 1,
    /// Global function.
    Function = 2,
  };

  /// Construct a global record.
  ///
  /// \param Name The symbol name.
  /// \param Linkage The linkage of the symbol.
  /// \param Flags The flags that describe attributes of the symbol.
  /// \param GV The kind of global.
  /// \param Inlined Whether the declaration is inlined (functions only).
  GlobalRecord(StringRef Name, RecordLinkage Linkage, SymbolFlags Flags,
               Kind GV, bool Inlined)
      : Record({Name, Linkage, Flags}), GV(GV), Inlined(Inlined) {}

  /// Whether this record represents a function.
  ///
  /// \return True if this record represents a function.
  bool isFunction() const { return GV == Kind::Function; }
  /// Whether this record represents a variable.
  ///
  /// \return True if this record represents a variable.
  bool isVariable() const { return GV == Kind::Variable; }
  /// Set the global kind if it is still unknown.
  ///
  /// \param V The kind to assign when the current kind is unknown.
  void setKind(const Kind &V) {
    if (GV == Kind::Unknown)
      GV = V;
  }
  /// Whether the function declaration is inlined.
  ///
  /// \return True if the function declaration is inlined.
  bool isInlined() const { return Inlined; }

private:
  Kind GV;
  bool Inlined = false;
};

/// Objective-C instance variable record.
class ObjCIVarRecord : public Record {
public:
  /// Construct an Objective-C instance variable record.
  ///
  /// \param Name The ivar name.
  /// \param Linkage The linkage of the symbol.
  ObjCIVarRecord(StringRef Name, RecordLinkage Linkage)
      : Record({Name, Linkage, SymbolFlags::Data}) {}

  /// Build a scoped ivar name from a superclass and ivar identifier.
  ///
  /// \param SuperClass The owning class or category name.
  /// \param IVar The instance variable name.
  /// \return The scoped name \c SuperClass.IVar.
  static std::string createScopedName(StringRef SuperClass, StringRef IVar) {
    return (SuperClass + "." + IVar).str();
  }
};

/// Map from keys to owned records of type \p V.
template <typename V, typename K = StringRef,
          typename std::enable_if<std::is_base_of<Record, V>::value>::type * =
              nullptr>
using RecordMap = llvm::MapVector<K, std::unique_ptr<V>>;

/// Objective-C container with methods, properties, ivars, and protocols.
class ObjCContainerRecord : public Record {
public:
  /// Construct an Objective-C container record.
  ///
  /// \param Name The container name.
  /// \param Linkage The linkage of the symbol.
  ObjCContainerRecord(StringRef Name, RecordLinkage Linkage)
      : Record({Name, Linkage, SymbolFlags::Data}) {}

  /// Add an instance variable to this container.
  ///
  /// \param IVar The instance variable name.
  /// \param Linkage The linkage of the ivar symbol.
  /// \return Non-owning pointer to the added ivar record.
  LLVM_ABI ObjCIVarRecord *addObjCIVar(StringRef IVar, RecordLinkage Linkage);
  /// Find an instance variable by name.
  ///
  /// \param IVar The instance variable name.
  /// \return Non-owning pointer to the ivar, or null if not found.
  LLVM_ABI ObjCIVarRecord *findObjCIVar(StringRef IVar) const;
  /// Get all instance variables owned by this container.
  ///
  /// \return Non-owning pointers to the ivar records.
  LLVM_ABI std::vector<ObjCIVarRecord *> getObjCIVars() const;
  /// Get the linkage of this container.
  ///
  /// \return The linkage of the record.
  RecordLinkage getLinkage() const { return Linkage; }

private:
  RecordMap<ObjCIVarRecord> IVars;
};

/// Objective-C category that extends a class with additional ivars.
class ObjCCategoryRecord : public ObjCContainerRecord {
public:
  /// Construct an Objective-C category record.
  ///
  /// \param ClassToExtend The name of the class being extended.
  /// \param Name The category name.
  ObjCCategoryRecord(StringRef ClassToExtend, StringRef Name)
      : ObjCContainerRecord(Name, RecordLinkage::Unknown),
        ClassToExtend(ClassToExtend) {}

  /// Get the name of the class this category extends.
  ///
  /// \return The superclass (extended class) name.
  StringRef getSuperClassName() const { return ClassToExtend; }

private:
  StringRef ClassToExtend;
};

/// Objective-C interface (class) record.
class ObjCInterfaceRecord : public ObjCContainerRecord {
public:
  /// Construct an Objective-C interface record.
  ///
  /// \param Name The class name.
  /// \param Linkage The linkage of the symbol.
  /// \param SymType The ObjC interface symbol kinds this record represents.
  ObjCInterfaceRecord(StringRef Name, RecordLinkage Linkage,
                      ObjCIFSymbolKind SymType)
      : ObjCContainerRecord(Name, Linkage) {
    updateLinkageForSymbols(SymType, Linkage);
  }

  /// Whether the class has an exception type attribute symbol.
  ///
  /// \return True if an exception type attribute symbol is present.
  bool hasExceptionAttribute() const {
    return Linkages.EHType != RecordLinkage::Unknown;
  }
  /// Whether both class and metaclass symbols are exported or re-exported.
  ///
  /// \return True if class and metaclass are exported or re-exported.
  bool isCompleteInterface() const {
    return Linkages.Class >= RecordLinkage::Rexported &&
           Linkages.MetaClass >= RecordLinkage::Rexported;
  }
  /// Whether the given ObjC interface symbol kind is exported or re-exported.
  ///
  /// \param CurrType The ObjC interface symbol kind to query.
  /// \return True if that symbol kind is exported or re-exported.
  bool isExportedSymbol(ObjCIFSymbolKind CurrType) const {
    return getLinkageForSymbol(CurrType) >= RecordLinkage::Rexported;
  }

  /// Get the linkage for a specific ObjC interface symbol kind.
  ///
  /// \param CurrType The ObjC interface symbol kind to query.
  /// \return The linkage recorded for that symbol kind.
  LLVM_ABI RecordLinkage getLinkageForSymbol(ObjCIFSymbolKind CurrType) const;
  /// Update linkage for the ObjC interface symbol kinds in \p SymType.
  ///
  /// \param SymType The ObjC interface symbol kinds to update.
  /// \param Link The linkage to assign.
  LLVM_ABI void updateLinkageForSymbols(ObjCIFSymbolKind SymType,
                                        RecordLinkage Link);

  /// Add a category that extends this class.
  ///
  /// \param Record The category record to associate.
  /// \return True if the category was added.
  LLVM_ABI bool addObjCCategory(ObjCCategoryRecord *Record);
  /// Get all categories that extend this class.
  ///
  /// \return Non-owning pointers to the category records.
  LLVM_ABI std::vector<ObjCCategoryRecord *> getObjCCategories() const;

private:
  /// Linkage level for each symbol represented in ObjCInterfaceRecord.
  struct Linkages {
    RecordLinkage Class = RecordLinkage::Unknown;
    RecordLinkage MetaClass = RecordLinkage::Unknown;
    RecordLinkage EHType = RecordLinkage::Unknown;
    bool operator==(const Linkages &other) const {
      return std::tie(Class, MetaClass, EHType) ==
             std::tie(other.Class, other.MetaClass, other.EHType);
    }
    bool operator!=(const Linkages &other) const { return !(*this == other); }
  };
  Linkages Linkages;

  // Non-owning containers of categories that extend the class.
  llvm::MapVector<StringRef, ObjCCategoryRecord *> Categories;
};

} // end namespace MachO.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_RECORD_H
