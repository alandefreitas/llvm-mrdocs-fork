//===- ELFYAML.h - ELF YAMLIO implementation --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation
/// of ELF.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_ELFYAML_H
#define LLVM_OBJECTYAML_ELFYAML_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/ObjectYAML/BBAddrMapYAML.h"
#include "llvm/ObjectYAML/DWARFYAML.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace llvm {
/// YAML representations of ELF object files.
namespace ELFYAML {

/// Drop a unique suffix previously appended to an ELF symbol or section name.
/// \param S Name that may contain a unique suffix.
/// \return The name with any unique suffix removed.
LLVM_ABI StringRef dropUniqueSuffix(StringRef S);
/// Append a unique suffix derived from \p Msg to \p Name.
/// \param Name Base symbol or section name.
/// \param Msg Message used to build the unique suffix.
/// \return A new name that includes the unique suffix.
LLVM_ABI std::string appendUniqueSuffix(StringRef Name, const Twine &Msg);

// These types are invariant across 32/64-bit ELF, so for simplicity just
// directly give them their exact sizes. We don't need to worry about
// endianness because these are just the types in the YAMLIO structures,
// and are appropriately converted to the necessary endianness when
// reading/generating binary object files.
// The naming of these types is intended to be ELF_PREFIX, where PREFIX is
// the common prefix of the respective constants. E.g. ELF_EM corresponds
// to the `e_machine` constants, like `EM_X86_64`.
// In the future, these would probably be better suited by C++11 enum
// class's with appropriate fixed underlying type.
/// Strong typedef for an ELF object file type (e_type).
struct ELF_ET {
  /// Construct a zero-initialized ELF_ET value.
  ELF_ET() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_ET(const uint16_t v) : value(v) {}
  /// Copy-construct from another \c ELF_ET.
  /// \param v Value to copy.
  ELF_ET(const ELF_ET &v) = default;
  /// Copy-assign from another \c ELF_ET.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_ET &operator=(const ELF_ET &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_ET &operator=(const uint16_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint16_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_ET &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint16_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_ET &rhs) const { return value < rhs.value; }
  /// Stored ELF object file type value.
  uint16_t value;
  /// Underlying integral base type.
  using BaseType = uint16_t;
};

/// Strong typedef for an ELF program header type (p_type).
struct ELF_PT {
  /// Construct a zero-initialized ELF_PT value.
  ELF_PT() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_PT(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c ELF_PT.
  /// \param v Value to copy.
  ELF_PT(const ELF_PT &v) = default;
  /// Copy-assign from another \c ELF_PT.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_PT &operator=(const ELF_PT &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_PT &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_PT &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_PT &rhs) const { return value < rhs.value; }
  /// Stored ELF program header type value.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for an ELF machine architecture (e_machine).
struct ELF_EM {
  /// Construct a zero-initialized ELF_EM value.
  ELF_EM() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_EM(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c ELF_EM.
  /// \param v Value to copy.
  ELF_EM(const ELF_EM &v) = default;
  /// Copy-assign from another \c ELF_EM.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_EM &operator=(const ELF_EM &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_EM &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_EM &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_EM &rhs) const { return value < rhs.value; }
  /// Stored ELF machine architecture value.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for an ELF file class (EI_CLASS).
struct ELF_ELFCLASS {
  /// Construct a zero-initialized ELF_ELFCLASS value.
  ELF_ELFCLASS() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_ELFCLASS(const uint8_t v) : value(v) {}
  /// Copy-construct from another \c ELF_ELFCLASS.
  /// \param v Value to copy.
  ELF_ELFCLASS(const ELF_ELFCLASS &v) = default;
  /// Copy-assign from another \c ELF_ELFCLASS.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_ELFCLASS &operator=(const ELF_ELFCLASS &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_ELFCLASS &operator=(const uint8_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint8_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_ELFCLASS &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint8_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_ELFCLASS &rhs) const { return value < rhs.value; }
  /// Stored ELF file class value.
  uint8_t value;
  /// Underlying integral base type.
  using BaseType = uint8_t;
};

/// Strong typedef for ELF data encoding / endianness (EI_DATA).
struct ELF_ELFDATA {
  /// Construct a zero-initialized ELF_ELFDATA value.
  ELF_ELFDATA() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_ELFDATA(const uint8_t v) : value(v) {}
  /// Copy-construct from another \c ELF_ELFDATA.
  /// \param v Value to copy.
  ELF_ELFDATA(const ELF_ELFDATA &v) = default;
  /// Copy-assign from another \c ELF_ELFDATA.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_ELFDATA &operator=(const ELF_ELFDATA &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_ELFDATA &operator=(const uint8_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint8_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_ELFDATA &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint8_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_ELFDATA &rhs) const { return value < rhs.value; }
  /// Stored ELF data encoding value.
  uint8_t value;
  /// Underlying integral base type.
  using BaseType = uint8_t;
};

/// Strong typedef for an ELF OS/ABI identification (EI_OSABI).
struct ELF_ELFOSABI {
  /// Construct a zero-initialized ELF_ELFOSABI value.
  ELF_ELFOSABI() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_ELFOSABI(const uint8_t v) : value(v) {}
  /// Copy-construct from another \c ELF_ELFOSABI.
  /// \param v Value to copy.
  ELF_ELFOSABI(const ELF_ELFOSABI &v) = default;
  /// Copy-assign from another \c ELF_ELFOSABI.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_ELFOSABI &operator=(const ELF_ELFOSABI &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_ELFOSABI &operator=(const uint8_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint8_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_ELFOSABI &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint8_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_ELFOSABI &rhs) const { return value < rhs.value; }
  /// Stored ELF OS/ABI value.
  uint8_t value;
  /// Underlying integral base type.
  using BaseType = uint8_t;
};

// Just use 64, since it can hold 32-bit values too.
/// Strong typedef for ELF file header processor-specific flags (e_flags).
struct ELF_EF {
  /// Construct a zero-initialized ELF_EF value.
  ELF_EF() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_EF(const uint64_t v) : value(v) {}
  /// Copy-construct from another \c ELF_EF.
  /// \param v Value to copy.
  ELF_EF(const ELF_EF &v) = default;
  /// Copy-assign from another \c ELF_EF.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_EF &operator=(const ELF_EF &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_EF &operator=(const uint64_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint64_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_EF &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint64_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_EF &rhs) const { return value < rhs.value; }
  /// Stored ELF file header flags value.
  uint64_t value;
  /// Underlying integral base type.
  using BaseType = uint64_t;
};

// Just use 64, since it can hold 32-bit values too.
/// Strong typedef for an ELF dynamic table tag (d_tag).
struct ELF_DYNTAG {
  /// Construct a zero-initialized ELF_DYNTAG value.
  ELF_DYNTAG() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_DYNTAG(const uint64_t v) : value(v) {}
  /// Copy-construct from another \c ELF_DYNTAG.
  /// \param v Value to copy.
  ELF_DYNTAG(const ELF_DYNTAG &v) = default;
  /// Copy-assign from another \c ELF_DYNTAG.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_DYNTAG &operator=(const ELF_DYNTAG &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_DYNTAG &operator=(const uint64_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint64_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_DYNTAG &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint64_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_DYNTAG &rhs) const { return value < rhs.value; }
  /// Stored ELF dynamic table tag value.
  uint64_t value;
  /// Underlying integral base type.
  using BaseType = uint64_t;
};

/// Strong typedef for ELF program header flags (p_flags).
struct ELF_PF {
  /// Construct a zero-initialized ELF_PF value.
  ELF_PF() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_PF(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c ELF_PF.
  /// \param v Value to copy.
  ELF_PF(const ELF_PF &v) = default;
  /// Copy-assign from another \c ELF_PF.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_PF &operator=(const ELF_PF &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_PF &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_PF &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_PF &rhs) const { return value < rhs.value; }
  /// Stored ELF program header flags value.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for an ELF section type (sh_type).
struct ELF_SHT {
  /// Construct a zero-initialized ELF_SHT value.
  ELF_SHT() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_SHT(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c ELF_SHT.
  /// \param v Value to copy.
  ELF_SHT(const ELF_SHT &v) = default;
  /// Copy-assign from another \c ELF_SHT.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_SHT &operator=(const ELF_SHT &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_SHT &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_SHT &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_SHT &rhs) const { return value < rhs.value; }
  /// Stored ELF section type value.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for an ELF relocation type.
struct ELF_REL {
  /// Construct a zero-initialized ELF_REL value.
  ELF_REL() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_REL(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c ELF_REL.
  /// \param v Value to copy.
  ELF_REL(const ELF_REL &v) = default;
  /// Copy-assign from another \c ELF_REL.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_REL &operator=(const ELF_REL &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_REL &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_REL &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_REL &rhs) const { return value < rhs.value; }
  /// Stored ELF relocation type value.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for an ELF reserved section index value.
struct ELF_RSS {
  /// Construct a zero-initialized ELF_RSS value.
  ELF_RSS() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_RSS(const uint8_t v) : value(v) {}
  /// Copy-construct from another \c ELF_RSS.
  /// \param v Value to copy.
  ELF_RSS(const ELF_RSS &v) = default;
  /// Copy-assign from another \c ELF_RSS.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_RSS &operator=(const ELF_RSS &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_RSS &operator=(const uint8_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint8_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_RSS &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint8_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_RSS &rhs) const { return value < rhs.value; }
  /// Stored ELF reserved section index value.
  uint8_t value;
  /// Underlying integral base type.
  using BaseType = uint8_t;
};

// Just use 64, since it can hold 32-bit values too.
/// Strong typedef for ELF section header flags (sh_flags).
struct ELF_SHF {
  /// Construct a zero-initialized ELF_SHF value.
  ELF_SHF() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_SHF(const uint64_t v) : value(v) {}
  /// Copy-construct from another \c ELF_SHF.
  /// \param v Value to copy.
  ELF_SHF(const ELF_SHF &v) = default;
  /// Copy-assign from another \c ELF_SHF.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_SHF &operator=(const ELF_SHF &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_SHF &operator=(const uint64_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint64_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_SHF &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint64_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_SHF &rhs) const { return value < rhs.value; }
  /// Stored ELF section header flags value.
  uint64_t value;
  /// Underlying integral base type.
  using BaseType = uint64_t;
};

/// Strong typedef for an ELF section header index.
struct ELF_SHN {
  /// Construct a zero-initialized ELF_SHN value.
  ELF_SHN() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_SHN(const uint16_t v) : value(v) {}
  /// Copy-construct from another \c ELF_SHN.
  /// \param v Value to copy.
  ELF_SHN(const ELF_SHN &v) = default;
  /// Copy-assign from another \c ELF_SHN.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_SHN &operator=(const ELF_SHN &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_SHN &operator=(const uint16_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint16_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_SHN &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint16_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_SHN &rhs) const { return value < rhs.value; }
  /// Stored ELF section header index value.
  uint16_t value;
  /// Underlying integral base type.
  using BaseType = uint16_t;
};

/// Strong typedef for an ELF symbol binding (STB_*).
struct ELF_STB {
  /// Construct a zero-initialized ELF_STB value.
  ELF_STB() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_STB(const uint8_t v) : value(v) {}
  /// Copy-construct from another \c ELF_STB.
  /// \param v Value to copy.
  ELF_STB(const ELF_STB &v) = default;
  /// Copy-assign from another \c ELF_STB.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_STB &operator=(const ELF_STB &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_STB &operator=(const uint8_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint8_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_STB &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint8_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_STB &rhs) const { return value < rhs.value; }
  /// Stored ELF symbol binding value.
  uint8_t value;
  /// Underlying integral base type.
  using BaseType = uint8_t;
};

/// Strong typedef for an ELF symbol type (STT_*).
struct ELF_STT {
  /// Construct a zero-initialized ELF_STT value.
  ELF_STT() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_STT(const uint8_t v) : value(v) {}
  /// Copy-construct from another \c ELF_STT.
  /// \param v Value to copy.
  ELF_STT(const ELF_STT &v) = default;
  /// Copy-assign from another \c ELF_STT.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_STT &operator=(const ELF_STT &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_STT &operator=(const uint8_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint8_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_STT &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint8_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_STT &rhs) const { return value < rhs.value; }
  /// Stored ELF symbol type value.
  uint8_t value;
  /// Underlying integral base type.
  using BaseType = uint8_t;
};

/// Strong typedef for an ELF note type (n_type).
struct ELF_NT {
  /// Construct a zero-initialized ELF_NT value.
  ELF_NT() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  ELF_NT(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c ELF_NT.
  /// \param v Value to copy.
  ELF_NT(const ELF_NT &v) = default;
  /// Copy-assign from another \c ELF_NT.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  ELF_NT &operator=(const ELF_NT &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  ELF_NT &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const ELF_NT &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const ELF_NT &rhs) const { return value < rhs.value; }
  /// Stored ELF note type value.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};


/// Strong typedef for a MIPS AFL register-size field.
struct MIPS_AFL_REG {
  /// Construct a zero-initialized MIPS_AFL_REG value.
  MIPS_AFL_REG() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  MIPS_AFL_REG(const uint8_t v) : value(v) {}
  /// Copy-construct from another \c MIPS_AFL_REG.
  /// \param v Value to copy.
  MIPS_AFL_REG(const MIPS_AFL_REG &v) = default;
  /// Copy-assign from another \c MIPS_AFL_REG.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  MIPS_AFL_REG &operator=(const MIPS_AFL_REG &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  MIPS_AFL_REG &operator=(const uint8_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint8_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const MIPS_AFL_REG &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint8_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const MIPS_AFL_REG &rhs) const { return value < rhs.value; }
  /// Stored MIPS AFL register-size value.
  uint8_t value;
  /// Underlying integral base type.
  using BaseType = uint8_t;
};

/// Strong typedef for a MIPS floating-point ABI value.
struct MIPS_ABI_FP {
  /// Construct a zero-initialized MIPS_ABI_FP value.
  MIPS_ABI_FP() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  MIPS_ABI_FP(const uint8_t v) : value(v) {}
  /// Copy-construct from another \c MIPS_ABI_FP.
  /// \param v Value to copy.
  MIPS_ABI_FP(const MIPS_ABI_FP &v) = default;
  /// Copy-assign from another \c MIPS_ABI_FP.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  MIPS_ABI_FP &operator=(const MIPS_ABI_FP &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  MIPS_ABI_FP &operator=(const uint8_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint8_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const MIPS_ABI_FP &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint8_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const MIPS_ABI_FP &rhs) const { return value < rhs.value; }
  /// Stored MIPS floating-point ABI value.
  uint8_t value;
  /// Underlying integral base type.
  using BaseType = uint8_t;
};

/// Strong typedef for a MIPS AFL ISA extension value.
struct MIPS_AFL_EXT {
  /// Construct a zero-initialized MIPS_AFL_EXT value.
  MIPS_AFL_EXT() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  MIPS_AFL_EXT(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c MIPS_AFL_EXT.
  /// \param v Value to copy.
  MIPS_AFL_EXT(const MIPS_AFL_EXT &v) = default;
  /// Copy-assign from another \c MIPS_AFL_EXT.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  MIPS_AFL_EXT &operator=(const MIPS_AFL_EXT &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  MIPS_AFL_EXT &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const MIPS_AFL_EXT &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const MIPS_AFL_EXT &rhs) const { return value < rhs.value; }
  /// Stored MIPS AFL ISA extension value.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for MIPS AFL ASE flags.
struct MIPS_AFL_ASE {
  /// Construct a zero-initialized MIPS_AFL_ASE value.
  MIPS_AFL_ASE() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  MIPS_AFL_ASE(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c MIPS_AFL_ASE.
  /// \param v Value to copy.
  MIPS_AFL_ASE(const MIPS_AFL_ASE &v) = default;
  /// Copy-assign from another \c MIPS_AFL_ASE.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  MIPS_AFL_ASE &operator=(const MIPS_AFL_ASE &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  MIPS_AFL_ASE &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const MIPS_AFL_ASE &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const MIPS_AFL_ASE &rhs) const { return value < rhs.value; }
  /// Stored MIPS AFL ASE flags value.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for MIPS AFL flags1 bits.
struct MIPS_AFL_FLAGS1 {
  /// Construct a zero-initialized MIPS_AFL_FLAGS1 value.
  MIPS_AFL_FLAGS1() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  MIPS_AFL_FLAGS1(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c MIPS_AFL_FLAGS1.
  /// \param v Value to copy.
  MIPS_AFL_FLAGS1(const MIPS_AFL_FLAGS1 &v) = default;
  /// Copy-assign from another \c MIPS_AFL_FLAGS1.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  MIPS_AFL_FLAGS1 &operator=(const MIPS_AFL_FLAGS1 &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  MIPS_AFL_FLAGS1 &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const MIPS_AFL_FLAGS1 &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const MIPS_AFL_FLAGS1 &rhs) const { return value < rhs.value; }
  /// Stored MIPS AFL flags1 value.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a MIPS ISA level value.
struct MIPS_ISA {
  /// Construct a zero-initialized MIPS_ISA value.
  MIPS_ISA() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  MIPS_ISA(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c MIPS_ISA.
  /// \param v Value to copy.
  MIPS_ISA(const MIPS_ISA &v) = default;
  /// Copy-assign from another \c MIPS_ISA.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  MIPS_ISA &operator=(const MIPS_ISA &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  MIPS_ISA &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const MIPS_ISA &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const MIPS_ISA &rhs) const { return value < rhs.value; }
  /// Stored MIPS ISA level value.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};


/// Strong typedef for a YAML flow-style string value.
struct YAMLFlowString {
  /// Construct a zero-initialized YAMLFlowString value.
  YAMLFlowString() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  YAMLFlowString(const StringRef v) : value(v) {}
  /// Copy-construct from another \c YAMLFlowString.
  /// \param v Value to copy.
  YAMLFlowString(const YAMLFlowString &v) = default;
  /// Copy-assign from another \c YAMLFlowString.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  YAMLFlowString &operator=(const YAMLFlowString &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  YAMLFlowString &operator=(const StringRef &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const StringRef &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const YAMLFlowString &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const StringRef &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const YAMLFlowString &rhs) const { return value < rhs.value; }
  /// Stored string value.
  StringRef value;
  /// Underlying base type.
  using BaseType = StringRef;
};

/// Strong typedef for a signed integer that may be written as unsigned in YAML.
struct YAMLIntUInt {
  /// Construct a zero-initialized YAMLIntUInt value.
  YAMLIntUInt() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  YAMLIntUInt(const int64_t v) : value(v) {}
  /// Copy-construct from another \c YAMLIntUInt.
  /// \param v Value to copy.
  YAMLIntUInt(const YAMLIntUInt &v) = default;
  /// Copy-assign from another \c YAMLIntUInt.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  YAMLIntUInt &operator=(const YAMLIntUInt &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  YAMLIntUInt &operator=(const int64_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const int64_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const YAMLIntUInt &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const int64_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const YAMLIntUInt &rhs) const { return value < rhs.value; }
  /// Stored integer value.
  int64_t value;
  /// Underlying integral base type.
  using BaseType = int64_t;
};

/// Return the default section entry size for the given machine and section.
/// \param EMachine ELF machine architecture value.
/// \param SecType Section type.
/// \param SecName Section name (used for special cases such as .debug_str).
/// \return Default sh_entsize in bytes, or 0 when there is no default.
template <class ELFT>
unsigned getDefaultShEntSize(unsigned EMachine, ELF_SHT SecType,
                             StringRef SecName) {
  if (EMachine == ELF::EM_MIPS && SecType == ELF::SHT_MIPS_ABIFLAGS)
    return sizeof(object::Elf_Mips_ABIFlags<ELFT>);

  switch (SecType) {
  case ELF::SHT_SYMTAB:
  case ELF::SHT_DYNSYM:
    return sizeof(typename ELFT::Sym);
  case ELF::SHT_GROUP:
    return sizeof(typename ELFT::Word);
  case ELF::SHT_REL:
    return sizeof(typename ELFT::Rel);
  case ELF::SHT_RELA:
    return sizeof(typename ELFT::Rela);
  case ELF::SHT_RELR:
    return sizeof(typename ELFT::Relr);
  case ELF::SHT_DYNAMIC:
    return sizeof(typename ELFT::Dyn);
  case ELF::SHT_HASH:
    return sizeof(typename ELFT::Word);
  case ELF::SHT_SYMTAB_SHNDX:
    return sizeof(typename ELFT::Word);
  case ELF::SHT_GNU_versym:
    return sizeof(typename ELFT::Half);
  case ELF::SHT_LLVM_CALL_GRAPH_PROFILE:
    return sizeof(object::Elf_CGProfile_Impl<ELFT>);
  default:
    if (SecName == ".debug_str")
      return 1;
    return 0;
  }
}

// For now, hardcode 64 bits everywhere that 32 or 64 would be needed
// since 64-bit can hold 32-bit values too.
/// YAML representation of an ELF file header.
struct FileHeader {
  /// ELF file class (32-bit or 64-bit).
  ELF_ELFCLASS Class;
  /// ELF data encoding (endianness).
  ELF_ELFDATA Data;
  /// OS/ABI identification.
  ELF_ELFOSABI OSABI;
  /// ABI version byte from e_ident.
  llvm::yaml::Hex8 ABIVersion;
  /// Object file type (ET_REL, ET_EXEC, ...).
  ELF_ET Type;
  /// Optional machine architecture; defaults when omitted.
  std::optional<ELF_EM> Machine;
  /// Optional processor-specific file header flags.
  std::optional<ELF_EF> Flags;
  /// Entry point virtual address.
  llvm::yaml::Hex64 Entry;
  /// Optional name of the section header string table.
  std::optional<StringRef> SectionHeaderStringTable;

  /// Optional override for e_phoff.
  std::optional<llvm::yaml::Hex64> EPhOff;
  /// Optional override for e_phentsize.
  std::optional<llvm::yaml::Hex16> EPhEntSize;
  /// Optional override for e_phnum.
  std::optional<llvm::yaml::Hex16> EPhNum;
  /// Optional override for e_shentsize.
  std::optional<llvm::yaml::Hex16> EShEntSize;
  /// Optional override for e_shoff.
  std::optional<llvm::yaml::Hex64> EShOff;
  /// Optional override for e_shnum.
  std::optional<llvm::yaml::Hex16> EShNum;
  /// Optional override for e_shstrndx.
  std::optional<llvm::yaml::Hex16> EShStrNdx;
};

/// YAML representation of a section header table entry name.
struct SectionHeader {
  /// Section name as it appears in the section header string table.
  StringRef Name;
};

/// YAML representation of an ELF symbol table entry.
struct Symbol {
  /// Symbol name.
  StringRef Name;
  /// Symbol type (STT_*).
  ELF_STT Type;
  /// Optional section name this symbol is defined relative to.
  std::optional<StringRef> Section;
  /// Optional section header index, used instead of or with \c Section.
  std::optional<ELF_SHN> Index;
  /// Symbol binding (STB_*).
  ELF_STB Binding;
  /// Optional symbol value.
  std::optional<llvm::yaml::Hex64> Value;
  /// Optional symbol size.
  std::optional<llvm::yaml::Hex64> Size;
  /// Optional st_other visibility / other bits.
  std::optional<uint8_t> Other;

  /// Optional override for the st_name string-table offset.
  std::optional<uint32_t> StName;
};

/// YAML value that names either a section or a special section type token.
struct SectionOrType {
  /// Section name or ELF section-type string.
  StringRef sectionNameOrType;
};

/// YAML representation of an entry in an ELF dynamic section.
struct DynamicEntry {
  /// Dynamic tag (DT_*).
  ELF_DYNTAG Tag;
  /// Tag value or address.
  llvm::yaml::Hex64 Val;
};

/// YAML representation of a .stack_sizes section entry.
struct StackSizeEntry {
  /// Function address associated with this stack size.
  llvm::yaml::Hex64 Address;
  /// Stack size in bytes.
  llvm::yaml::Hex64 Size;
};

/// YAML representation of an ELF note.
struct NoteEntry {
  /// Note name string.
  StringRef Name;
  /// Note descriptor bytes.
  yaml::BinaryRef Desc;
  /// Note type.
  ELF_NT Type;
};

/// Base type for a chunk of content in an ELF YAML object.
///
/// Chunks are either sections or special non-section blobs such as fills and
/// the section header table description.
struct LLVM_ABI Chunk {
  /// Discriminator for the concrete chunk kind.
  enum class ChunkKind {
    /// SHT_DYNAMIC section.
    Dynamic,
    /// SHT_GROUP section.
    Group,
    /// Generic raw-content section.
    RawContent,
    /// SHT_REL / SHT_RELA relocation section.
    Relocation,
    /// SHT_RELR relocation section.
    Relr,
    /// SHT_NOBITS section.
    NoBits,
    /// SHT_NOTE section.
    Note,
    /// SHT_HASH section.
    Hash,
    /// SHT_GNU_HASH section.
    GnuHash,
    /// SHT_GNU_verdef section.
    Verdef,
    /// SHT_GNU_verneed section.
    Verneed,
    /// .stack_sizes section.
    StackSizes,
    /// SHT_SYMTAB_SHNDX section.
    SymtabShndxSection,
    /// SHT_GNU_versym section.
    Symver,
    /// ARM index table section.
    ARMIndexTable,
    /// SHT_MIPS_ABIFLAGS section.
    MipsABIFlags,
    /// SHT_LLVM_ADDRSIG section.
    Addrsig,
    /// SHT_LLVM_LINKER_OPTIONS section.
    LinkerOptions,
    /// SHT_LLVM_DEPENDENT_LIBRARIES section.
    DependentLibraries,
    /// SHT_LLVM_CALL_GRAPH_PROFILE section.
    CallGraphProfile,
    /// SHT_LLVM_BB_ADDR_MAP section.
    BBAddrMap,

    // Special chunks.
    /// First enumerator value for non-section special chunks.
    SpecialChunksStart,
    /// Fill pattern placed outside of any section.
    Fill = SpecialChunksStart,
    /// Explicit section header table description.
    SectionHeaderTable,
  };

  /// Kind of this chunk.
  ChunkKind Kind;
  /// Chunk or section name.
  StringRef Name;
  /// Optional file offset override for this chunk.
  std::optional<llvm::yaml::Hex64> Offset;

  /// Whether this chunk was created implicitly rather than loaded from YAML.
  ///
  /// Usually chunks are not created implicitly, but rather loaded from YAML.
  /// This flag is used to signal whether this is the case or not.
  bool IsImplicit;

  /// Construct a chunk of kind \p K.
  /// \param K Chunk kind discriminator.
  /// \param Implicit Whether the chunk is implicit.
  Chunk(ChunkKind K, bool Implicit) : Kind(K), IsImplicit(Implicit) {}
  /// Destroy a chunk.
  virtual ~Chunk();
};

/// YAML representation of a generic ELF section.
struct Section : public Chunk {
  /// Section type (SHT_*).
  ELF_SHT Type;
  /// Optional section flags (SHF_*).
  std::optional<ELF_SHF> Flags;
  /// Optional section virtual address.
  std::optional<llvm::yaml::Hex64> Address;
  /// Optional sh_link as a section name or index string.
  std::optional<StringRef> Link;
  /// Section address alignment.
  llvm::yaml::Hex64 AddressAlign;
  /// Optional entry size for table sections.
  std::optional<llvm::yaml::Hex64> EntSize;

  /// Optional raw section contents.
  std::optional<yaml::BinaryRef> Content;
  /// Optional section size; may pad or truncate \c Content.
  std::optional<llvm::yaml::Hex64> Size;

  /// Original section index from the source object when dumping.
  unsigned OriginalSecNdx;

  /// Construct a section chunk of the given kind.
  /// \param Kind Concrete section chunk kind.
  /// \param IsImplicit Whether the section is implicit.
  Section(ChunkKind Kind, bool IsImplicit = false) : Chunk(Kind, IsImplicit) {}

  /// Return true if \p S is a section chunk (not a special chunk).
  /// \param S Chunk to test.
  /// \return True when \p S is a section.
  static bool classof(const Chunk *S) {
    return S->Kind < ChunkKind::SpecialChunksStart;
  }

  /// Return named section entries and whether each is present.
  ///
  /// Some derived sections might have their own special entries. This method
  /// returns a vector of <entry name, is used> pairs. It is used for section
  /// validation.
  /// \return Pairs of entry key names and whether they are set.
  virtual std::vector<std::pair<StringRef, bool>> getEntries() const {
    return {};
  };

  // The following members are used to override section fields which is
  // useful for creating invalid objects.

  /// Optional override for the sh_addralign field.
  std::optional<llvm::yaml::Hex64> ShAddrAlign;

  /// Optional override for the offset stored in the sh_name field.
  ///
  /// It does not affect the name stored in the string table.
  std::optional<llvm::yaml::Hex64> ShName;

  /// Optional override for the sh_offset field.
  ///
  /// It does not place the section data at the offset specified.
  std::optional<llvm::yaml::Hex64> ShOffset;

  /// Optional override for the sh_size field.
  ///
  /// It does not affect the content written.
  std::optional<llvm::yaml::Hex64> ShSize;

  /// Optional override for the sh_flags field.
  std::optional<llvm::yaml::Hex64> ShFlags;

  /// Optional override for the sh_type field.
  ///
  /// It is useful when we want to use specific YAML keys for a section of a
  /// particular type to describe the content, but still want to have a
  /// different final type for the section.
  std::optional<ELF_SHT> ShType;
};

/// Fill is a block of data which is placed outside of sections.
///
/// It is not present in the sections header table, but it might affect the
/// output file size and program headers produced.
struct Fill : Chunk {
  /// Optional repeating fill pattern bytes.
  std::optional<yaml::BinaryRef> Pattern;
  /// Size of the fill region in bytes.
  llvm::yaml::Hex64 Size;

  /// Construct an empty fill chunk.
  Fill() : Chunk(ChunkKind::Fill, /*Implicit=*/false) {}

  /// Return true if \p S is a fill chunk.
  /// \param S Chunk to test.
  /// \return True when \p S is a fill.
  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Fill; }
};

/// YAML representation of the ELF section header table layout.
struct SectionHeaderTable : Chunk {
  /// Construct a section header table chunk.
  /// \param IsImplicit Whether the table is implicit.
  SectionHeaderTable(bool IsImplicit)
      : Chunk(ChunkKind::SectionHeaderTable, IsImplicit) {}

  /// Return true if \p S is a section header table chunk.
  /// \param S Chunk to test.
  /// \return True when \p S is a section header table.
  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::SectionHeaderTable;
  }

  /// Optional explicit ordered list of section headers to emit.
  std::optional<std::vector<SectionHeader>> Sections;
  /// Optional list of sections excluded from the header table.
  std::optional<std::vector<SectionHeader>> Excluded;
  /// Optional flag to omit section headers entirely.
  std::optional<bool> NoHeaders;

  /// Return the number of section headers that should be written.
  /// \param SectionsNum Number of sections in the object.
  /// \return Header count including the null section when applicable.
  size_t getNumHeaders(size_t SectionsNum) const {
    if (IsImplicit || isDefault())
      return SectionsNum;
    if (NoHeaders)
      return (*NoHeaders) ? 0 : SectionsNum;
    return (Sections ? Sections->size() : 0) + /*Null section*/ 1;
  }

  /// Return true when no explicit header-table overrides are set.
  /// \return True if Sections, Excluded, and NoHeaders are all unset.
  bool isDefault() const { return !Sections && !Excluded && !NoHeaders; }

  /// YAML type string used to identify this chunk.
  static constexpr StringRef TypeStr = "SectionHeaderTable";
};

/// YAML representation of an SHT_LLVM_BB_ADDR_MAP section.
struct BBAddrMapSection : Section {
  /// Optional basic-block address map entries.
  std::optional<std::vector<BBAddrMapYAML::BBAddrMapEntry>> Entries;
  /// Optional PGO analysis map entries paired with \c Entries.
  std::optional<std::vector<BBAddrMapYAML::PGOAnalysisMapEntry>> PGOAnalyses;

  /// Construct an empty BB address map section.
  BBAddrMapSection() : Section(ChunkKind::BBAddrMap) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  /// Return true if \p S is a BB address map section.
  /// \param S Chunk to test.
  /// \return True when \p S is a BBAddrMap section.
  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::BBAddrMap;
  }
};

/// YAML representation of a .stack_sizes section.
struct StackSizesSection : Section {
  /// Optional stack size entries.
  std::optional<std::vector<StackSizeEntry>> Entries;

  /// Construct an empty stack sizes section.
  StackSizesSection() : Section(ChunkKind::StackSizes) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  /// Return true if \p S is a stack sizes section.
  /// \param S Chunk to test.
  /// \return True when \p S is a StackSizes section.
  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::StackSizes;
  }

  /// Return true if \p Name is the conventional .stack_sizes section name.
  /// \param Name Section name to test.
  /// \return True when \p Name equals ".stack_sizes".
  static bool nameMatches(StringRef Name) {
    return Name == ".stack_sizes";
  }
};

/// YAML representation of an SHT_DYNAMIC section.
struct DynamicSection : Section {
  /// Optional dynamic table entries.
  std::optional<std::vector<DynamicEntry>> Entries;

  /// Construct an empty dynamic section.
  DynamicSection() : Section(ChunkKind::Dynamic) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  /// Return true if \p S is a dynamic section.
  /// \param S Chunk to test.
  /// \return True when \p S is a Dynamic section.
  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Dynamic; }
};

/// YAML representation of a raw-content ELF section.
struct RawContentSection : Section {
  /// Optional sh_info value.
  std::optional<llvm::yaml::Hex64> Info;

  /// Construct an empty raw content section.
  RawContentSection() : Section(ChunkKind::RawContent) {}

  /// Return true if \p S is a raw content section.
  /// \param S Chunk to test.
  /// \return True when \p S is a RawContent section.
  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::RawContent;
  }

  /// Byte buffer used when content is read as an array of bytes.
  std::optional<std::vector<uint8_t>> ContentBuf;
};

/// YAML representation of an SHT_NOBITS section.
struct NoBitsSection : Section {
  /// Construct an empty SHT_NOBITS section.
  NoBitsSection() : Section(ChunkKind::NoBits) {}

  /// Return true if \p S is a nobits section.
  /// \param S Chunk to test.
  /// \return True when \p S is a NoBits section.
  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::NoBits; }
};

/// YAML representation of an SHT_NOTE section.
struct NoteSection : Section {
  /// Optional note entries.
  std::optional<std::vector<ELFYAML::NoteEntry>> Notes;

  /// Construct an empty note section.
  NoteSection() : Section(ChunkKind::Note) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Notes", Notes.has_value()}};
  };

  /// Return true if \p S is a note section.
  /// \param S Chunk to test.
  /// \return True when \p S is a Note section.
  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Note; }
};

/// YAML representation of an SHT_HASH section.
struct HashSection : Section {
  /// Optional hash bucket array.
  std::optional<std::vector<uint32_t>> Bucket;
  /// Optional hash chain array.
  std::optional<std::vector<uint32_t>> Chain;

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Bucket", Bucket.has_value()}, {"Chain", Chain.has_value()}};
  };

  // The following members are used to override section fields.
  // This is useful for creating invalid objects.
  /// Optional override for the number of hash buckets.
  std::optional<llvm::yaml::Hex64> NBucket;
  /// Optional override for the number of hash chain entries.
  std::optional<llvm::yaml::Hex64> NChain;

  /// Construct an empty hash section.
  HashSection() : Section(ChunkKind::Hash) {}

  /// Return true if \p S is a hash section.
  /// \param S Chunk to test.
  /// \return True when \p S is a Hash section.
  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Hash; }
};

/// YAML representation of the GNU hash section header fields.
struct GnuHashHeader {
  /// Optional override for the number of hash buckets.
  ///
  /// Not used when dumping the object, but can be used to override
  /// the real number of buckets when emiting an object from a YAML document.
  std::optional<llvm::yaml::Hex32> NBuckets;

  /// Index of the first dynamic symbol included in the hash table.
  llvm::yaml::Hex32 SymNdx;

  /// Optional override for the number of Bloom filter words.
  ///
  /// Not used when dumping the object, but can be used to override the real
  /// number of words in the Bloom filter when emiting an object from a YAML
  /// document.
  std::optional<llvm::yaml::Hex32> MaskWords;

  /// Shift constant used by the Bloom filter.
  llvm::yaml::Hex32 Shift2;
};

/// YAML representation of an SHT_GNU_HASH section.
struct GnuHashSection : Section {
  /// Optional GNU hash header fields.
  std::optional<GnuHashHeader> Header;
  /// Optional Bloom filter words.
  std::optional<std::vector<llvm::yaml::Hex64>> BloomFilter;
  /// Optional hash bucket values.
  std::optional<std::vector<llvm::yaml::Hex32>> HashBuckets;
  /// Optional hash values.
  std::optional<std::vector<llvm::yaml::Hex32>> HashValues;

  /// Construct an empty GNU hash section.
  GnuHashSection() : Section(ChunkKind::GnuHash) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Header", Header.has_value()},
            {"BloomFilter", BloomFilter.has_value()},
            {"HashBuckets", HashBuckets.has_value()},
            {"HashValues", HashValues.has_value()}};
  };

  /// Return true if \p S is a GNU hash section.
  /// \param S Chunk to test.
  /// \return True when \p S is a GnuHash section.
  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::GnuHash; }
};

/// YAML representation of a vernaux entry in a verneed section.
struct VernauxEntry {
  /// Hash value for the dependency version name.
  uint32_t Hash;
  /// Version dependency flags.
  uint16_t Flags;
  /// Version index used in versym entries.
  uint16_t Other;
  /// Version dependency name.
  StringRef Name;
};

/// YAML representation of a verneed entry describing a needed shared object.
struct VerneedEntry {
  /// Version of the verneed structure.
  uint16_t Version;
  /// File name of the needed shared object.
  StringRef File;
  /// Auxiliary vernaux entries for this dependency.
  std::vector<VernauxEntry> AuxV;
};

/// YAML representation of an SHT_GNU_verneed section.
struct VerneedSection : Section {
  /// Optional verneed dependency entries.
  std::optional<std::vector<VerneedEntry>> VerneedV;
  /// Optional sh_info value (often the entry count).
  std::optional<llvm::yaml::Hex64> Info;

  /// Construct an empty verneed section.
  VerneedSection() : Section(ChunkKind::Verneed) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Dependencies", VerneedV.has_value()}};
  };

  /// Return true if \p S is a verneed section.
  /// \param S Chunk to test.
  /// \return True when \p S is a Verneed section.
  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::Verneed;
  }
};

/// YAML representation of an SHT_LLVM_ADDRSIG section.
struct AddrsigSection : Section {
  /// Optional address-significance symbol names or indices.
  std::optional<std::vector<YAMLFlowString>> Symbols;

  /// Construct an empty addrsig section.
  AddrsigSection() : Section(ChunkKind::Addrsig) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Symbols", Symbols.has_value()}};
  };

  /// Return true if \p S is an addrsig section.
  /// \param S Chunk to test.
  /// \return True when \p S is an Addrsig section.
  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Addrsig; }
};

/// YAML representation of a linker option key/value pair.
struct LinkerOption {
  /// Linker option key.
  StringRef Key;
  /// Linker option value.
  StringRef Value;
};

/// YAML representation of an SHT_LLVM_LINKER_OPTIONS section.
struct LinkerOptionsSection : Section {
  /// Optional linker option entries.
  std::optional<std::vector<LinkerOption>> Options;

  /// Construct an empty linker options section.
  LinkerOptionsSection() : Section(ChunkKind::LinkerOptions) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Options", Options.has_value()}};
  };

  /// Return true if \p S is a linker options section.
  /// \param S Chunk to test.
  /// \return True when \p S is a LinkerOptions section.
  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::LinkerOptions;
  }
};

/// YAML representation of an SHT_LLVM_DEPENDENT_LIBRARIES section.
struct DependentLibrariesSection : Section {
  /// Optional dependent library names.
  std::optional<std::vector<YAMLFlowString>> Libs;

