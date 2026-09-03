///===- llvm/Frontend/Offloading/PropertySet.h ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
///===---------------------------------------------------------------------===//
/// \file This file defines PropertySetRegistry and PropertyValue types and
/// provides helper functions to translate PropertySetRegistry from/to JSON.
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

#include <map>
#include <variant>

namespace llvm {
class raw_ostream;
class MemoryBufferRef;

namespace offloading {

/// Contiguous sequence of bytes used as a property value payload.
using ByteArray = SmallVector<unsigned char, 0>;
/// Single property value, either a 32-bit integer or a byte array.
using PropertyValue = std::variant<uint32_t, ByteArray>;
/// Named map of property values within one property set.
using PropertySet = std::map<std::string, PropertyValue>;
/// Named map of property sets forming a property set registry.
using PropertySetRegistry = std::map<std::string, PropertySet>;

/// Serializes a property set registry to JSON on the given stream.
/// \param P The property set registry to write.
/// \param O The output stream that receives the JSON.
LLVM_ABI void writePropertiesToJSON(const PropertySetRegistry &P,
                                    raw_ostream &O);
/// Parses a property set registry from a JSON buffer.
/// \param Buf The memory buffer containing JSON to parse.
/// \return The parsed registry, or an error if parsing fails.
LLVM_ABI Expected<PropertySetRegistry>
readPropertiesFromJSON(MemoryBufferRef Buf);

} // namespace offloading
} // namespace llvm
