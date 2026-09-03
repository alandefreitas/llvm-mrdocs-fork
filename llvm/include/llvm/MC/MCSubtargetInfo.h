//===- llvm/MC/MCSubtargetInfo.h - Subtarget Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the subtarget options of a Target machine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSUBTARGETINFO_H
#define LLVM_MC_MCSUBTARGETINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringTable.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

namespace llvm {

class MCInst;

//===----------------------------------------------------------------------===//

/// Used to provide key value pairs for feature and CPU bit flags.
struct SubtargetFeatureKV {
  /// Relative offset from this object to the feature key string.
  uint16_t KeyStrOff;
  /// Relative offset from this object to the feature description string.
  uint16_t DescStrOff;
  unsigned Value;                       ///< K-V integer value
  FeatureBitArray Implies;              ///< K-V bit mask

  /// Construct a feature key-value entry with string offsets and implies mask.
  ///
  /// \param KeyStrOff Relative offset to the feature key string.
  /// \param DescStrOff Relative offset to the feature description string.
  /// \param Value Feature bit value or enum identifier.
  /// \param Implies Feature bits implied by this feature.
  constexpr SubtargetFeatureKV(uint16_t KeyStrOff, uint16_t DescStrOff,
                               unsigned Value, FeatureBitArray Implies)
      : KeyStrOff(KeyStrOff), DescStrOff(DescStrOff), Value(Value),
        Implies(Implies) {}

  // Because of relative string offsets, this type is not copyable.
  /// Deleted copy constructor; relative string offsets are not relocatable.
  ///
  /// \param Other Unused; copy construction is deleted.
  SubtargetFeatureKV(const SubtargetFeatureKV &Other) = delete;
  /// Deleted copy assignment; relative string offsets are not relocatable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  SubtargetFeatureKV &operator=(const SubtargetFeatureKV &Other) = delete;

  /// Return the feature key string.
  ///
  /// \return Pointer to the feature key string.
  const char *key() const {
    return reinterpret_cast<const char *>(this) + KeyStrOff;
  }

  /// Return the feature description string.
  ///
  /// \return Pointer to the feature description string.
  const char *desc() const {
    return reinterpret_cast<const char *>(this) + DescStrOff;
  }

  /// Compare routine for std::lower_bound.
  ///
  /// \param S String to compare against this entry's key.
  /// \return True if this entry's key is less than \p S.
  bool operator<(StringRef S) const { return StringRef(key()) < S; }

  /// Compare routine for std::is_sorted.
  ///
  /// \param Other Other feature entry to compare keys against.
  /// \return True if this entry's key is less than \p Other's key.
  bool operator<(const SubtargetFeatureKV &Other) const {
    return StringRef(key()) < StringRef(Other.key());
  }
};

/// Storage for a table of SubtargetFeatureKV entries and their string table.
template <size_t NumFeatures, size_t FeatureStrTabSize>
struct SubtargetFeatureKVStorage {
  /// Array of feature key-value entries.
  SubtargetFeatureKV Features[NumFeatures];
  /// Contiguous string table referenced by relative offsets in \p Features.
  char Strings[FeatureStrTabSize];
};

//===----------------------------------------------------------------------===//

/// Used to provide key value pairs for feature and CPU bit flags.
struct SubtargetSubTypeKV {
  /// Relative offset from this object to the CPU/subtype key string.
  uint16_t KeyStrOff;
  /// Index into the processor scheduling model table.
  unsigned SchedModelIdx;
  FeatureBitArray Implies;              ///< K-V bit mask
  FeatureBitArray TuneImplies;          ///< K-V bit mask

  /// Construct a CPU/subtype key-value entry.
  ///
  /// \param KeyStrOff Relative offset to the CPU/subtype key string.
  /// \param Implies Feature bits implied when selecting this CPU.
  /// \param TuneImplies Feature bits implied when tuning for this CPU.
  /// \param SchedModelIdx Index of the scheduling model for this CPU.
  constexpr SubtargetSubTypeKV(uint16_t KeyStrOff, FeatureBitArray Implies,
                               FeatureBitArray TuneImplies,
                               unsigned SchedModelIdx)
      : KeyStrOff(KeyStrOff), SchedModelIdx(SchedModelIdx), Implies(Implies),
        TuneImplies(TuneImplies) {}

