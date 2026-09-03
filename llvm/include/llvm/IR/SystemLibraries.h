//===------------------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_SYSTEMLIBRARIES_H
#define LLVM_IR_SYSTEMLIBRARIES_H

namespace llvm {
/// List of known vector-functions libraries.
///
/// The vector-functions library defines, which functions are vectorizable
/// and with which factor. The library can be specified by either frontend,
/// or a commandline option, and then used by
/// addVectorizableFunctionsFromVecLib for filling up the tables of
/// vectorizable functions.
enum class VectorLibrary {
  NoLibrary,        ///< Don't use any vector library.
  Accelerate,       ///< Use Apple's Accelerate framework for vector math.
  DarwinLibSystemM, ///< Use Darwin's libsystem_m.
  LIBMVEC,          ///< Use GLIBC Vector Math library.
  MASSV,            ///< Use IBM MASS vector library.
  SVML,             ///< Use Intel short vector math library.
  SLEEFGNUABI,      ///< Use SLEEF with the GNU ABI for vector math.
  ArmPL,            ///< Use Arm Performance Libraries.
  AMDLIBM           ///< Use AMD Math Vector library.
};

} // namespace llvm

#endif // LLVM_IR_SYSTEMLIBRARIES_H
