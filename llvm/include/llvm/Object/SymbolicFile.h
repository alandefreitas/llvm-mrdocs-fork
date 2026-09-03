//===- SymbolicFile.h - Interface that only provides symbols ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SymbolicFile interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_SYMBOLICFILE_H
#define LLVM_OBJECT_SYMBOLICFILE_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>

namespace llvm {

class LLVMContext;
class raw_ostream;

namespace object {

/// Opaque handle identifying a content object (symbol, section, etc.) in a
/// SymbolicFile.
union DataRefImpl {
  // This entire union should probably be a
  // char[max(8, sizeof(uintptr_t))] and require the impl to cast.
  /// Pair of 32-bit words used when the format stores a two-word key.
  struct {
    uint32_t a; ///< First 32-bit word of the two-word key.
    uint32_t b; ///< Second 32-bit word of the two-word key.
  } d;
  /// Opaque pointer used when the format stores a single address-sized key.
  uintptr_t p;

  /// Zero-initializes the union.
  DataRefImpl() { std::memset(this, 0, sizeof(DataRefImpl)); }
};

/// Print \p D as a hex pointer and its a/b dword pair.
///
/// \param OS Stream to write to.
/// \param D DataRefImpl value to print.
/// \return A reference to \p OS.
template <typename OStream>
OStream& operator<<(OStream &OS, const DataRefImpl &D) {
  OS << "(" << format("0x%08" PRIxPTR, D.p) << " (" << format("0x%08x", D.d.a)
     << ", " << format("0x%08x", D.d.b) << "))";
  return OS;
}

/// True if both DataRefImpl values are bitwise identical.
///
/// \param a Left-hand DataRefImpl.
/// \param b Right-hand DataRefImpl.
/// \return True if both DataRefImpl values are bitwise identical.
inline bool operator==(const DataRefImpl &a, const DataRefImpl &b) {
  // Check bitwise identical. This is the only legal way to compare a union w/o
  // knowing which member is in use.
  return std::memcmp(&a, &b, sizeof(DataRefImpl)) == 0;
}

/// True if the two DataRefImpl values differ bitwise.
///
/// \param a Left-hand DataRefImpl.
/// \param b Right-hand DataRefImpl.
/// \return True if the two DataRefImpl values differ bitwise.
inline bool operator!=(const DataRefImpl &a, const DataRefImpl &b) {
  return !operator==(a, b);
}

/// Bitwise lexicographical compare of two DataRefImpl values.
///
/// \param a Left-hand DataRefImpl.
/// \param b Right-hand DataRefImpl.
/// \return True if \p a orders before \p b bitwise.
inline bool operator<(const DataRefImpl &a, const DataRefImpl &b) {
  // Check bitwise identical. This is the only legal way to compare a union w/o
  // knowing which member is in use.
  return std::memcmp(&a, &b, sizeof(DataRefImpl)) < 0;
}

/// Forward iterator over content objects of type \p content_type in a SymbolicFile.
template <class content_type> class content_iterator {
  content_type Current;

public:
  /// Marks this as a forward iterator.
  using iterator_category = std::forward_iterator_tag;
  /// Element type yielded by this iterator.
  using value_type = const content_type;
  /// Distance type for iterator arithmetic.
  using difference_type = std::ptrdiff_t;
  /// Pointer to the current content object.
  using pointer = value_type *;
  /// Reference to the current content object.
  using reference = value_type &;

  /// Construct an iterator positioned at \p symb.
  ///
  /// \param symb Content object that becomes the iterator's current position.
  content_iterator(content_type symb) : Current(std::move(symb)) {}

  /// Access the current content object.
  ///
  /// \return A pointer to the current content object.
  const content_type *operator->() const { return &Current; }

  /// Dereference to the current content object.
  ///
  /// \return A reference to the current content object.
  const content_type &operator*() const { return Current; }

  /// True if both iterators refer to the same content object.
  ///
  /// \param other Iterator to compare against.
  /// \return True if both iterators refer to the same content object.
  bool operator==(const content_iterator &other) const {
    return Current == other.Current;
  }

  /// True if the iterators refer to different content objects.
  ///
  /// \param other Iterator to compare against.
  /// \return True if the iterators refer to different content objects.
  bool operator!=(const content_iterator &other) const {
    return !(*this == other);
  }

  /// Advance to the next content object (preincrement).
  ///
  /// \return A reference to this iterator after advancing.
  content_iterator &operator++() {
    Current.moveNext();
    return *this;
  }
};

class SymbolicFile;

/// This is a value type class that represents a single symbol in the list of
/// symbols in the object file.
class BasicSymbolRef {
  DataRefImpl SymbolPimpl;
  const SymbolicFile *OwningObject = nullptr;

public:
  /// Bit flags describing symbol properties.
  enum Flags : unsigned {
    SF_None = 0,                 ///< No flags set.
    SF_Undefined = 1U << 0,      ///< Defined in another object file.
    SF_Global = 1U << 1,         ///< Global symbol.
    SF_Weak = 1U << 2,           ///< Weak symbol.
    SF_Absolute = 1U << 3,       ///< Absolute symbol.
    SF_Common = 1U << 4,         ///< Common linkage.
    SF_Indirect = 1U << 5,       ///< Alias to another symbol.
    SF_Exported = 1U << 6,       ///< Visible to other DSOs.
    SF_FormatSpecific = 1U << 7, ///< Format-specific (e.g. section symbols).
    SF_Thumb = 1U << 8,          ///< Thumb symbol in a 32-bit ARM binary.
    SF_Hidden = 1U << 9,         ///< Hidden visibility.
    SF_Const = 1U << 10,         ///< Constant symbol value.
    SF_Executable = 1U << 11,    ///< Points to an executable section (IR only).
  };