  // Because of relative string offsets, this type is not copyable.
  /// Deleted copy constructor; relative string offsets are not relocatable.
  ///
  /// \param Other Unused; copy construction is deleted.
  SubtargetSubTypeKV(const SubtargetSubTypeKV &Other) = delete;
  /// Deleted copy assignment; relative string offsets are not relocatable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  SubtargetSubTypeKV &operator=(const SubtargetSubTypeKV &Other) = delete;

  /// Return the CPU/subtype key string.
  ///
  /// \return Pointer to the CPU/subtype key string.
  const char *key() const {
    return reinterpret_cast<const char *>(this) + KeyStrOff;
  }

  /// Compare routine for std::lower_bound.
  ///
  /// \param S String to compare against this entry's key.
  /// \return True if this entry's key is less than \p S.
  bool operator<(StringRef S) const { return StringRef(key()) < S; }

  /// Compare routine for std::is_sorted.
  ///
  /// \param Other Other subtype entry to compare keys against.
  /// \return True if this entry's key is less than \p Other's key.
  bool operator<(const SubtargetSubTypeKV &Other) const {
    return StringRef(key()) < StringRef(Other.key());
  }
};

/// Maps a CPU alias name to the index of the processor it resolves to.
struct SubtargetSubTypeAliasKV {
  uint16_t KeyStrOff;  ///< Relative offset to the alias name.
  uint16_t SubTypeIdx; ///< Index into the SubtargetSubTypeKV array.

  // MSVC STL before VS2022 value-initializes an element of std::array<T, 0>,
  // which every target defining no processor aliases instantiates.
  /// Default-construct an empty alias entry for zero-sized alias arrays.
  constexpr SubtargetSubTypeAliasKV() : KeyStrOff(0), SubTypeIdx(0) {}

  /// Construct an alias entry mapping a name to a subtype index.
  ///
  /// \param KeyStrOff Relative offset to the alias name string.
  /// \param SubTypeIdx Index of the processor this alias resolves to.
  constexpr SubtargetSubTypeAliasKV(uint16_t KeyStrOff, uint16_t SubTypeIdx)
      : KeyStrOff(KeyStrOff), SubTypeIdx(SubTypeIdx) {}

  /// Deleted copy constructor; relative string offsets are not relocatable.
  ///
  /// \param Other Unused; copy construction is deleted.
  SubtargetSubTypeAliasKV(const SubtargetSubTypeAliasKV &Other) = delete;
  /// Deleted copy assignment; relative string offsets are not relocatable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  SubtargetSubTypeAliasKV &
  operator=(const SubtargetSubTypeAliasKV &Other) = delete;

  /// Return the CPU alias name string.
  ///
  /// \return Pointer to the CPU alias name string.
  const char *key() const {
    return reinterpret_cast<const char *>(this) + KeyStrOff;
  }

  /// Compare this alias key against \p S for ordered lookup.
  ///
  /// \param S String to compare against this entry's key.
  /// \return True if this entry's key is less than \p S.
  bool operator<(StringRef S) const { return StringRef(key()) < S; }
};

/// Storage for CPU/subtype entries, aliases, and their string table.
template <size_t NumSubTypes, size_t NumAliases, size_t SubTypeStrTabSize>
struct SubtargetSubTypeKVStorage {
  /// Array of CPU/subtype key-value entries.
  SubtargetSubTypeKV SubTypes[NumSubTypes];
  /// Array of CPU alias entries mapping names to \p SubTypes indices.
  std::array<SubtargetSubTypeAliasKV, NumAliases> Aliases;
  /// Contiguous string table referenced by relative offsets in the entries.
  char Strings[SubTypeStrTabSize];
};

//===----------------------------------------------------------------------===//
///
/// Generic base class for all target subtargets.
///
class LLVM_ABI MCSubtargetInfo {
  Triple TargetTriple;
  std::string CPU; // CPU being targeted.
  std::string TuneCPU; // CPU being tuned for.
  StringTable ProcNames; // Processor list, including aliases
  ArrayRef<SubtargetFeatureKV> ProcFeatures;  // Processor feature list
  ArrayRef<SubtargetSubTypeKV> ProcDesc;  // Processor descriptions
  ArrayRef<SubtargetSubTypeAliasKV> ProcAliases; // CPU alias -> processor map
  const MCSchedModel *ProcSchedModels;    ///< Processor scheduling models.

  // Scheduler machine model
  const MCWriteProcResEntry *WriteProcResTable;
  const MCWriteLatencyEntry *WriteLatencyTable;
  const MCReadAdvanceEntry *ReadAdvanceTable;
  const MCSchedModel *CPUSchedModel;