  /// Construct an empty dependent libraries section.
  DependentLibrariesSection() : Section(ChunkKind::DependentLibraries) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Libraries", Libs.has_value()}};
  };

  /// Return true if \p S is a dependent libraries section.
  /// \param S Chunk to test.
  /// \return True when \p S is a DependentLibraries section.
  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::DependentLibraries;
  }
};

/// Represents the call graph profile section entry.
struct CallGraphEntryWeight {
  /// The weight of the edge.
  uint64_t Weight;
};

/// YAML representation of an SHT_LLVM_CALL_GRAPH_PROFILE section.
struct CallGraphProfileSection : Section {
  /// Optional call-graph profile edge weights.
  std::optional<std::vector<CallGraphEntryWeight>> Entries;

  /// Construct an empty call graph profile section.
  CallGraphProfileSection() : Section(ChunkKind::CallGraphProfile) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  /// Return true if \p S is a call graph profile section.
  /// \param S Chunk to test.
  /// \return True when \p S is a CallGraphProfile section.
  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::CallGraphProfile;
  }
};

/// YAML representation of an SHT_GNU_versym section.
struct SymverSection : Section {
  /// Optional symbol version indices.
  std::optional<std::vector<uint16_t>> Entries;

  /// Construct an empty versym section.
  SymverSection() : Section(ChunkKind::Symver) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  /// Return true if \p S is a versym section.
  /// \param S Chunk to test.
  /// \return True when \p S is a Symver section.
  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Symver; }
};

