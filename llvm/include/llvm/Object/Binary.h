//===- Binary.h - A generic binary file -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Binary class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_BINARY_H
#define LLVM_OBJECT_BINARY_H

#include "llvm-c/Types.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>
#include <utility>

namespace llvm {

/// Forward declaration of the LLVM IR context class.
class LLVMContext;
class StringRef;

namespace object {

/// Base class for an in-memory binary file (object, archive, IR, etc.).
class LLVM_ABI Binary {
private:
  unsigned int TypeID;

protected:
  /// Underlying file contents for this binary.
  MemoryBufferRef Data;

  /// Construct a Binary of \p Type backed by \p Source.
  ///
  /// \param Type Discriminator type ID for the concrete subclass.
  /// \param Source Memory buffer holding the binary's file contents.
  Binary(unsigned int Type, MemoryBufferRef Source);

  /// Discriminators for concrete Binary subclasses.
  enum {
    ID_Archive,              ///< Static or dynamic archive.
    ID_MachOUniversalBinary, ///< Mach-O fat/universal binary.
    ID_COFFImportFile,       ///< COFF import library member.
    ID_IR,                   ///< LLVM IR bitcode.
    ID_TapiUniversal,        ///< Text-based stub (TAPI) universal file.
    ID_TapiFile,             ///< Text-based stub (TAPI) file.

    ID_Minidump, ///< Windows minidump.

    ID_WinRes, ///< Windows resource (.res) file.

    ID_Offload, ///< Offloading binary file.

    ID_StartObjects, ///< Sentinel before object-file type IDs.
    ID_COFF,         ///< COFF object file.

    ID_XCOFF32, ///< AIX XCOFF 32-bit object.
    ID_XCOFF64, ///< AIX XCOFF 64-bit object.

    ID_ELF32L, ///< ELF 32-bit, little endian.
    ID_ELF32B, ///< ELF 32-bit, big endian.
    ID_ELF64L, ///< ELF 64-bit, little endian.
    ID_ELF64B, ///< ELF 64-bit, big endian.

    ID_MachO32L, ///< Mach-O 32-bit, little endian.
    ID_MachO32B, ///< Mach-O 32-bit, big endian.
    ID_MachO64L, ///< Mach-O 64-bit, little endian.
    ID_MachO64B, ///< Mach-O 64-bit, big endian.

    ID_GOFF,        ///< GOFF object file.
    ID_Wasm,        ///< WebAssembly object file.
    ID_DXContainer, ///< DirectX container.

    ID_EndObjects ///< Sentinel after object-file type IDs.
  };

  /// Map ELF endianness and bitness to the matching type ID.
  ///
  /// \param isLE True for little-endian ELF; false for big-endian.
  /// \param is64Bits True for 64-bit ELF; false for 32-bit.
  /// \return The Binary type ID for the given ELF layout.
  static inline unsigned int getELFType(bool isLE, bool is64Bits) {
    if (isLE)
      return is64Bits ? ID_ELF64L : ID_ELF32L;
    else
      return is64Bits ? ID_ELF64B : ID_ELF32B;
  }

  /// Map Mach-O endianness and bitness to the matching type ID.
  ///
  /// \param isLE True for little-endian Mach-O; false for big-endian.
  /// \param is64Bits True for 64-bit Mach-O; false for 32-bit.
  /// \return The Binary type ID for the given Mach-O layout.
  static unsigned int getMachOType(bool isLE, bool is64Bits) {
    if (isLE)
      return is64Bits ? ID_MachO64L : ID_MachO32L;
    else
      return is64Bits ? ID_MachO64B : ID_MachO32B;
  }

public:
  /// Default construction is deleted; binaries require a type and buffer.
  Binary() = delete;
  /// Deleted copy constructor.
  ///
  /// \param other Binary that would have been copied (unused; deleted).
  Binary(const Binary &other) = delete;
  /// Virtual destructor for polymorphic Binary subclasses.
  virtual ~Binary();

  /// Perform format-specific initialization of the binary's content.
  ///
  /// \return Success, or an error if content initialization fails.
  virtual Error initContent() { return Error::success(); };

  /// Returns the binary's raw buffer contents.
  ///
  /// \return The binary's raw buffer contents.
  StringRef getData() const;
  /// Returns the file name (buffer identifier) for this binary.
  ///
  /// \return The file name (buffer identifier) for this binary.
  StringRef getFileName() const;
  /// Returns a MemoryBufferRef over the binary's data.
  ///
  /// \return A MemoryBufferRef over the binary's data.
  MemoryBufferRef getMemoryBufferRef() const;

  // Cast methods.
  /// Return the discriminator type ID for this binary.
  ///
  /// \return The discriminator type ID for this binary.
  unsigned int getType() const { return TypeID; }

  // Convenience methods
  /// True if this is a concrete object-file format (ELF, COFF, Mach-O, etc.).
  ///
  /// \return True if this is a concrete object-file format.
  bool isObject() const {
    return TypeID > ID_StartObjects && TypeID < ID_EndObjects;
  }

