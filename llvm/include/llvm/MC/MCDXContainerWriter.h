//===- llvm/MC/MCDXContainerWriter.h - DXContainer Writer -*- C++ -------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDXCONTAINERWRITER_H
#define LLVM_MC_MCDXCONTAINERWRITER_H

#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

class raw_pwrite_stream;

/// Target-specific DXContainer object-file writer hooks.
class LLVM_ABI MCDXContainerTargetWriter : public MCObjectTargetWriter {
protected:
  /// Construct a DXContainer target object writer.
  MCDXContainerTargetWriter() {}

public:
  /// Destroy the DXContainer target object writer.
  ~MCDXContainerTargetWriter() override;

  /// Return the object file format handled by this writer.
  ///
  /// \return - The DXContainer object file format.
  Triple::ObjectFormatType getFormat() const override {
    return Triple::DXContainer;
  }
  /// Return true if \p W is a DXContainer target object writer.
  ///
  /// \param W - Object target writer to test.
  /// \return - True if \p W is a DXContainer target object writer.
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::DXContainer;
  }
};

/// Contains PDB output file name.
static constexpr StringLiteral PdbFileNameSectionName = "PDBNAME";
/// Contains module hash.
static constexpr StringLiteral ModuleHashSectionName = "PDBHASH";

/// A named payload to emit as one part of a DXContainer file.
struct MCDXContainerPart {
  /// Four-character part name as stored in the DXContainer part header.
  StringRef Name;
  /// Bytes to emit as the part body, excluding any generated headers.
  StringRef Data;
};

/// Base writer that emits DXContainer files from a sequence of named parts.
class LLVM_ABI MCDXContainerBaseWriter {
protected:
  /// Return the parts to emit, in file order.
  ///
  /// The default implementation is unimplemented and must be overridden.
  ///
  /// \return - Parts to emit, in file order.
  virtual ArrayRef<MCDXContainerPart> collectParts() {
    llvm_unreachable("Unimplemented");
  }

  /// Return true if a section should be omitted from the container.
  ///
  /// Empty sections, the PDB file-name and module-hash auxiliary sections, and
  /// the ILDB part when slim debug is enabled are skipped.
  ///
  /// \param SectionName - Four-character section or part name.
  /// \param SectionSize - Size of the section payload in bytes.
  /// \return - True if the section should be omitted from the container.
  virtual bool shouldSkipSection(StringRef SectionName, size_t SectionSize);

public:
  /// Construct a DXContainer base writer.
  MCDXContainerBaseWriter() {}
  /// Destroy the DXContainer base writer.
  virtual ~MCDXContainerBaseWriter();

  /// Write a DXContainer file built from \c collectParts() to \p OS.
  ///
  /// \param OS - Stream to write the container to.
  /// \param TT - Target triple used for DXIL program headers.
  void write(raw_ostream &OS, const Triple &TT);
};

/// DXContainer object writer that emits DXContainer files from an MCAssembler.
class LLVM_ABI DXContainerObjectWriter final : public MCDXContainerBaseWriter,
                                               public MCObjectWriter {
  support::endian::Writer W;
  std::unique_ptr<MCDXContainerTargetWriter> TargetObjectWriter;
  SmallVector<MCDXContainerPart> Parts;
  SmallVector<SmallString<0>> SectionBuffers;

  void clearParts();

protected:
  /// Return assembler sections as DXContainer parts, omitting skipped sections.
  ///
  /// \return - Parts to emit, in file order.
  ArrayRef<MCDXContainerPart> collectParts() override;
  /// Return true if a section should be omitted from the object.
  ///
  /// Skips ILDB unless debug is being embedded, skips SRCI, then applies the
  /// base writer's skip rules.
  ///
  /// \param SectionName - Four-character section or part name.
  /// \param SectionSize - Size of the section payload in bytes.
  /// \return - True if the section should be omitted from the object.
  bool shouldSkipSection(StringRef SectionName, size_t SectionSize) override;

public:
  /// Construct a DXContainer object writer.
  ///
  /// \param MOTW - Target-specific DXContainer writer.
  /// \param OS - Stream to write the object to.
  DXContainerObjectWriter(std::unique_ptr<MCDXContainerTargetWriter> MOTW,
                          raw_pwrite_stream &OS)
      : W(OS, llvm::endianness::little), TargetObjectWriter(std::move(MOTW)) {}

  /// Write the DXContainer object file and return the number of bytes written.
  ///
  /// \return - Number of bytes written to the output stream.
  uint64_t writeObject() override;
};

} // end namespace llvm

#endif // LLVM_MC_MCDXCONTAINERWRITER_H
