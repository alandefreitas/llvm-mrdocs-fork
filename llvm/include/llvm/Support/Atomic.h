//===- llvm/Support/Atomic.h - Atomic Operations -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the llvm::sys atomic operations.
//
// DO NOT USE IN NEW CODE!
//
// New code should always rely on the std::atomic facilities in C++11.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ATOMIC_H
#define LLVM_SUPPORT_ATOMIC_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"

// Windows will at times define MemoryFence.
#ifdef MemoryFence
#undef MemoryFence
#endif

namespace llvm {
  namespace sys {
  /// Issues a full memory fence that orders preceding and following accesses.
  LLVM_ABI void MemoryFence();

#ifdef _MSC_VER
  /// Integer type used as the value type for legacy compare-and-swap.
  using cas_flag = long;
#else
  /// Integer type used as the value type for legacy compare-and-swap.
  using cas_flag = uint32_t;
#endif
  /// Atomically compares the value at \p ptr with \p old_value and stores
  /// \p new_value if they are equal.
  ///
  /// \param ptr Location of the value to compare and possibly update.
  /// \param new_value Value written to \p ptr when the comparison succeeds.
  /// \param old_value Expected current value at \p ptr.
  /// \returns The value previously held at \p ptr.
  LLVM_ABI cas_flag CompareAndSwap(volatile cas_flag *ptr, cas_flag new_value,
                                   cas_flag old_value);
  }
}

#endif