  /// True if this binary exposes a symbol table (IR, object, COFF import, or TAPI).
  ///
  /// \return True if this binary exposes a symbol table.
  bool isSymbolic() const {
    return isIR() || isObject() || isCOFFImportFile() || isTapiFile();
  }

  /// True if this is a static or dynamic archive.
  ///
  /// \return True if this is a static or dynamic archive.
  bool isArchive() const { return TypeID == ID_Archive; }

  /// True if this is a Mach-O fat/universal binary.
  ///
  /// \return True if this is a Mach-O fat/universal binary.
  bool isMachOUniversalBinary() const {
    return TypeID == ID_MachOUniversalBinary;
  }

  /// True if this is a text-based stub (TAPI) universal file.
  ///
  /// \return True if this is a text-based stub (TAPI) universal file.
  bool isTapiUniversal() const { return TypeID == ID_TapiUniversal; }

  /// True if this is an ELF object (any bitness/endianness).
  ///
  /// \return True if this is an ELF object.
  bool isELF() const {
    return TypeID >= ID_ELF32L && TypeID <= ID_ELF64B;
  }

  /// True if this is a Mach-O object (32- or 64-bit, either endianness).
  ///
  /// \return True if this is a Mach-O object.
  bool isMachO() const {
    return TypeID >= ID_MachO32L && TypeID <= ID_MachO64B;
  }

  /// True if this is a COFF object file.
  ///
  /// \return True if this is a COFF object file.
  bool isCOFF() const {
    return TypeID == ID_COFF;
  }

  /// True if this is an AIX XCOFF object (32- or 64-bit).
  ///
  /// \return True if this is an AIX XCOFF object.
  bool isXCOFF() const { return TypeID == ID_XCOFF32 || TypeID == ID_XCOFF64; }

  /// True if this is a WebAssembly object file.
  ///
  /// \return True if this is a WebAssembly object file.
  bool isWasm() const { return TypeID == ID_Wasm; }

  /// True if this is an offloading binary file.
  ///
  /// \return True if this is an offloading binary file.
  bool isOffloadFile() const { return TypeID == ID_Offload; }

  /// True if this is a COFF import library member.
  ///
  /// \return True if this is a COFF import library member.
  bool isCOFFImportFile() const {
    return TypeID == ID_COFFImportFile;
  }

  /// True if this is an LLVM IR bitcode file.
  ///
  /// \return True if this is an LLVM IR bitcode file.
  bool isIR() const {
    return TypeID == ID_IR;
  }

  /// True if this is a GOFF object file.
  ///
  /// \return True if this is a GOFF object file.
  bool isGOFF() const { return TypeID == ID_GOFF; }

  /// True if this is a Windows minidump.
  ///
  /// \return True if this is a Windows minidump.
  bool isMinidump() const { return TypeID == ID_Minidump; }

  /// True if this is a text-based stub (TAPI) file.
  ///
  /// \return True if this is a text-based stub (TAPI) file.
  bool isTapiFile() const { return TypeID == ID_TapiFile; }

  /// True if this binary's object format is little-endian.
  ///
  /// \return True if this binary's object format is little-endian.
  bool isLittleEndian() const {
    return !(TypeID == ID_ELF32B || TypeID == ID_ELF64B ||
             TypeID == ID_MachO32B || TypeID == ID_MachO64B ||
             TypeID == ID_XCOFF32 || TypeID == ID_XCOFF64);
  }

  /// True if this is a Windows resource (.res) file.
  ///
  /// \return True if this is a Windows resource (.res) file.
  bool isWinRes() const { return TypeID == ID_WinRes; }

  /// True if this is a DirectX container.
  ///
  /// \return True if this is a DirectX container.
  bool isDXContainer() const { return TypeID == ID_DXContainer; }

  /// Maps this binary's type to a Triple::ObjectFormatType.
  ///
  /// \return The matching Triple::ObjectFormatType, or UnknownObjectFormat.
  Triple::ObjectFormatType getTripleObjectFormat() const {
    if (isCOFF())
      return Triple::COFF;
    if (isMachO())
      return Triple::MachO;
    if (isELF())
      return Triple::ELF;
    if (isGOFF())
      return Triple::GOFF;
    return Triple::UnknownObjectFormat;
  }

