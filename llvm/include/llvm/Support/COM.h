//===- llvm/Support/COM.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Provides a library for accessing COM functionality of the Host OS.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_COM_H
#define LLVM_SUPPORT_COM_H

#include "llvm/Support/Compiler.h"

namespace llvm {
namespace sys {

/// Threading model used when initializing COM on Windows.
enum class COMThreadingMode {
  /// Apartment-threaded (COINIT_APARTMENTTHREADED) COM.
  SingleThreaded,
  /// Multi-threaded (COINIT_MULTITHREADED) COM.
  MultiThreaded
};

/// RAII helper that initializes COM for the current thread.
///
/// On Windows, construction calls \c CoInitializeEx and destruction calls
/// \c CoUninitialize. On other platforms this is a no-op.
class InitializeCOMRAII {
public:
  /// Initialize COM for the current thread with the given threading mode.
  ///
  /// \param Threading COM threading model to request.
  /// \param SpeedOverMemory If true, prefer speed over memory when initializing
  ///        COM (\c COINIT_SPEED_OVER_MEMORY on Windows).
  LLVM_ABI explicit InitializeCOMRAII(COMThreadingMode Threading,
                                      bool SpeedOverMemory = false);
  /// Uninitialize COM for the current thread.
  LLVM_ABI ~InitializeCOMRAII();

private:
  InitializeCOMRAII(const InitializeCOMRAII &) = delete;
  void operator=(const InitializeCOMRAII &) = delete;
};
}
}

#endif