/// YAML representation of a version definition entry.
struct VerdefEntry {
  /// Optional version of the verdef structure.
  std::optional<uint16_t> Version;
  /// Optional version definition flags.
  std::optional<uint16_t> Flags;
  /// Optional version index.
  std::optional<uint16_t> VersionNdx;
  /// Optional hash of the version name.
  std::optional<uint32_t> Hash;
  /// Optional number of auxiliary version names.
  std::optional<uint16_t> VDAux;
  /// Version names associated with this definition.
  std::vector<StringRef> VerNames;
};

/// YAML representation of an SHT_GNU_verdef section.
struct VerdefSection : Section {
  /// Optional version definition entries.
  std::optional<std::vector<VerdefEntry>> Entries;
  /// Optional sh_info value.
  std::optional<llvm::yaml::Hex64> Info;

  /// Construct an empty verdef section.
  VerdefSection() : Section(ChunkKind::Verdef) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  /// Return true if \p S is a verdef section.
  /// \param S Chunk to test.
  /// \return True when \p S is a Verdef section.
  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Verdef; }
};

/// YAML representation of an SHT_GROUP section.
struct GroupSection : Section {
  /// Optional group member section names or types.
  ///
  /// Members of a group contain a flag and a list of section indices
  /// that are part of the group.
  std::optional<std::vector<SectionOrType>> Members;
  /// Optional group signature symbol name (stored in sh_info).
  std::optional<StringRef> Signature; /* Info */