  const InstrStage *Stages;            // Instruction itinerary stages
  const unsigned *OperandCycles;       // Itinerary operand cycles
  const unsigned *ForwardingPaths;
  FeatureBitset FeatureBits;           // Feature bits for current CPU + FS
  std::string FeatureString;           // Feature string

public:
  /// Copy-construct subtarget info from another instance.
  ///
  /// \param Other Subtarget info to copy.
  MCSubtargetInfo(const MCSubtargetInfo &Other) = default;
  /// Construct subtarget info for a triple, CPU, and feature configuration.
  ///
  /// \param TT Target triple for this subtarget.
  /// \param CPU CPU name being targeted.
  /// \param TuneCPU CPU name being tuned for.
  /// \param FS Feature string of plus/minus feature flags.
  /// \param PN Processor name string table, including aliases.
  /// \param PF Processor feature key-value table.
  /// \param PD Processor/subtype description table.
  /// \param PA CPU alias to processor index map.
  /// \param PSM Array of processor scheduling models.
  /// \param WPR Write processor-resource table.
  /// \param WL Write latency table.
  /// \param RA Read advance table.
  /// \param IS Instruction itinerary stage table.
  /// \param OC Itinerary operand cycle table.
  /// \param FP Forwarding path table.
  MCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef TuneCPU,
                  StringRef FS, StringTable PN, ArrayRef<SubtargetFeatureKV> PF,
                  ArrayRef<SubtargetSubTypeKV> PD,
                  ArrayRef<SubtargetSubTypeAliasKV> PA, const MCSchedModel *PSM,
                  const MCWriteProcResEntry *WPR, const MCWriteLatencyEntry *WL,
                  const MCReadAdvanceEntry *RA, const InstrStage *IS,
                  const unsigned *OC, const unsigned *FP);
  /// Deleted default constructor; a triple and CPU tables are required.
  MCSubtargetInfo() = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other Unused; copy assignment is deleted.
  MCSubtargetInfo &operator=(const MCSubtargetInfo &Other) = delete;
  /// Deleted move assignment.
  ///
  /// \param Other Unused; move assignment is deleted.
  MCSubtargetInfo &operator=(MCSubtargetInfo &&Other) = delete;
  /// Destroy the subtarget info.
  virtual ~MCSubtargetInfo() = default;

  /// Return the target triple for this subtarget.
  ///
  /// \return The target triple for this subtarget.
  const Triple &getTargetTriple() const { return TargetTriple; }
  /// Return the CPU name being targeted.
  ///
  /// \return The CPU name being targeted.
  StringRef getCPU() const { return CPU; }
  /// Return the CPU name being tuned for.
  ///
  /// \return The CPU name being tuned for.
  StringRef getTuneCPU() const { return TuneCPU; }

  /// Return the feature bitset for the current CPU and feature string.
  ///
  /// \return The feature bitset for the current CPU and feature string.
  const FeatureBitset& getFeatureBits() const { return FeatureBits; }
  /// Replace the current feature bitset.
  ///
  /// \param FeatureBits_ New feature bits to install.
  void setFeatureBits(const FeatureBitset &FeatureBits_) {
    FeatureBits = FeatureBits_;
  }

  /// Return the feature string used to configure this subtarget.
  ///
  /// \return The feature string used to configure this subtarget.
  StringRef getFeatureString() const { return FeatureString; }

  /// Return true if feature \p Feature is enabled.
  ///
  /// \param Feature Feature bit index to test.
  /// \return True if feature \p Feature is enabled.
  bool hasFeature(unsigned Feature) const {
    return FeatureBits[Feature];
  }

protected:
  /// Initialize the scheduling model and feature bits.
  ///
  /// FIXME: Find a way to stick this in the constructor, since it should only
  /// be called during initialization.
  ///
  /// \param CPU CPU name being targeted.
  /// \param TuneCPU CPU name being tuned for.
  /// \param FS Feature string of plus/minus feature flags.
  void InitMCProcessorInfo(StringRef CPU, StringRef TuneCPU, StringRef FS);

public:
  /// Set the features to the default for the given CPU and TuneCPU, with ano
  /// appended feature string.
  ///
  /// \param CPU CPU name whose default features are applied.
  /// \param TuneCPU CPU name whose tune-implies features are applied.
  /// \param FS Additional feature string appended to the defaults.
  void setDefaultFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  /// Toggle a feature and return the re-computed feature bits.
  /// This version does not change the implied bits.
  ///
  /// \param FB Feature bit (as a uint64_t mask) to toggle.
  /// \return The re-computed feature bits after toggling \p FB.
  const FeatureBitset &ToggleFeature(uint64_t FB);