  /// Default-construct an empty symbol reference.
  BasicSymbolRef() = default;
  /// Construct a symbol reference for \p SymbolP in \p Owner.
  ///
  /// \param SymbolP Opaque format-specific symbol handle.
  /// \param Owner SymbolicFile that owns the symbol.
  BasicSymbolRef(DataRefImpl SymbolP, const SymbolicFile *Owner);

  /// True if this symbol refers to the same entry as \p Other.
  ///
  /// \param Other Symbol reference to compare against.
  /// \return True if the symbols refer to the same entry.
  bool operator==(const BasicSymbolRef &Other) const;
  /// Order symbols by their underlying DataRefImpl representation.
  ///
  /// \param Other Symbol reference to compare against.
  /// \return True if this symbol orders before \p Other.
  bool operator<(const BasicSymbolRef &Other) const;

  /// Advance to the next symbol in the owning SymbolicFile.
  void moveNext();

  /// Writes the symbol name to \p OS.
  ///
  /// \param OS Stream to write the symbol name to.
  /// \return Error::success() on success, or an error if printing fails.
  Error printName(raw_ostream &OS) const;

  /// Get symbol flags (bitwise OR of SymbolRef::Flags)
  ///
  /// \return The symbol flags, or an error if unavailable.
  Expected<uint32_t> getFlags() const;

  /// Opaque format-specific handle for this symbol.
  ///
  /// \return The opaque DataRefImpl for this symbol.
  DataRefImpl getRawDataRefImpl() const;
  /// Returns the SymbolicFile that owns this symbol.
  ///
  /// \return Pointer to the owning SymbolicFile.
  const SymbolicFile *getObject() const;
};

/// Iterator over BasicSymbolRef symbols in a SymbolicFile.
using basic_symbol_iterator = content_iterator<BasicSymbolRef>;

/// Binary that exposes a symbol table (object file, IR, COFF import, or TAPI).
class LLVM_ABI SymbolicFile : public Binary {
public:
  /// Construct a SymbolicFile of \p Type backed by \p Source.
  ///
  /// \param Type Binary type discriminator for this file.
  /// \param Source Memory buffer holding the file contents.
  SymbolicFile(unsigned int Type, MemoryBufferRef Source);
  /// Virtual destructor for polymorphic SymbolicFile subclasses.
  ~SymbolicFile() override;

  // virtual interface.
  /// Advances \p Symb to the next symbol.
  ///
  /// \param Symb Symbol data reference to advance.
  virtual void moveSymbolNext(DataRefImpl &Symb) const = 0;

