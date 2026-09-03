//===--- Duration.h - wrapper around std::chrono::Duration ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  The sole purpose of this file is to avoid the dependency on <chrono> in
//  raw_ostream.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DURATION_H
#define LLVM_SUPPORT_DURATION_H

#include <chrono>

namespace llvm {
class Duration {
  std::chrono::milliseconds Value;
  public:
  /// Construct a Duration from a milliseconds value.
  ///
  /// \param Value Duration in milliseconds.
  Duration(std::chrono::milliseconds Value) : Value(Value) {}
  /// Return the stored duration in milliseconds.
  ///
  /// \return The duration in milliseconds.
  std::chrono::milliseconds getDuration() const { return Value; }
};
}

#endif
