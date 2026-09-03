//===-- llvm/MC/MCWasmObjectWriter.h - Wasm Object Writer -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCWASMOBJECTWRITER_H
#define LLVM_MC_MCWASMOBJECTWRITER_H

#include "llvm/MC/MCObjectWriter.h"
#include <memory>

namespace llvm {

class MCFixup;
class MCSectionWasm;
class MCValue;
class raw_pwrite_stream;

/// Target-specific Wasm object-file writer hooks.
class LLVM_ABI MCWasmObjectTargetWriter : public MCObjectTargetWriter {
  const unsigned Is64Bit : 1;
  const unsigned IsEmscripten : 1;

protected:
  /// Construct a Wasm target object writer.
  ///
  /// \param Is64Bit_ - True if writing a 64-bit Wasm object.
  /// \param IsEmscripten - True if targeting the Emscripten ABI.
  explicit MCWasmObjectTargetWriter(bool Is64Bit_, bool IsEmscripten);

public:
  /// Destroy the Wasm target object writer.
  ~MCWasmObjectTargetWriter() override;

  /// Return the object file format handled by this writer.
  ///
  /// \returns The Wasm object format type.
  Triple::ObjectFormatType getFormat() const override { return Triple::Wasm; }
  /// Return true if \p W is a Wasm target object writer.
  ///
  /// \param W - Object target writer to test.
  /// \returns True if \p W is a Wasm target object writer.
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::Wasm;
  }

  /// Return the Wasm relocation type for \p Fixup applied to \p Target.
  ///
  /// \param Target - Relocatable expression evaluated for the fixup.
  /// \param Fixup - Fixup being recorded as a relocation.
  /// \param FixupSection - Section containing the fixup.
  /// \param IsLocRel - True if the relocation is a local relative relocation.
  /// \returns The Wasm relocation type for the fixup.
  virtual unsigned getRelocType(const MCValue &Target, const MCFixup &Fixup,
                                const MCSectionWasm &FixupSection,
                                bool IsLocRel) const = 0;

  /// \name Accessors
  /// @{
  /// Return true if this writer targets 64-bit Wasm.
  ///
  /// \returns True if this writer targets 64-bit Wasm.
  bool is64Bit() const { return Is64Bit; }
  /// Return true if this writer targets the Emscripten ABI.
  ///
  /// \returns True if this writer targets the Emscripten ABI.
  bool isEmscripten() const { return IsEmscripten; }
  /// @}
};

/// Construct a new Wasm writer instance.
///
/// \param MOTW - The target specific Wasm writer subclass.
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
LLVM_ABI std::unique_ptr<MCObjectWriter>
createWasmObjectWriter(std::unique_ptr<MCWasmObjectTargetWriter> MOTW,
                       raw_pwrite_stream &OS);

/// Construct a new Wasm writer that emits split DWARF to a separate stream.
///
/// \param MOTW - The target specific Wasm writer subclass.
/// \param OS - The stream to write the main object to.
/// \param DwoOS - The stream to write the DWO object to.
/// \returns The constructed object writer.
LLVM_ABI std::unique_ptr<MCObjectWriter>
createWasmDwoObjectWriter(std::unique_ptr<MCWasmObjectTargetWriter> MOTW,
                          raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS);

} // namespace llvm

#endif
