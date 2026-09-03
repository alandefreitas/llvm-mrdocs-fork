//===- IPDBFrameData.h - base interface for frame data ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBFRAMEDATA_H
#define LLVM_DEBUGINFO_PDB_IPDBFRAMEDATA_H

#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <string>

namespace llvm {
namespace pdb {

/// IPDBFrameData defines an interface used to represent a frame data of some
/// code block.
class LLVM_ABI IPDBFrameData {
public:
  /// Destroy the frame data.
  virtual ~IPDBFrameData();

  /// Return the offset portion of the frame's code location.
  /// @return The section-relative address offset of the frame's code.
  virtual uint32_t getAddressOffset() const = 0;
  /// Return the section portion of the frame's code location.
  /// @return The section index of the frame's code location.
  virtual uint32_t getAddressSection() const = 0;
  /// Return the length in bytes of the code block described by this frame.
  /// @return The length in bytes of the code block.
  virtual uint32_t getLengthBlock() const = 0;
  /// Return the FPO program string that describes how to unwind this frame.
  /// @return The FPO program string for unwinding this frame.
  virtual std::string getProgram() const = 0;
  /// Return the relative virtual address of the frame's code location.
  /// @return The relative virtual address of the frame's code.
  virtual uint32_t getRelativeVirtualAddress() const = 0;
  /// Return the virtual address of the frame's code location.
  /// @return The virtual address of the frame's code.
  virtual uint64_t getVirtualAddress() const = 0;
};

} // namespace pdb
} // namespace llvm

#endif