  /// Toggle a feature and return the re-computed feature bits.
  /// This version does not change the implied bits.
  ///
  /// \param FB Feature bits to toggle without updating implies.
  /// \return The re-computed feature bits after toggling \p FB.
  const FeatureBitset &ToggleFeature(const FeatureBitset &FB);

  /// Toggle a set of features and return the re-computed feature bits.
  /// This version will also change all implied bits.
  ///
  /// \param FS Feature string of plus/minus flags to toggle.
  /// \return The re-computed feature bits after toggling \p FS.
  const FeatureBitset &ToggleFeature(StringRef FS);

  /// Apply a feature flag and return the re-computed feature bits, including
  /// all feature bits implied by the flag.
  ///
  /// \param FS Feature flag string to apply.
  /// \return The re-computed feature bits after applying \p FS.
  const FeatureBitset &ApplyFeatureFlag(StringRef FS);

  /// Set/clear additional feature bits, including all other bits they imply.
  ///
  /// \param FB Feature bits to set transitively with their implies.
  /// \return The re-computed feature bits after setting \p FB.
  const FeatureBitset &SetFeatureBitsTransitively(const FeatureBitset &FB);
  /// Clear feature bits and all other bits they imply.
  ///
  /// \param FB Feature bits to clear transitively with their implies.
  /// \return The re-computed feature bits after clearing \p FB.
  const FeatureBitset &ClearFeatureBitsTransitively(const FeatureBitset &FB);

  /// Check whether the subtarget features are enabled/disabled as per
  /// the provided string, ignoring all other features.
  ///
  /// \param FS Feature string describing the expected enable/disable state.
  /// \return True if the listed features match the current enable/disable
  ///         state.
  bool checkFeatures(StringRef FS) const;

  /// Check whether the current subtarget satisfies a target feature expression.
  ///
  /// The expression uses feature names from the target's subtarget feature
  /// table. Comma means AND, | means OR, comma has higher precedence than |,
  /// and parentheses group expressions.
  ///
  /// \param FeatureExpr Feature expression string to evaluate.
  /// \return True if the current subtarget satisfies \p FeatureExpr.
  bool checkFeatureExpression(StringRef FeatureExpr) const;

  /// Get the machine model of a CPU.
  ///
  /// \param CPU CPU name whose scheduling model is requested.
  /// \return The scheduling model for \p CPU.
  const MCSchedModel &getSchedModelForCPU(StringRef CPU) const;

  /// Get the machine model for this subtarget's CPU.
  ///
  /// \return The scheduling model for this subtarget's CPU.
  const MCSchedModel &getSchedModel() const { return *CPUSchedModel; }

  /// Return an iterator at the first process resource consumed by the given
  /// scheduling class.
  ///
  /// \param SC Scheduling class whose write processor resources are iterated.
  /// \return Pointer to the first write processor-resource entry for \p SC.
  const MCWriteProcResEntry *getWriteProcResBegin(
    const MCSchedClassDesc *SC) const {
    return &WriteProcResTable[SC->WriteProcResIdx];
  }
  /// Return an iterator one past the last process resource for \p SC.
  ///
  /// \param SC Scheduling class whose write processor resources are iterated.
  /// \return Pointer one past the last write processor-resource entry for
  ///         \p SC.
  const MCWriteProcResEntry *getWriteProcResEnd(
    const MCSchedClassDesc *SC) const {
    return getWriteProcResBegin(SC) + SC->NumWriteProcResEntries;
  }

  /// Return the write-latency entry for definition \p DefIdx of \p SC.
  ///
  /// \param SC Scheduling class describing the instruction writes.
  /// \param DefIdx Definition operand index within the scheduling class.
  /// \return Pointer to the write-latency entry for \p DefIdx of \p SC.
  const MCWriteLatencyEntry *getWriteLatencyEntry(const MCSchedClassDesc *SC,
                                                  unsigned DefIdx) const {
    assert(DefIdx < SC->NumWriteLatencyEntries &&
           "MachineModel does not specify a WriteResource for DefIdx");

    return &WriteLatencyTable[SC->WriteLatencyIdx + DefIdx];
  }

