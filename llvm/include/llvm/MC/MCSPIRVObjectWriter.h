//===-- llvm/MC/MCSPIRVObjectWriter.h - SPIR-V Object Writer -----*- C++ *-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSPIRVOBJECTWRITER_H
#define LLVM_MC_MCSPIRVOBJECTWRITER_H

#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace llvm {

/// Target-specific SPIR-V object-file writer hooks.
class MCSPIRVObjectTargetWriter : public MCObjectTargetWriter {
public:
  /// Return the object file format handled by this writer.
  ///
  /// \returns The SPIR-V object format type.
  Triple::ObjectFormatType getFormat() const override { return Triple::SPIRV; }
  /// Return true if \p W is a SPIR-V target object writer.
  ///
  /// \param W - Object target writer to test.
  /// \returns True if \p W is a SPIR-V target object writer.
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::SPIRV;
  }
};

/// SPIR-V object writer that emits SPIR-V object files from an MCAssembler.
class LLVM_ABI SPIRVObjectWriter final : public MCObjectWriter {
  support::endian::Writer W;
  std::unique_ptr<MCSPIRVObjectTargetWriter> TargetObjectWriter;

  struct VersionInfoType {
    unsigned Major = 0;
    unsigned Minor = 0;
    unsigned Bound = 0;
  } VersionInfo;

public:
  /// Construct a SPIR-V object writer.
  ///
  /// \param MOTW - The target specific SPIR-V writer subclass.
  /// \param OS - The stream to write to.
  SPIRVObjectWriter(std::unique_ptr<MCSPIRVObjectTargetWriter> MOTW,
                    raw_pwrite_stream &OS)
      : W(OS, llvm::endianness::little), TargetObjectWriter(std::move(MOTW)) {}

  /// Set the SPIR-V module version and ID bound written in the header.
  ///
  /// \param Major - SPIR-V major version number.
  /// \param Minor - SPIR-V minor version number.
  /// \param Bound - Bound on the range of IDs used by the module.
  void setBuildVersion(unsigned Major, unsigned Minor, unsigned Bound);

private:
  uint64_t writeObject() override;
  void writeHeader(const MCAssembler &Asm);
};

/// Construct a new SPIR-V writer instance.
///
/// \param MOTW - The target specific SPIR-V writer subclass.
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
LLVM_ABI std::unique_ptr<MCObjectWriter>
createSPIRVObjectWriter(std::unique_ptr<MCSPIRVObjectTargetWriter> MOTW,
                        raw_pwrite_stream &OS);

} // namespace llvm

#endif