  /// Check that \p Addr..\p Addr+\p Size lies within buffer \p M.
  ///
  /// \param M Memory buffer that defines the valid address range.
  /// \param Addr Start address of the range to validate.
  /// \param Size Byte length of the range to validate.
  /// \return Success if the range is in bounds; unexpected_eof otherwise.
  static Error checkOffset(MemoryBufferRef M, uintptr_t Addr,
                           const uint64_t Size) {
    if (Addr + Size < Addr || Addr + Size < Size ||
        Addr + Size > reinterpret_cast<uintptr_t>(M.getBufferEnd()) ||
        Addr < reinterpret_cast<uintptr_t>(M.getBufferStart())) {
      return errorCodeToError(object_error::unexpected_eof);
    }
    return Error::success();
  }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Convert an LLVMBinaryRef opaque handle to a Binary pointer.
///
/// \param P Opaque C API handle to unwrap.
/// \return The Binary pointer corresponding to \p P.
inline Binary *unwrap(LLVMBinaryRef P) {
  return reinterpret_cast<Binary *>(P);
}

/// Convert a Binary pointer to an LLVMBinaryRef opaque handle.
///
/// \param P Binary instance to wrap for the C API.
/// \return An opaque LLVMBinaryRef for \p P.
inline LLVMBinaryRef wrap(const Binary *P) {
  return reinterpret_cast<LLVMBinaryRef>(const_cast<Binary *>(P));
}

/// Convert an LLVMBinaryRef to a pointer of Binary subclass \tparam T.
///
/// \param P Opaque C API handle to unwrap and cast.
/// \return A pointer of type T corresponding to \p P.
template <typename T> inline T *unwrap(LLVMBinaryRef P) {
  return cast<T>(unwrap(P));
}

/// Create a Binary from Source, autodetecting the file type.
///
/// \param Source The data to create the Binary from.
/// \param Context Optional LLVM IR context used when Source is bitcode;
///        may be null.
/// \param InitContent If true, run format-specific content initialization.
/// \return An owning pointer to the created Binary, or an error.
LLVM_ABI Expected<std::unique_ptr<Binary>>
createBinary(MemoryBufferRef Source, LLVMContext *Context = nullptr,
             bool InitContent = true);

/// Owns a Binary and the MemoryBuffer that backs it.
template <typename T> class OwningBinary {
  std::unique_ptr<T> Bin;
  std::unique_ptr<MemoryBuffer> Buf;

public:
  /// Construct an empty OwningBinary with no owned binary or buffer.
  OwningBinary();
  /// Take ownership of \p Bin and the MemoryBuffer \p Buf that backs it.
  ///
  /// \param Bin Binary instance to own.
  /// \param Buf MemoryBuffer that backs \p Bin.
  OwningBinary(std::unique_ptr<T> Bin, std::unique_ptr<MemoryBuffer> Buf);
  /// Move-construct by taking ownership from \p Other.
  ///
  /// \param Other OwningBinary to move from.
  OwningBinary(OwningBinary<T>&& Other);
  /// Move-assign from \p Other, transferring ownership of binary and buffer.
  ///
  /// \param Other OwningBinary to move from.
  /// \return A reference to this OwningBinary.
  OwningBinary<T> &operator=(OwningBinary<T> &&Other);

  /// Release ownership of the Binary and its backing MemoryBuffer.
  ///
  /// \return A pair of the owned Binary and MemoryBuffer unique_ptrs.
  std::pair<std::unique_ptr<T>, std::unique_ptr<MemoryBuffer>> takeBinary();

  /// Returns a pointer to the owned Binary, or null if empty.
  ///
  /// \return A pointer to the owned Binary, or null if empty.
  T* getBinary();
  /// Returns a pointer to the owned Binary, or null if empty.
  ///
  /// \return A pointer to the owned Binary, or null if empty.
  const T* getBinary() const;
};

template <typename T>
OwningBinary<T>::OwningBinary(std::unique_ptr<T> Bin,
                              std::unique_ptr<MemoryBuffer> Buf)
    : Bin(std::move(Bin)), Buf(std::move(Buf)) {}

template <typename T> OwningBinary<T>::OwningBinary() = default;

template <typename T>
OwningBinary<T>::OwningBinary(OwningBinary &&Other)
    : Bin(std::move(Other.Bin)), Buf(std::move(Other.Buf)) {}

template <typename T>
OwningBinary<T> &OwningBinary<T>::operator=(OwningBinary &&Other) {
  Bin = std::move(Other.Bin);
  Buf = std::move(Other.Buf);
  return *this;
}

template <typename T>
std::pair<std::unique_ptr<T>, std::unique_ptr<MemoryBuffer>>
OwningBinary<T>::takeBinary() {
  return std::make_pair(std::move(Bin), std::move(Buf));
}

template <typename T> T* OwningBinary<T>::getBinary() {
  return Bin.get();
}

template <typename T> const T* OwningBinary<T>::getBinary() const {
  return Bin.get();
}

/// Create a Binary by reading the file at Path, autodetecting its type.
///
/// \param Path Path to the file to read.
/// \param Context Optional LLVM IR context used when the file is bitcode;
///        may be null.
/// \param InitContent If true, run format-specific content initialization.
/// \return An OwningBinary wrapping the created Binary, or an error.
LLVM_ABI Expected<OwningBinary<Binary>>
createBinary(StringRef Path, LLVMContext *Context = nullptr,
             bool InitContent = true);

} // end namespace object

} // end namespace llvm

#endif // LLVM_OBJECT_BINARY_H