  /// Construct an empty group section.
  GroupSection() : Section(ChunkKind::Group) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Members", Members.has_value()}};
  };

  /// Return true if \p S is a group section.
  /// \param S Chunk to test.
  /// \return True when \p S is a Group section.
  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Group; }
};

/// YAML representation of an ELF relocation entry.
struct Relocation {
  /// Offset within the relocated section.
  llvm::yaml::Hex64 Offset;
  /// Relocation addend.
  YAMLIntUInt Addend;
  /// Relocation type.
  ELF_REL Type;
  /// Optional symbol name referenced by this relocation.
  std::optional<StringRef> Symbol;
};

/// YAML representation of an SHT_REL / SHT_RELA section.
struct RelocationSection : Section {
  /// Optional relocation entries.
  std::optional<std::vector<Relocation>> Relocations;
  /// Name of the section being relocated (sh_info).
  StringRef RelocatableSec; /* Info */

  /// Construct an empty relocation section.
  RelocationSection() : Section(ChunkKind::Relocation) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Relocations", Relocations.has_value()}};
  };

  /// Return true if \p S is a relocation section.
  /// \param S Chunk to test.
  /// \return True when \p S is a Relocation section.
  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::Relocation;
  }
};

/// YAML representation of an SHT_RELR section.
struct RelrSection : Section {
  /// Optional relative relocation entries.
  std::optional<std::vector<llvm::yaml::Hex64>> Entries;

