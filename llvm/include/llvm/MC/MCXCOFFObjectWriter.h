//===-- llvm/MC/MCXCOFFObjectWriter.h - XCOFF Object Writer ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCXCOFFOBJECTWRITER_H
#define LLVM_MC_MCXCOFFOBJECTWRITER_H

#include "llvm/MC/MCObjectWriter.h"

namespace llvm {

class raw_pwrite_stream;

/// Target-specific XCOFF object-file writer hooks.
class LLVM_ABI MCXCOFFObjectTargetWriter : public MCObjectTargetWriter {
protected:
  /// Construct an XCOFF target object writer.
  ///
  /// \param Is64Bit - True if writing a 64-bit XCOFF object.
  MCXCOFFObjectTargetWriter(bool Is64Bit);

public:
  /// Destroy the XCOFF target object writer.
  ~MCXCOFFObjectTargetWriter() override;

  /// Return the object file format handled by this writer.
  ///
  /// \returns The XCOFF object format type.
  Triple::ObjectFormatType getFormat() const override { return Triple::XCOFF; }
  /// Return true if \p W is an XCOFF target object writer.
  ///
  /// \param W - Object target writer to test.
  /// \returns True if \p W is an XCOFF target object writer.
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::XCOFF;
  }
  /// Return true if this writer targets 64-bit XCOFF.
  ///
  /// \returns True if this writer targets 64-bit XCOFF.
  bool is64Bit() const { return Is64Bit; }

  /// Return the XCOFF relocation type and encoded sign/size for \p Fixup.
  ///
  /// The first element of the pair is the relocation type. The second contains
  /// the signedness and size encoded as in the XCOFF \c r_rsize field.
  ///
  /// \param Target - Relocatable expression evaluated for the fixup.
  /// \param Fixup - Fixup being recorded as a relocation.
  /// \param IsPCRel - True if the relocation is PC-relative.
  /// \returns A pair of the relocation type and the encoded sign/size.
  virtual std::pair<uint8_t, uint8_t>
  getRelocTypeAndSignSize(const MCValue &Target, const MCFixup &Fixup,
                          bool IsPCRel) const = 0;

private:
  bool Is64Bit;
};

/// XCOFF object writer that emits XCOFF object files from an MCAssembler.
class XCOFFObjectWriter : public MCObjectWriter {
  // AIX specific CPU type.
  std::string CPUType;

public:
  /// Record a trap in the XCOFF exception section for \p Symbol.
  ///
  /// \param Symbol - Function containing the trap.
  /// \param Trap - Trap-instruction symbol.
  /// \param LanguageCode - Language code for the exception entry.
  /// \param ReasonCode - Reason code for the exception entry.
  /// \param FunctionSize - Size of the function containing the trap.
  /// \param hasDebug - True if the function has debug information.
  virtual void addExceptionEntry(const MCSymbol *Symbol, const MCSymbol *Trap,
                                 unsigned LanguageCode, unsigned ReasonCode,
                                 unsigned FunctionSize, bool hasDebug) = 0;
  /// Record a C_INFO symbol with embedded metadata in the .info section.
  ///
  /// \param Name - Name of the C_INFO symbol.
  /// \param Metadata - Embedded metadata payload.
  virtual void addCInfoSymEntry(StringRef Name, StringRef Metadata) = 0;
  /// Return the AIX CPU name stored for C_FILE symbol encoding.
  ///
  /// \returns The AIX CPU name used when encoding C_FILE symbols.
  StringRef getCPUType() const { return CPUType; }
  /// Set the AIX CPU name used when encoding C_FILE symbols.
  ///
  /// \param TargetCPU - Target CPU name to encode in C_FILE symbols.
  void setCPU(StringRef TargetCPU) { CPUType = TargetCPU; }
};

/// Construct a new XCOFF writer instance.
///
/// \param MOTW - The target specific XCOFF writer subclass.
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
LLVM_ABI std::unique_ptr<MCObjectWriter>
createXCOFFObjectWriter(std::unique_ptr<MCXCOFFObjectTargetWriter> MOTW,
                        raw_pwrite_stream &OS);

} // end namespace llvm

#endif // LLVM_MC_MCXCOFFOBJECTWRITER_H
