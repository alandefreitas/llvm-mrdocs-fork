//===- StackMapParser.h - StackMap Parsing Support --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_STACKMAPPARSER_H
#define LLVM_OBJECT_STACKMAPPARSER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/Endian.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace llvm {

/// A parser for the latest stackmap format.  At the moment, latest=V3.
template <llvm::endianness Endianness> class StackMapParser {
public:
  /// Iterator that wraps a stack map accessor and advances via AccessorT::next.
  template <typename AccessorT>
  class AccessorIterator {
  public:
    /// Construct an iterator that owns a copy of \p A.
    ///
    /// \param A Accessor positioned at the current element.
    AccessorIterator(AccessorT A) : A(A) {}

    /// Advance to the next element and return this iterator.
    ///
    /// \return A reference to this iterator after advancing.
    AccessorIterator& operator++() { A = A.next(); return *this; }
    /// Advance to the next element and return the previous iterator value.
    ///
    /// \param Unused Dummy parameter distinguishing postfix from prefix.
    /// \return A copy of the iterator before advancing.
    AccessorIterator operator++([[maybe_unused]] int Unused) {
      auto tmp = *this;
      ++*this;
      return tmp;
    }

    /// Return true if this iterator and \p Other refer to the same element.
    ///
    /// \param Other Iterator to compare against.
    /// \return True if both iterators refer to the same element.
    bool operator==(const AccessorIterator &Other) const {
      return A.P == Other.A.P;
    }

    /// Return true if this iterator and \p Other refer to different elements.
    ///
    /// \param Other Iterator to compare against.
    /// \return True if the iterators refer to different elements.
    bool operator!=(const AccessorIterator &Other) const {
      return !(*this == Other);
    }

    /// Return a reference to the current accessor.
    ///
    /// \return A reference to the current accessor.
    AccessorT& operator*() { return A; }
    /// Return a pointer to the current accessor.
    ///
    /// \return A pointer to the current accessor.
    AccessorT* operator->() { return &A; }

  private:
    AccessorT A;
  };

  /// Accessor for function records.
  class FunctionAccessor {
    friend class StackMapParser;

  public:
    /// Get the function address.
    ///
    /// \return The address of the function.
    uint64_t getFunctionAddress() const {
      return read<uint64_t>(P);
    }

    /// Get the function's stack size.
    ///
    /// \return The stack size of the function.
    uint64_t getStackSize() const {
      return read<uint64_t>(P + sizeof(uint64_t));
    }

    /// Get the number of callsite records.
    ///
    /// \return The number of callsite records for this function.
    uint64_t getRecordCount() const {
      return read<uint64_t>(P + (2 * sizeof(uint64_t)));
    }

  private:
    FunctionAccessor(const uint8_t *P) : P(P) {}

    const static int FunctionAccessorSize = 3 * sizeof(uint64_t);

    FunctionAccessor next() const {
      return FunctionAccessor(P + FunctionAccessorSize);
    }

    const uint8_t *P;
  };

  /// Accessor for constants.
  class ConstantAccessor {
    friend class StackMapParser;

  public:
    /// Return the value of this constant.
    ///
    /// \return The value of this constant.
    uint64_t getValue() const { return read<uint64_t>(P); }

  private:
    ConstantAccessor(const uint8_t *P) : P(P) {}

    const static int ConstantAccessorSize = sizeof(uint64_t);

    ConstantAccessor next() const {
      return ConstantAccessor(P + ConstantAccessorSize);
    }

    const uint8_t *P;
  };

  /// Kind of value encoded in a stack map location record.
  enum class LocationKind : uint8_t {
    Register = 1,     ///< Value lives in a register.
    Direct = 2,       ///< Direct memory reference: register + offset.
    Indirect = 3,     ///< Indirect memory reference through register + offset.
    Constant = 4,     ///< Immediate constant value.
    ConstantIndex = 5 ///< Index into the stack map constant pool.
  };

  /// Accessor for location records.
  class LocationAccessor {
    friend class StackMapParser;
    /// Accessor for stackmap records.
    friend class RecordAccessor;

  public:
    /// Get the Kind for this location.
    ///
    /// \return The location kind for this location.
    LocationKind getKind() const {
      return LocationKind(P[KindOffset]);
    }

    /// Get the Size for this location.
    ///
    /// \return The size in bytes of this location.
    unsigned getSizeInBytes() const {
        return read<uint16_t>(P + SizeOffset);

    }

    /// Get the Dwarf register number for this location.
    ///
    /// \return The DWARF register number for this location.
    uint16_t getDwarfRegNum() const {
      return read<uint16_t>(P + DwarfRegNumOffset);
    }

    /// Get the small-constant for this location. (Kind must be Constant).
    ///
    /// \return The small constant value for this location.
    uint32_t getSmallConstant() const {
      assert(getKind() == LocationKind::Constant && "Not a small constant.");
      return read<uint32_t>(P + SmallConstantOffset);
    }

    /// Get the constant-index for this location. (Kind must be ConstantIndex).
    ///
    /// \return The constant-pool index for this location.
    uint32_t getConstantIndex() const {
      assert(getKind() == LocationKind::ConstantIndex &&
             "Not a constant-index.");
      return read<uint32_t>(P + SmallConstantOffset);
    }

    /// Get the offset for this location. (Kind must be Direct or Indirect).
    ///
    /// \return The offset for this direct or indirect location.
    int32_t getOffset() const {
      assert((getKind() == LocationKind::Direct ||
              getKind() == LocationKind::Indirect) &&
             "Not direct or indirect.");
      return read<int32_t>(P + SmallConstantOffset);
    }

  private:
    LocationAccessor(const uint8_t *P) : P(P) {}

    LocationAccessor next() const {
      return LocationAccessor(P + LocationAccessorSize);
    }

    static const int KindOffset = 0;
    static const int SizeOffset = KindOffset + sizeof(uint16_t);
    static const int DwarfRegNumOffset = SizeOffset + sizeof(uint16_t);
    static const int SmallConstantOffset = DwarfRegNumOffset + sizeof(uint32_t);
    static const int LocationAccessorSize = sizeof(uint64_t) + sizeof(uint32_t);

    const uint8_t *P;
  };

  /// Accessor for stackmap live-out fields.
  class LiveOutAccessor {
    friend class StackMapParser;
    friend class RecordAccessor;

  public:
    /// Get the Dwarf register number for this live-out.
    ///
    /// \return The DWARF register number for this live-out.
    uint16_t getDwarfRegNum() const {
      return read<uint16_t>(P + DwarfRegNumOffset);
    }

    /// Get the size in bytes of live [sub]register.
    ///
    /// \return The size in bytes of the live [sub]register.
    unsigned getSizeInBytes() const {
      return read<uint8_t>(P + SizeOffset);
    }

  private:
    LiveOutAccessor(const uint8_t *P) : P(P) {}

    LiveOutAccessor next() const {
      return LiveOutAccessor(P + LiveOutAccessorSize);
    }

    static const int DwarfRegNumOffset = 0;
    static const int SizeOffset =
      DwarfRegNumOffset + sizeof(uint16_t) + sizeof(uint8_t);
    static const int LiveOutAccessorSize = sizeof(uint32_t);

    const uint8_t *P;
  };

  /// Accessor for stackmap records.
  class RecordAccessor {
    friend class StackMapParser;

  public:
    /// Iterator over location accessors in this record.
    using location_iterator = AccessorIterator<LocationAccessor>;
    /// Iterator over live-out accessors in this record.
    using liveout_iterator = AccessorIterator<LiveOutAccessor>;

    /// Get the patchpoint/stackmap ID for this record.
    ///
    /// \return The patchpoint/stackmap ID for this record.
    uint64_t getID() const {
      return read<uint64_t>(P + PatchpointIDOffset);
    }

    /// Get the instruction offset (from the start of the containing function)
    /// for this record.
    ///
    /// \return The instruction offset from the start of the containing
    /// function.
    uint32_t getInstructionOffset() const {
      return read<uint32_t>(P + InstructionOffsetOffset);
    }

    /// Get the number of locations contained in this record.
    ///
    /// \return The number of locations in this record.
    uint16_t getNumLocations() const {
      return read<uint16_t>(P + NumLocationsOffset);
    }

    /// Get the location with the given index.
    ///
    /// \param LocationIndex Zero-based index of the location in this record.
    /// \return An accessor for the location at \p LocationIndex.
    LocationAccessor getLocation(unsigned LocationIndex) const {
      unsigned LocationOffset =
        LocationListOffset + LocationIndex * LocationSize;
      return LocationAccessor(P + LocationOffset);
    }

    /// Begin iterator for locations.
    ///
    /// \return An iterator to the first location in this record.
    location_iterator location_begin() const {
      return location_iterator(getLocation(0));
    }

    /// End iterator for locations.
    ///
    /// \return An iterator one past the last location in this record.
    location_iterator location_end() const {
      return location_iterator(getLocation(getNumLocations()));
    }

    /// Iterator range for locations.
    ///
    /// \return A range covering all locations in this record.
    iterator_range<location_iterator> locations() const {
      return make_range(location_begin(), location_end());
    }

    /// Get the number of liveouts contained in this record.
    ///
    /// \return The number of live-outs in this record.
    uint16_t getNumLiveOuts() const {
      return read<uint16_t>(P + getNumLiveOutsOffset());
    }

    /// Get the live-out with the given index.
    ///
    /// \param LiveOutIndex Zero-based index of the live-out in this record.
    /// \return An accessor for the live-out at \p LiveOutIndex.
    LiveOutAccessor getLiveOut(unsigned LiveOutIndex) const {
      unsigned LiveOutOffset =
        getNumLiveOutsOffset() + sizeof(uint16_t) + LiveOutIndex * LiveOutSize;
      return LiveOutAccessor(P + LiveOutOffset);
    }

    /// Begin iterator for live-outs.
    ///
    /// \return An iterator to the first live-out in this record.
    liveout_iterator liveouts_begin() const {
      return liveout_iterator(getLiveOut(0));
    }

    /// End iterator for live-outs.
    ///
    /// \return An iterator one past the last live-out in this record.
    liveout_iterator liveouts_end() const {
      return liveout_iterator(getLiveOut(getNumLiveOuts()));
    }

    /// Iterator range for live-outs.
    ///
    /// \return A range covering all live-outs in this record.
    iterator_range<liveout_iterator> liveouts() const {
      return make_range(liveouts_begin(), liveouts_end());
    }

  private:
    RecordAccessor(const uint8_t *P) : P(P) {}

    unsigned getNumLiveOutsOffset() const {
      unsigned LocOffset = 
          ((LocationListOffset + LocationSize * getNumLocations()) + 7) & ~0x7; 
      return LocOffset + sizeof(uint16_t);
    }

    unsigned getSizeInBytes() const {
      unsigned RecordSize =
        getNumLiveOutsOffset() + sizeof(uint16_t) + getNumLiveOuts() * LiveOutSize;
      return (RecordSize + 7) & ~0x7;
    }

    RecordAccessor next() const {
      return RecordAccessor(P + getSizeInBytes());
    }

    static const unsigned PatchpointIDOffset = 0;
    static const unsigned InstructionOffsetOffset =
      PatchpointIDOffset + sizeof(uint64_t);
    static const unsigned NumLocationsOffset =
      InstructionOffsetOffset + sizeof(uint32_t) + sizeof(uint16_t);
    static const unsigned LocationListOffset =
      NumLocationsOffset + sizeof(uint16_t);
    static const unsigned LocationSize = sizeof(uint64_t) + sizeof(uint32_t);
    static const unsigned LiveOutSize = sizeof(uint32_t);

    const uint8_t *P;
  };

  /// Construct a parser for a version-3 stackmap. StackMap data will be read
  /// from the given array.
  ///
  /// \param StackMapSection Bytes of the version-3 stack map section to parse.
  StackMapParser(ArrayRef<uint8_t> StackMapSection)
      : StackMapSection(StackMapSection) {
    ConstantsListOffset = FunctionListOffset + getNumFunctions() * FunctionSize;

    assert(StackMapSection[0] == 3 &&
           "StackMapParser can only parse version 3 stackmaps");

    unsigned CurrentRecordOffset =
      ConstantsListOffset + getNumConstants() * ConstantSize;

    for (unsigned I = 0, E = getNumRecords(); I != E; ++I) {
      StackMapRecordOffsets.push_back(CurrentRecordOffset);
      CurrentRecordOffset +=
        RecordAccessor(&StackMapSection[CurrentRecordOffset]).getSizeInBytes();
    }
  }

  /// Validates the header of the specified stack map section.
  ///
  /// \param StackMapSection Bytes of the stack map section whose header is
  /// checked.
  /// \return Success if the header is valid, or an error describing the
  /// problem.
  static Error validateHeader(ArrayRef<uint8_t> StackMapSection) {
    // See the comment for StackMaps::emitStackmapHeader().
    if (StackMapSection.size() < 16)
      return object::createError(
          "the stack map section size (" + Twine(StackMapSection.size()) +
          ") is less than the minimum possible size of its header (16)");

    unsigned Version = StackMapSection[0];
    if (Version != 3)
      return object::createError(
          "the version (" + Twine(Version) +
          ") of the stack map section is unsupported, the "
          "supported version is 3");
    return Error::success();
  }

  /// Iterator over function accessors in the stack map.
  using function_iterator = AccessorIterator<FunctionAccessor>;
  /// Iterator over constant accessors in the stack map.
  using constant_iterator = AccessorIterator<ConstantAccessor>;
  /// Iterator over record accessors in the stack map.
  using record_iterator = AccessorIterator<RecordAccessor>;

  /// Get the version number of this stackmap. (Always returns 3).
  ///
  /// \return The stack map format version (always 3).
  unsigned getVersion() const { return 3; }

  /// Get the number of functions in the stack map.
  ///
  /// \return The number of functions in the stack map.
  uint32_t getNumFunctions() const {
    return read<uint32_t>(&StackMapSection[NumFunctionsOffset]);
  }

  /// Get the number of large constants in the stack map.
  ///
  /// \return The number of large constants in the stack map.
  uint32_t getNumConstants() const {
    return read<uint32_t>(&StackMapSection[NumConstantsOffset]);
  }

  /// Get the number of stackmap records in the stackmap.
  ///
  /// \return The number of stack map records.
  uint32_t getNumRecords() const {
    return read<uint32_t>(&StackMapSection[NumRecordsOffset]);
  }

  /// Return an FunctionAccessor for the given function index.
  ///
  /// \param FunctionIndex Zero-based index of the function in the stack map.
  /// \return An accessor for the function at \p FunctionIndex.
  FunctionAccessor getFunction(unsigned FunctionIndex) const {
    return FunctionAccessor(StackMapSection.data() +
                            getFunctionOffset(FunctionIndex));
  }

  /// Begin iterator for functions.
  ///
  /// \return An iterator to the first function in the stack map.
  function_iterator functions_begin() const {
    return function_iterator(getFunction(0));
  }

  /// End iterator for functions.
  ///
  /// \return An iterator one past the last function in the stack map.
  function_iterator functions_end() const {
    return function_iterator(
             FunctionAccessor(StackMapSection.data() +
                              getFunctionOffset(getNumFunctions())));
  }

  /// Iterator range for functions.
  ///
  /// \return A range covering all functions in the stack map.
  iterator_range<function_iterator> functions() const {
    return make_range(functions_begin(), functions_end());
  }

  /// Return the large constant at the given index.
  ///
  /// \param ConstantIndex Zero-based index of the constant in the stack map.
  /// \return An accessor for the constant at \p ConstantIndex.
  ConstantAccessor getConstant(unsigned ConstantIndex) const {
    return ConstantAccessor(StackMapSection.data() +
                            getConstantOffset(ConstantIndex));
  }

  /// Begin iterator for constants.
  ///
  /// \return An iterator to the first constant in the stack map.
  constant_iterator constants_begin() const {
    return constant_iterator(getConstant(0));
  }

  /// End iterator for constants.
  ///
  /// \return An iterator one past the last constant in the stack map.
  constant_iterator constants_end() const {
    return constant_iterator(
             ConstantAccessor(StackMapSection.data() +
                              getConstantOffset(getNumConstants())));
  }

  /// Iterator range for constants.
  ///
  /// \return A range covering all constants in the stack map.
  iterator_range<constant_iterator> constants() const {
    return make_range(constants_begin(), constants_end());
  }

  /// Return a RecordAccessor for the given record index.
  ///
  /// \param RecordIndex Zero-based index of the stack map record.
  /// \return An accessor for the record at \p RecordIndex.
  RecordAccessor getRecord(unsigned RecordIndex) const {
    std::size_t RecordOffset = StackMapRecordOffsets[RecordIndex];
    return RecordAccessor(StackMapSection.data() + RecordOffset);
  }

  /// Begin iterator for records.
  ///
  /// \return An iterator to the first record in the stack map.
  record_iterator records_begin() const {
    if (getNumRecords() == 0)
      return record_iterator(RecordAccessor(nullptr));
    return record_iterator(getRecord(0));
  }

  /// End iterator for records.
  ///
  /// \return An iterator one past the last record in the stack map.
  record_iterator records_end() const {
    // Records need to be handled specially, since we cache the start addresses
    // for them: We can't just compute the 1-past-the-end address, we have to
    // look at the last record and use the 'next' method.
    if (getNumRecords() == 0)
      return record_iterator(RecordAccessor(nullptr));
    return record_iterator(getRecord(getNumRecords() - 1).next());
  }

  /// Iterator range for records.
  ///
  /// \return A range covering all records in the stack map.
  iterator_range<record_iterator> records() const {
    return make_range(records_begin(), records_end());
  }

private:
  template <typename T>
  static T read(const uint8_t *P) {
    return support::endian::read<T, Endianness>(P);
  }

  static const unsigned HeaderOffset = 0;
  static const unsigned NumFunctionsOffset = HeaderOffset + sizeof(uint32_t);
  static const unsigned NumConstantsOffset = NumFunctionsOffset + sizeof(uint32_t);
  static const unsigned NumRecordsOffset = NumConstantsOffset + sizeof(uint32_t);
  static const unsigned FunctionListOffset = NumRecordsOffset + sizeof(uint32_t);

  static const unsigned FunctionSize = 3 * sizeof(uint64_t);
  static const unsigned ConstantSize = sizeof(uint64_t);

  std::size_t getFunctionOffset(unsigned FunctionIndex) const {
    return FunctionListOffset + FunctionIndex * FunctionSize;
  }

  std::size_t getConstantOffset(unsigned ConstantIndex) const {
    return ConstantsListOffset + ConstantIndex * ConstantSize;
  }

  ArrayRef<uint8_t> StackMapSection;
  unsigned ConstantsListOffset;
  std::vector<unsigned> StackMapRecordOffsets;
};

} // end namespace llvm

#endif // LLVM_OBJECT_STACKMAPPARSER_H