  /// Construct an empty RELR section.
  RelrSection() : Section(ChunkKind::Relr) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  /// Return true if \p S is a RELR section.
  /// \param S Chunk to test.
  /// \return True when \p S is a Relr section.
  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::Relr;
  }
};

/// YAML representation of an SHT_SYMTAB_SHNDX section.
struct SymtabShndxSection : Section {
  /// Optional extended section index entries.
  std::optional<std::vector<uint32_t>> Entries;

  /// Construct an empty symtab shndx section.
  SymtabShndxSection() : Section(ChunkKind::SymtabShndxSection) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  /// Return true if \p S is a symtab shndx section.
  /// \param S Chunk to test.
  /// \return True when \p S is a SymtabShndxSection.
  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::SymtabShndxSection;
  }
};

/// YAML representation of an ARM index table entry.
struct ARMIndexTableEntry {
  /// Offset of the indexed function.
  llvm::yaml::Hex32 Offset;
  /// Index table value for the function.
  llvm::yaml::Hex32 Value;
};

/// YAML representation of an ARM index table section.
struct ARMIndexTableSection : Section {
  /// Optional ARM index table entries.
  std::optional<std::vector<ARMIndexTableEntry>> Entries;

  /// Construct an empty ARM index table section.
  ARMIndexTableSection() : Section(ChunkKind::ARMIndexTable) {}

