//===--- CodeGenOptions.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines frontend codegen options common to clang and flang
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_DRIVER_CODEGENOPTIONS_H
#define LLVM_FRONTEND_DRIVER_CODEGENOPTIONS_H

#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {
class Triple;
class TargetLibraryInfoImpl;
enum class VectorLibrary;
} // namespace llvm

namespace llvm {
/// Shared frontend driver code-generation options used by Clang and Flang.
namespace driver {
// The current supported vector libraries in enum \VectorLibrary are 9(including
// the NoLibrary). Changing the bitcount from 3 to 4 so that more than 8 values
// can be supported. Now the maximum number of vector libraries supported
// increase from 8(2^3) to 16(2^4).
//
// ENUM_CODEGENOPT(VecLib, llvm::driver::VectorLibrary,
// <bitcount>4</bitcount>, llvm::driver::VectorLibrary::NoLibrary) is the
// currently defined in clang/include/clang/Basic/CodeGenOptions.def
// bitcount is the number of bits used to represent the enum value.
//
// IMPORTANT NOTE: When adding a new vector library support, and if count of
// supported vector libraries crosses the current max limit. Please increment
// the bitcount value.

/// Vector library option used with -fveclib=
enum class VectorLibrary {
  /// Don't use any vector library.
  NoLibrary,
  /// Use the Accelerate framework.
  Accelerate,
  /// Use the GLIBC vector math library.
  LIBMVEC,
  /// Use the IBM MASS vector library.
  MASSV,
  /// Use the Intel short vector math library.
  SVML,
  /// Use SLEEF, the SIMD Library for Evaluating Elementary Functions.
  SLEEF,
  /// Use Darwin's libsystem_m vector functions.
  Darwin_libsystem_m,
  /// Use Arm Performance Libraries.
  ArmPL,
  /// Use the AMD vector math library.
  AMDLIBM
};

/// Map a driver VectorLibrary value to the IR-level llvm::VectorLibrary.
///
/// \param VecLib Vector library selected by the frontend \c -fveclib= option.
/// \return The corresponding enumerator used by TargetLibraryInfo.
LLVM_ABI llvm::VectorLibrary
convertDriverVectorLibraryToVectorLibrary(llvm::driver::VectorLibrary VecLib);

/// Create a TargetLibraryInfoImpl for a target triple and vector library.
///
/// \param TargetTriple Triple that selects which library functions are
///                     available.
/// \param Veclib Driver vector-library option whose mappings are installed.
/// \return A new TargetLibraryInfoImpl; the caller takes ownership.
LLVM_ABI TargetLibraryInfoImpl *createTLII(const llvm::Triple &TargetTriple,
                                           VectorLibrary Veclib);

/// Kind of profile-guided optimization instrumentation to emit.
enum ProfileInstrKind {
  /// Profile instrumentation is turned off.
  ProfileNone,
  /// Clang instrumentation that generates execution counts for PGO.
  ProfileClangInstr,
  /// IR-level PGO instrumentation in LLVM.
  ProfileIRInstr,
  /// IR-level context-sensitive PGO instrumentation in LLVM.
  ProfileCSIRInstr,
  /// IR-level sample-PGO cold-function coverage instrumentation in LLVM.
  ProfileIRSampleColdCov,
};

/// Return the default filename used when generating a profile.
///
/// When profile correlation is enabled the name is \c default_%m.proflite;
/// otherwise it is \c default_%m.profraw.
/// \return The default profile filename string.
LLVM_ABI std::string getDefaultProfileGenName();
} // end namespace driver
} // end namespace llvm

#endif