  /// Print the name of symbol \p Symb to \p OS.
  ///
  /// \param OS Stream to write the symbol name to.
  /// \param Symb Symbol data reference whose name is printed.
  /// \return Error::success() on success, or an error if printing fails.
  virtual Error printSymbolName(raw_ostream &OS, DataRefImpl Symb) const = 0;

  /// Flags for symbol \p Symb (bitwise OR of BasicSymbolRef::Flags).
  ///
  /// \param Symb Symbol data reference whose flags are returned.
  /// \return The symbol flags, or an error if unavailable.
  virtual Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const = 0;

  /// Iterator to the first symbol in this file.
  ///
  /// \return An iterator to the first symbol in this file.
  virtual basic_symbol_iterator symbol_begin() const = 0;

  /// Past-the-end iterator for symbols in this file.
  ///
  /// \return A past-the-end iterator for symbols in this file.
  virtual basic_symbol_iterator symbol_end() const = 0;

  /// True if this file uses a 64-bit address size.
  ///
  /// \return True if this file uses a 64-bit address size.
  virtual bool is64Bit() const = 0;

  // convenience wrappers.
  /// Iterator range over BasicSymbolRef symbols.
  using basic_symbol_iterator_range = iterator_range<basic_symbol_iterator>;
  /// Range over all symbols in this file.
  ///
  /// \return An iterator range over all symbols in this file.
  basic_symbol_iterator_range symbols() const {
    return basic_symbol_iterator_range(symbol_begin(), symbol_end());
  }

  // construction aux.
  /// Create a SymbolicFile from \p Object with the given type and context.
  ///
  /// \param Object Memory buffer holding the file contents.
  /// \param Type Detected or requested file magic / type.
  /// \param Context Optional LLVM IR context (used for bitcode).
  /// \param InitContent Whether to initialize format-specific content.
  /// \return A SymbolicFile for \p Object, or an error on failure.
  static Expected<std::unique_ptr<SymbolicFile>>
  createSymbolicFile(MemoryBufferRef Object, llvm::file_magic Type,
                     LLVMContext *Context, bool InitContent = true);

  /// Create a SymbolicFile from \p Object, autodetecting the file type.
  ///
  /// \param Object Memory buffer holding the file contents.
  /// \return A SymbolicFile for \p Object, or an error on failure.
  static Expected<std::unique_ptr<SymbolicFile>>
  createSymbolicFile(MemoryBufferRef Object) {
    return createSymbolicFile(Object, llvm::file_magic::unknown, nullptr);
  }

  /// True if \p v is a SymbolicFile (IR, object, COFF import, or TAPI).
  ///
  /// \param v Binary to test.
  /// \return True if \p v is a SymbolicFile.
  static bool classof(const Binary *v) {
    return v->isSymbolic();
  }

  /// True if \p Type identifies a symbolic file format (optionally using \p Context for IR).
  ///
  /// \param Type File magic / type to test.
  /// \param Context Optional LLVM IR context used when recognizing bitcode.
  /// \return True if \p Type identifies a symbolic file format.
  static bool isSymbolicFile(file_magic Type, const LLVMContext *Context);
};

inline BasicSymbolRef::BasicSymbolRef(DataRefImpl SymbolP,
                                      const SymbolicFile *Owner)
    : SymbolPimpl(SymbolP), OwningObject(Owner) {}

inline bool BasicSymbolRef::operator==(const BasicSymbolRef &Other) const {
  return SymbolPimpl == Other.SymbolPimpl;
}

inline bool BasicSymbolRef::operator<(const BasicSymbolRef &Other) const {
  return SymbolPimpl < Other.SymbolPimpl;
}

inline void BasicSymbolRef::moveNext() {
  return OwningObject->moveSymbolNext(SymbolPimpl);
}

inline Error BasicSymbolRef::printName(raw_ostream &OS) const {
  return OwningObject->printSymbolName(OS, SymbolPimpl);
}

inline Expected<uint32_t> BasicSymbolRef::getFlags() const {
  return OwningObject->getSymbolFlags(SymbolPimpl);
}

inline DataRefImpl BasicSymbolRef::getRawDataRefImpl() const {
  return SymbolPimpl;
}

inline const SymbolicFile *BasicSymbolRef::getObject() const {
  return OwningObject;
}

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_SYMBOLICFILE_H