  /// Return which structured entries are present in this section.
  /// \return Entry name / present pairs used for validation.
  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  /// Return true if \p S is an ARM index table section.
  /// \param S Chunk to test.
  /// \return True when \p S is an ARMIndexTable section.
  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::ARMIndexTable;
  }
};

/// Represents a .MIPS.abiflags section.
struct MipsABIFlags : Section {
  /// Version of the MIPS ABI flags structure.
  llvm::yaml::Hex16 Version;
  /// MIPS ISA level.
  MIPS_ISA ISALevel;
  /// MIPS ISA revision.
  llvm::yaml::Hex8 ISARevision;
  /// General-purpose register size.
  MIPS_AFL_REG GPRSize;
  /// Coprocessor 1 register size.
  MIPS_AFL_REG CPR1Size;
  /// Coprocessor 2 register size.
  MIPS_AFL_REG CPR2Size;
  /// Floating-point ABI.
  MIPS_ABI_FP FpABI;
  /// ISA extension.
  MIPS_AFL_EXT ISAExtension;
  /// Application-specific extensions in use.
  MIPS_AFL_ASE ASEs;
  /// Additional ABI flags (flags1).
  MIPS_AFL_FLAGS1 Flags1;
  /// Additional ABI flags (flags2).
  llvm::yaml::Hex32 Flags2;

  /// Construct an empty MIPS ABI flags section.
  MipsABIFlags() : Section(ChunkKind::MipsABIFlags) {}

  /// Return true if \p S is a MIPS ABI flags section.
  /// \param S Chunk to test.
  /// \return True when \p S is a MipsABIFlags section.
  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::MipsABIFlags;
  }
};

/// YAML representation of an ELF program header.
struct ProgramHeader {
  /// Segment type (PT_*).
  ELF_PT Type;
  /// Segment flags (PF_*).
  ELF_PF Flags;
  /// Virtual address of the segment.
  llvm::yaml::Hex64 VAddr;
  /// Physical address of the segment.
  llvm::yaml::Hex64 PAddr;
  /// Optional segment alignment.
  std::optional<llvm::yaml::Hex64> Align;
  /// Optional file size of the segment.
  std::optional<llvm::yaml::Hex64> FileSize;
  /// Optional memory size of the segment.
  std::optional<llvm::yaml::Hex64> MemSize;
  /// Optional file offset of the segment.
  std::optional<llvm::yaml::Hex64> Offset;
  /// Optional name of the first section covered by this segment.
  std::optional<StringRef> FirstSec;
  /// Optional name of the last section covered by this segment.
  std::optional<StringRef> LastSec;

  /// Chunks belonging to this program header from FirstSec through LastSec.
  std::vector<Chunk *> Chunks;
};

/// YAML representation of a complete ELF object.
struct Object {
  /// ELF file header.
  FileHeader Header;
  /// Program headers (segments).
  std::vector<ProgramHeader> ProgramHeaders;

