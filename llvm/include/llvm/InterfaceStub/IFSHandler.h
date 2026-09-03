//===- IFSHandler.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===/
///
/// \file
/// This file declares an interface for reading and writing .ifs (text-based
/// InterFace Stub) files.
///
//===-----------------------------------------------------------------------===/

#ifndef LLVM_INTERFACESTUB_IFSHANDLER_H
#define LLVM_INTERFACESTUB_IFSHANDLER_H

#include "IFSStub.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/VersionTuple.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class raw_ostream;
class Error;
class StringRef;

namespace ifs {

struct IFSStub;

/// Current IFS text-stub format version supported by this library.
const VersionTuple IFSVersionCurrent(3, 0);

/// Attempts to read an IFS interface file from a StringRef buffer.
///
/// @param Buf Buffer containing the IFS text stub to parse.
/// @return The parsed IFS stub, or an error if parsing fails.
LLVM_ABI Expected<std::unique_ptr<IFSStub>> readIFSFromBuffer(StringRef Buf);

/// Attempts to write an IFS interface file to a raw_ostream.
///
/// @param OS Output stream to write the IFS text stub to.
/// @param Stub IFS stub to serialize.
/// @return Success, or an error if serialization fails.
LLVM_ABI Error writeIFSToOutputStream(raw_ostream &OS, const IFSStub &Stub);

/// Override the target platform inforation in the text stub.
///
/// @param Stub IFS stub whose target fields will be overridden.
/// @param OverrideArch Optional architecture override; conflicts error.
/// @param OverrideEndianness Optional endianness override; conflicts error.
/// @param OverrideBitWidth Optional bit-width override; conflicts error.
/// @param OverrideTriple Optional triple override; conflicts error.
/// @return Success, or an error if an override conflicts with existing values.
LLVM_ABI Error
overrideIFSTarget(IFSStub &Stub, std::optional<IFSArch> OverrideArch,
                  std::optional<IFSEndiannessType> OverrideEndianness,
                  std::optional<IFSBitWidthType> OverrideBitWidth,
                  std::optional<std::string> OverrideTriple);

/// Validate the target platform inforation in the text stub.
///
/// @param Stub IFS stub whose target fields will be validated.
/// @param ParseTriple If true, expand a triple into arch/endian/bit-width.
/// @return Success, or an error if the target fields are inconsistent.
LLVM_ABI Error validateIFSTarget(IFSStub &Stub, bool ParseTriple);

/// Strips target platform information from the text stub.
///
/// @param Stub IFS stub whose target fields will be cleared.
/// @param StripTriple If true, clear the triple and derived target fields.
/// @param StripArch If true, clear architecture fields.
/// @param StripEndianness If true, clear endianness.
/// @param StripBitWidth If true, clear bit-width.
LLVM_ABI void stripIFSTarget(IFSStub &Stub, bool StripTriple, bool StripArch,
                             bool StripEndianness, bool StripBitWidth);

/// Filters symbols from an IFS stub by undefined status and exclude globs.
///
/// @param Stub IFS stub whose symbol list will be filtered in place.
/// @param StripUndefined If true, remove undefined symbols.
/// @param Exclude Glob patterns of symbol names to exclude.
/// @return Success, or an error if filtering fails.
LLVM_ABI Error filterIFSSyms(IFSStub &Stub, bool StripUndefined,
                             const std::vector<std::string> &Exclude = {});

/// Parse llvm triple string into a IFSTarget struct.
///
/// @param TripleStr LLVM triple string to parse into IFS target fields.
/// @return The IFS target fields derived from the triple string.
LLVM_ABI IFSTarget parseTriple(StringRef TripleStr);

} // end namespace ifs
} // end namespace llvm

#endif // LLVM_INTERFACESTUB_IFSHANDLER_H
