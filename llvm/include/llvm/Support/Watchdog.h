//===--- Watchdog.h - Watchdog timer ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file declares the llvm::sys::Watchdog class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_WATCHDOG_H
#define LLVM_SUPPORT_WATCHDOG_H

#include "llvm/Support/Compiler.h"

namespace llvm {
  namespace sys {

    /// Abstraction for a timeout around an operation that must complete in time.
    ///
    /// Failure to complete before the timeout is an unrecoverable situation and
    /// no mechanisms to attempt to handle it are provided.
    class Watchdog {
    public:
      /// Start a watchdog that expires after \p seconds.
      ///
      /// \param seconds Timeout in seconds before the watchdog fires.
      LLVM_ABI Watchdog(unsigned int seconds);
      /// Cancel the watchdog if the timeout has not yet fired.
      LLVM_ABI ~Watchdog();

    private:
      // Noncopyable.
      Watchdog(const Watchdog &other) = delete;
      Watchdog &operator=(const Watchdog &other) = delete;
    };
  }
}

#endif