  /// Output section descriptions and non-section custom data chunks.
  ///
  /// An object might contain output section descriptions as well as
  /// custom data that does not belong to any section.
  std::vector<std::unique_ptr<Chunk>> Chunks;

  /// Optional static symbol table entries as a top-level YAML key.
  ///
  /// Although in reality the symbols reside in a section, it is a lot
  /// cleaner and nicer if we read them from the YAML as a separate
  /// top-level key, which automatically ensures that invariants like there
  /// being a single SHT_SYMTAB section are upheld.
  std::optional<std::vector<Symbol>> Symbols;
  /// Optional dynamic symbol table entries as a top-level YAML key.
  std::optional<std::vector<Symbol>> DynamicSymbols;
  /// Optional DWARF debug info embedded in the object YAML.
  std::optional<DWARFYAML::Data> DWARF;

  /// Return pointers to all section chunks in this object.
  /// \return Sections found among \c Chunks.
  std::vector<Section *> getSections() {
    std::vector<Section *> Ret;
    for (const std::unique_ptr<Chunk> &Sec : Chunks)
      if (auto S = dyn_cast<ELFYAML::Section>(Sec.get()))
        Ret.push_back(S);
    return Ret;
  }

  /// Return the section header table chunk for this object.
  /// \return The section header table chunk (always present).
  const SectionHeaderTable &getSectionHeaderTable() const {
    for (const std::unique_ptr<Chunk> &C : Chunks)
      if (auto *S = dyn_cast<ELFYAML::SectionHeaderTable>(C.get()))
        return *S;
    llvm_unreachable("the section header table chunk must always be present");
  }

  /// Return the OS/ABI value from the file header.
  /// \return OS/ABI identification.
  LLVM_ABI ELF_ELFOSABI getOSAbi() const;
  /// Return the machine architecture from the file header.
  /// \return Machine architecture value.
  LLVM_ABI unsigned getMachine() const;
};

/// Return whether SHT_NOBITS section \p S should allocate file space.
/// \param Phdrs Program headers that may cover \p S.
/// \param S Nobits section under consideration.
/// \return True if file space should be allocated for \p S.
LLVM_ABI bool shouldAllocateFileSpace(ArrayRef<ProgramHeader> Phdrs,
                                      const NoBitsSection &S);

} // end namespace ELFYAML
} // end namespace llvm

namespace llvm {
namespace yaml {

/// Sequences of stack size entries use block formatting.
template <> struct SequenceElementTraits<ELFYAML::StackSizeEntry> {
  /// Emit sequences of stack size entries in block style.
  static const bool flow = false;
};

/// Sequences of dynamic entries use block formatting.
template <> struct SequenceElementTraits<ELFYAML::DynamicEntry> {
  /// Emit sequences of dynamic entries in block style.
  static const bool flow = false;
};

/// Sequences of linker options use block formatting.
template <> struct SequenceElementTraits<ELFYAML::LinkerOption> {
  /// Emit sequences of linker options in block style.
  static const bool flow = false;
};

/// Sequences of call graph entry weights use block formatting.
template <> struct SequenceElementTraits<ELFYAML::CallGraphEntryWeight> {
  /// Emit sequences of call graph entry weights in block style.
  static const bool flow = false;
};

/// Sequences of note entries use block formatting.
template <> struct SequenceElementTraits<ELFYAML::NoteEntry> {
  /// Emit sequences of note entries in block style.
  static const bool flow = false;
};

/// Sequences of program headers use block formatting.
template <> struct SequenceElementTraits<ELFYAML::ProgramHeader> {
  /// Emit sequences of program headers in block style.
  static const bool flow = false;
};

/// Sequences of section headers use block formatting.
template <> struct SequenceElementTraits<ELFYAML::SectionHeader> {
  /// Emit sequences of section headers in block style.
  static const bool flow = false;
};

/// Sequences of chunk pointers use block formatting.
template <> struct SequenceElementTraits<std::unique_ptr<ELFYAML::Chunk>> {
  /// Emit sequences of chunk pointers in block style.
  static const bool flow = false;
};

/// Sequences of symbols use block formatting.
template <> struct SequenceElementTraits<ELFYAML::Symbol> {
  /// Emit sequences of symbols in block style.
  static const bool flow = false;
};

/// Sequences of verdef entries use block formatting.
template <> struct SequenceElementTraits<ELFYAML::VerdefEntry> {
  /// Emit sequences of verdef entries in block style.
  static const bool flow = false;
};

/// Sequences of vernaux entries use block formatting.
template <> struct SequenceElementTraits<ELFYAML::VernauxEntry> {
  /// Emit sequences of vernaux entries in block style.
  static const bool flow = false;
};

/// Sequences of verneed entries use block formatting.
template <> struct SequenceElementTraits<ELFYAML::VerneedEntry> {
  /// Emit sequences of verneed entries in block style.
  static const bool flow = false;
};

/// Sequences of relocations use block formatting.
template <> struct SequenceElementTraits<ELFYAML::Relocation> {
  /// Emit sequences of relocations in block style.
  static const bool flow = false;
};

/// Sequences of section-or-type values use block formatting.
template <> struct SequenceElementTraits<ELFYAML::SectionOrType> {
  /// Emit sequences of section-or-type values in block style.
  static const bool flow = false;
};

/// Sequences of ARM index table entries use block formatting.
template <> struct SequenceElementTraits<ELFYAML::ARMIndexTableEntry> {
  /// Emit sequences of ARM index table entries in block style.
  static const bool flow = false;
};

} // end namespace yaml
} // end namespace llvm