  /// Return the read-advance cycle count for a use of \p SC.
  ///
  /// \param SC Scheduling class describing the instruction reads.
  /// \param UseIdx Use operand index within the scheduling class.
  /// \param WriteResID Write resource ID of the producing definition.
  /// \return Read-advance cycle count for the matching use, or zero if none.
  int getReadAdvanceCycles(const MCSchedClassDesc *SC, unsigned UseIdx,
                           unsigned WriteResID) const {
    // TODO: The number of read advance entries in a class can be significant
    // (~50). Consider compressing the WriteID into a dense ID of those that are
    // used by ReadAdvance and representing them as a bitset.
    for (const MCReadAdvanceEntry *I = &ReadAdvanceTable[SC->ReadAdvanceIdx],
           *E = I + SC->NumReadAdvanceEntries; I != E; ++I) {
      if (I->UseIdx < UseIdx)
        continue;
      if (I->UseIdx > UseIdx)
        break;
      // Find the first WriteResIdx match, which has the highest cycle count.
      if (!I->WriteResourceID || I->WriteResourceID == WriteResID) {
        return I->Cycles;
      }
    }
    return 0;
  }

  /// Return the set of ReadAdvance entries declared by the scheduling class
  /// descriptor in input.
  ///
  /// \param SC Scheduling class whose read-advance entries are returned.
  /// \return Array of read-advance entries for \p SC, or empty if none.
  ArrayRef<MCReadAdvanceEntry>
  getReadAdvanceEntries(const MCSchedClassDesc &SC) const {
    if (!SC.NumReadAdvanceEntries)
      return ArrayRef<MCReadAdvanceEntry>();
    return ArrayRef<MCReadAdvanceEntry>(&ReadAdvanceTable[SC.ReadAdvanceIdx],
                                        SC.NumReadAdvanceEntries);
  }

  /// Get scheduling itinerary of a CPU.
  ///
  /// \param CPU CPU name whose instruction itinerary is requested.
  /// \return Instruction itinerary data for \p CPU.
  InstrItineraryData getInstrItineraryForCPU(StringRef CPU) const;

  /// Initialize an InstrItineraryData instance.
  ///
  /// \param InstrItins Itinerary data object to initialize for this subtarget.
  void initInstrItins(InstrItineraryData &InstrItins) const;

  /// Resolve a variant scheduling class for the given MCInst and CPU.
  ///
  /// \param SchedClass Variant scheduling class index to resolve.
  /// \param MI Machine instruction whose operands may affect the class.
  /// \param MCII Instruction info used while resolving the variant.
  /// \param CPUID CPU identifier used to select the resolved class.
  /// \return Resolved scheduling class index, or zero by default.
  virtual unsigned resolveVariantSchedClass(unsigned SchedClass,
                                            const MCInst *MI,
                                            const MCInstrInfo *MCII,
                                            unsigned CPUID) const {
    return 0;
  }

  /// Look up the processor entry for a CPU name, resolving aliases. Returns
  /// nullptr if the name matches neither a processor nor an alias.
  ///
  /// \param CPU CPU or alias name to resolve.
  /// \return Pointer to the processor entry, or nullptr if not found.
  const SubtargetSubTypeKV *resolveCPU(StringRef CPU) const;

  /// Check whether the CPU string is valid.
  ///
  /// \param CPU CPU or alias name to validate.
  /// \return True if \p CPU names a known processor or alias.
  bool isCPUStringValid(StringRef CPU) const { return resolveCPU(CPU); }

  /// Return processor descriptions.
  ///
  /// \return Array of all processor/subtype descriptions.
  ArrayRef<SubtargetSubTypeKV> getAllProcessorDescriptions() const {
    return ProcDesc;
  }

  /// Return processor features.
  ///
  /// \return Array of all processor feature key-value entries.
  ArrayRef<SubtargetFeatureKV> getAllProcessorFeatures() const {
    return ProcFeatures;
  }

  /// Return the list of processor features currently enabled.
  ///
  /// \return Pointers to the currently enabled processor feature entries.
  std::vector<const SubtargetFeatureKV *> getEnabledProcessorFeatures() const;

