//===- OMPDeviceConstants.h - OpenMP device related constants ----- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines constans that will be used by both host and device
/// compilation.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_OPENMP_OMPDEVICECONSTANTS_H
#define LLVM_FRONTEND_OPENMP_OMPDEVICECONSTANTS_H

namespace llvm {
namespace omp {

/// Bit flags describing the execution mode of an OpenMP target kernel.
enum OMPTgtExecModeFlags : unsigned char {
  /// Bare kernel with no OpenMP runtime state machine.
  OMP_TGT_EXEC_MODE_BARE = 0,
  /// Generic mode where a single thread runs the sequential regions.
  OMP_TGT_EXEC_MODE_GENERIC = 1 << 0,
  /// SPMD mode where all threads execute the kernel in parallel.
  OMP_TGT_EXEC_MODE_SPMD = 1 << 1,
  /// Combined generic and SPMD execution mode.
  OMP_TGT_EXEC_MODE_GENERIC_SPMD =
      OMP_TGT_EXEC_MODE_GENERIC | OMP_TGT_EXEC_MODE_SPMD,
  /// SPMD mode without a worksharing loop around the kernel body.
  OMP_TGT_EXEC_MODE_SPMD_NO_LOOP = 1 << 2 | OMP_TGT_EXEC_MODE_SPMD
};

} // end namespace omp
} // end namespace llvm

#endif // LLVM_FRONTEND_OPENMP_OMPDEVICECONSTANTS_H