namespace llvm {
namespace yaml {

/// YAMLIO scalar traits for \c ELFYAML::YAMLIntUInt.
template <> struct ScalarTraits<ELFYAML::YAMLIntUInt> {
  /// Write \p Val as a YAML scalar to \p Out.
  /// \param Val Integer value to write.
  /// \param Ctx Optional YAML context pointer.
  /// \param Out Output stream.
  LLVM_ABI static void output(const ELFYAML::YAMLIntUInt &Val, void *Ctx,
                              raw_ostream &Out);
  /// Parse YAML scalar \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Optional YAML context pointer.
  /// \param Val Destination integer value.
  /// \return Empty string on success, or an error message.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx,
                                  ELFYAML::YAMLIntUInt &Val);
  /// Return whether \p Scalar must be quoted in YAML.
  /// \param Scalar Scalar text being considered.
  /// \return Quoting requirement for \p Scalar.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_ET.
template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_ET> {
  /// Map ELF object file type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Object file type being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_ET &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_PT.
template <> struct ScalarEnumerationTraits<ELFYAML::ELF_PT> {
  /// Map ELF program header type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Program header type being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_PT &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_NT.
template <> struct ScalarEnumerationTraits<ELFYAML::ELF_NT> {
  /// Map ELF note type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Note type being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_NT &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_EM.
template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_EM> {
  /// Map ELF machine architecture enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Machine architecture being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_EM &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_ELFCLASS.
template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_ELFCLASS> {
  /// Map ELF file class enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value File class being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_ELFCLASS &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_ELFDATA.
template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_ELFDATA> {
  /// Map ELF data encoding enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Data encoding being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_ELFDATA &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_ELFOSABI.
template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_ELFOSABI> {
  /// Map ELF OS/ABI enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value OS/ABI value being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_ELFOSABI &Value);
};

/// YAMLIO scalar bitset traits for \c ELFYAML::ELF_EF.
template <>
struct ScalarBitSetTraits<ELFYAML::ELF_EF> {
  /// Map ELF file header flag bits to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value File header flags being mapped.
  LLVM_ABI static void bitset(IO &IO, ELFYAML::ELF_EF &Value);
};

/// YAMLIO scalar bitset traits for \c ELFYAML::ELF_PF.
template <> struct ScalarBitSetTraits<ELFYAML::ELF_PF> {
  /// Map ELF program header flag bits to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Program header flags being mapped.
  LLVM_ABI static void bitset(IO &IO, ELFYAML::ELF_PF &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_SHT.
template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_SHT> {
  /// Map ELF section type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Section type being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_SHT &Value);
};

/// YAMLIO scalar bitset traits for \c ELFYAML::ELF_SHF.
template <>
struct ScalarBitSetTraits<ELFYAML::ELF_SHF> {
  /// Map ELF section flag bits to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Section flags being mapped.
  LLVM_ABI static void bitset(IO &IO, ELFYAML::ELF_SHF &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_SHN.
template <> struct ScalarEnumerationTraits<ELFYAML::ELF_SHN> {
  /// Map ELF section index enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Section index being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_SHN &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_STB.
template <> struct ScalarEnumerationTraits<ELFYAML::ELF_STB> {
  /// Map ELF symbol binding enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Symbol binding being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_STB &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_STT.
template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_STT> {
  /// Map ELF symbol type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Symbol type being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_STT &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_REL.
template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_REL> {
  /// Map ELF relocation type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Relocation type being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_REL &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_DYNTAG.
template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_DYNTAG> {
  /// Map ELF dynamic tag enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Dynamic tag being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_DYNTAG &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::ELF_RSS.
template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_RSS> {
  /// Map ELF reserved section index enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Reserved section index being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::ELF_RSS &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::MIPS_AFL_REG.
template <>
struct ScalarEnumerationTraits<ELFYAML::MIPS_AFL_REG> {
  /// Map MIPS AFL register-size enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Register-size value being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::MIPS_AFL_REG &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::MIPS_ABI_FP.
template <>
struct ScalarEnumerationTraits<ELFYAML::MIPS_ABI_FP> {
  /// Map MIPS floating-point ABI enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Floating-point ABI being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::MIPS_ABI_FP &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::MIPS_AFL_EXT.
template <>
struct ScalarEnumerationTraits<ELFYAML::MIPS_AFL_EXT> {
  /// Map MIPS AFL ISA extension enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value ISA extension being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::MIPS_AFL_EXT &Value);
};

/// YAMLIO scalar enumeration traits for \c ELFYAML::MIPS_ISA.
template <>
struct ScalarEnumerationTraits<ELFYAML::MIPS_ISA> {
  /// Map MIPS ISA level enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value ISA level being mapped.
  LLVM_ABI static void enumeration(IO &IO, ELFYAML::MIPS_ISA &Value);
};

/// YAMLIO scalar bitset traits for \c ELFYAML::MIPS_AFL_ASE.
template <>
struct ScalarBitSetTraits<ELFYAML::MIPS_AFL_ASE> {
  /// Map MIPS AFL ASE flag bits to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value ASE flags being mapped.
  LLVM_ABI static void bitset(IO &IO, ELFYAML::MIPS_AFL_ASE &Value);
};

/// YAMLIO scalar bitset traits for \c ELFYAML::MIPS_AFL_FLAGS1.
template <>
struct ScalarBitSetTraits<ELFYAML::MIPS_AFL_FLAGS1> {
  /// Map MIPS AFL flags1 bits to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Flags1 value being mapped.
  LLVM_ABI static void bitset(IO &IO, ELFYAML::MIPS_AFL_FLAGS1 &Value);
};

/// YAMLIO mapping traits for \c ELFYAML::FileHeader.
template <>
struct MappingTraits<ELFYAML::FileHeader> {
  /// Map ELF file header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param FileHdr File header being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::FileHeader &FileHdr);
};

/// YAMLIO mapping traits for \c ELFYAML::SectionHeader.
template <> struct MappingTraits<ELFYAML::SectionHeader> {
  /// Map section header name fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param SHdr Section header being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::SectionHeader &SHdr);
};

/// YAMLIO mapping traits for \c ELFYAML::ProgramHeader.
template <> struct MappingTraits<ELFYAML::ProgramHeader> {
  /// Map program header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param FileHdr Program header being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::ProgramHeader &FileHdr);
  /// Validate a mapped program header.
  /// \param IO YAML input/output state.
  /// \param FileHdr Program header being validated.
  /// \return Empty string on success, or an error message.
  LLVM_ABI static std::string validate(IO &IO, ELFYAML::ProgramHeader &FileHdr);
};

/// YAMLIO mapping traits for \c ELFYAML::Symbol.
template <>
struct MappingTraits<ELFYAML::Symbol> {
  /// Map ELF symbol fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Symbol Symbol being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::Symbol &Symbol);
  /// Validate a mapped symbol.
  /// \param IO YAML input/output state.
  /// \param Symbol Symbol being validated.
  /// \return Empty string on success, or an error message.
  LLVM_ABI static std::string validate(IO &IO, ELFYAML::Symbol &Symbol);
};

/// YAMLIO mapping traits for \c ELFYAML::StackSizeEntry.
template <> struct MappingTraits<ELFYAML::StackSizeEntry> {
  /// Map stack size entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Rel Stack size entry being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::StackSizeEntry &Rel);
};

/// YAMLIO mapping traits for \c ELFYAML::GnuHashHeader.
template <> struct MappingTraits<ELFYAML::GnuHashHeader> {
  /// Map GNU hash header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Rel GNU hash header being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::GnuHashHeader &Rel);
};

/// YAMLIO mapping traits for \c ELFYAML::DynamicEntry.
template <> struct MappingTraits<ELFYAML::DynamicEntry> {
  /// Map dynamic entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Rel Dynamic entry being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::DynamicEntry &Rel);
};

/// YAMLIO mapping traits for \c ELFYAML::NoteEntry.
template <> struct MappingTraits<ELFYAML::NoteEntry> {
  /// Map note entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param N Note entry being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::NoteEntry &N);
};

/// YAMLIO mapping traits for \c ELFYAML::VerdefEntry.
template <> struct MappingTraits<ELFYAML::VerdefEntry> {
  /// Map verdef entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E Verdef entry being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::VerdefEntry &E);
};

/// YAMLIO mapping traits for \c ELFYAML::VerneedEntry.
template <> struct MappingTraits<ELFYAML::VerneedEntry> {
  /// Map verneed entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E Verneed entry being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::VerneedEntry &E);
};

/// YAMLIO mapping traits for \c ELFYAML::VernauxEntry.
template <> struct MappingTraits<ELFYAML::VernauxEntry> {
  /// Map vernaux entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E Vernaux entry being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::VernauxEntry &E);
};

/// YAMLIO mapping traits for \c ELFYAML::LinkerOption.
template <> struct MappingTraits<ELFYAML::LinkerOption> {
  /// Map linker option fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Sym Linker option being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::LinkerOption &Sym);
};

/// YAMLIO mapping traits for \c ELFYAML::CallGraphEntryWeight.
template <> struct MappingTraits<ELFYAML::CallGraphEntryWeight> {
  /// Map call graph entry weight fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E Call graph entry weight being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::CallGraphEntryWeight &E);
};

/// YAMLIO mapping traits for \c ELFYAML::Relocation.
template <> struct MappingTraits<ELFYAML::Relocation> {
  /// Map relocation fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Rel Relocation being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::Relocation &Rel);
};

/// YAMLIO mapping traits for \c ELFYAML::ARMIndexTableEntry.
template <> struct MappingTraits<ELFYAML::ARMIndexTableEntry> {
  /// Map ARM index table entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param E ARM index table entry being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::ARMIndexTableEntry &E);
};

/// YAMLIO mapping traits for \c std::unique_ptr<ELFYAML::Chunk>.
template <> struct MappingTraits<std::unique_ptr<ELFYAML::Chunk>> {
  /// Map an ELF chunk (section or special chunk) to and from YAML.
  /// \param IO YAML input/output state.
  /// \param C Chunk being mapped.
  LLVM_ABI static void mapping(IO &IO, std::unique_ptr<ELFYAML::Chunk> &C);
  /// Validate a mapped chunk.
  /// \param io YAML input/output state.
  /// \param C Chunk being validated.
  /// \return Empty string on success, or an error message.
  LLVM_ABI static std::string validate(IO &io,
                                       std::unique_ptr<ELFYAML::Chunk> &C);
};

/// YAMLIO mapping traits for \c ELFYAML::Object.
template <>
struct MappingTraits<ELFYAML::Object> {
  /// Map ELF object fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Object Object being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::Object &Object);
};

/// YAMLIO mapping traits for \c ELFYAML::SectionOrType.
template <> struct MappingTraits<ELFYAML::SectionOrType> {
  /// Map section-or-type fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param sectionOrType Section-or-type value being mapped.
  LLVM_ABI static void mapping(IO &IO, ELFYAML::SectionOrType &sectionOrType);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_ELFYAML_H