  /// Identify which kind of hardware-mode ID to retrieve from a subtarget.
  ///
  /// HwMode IDs are stored and accessed in a bit set format, enabling
  /// users to efficiently retrieve specific IDs, such as the RegInfo
  /// HwMode ID, from the set as required. Using this approach, various
  /// types of HwMode IDs can be added to a subtarget to manage different
  /// attributes within that subtarget, significantly enhancing the
  /// scalability and usability of HwMode. Moreover, to ensure compatibility,
  /// this method also supports controlling multiple attributes with a single
  /// HwMode ID, just as was done previously.
  enum HwModeType {
    /// Return the smallest HwMode ID of the current subtarget.
    HwMode_Default,
    /// Return the HwMode ID that controls the ValueType.
    HwMode_ValueType,
    /// Return the HwMode ID that controls RegSizeInfo, SubRegRange, and
    /// RegisterClass.
    HwMode_RegInfo,
    /// Return the HwMode ID that controls the EncodingInfo.
    HwMode_EncodingInfo
  };

  /// Return a bit set containing all HwMode IDs of the current subtarget.
  ///
  /// \return Bit set of all HwMode IDs for this subtarget, or zero by default.
  virtual unsigned getHwModeSet() const { return 0; }

  /// Return the HwMode ID for the requested \p type from this subtarget.
  ///
  /// HwMode ID corresponding to the 'type' parameter is retrieved from the
  /// HwMode bit set of the current subtarget. It’s important to note that if
  /// the current subtarget possesses two HwMode IDs and both control a single
  /// attribute (such as RegInfo), this interface will result in an error.
  ///
  /// \param type Kind of hardware-mode ID to retrieve.
  /// \return HwMode ID of the requested \p type, or zero by default.
  virtual unsigned getHwMode(enum HwModeType type = HwMode_Default) const {
    return 0;
  }

  /// Return the cache size in bytes for the given level of cache.
  /// Level is zero-based, so a value of zero means the first level of
  /// cache.
  ///
  /// \param Level Zero-based cache hierarchy level.
  /// \return Cache size in bytes at \p Level, or nullopt if unknown.
  virtual std::optional<unsigned> getCacheSize(unsigned Level) const;

  /// Return the cache associatvity for the given level of cache.
  /// Level is zero-based, so a value of zero means the first level of
  /// cache.
  ///
  /// \param Level Zero-based cache hierarchy level.
  /// \return Cache associativity at \p Level, or nullopt if unknown.
  virtual std::optional<unsigned> getCacheAssociativity(unsigned Level) const;

  /// Return the target cache line size in bytes at a given level.
  ///
  /// \param Level Zero-based cache hierarchy level.
  /// \return Cache line size in bytes at \p Level, or nullopt if unknown.
  virtual std::optional<unsigned> getCacheLineSize(unsigned Level) const;

  /// Return the target cache line size in bytes.
  ///
  /// By default, return the line size for the bottom-most level of cache.
  /// This provides a more convenient interface for the common case where all
  /// cache levels have the same line size. Return zero if there is no cache
  /// model.
  ///
  /// \return Cache line size in bytes, or zero if there is no cache model.
  virtual unsigned getCacheLineSize() const {
    std::optional<unsigned> Size = getCacheLineSize(0);
    if (Size)
      return *Size;

    return 0;
  }

  /// Return the preferred prefetch distance in terms of instructions.
  ///
  /// \return Preferred prefetch distance in instructions.
  virtual unsigned getPrefetchDistance() const;

  /// Return the maximum prefetch distance in terms of loop
  /// iterations.
  ///
  /// \return Maximum prefetch distance in loop iterations.
  virtual unsigned getMaxPrefetchIterationsAhead() const;

  /// Return true if prefetching should also be done for writes.
  ///
  /// \return True if write prefetching should be enabled.
  virtual bool enableWritePrefetching() const;

  /// Return the minimum stride necessary to trigger software
  /// prefetching.
  ///
  /// \param NumMemAccesses Number of memory accesses in the region.
  /// \param NumStridedMemAccesses Number of strided memory accesses.
  /// \param NumPrefetches Number of prefetches already planned.
  /// \param HasCall True if the region contains a call.
  /// \return Minimum stride that should trigger software prefetching.
  virtual unsigned getMinPrefetchStride(unsigned NumMemAccesses,
                                        unsigned NumStridedMemAccesses,
                                        unsigned NumPrefetches,
                                        bool HasCall) const;

  /// Return true if the target wants to issue a prefetch in address space
  /// \p AS.
  ///
  /// \param AS Address space identifier to consider for prefetching.
  /// \return True if prefetching should be issued in address space \p AS.
  virtual bool shouldPrefetchAddressSpace(unsigned AS) const;
};

} // end namespace llvm

#endif // LLVM_MC_MCSUBTARGETINFO_H
