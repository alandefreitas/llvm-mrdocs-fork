//===- InstrProfCorrelator.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file defines InstrProfCorrelator used to generate PGO/coverage profiles
// from raw profile data and debug info/binary file.
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_INSTRPROFCORRELATOR_H
#define LLVM_PROFILEDATA_INSTRPROFCORRELATOR_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Debuginfod/BuildIDFetcher.h"
#include "llvm/Object/BuildID.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/YAMLTraits.h"
#include <optional>
#include <vector>

namespace llvm {
class DWARFDie;
namespace object {
class ObjectFile;
}

/// InstrProfCorrelator - A base class used to create raw instrumentation data
/// to their functions.
class InstrProfCorrelator {
public:
  /// Indicate if we should use the debug info or profile metadata sections to
  /// correlate.
  enum ProfCorrelatorKind {
    /// Do not correlate profiles.
    NONE,
    /// Correlate using DWARF debug info.
    DEBUG_INFO,
    /// Correlate using profile metadata in the binary.
    BINARY
  };

  /// Construct a correlator for the object file at \p Filename.
  /// \param Filename Path to the object file used for correlation.
  /// \param FileKind Kind of correlation data to read from the object.
  /// \param BIDFetcher Optional build-ID fetcher used to resolve the binary.
  /// \param BIs Build IDs from the profile used with \p BIDFetcher.
  /// \return The correlator on success, or an error if construction fails.
  LLVM_ABI static llvm::Expected<std::unique_ptr<InstrProfCorrelator>>
  get(StringRef Filename, ProfCorrelatorKind FileKind,
      const object::BuildIDFetcher *BIDFetcher = nullptr,
      const ArrayRef<llvm::object::BuildID> BIs = {});

  /// Construct a ProfileData vector used to correlate raw instrumentation data
  /// to their functions.
  /// \param MaxWarnings the maximum number of warnings to emit (0 = no limit)
  /// \return Success, or an error if correlation fails.
  virtual Error correlateProfileData(int MaxWarnings) = 0;

  /// Process debug info and dump the correlation data.
  /// \param MaxWarnings the maximum number of warnings to emit (0 = no limit)
  /// \param OS Output stream that receives the YAML dump.
  /// \return Success, or an error if dumping fails.
  virtual Error dumpYaml(int MaxWarnings, raw_ostream &OS) = 0;

  /// Return the number of ProfileData elements.
  /// \return Number of ProfileData elements, or nullopt if unavailable.
  LLVM_ABI std::optional<size_t> getDataSize() const;

  /// Return a pointer to the names string that this class constructs.
  /// \return Pointer to the constructed names string.
  const char *getNamesPointer() const { return Names.c_str(); }

  /// Return the number of bytes in the names string.
  /// \return Size of the names string in bytes.
  size_t getNamesSize() const { return Names.size(); }

  /// Return the size of the counters section in bytes.
  /// \return Size of the counters section in bytes.
  uint64_t getCountersSectionSize() const {
    return Ctx->CountersSectionEnd - Ctx->CountersSectionStart;
  }

  /// DWARF annotation name for the instrumented function's name.
  LLVM_ABI static const char *FunctionNameAttributeName;
  /// DWARF annotation name for the function CFG hash.
  LLVM_ABI static const char *CFGHashAttributeName;
  /// DWARF annotation name for the number of counters.
  LLVM_ABI static const char *NumCountersAttributeName;
  /// DWARF annotation name for the number of bitmap bits.
  LLVM_ABI static const char *NumBitmapBitsAttributeName;

  /// Pointer width of a concrete InstrProfCorrelatorImpl specialization.
  enum InstrProfCorrelatorKind {
    /// Correlator for 32-bit profile pointer values.
    CK_32Bit,
    /// Correlator for 64-bit profile pointer values.
    CK_64Bit
  };
  /// Return whether this correlator uses 32-bit or 64-bit pointers.
  /// \return The pointer-width kind of this correlator.
  InstrProfCorrelatorKind getKind() const { return Kind; }
  /// Destroy the correlator.
  virtual ~InstrProfCorrelator() = default;

protected:
  /// Object-file context used while correlating raw profile data.
  struct Context {
    /// Build a correlation context from \p Buffer and object file \p Obj.
    /// \param Buffer Memory buffer owning the object file contents.
    /// \param Obj Parsed object file used to locate profile sections.
    /// \param FileKind Kind of correlation data to extract from \p Obj.
    /// \return The context on success, or an error if construction fails.
    LLVM_ABI static llvm::Expected<std::unique_ptr<Context>>
    get(std::unique_ptr<MemoryBuffer> Buffer, object::ObjectFile &Obj,
        ProfCorrelatorKind FileKind);
    /// Memory buffer that owns the object file bytes.
    std::unique_ptr<MemoryBuffer> Buffer;
    /// The address range of the __llvm_prf_cnts section.
    uint64_t CountersSectionStart;
    /// Exclusive end address of the __llvm_prf_cnts section.
    uint64_t CountersSectionEnd;
    /// The address range of the __llvm_prf_bits section.
    uint64_t BitmapSectionStart;
    /// Exclusive end address of the __llvm_prf_bits section.
    uint64_t BitmapSectionEnd;
    /// The pointer points to start/end of profile data/name sections if
    /// FileKind is Binary.
    const char *DataStart;
    /// Pointer past the end of the profile data section when FileKind is Binary.
    const char *DataEnd;
    /// Pointer to the start of the profile names section when FileKind is Binary.
    const char *NameStart;
    /// Size in bytes of the profile names section when FileKind is Binary.
    size_t NameSize;
    /// Mach-O linker fixup chain resolutions for Binary correlation.
    ///
    /// Present when FileKind is Binary. The mapping is from an address relative
    /// to the start of __llvm_covdata to the resolved pointer value at that
    /// address.
    llvm::DenseMap<uint64_t, uint64_t> MachOFixups;
    /// True if target and host have different endian orders.
    bool ShouldSwapBytes;
  };
  /// Correlation context for the object file being processed.
  const std::unique_ptr<Context> Ctx;

  /// Construct a correlator of kind \p K with context \p Ctx.
  /// \param K Pointer-width kind of this correlator.
  /// \param Ctx Object-file context used during correlation.
  InstrProfCorrelator(InstrProfCorrelatorKind K, std::unique_ptr<Context> Ctx)
      : Ctx(std::move(Ctx)), Kind(K) {}

  /// Concatenated function names string constructed during correlation.
  std::string Names;
  /// Individual function names collected before building \c Names.
  std::vector<std::string> NamesVec;

  /// Probe metadata describing one instrumented function for YAML dumps.
  struct Probe {
    /// Demangled or source-level function name.
    std::string FunctionName;
    /// Optional mangled linkage name of the function.
    std::optional<std::string> LinkageName;
    /// Hash of the function control-flow graph.
    yaml::Hex64 CFGHash;
    /// Offset of the function's counters within the counters section.
    yaml::Hex64 CounterOffset;
    /// Offset of the function's bitmap within the bitmap section.
    yaml::Hex64 BitmapOffset;
    /// Number of counters associated with the function.
    uint32_t NumCounters;
    /// Number of bitmap bytes associated with the function.
    uint32_t NumBitmapBytes;
    /// Optional source file path for the probe.
    std::optional<std::string> FilePath;
    /// Optional source line number for the probe.
    std::optional<int> LineNumber;
  };

  /// Collection of probes produced while dumping correlation data as YAML.
  struct CorrelationData {
    /// Probes discovered during correlation.
    std::vector<Probe> Probes;
  };

  friend struct yaml::MappingTraits<Probe>;
  friend struct yaml::SequenceElementTraits<Probe>;
  friend struct yaml::MappingTraits<CorrelationData>;

private:
  static llvm::Expected<std::unique_ptr<InstrProfCorrelator>>
  get(std::unique_ptr<MemoryBuffer> Buffer, ProfCorrelatorKind FileKind);

  const InstrProfCorrelatorKind Kind;
};

/// InstrProfCorrelatorImpl - A child of InstrProfCorrelator with a template
/// pointer type so that the ProfileData vector can be materialized.
template <class IntPtrT>
class InstrProfCorrelatorImpl : public InstrProfCorrelator {
public:
  /// Construct an implementation correlator with context \p Ctx.
  /// \param Ctx Object-file context used during correlation.
  InstrProfCorrelatorImpl(std::unique_ptr<InstrProfCorrelator::Context> Ctx);
  /// Return true if \p C is an InstrProfCorrelatorImpl of this pointer width.
  /// \param C Correlator instance to test.
  /// \return True if \p C is an InstrProfCorrelatorImpl of this pointer width.
  static bool classof(const InstrProfCorrelator *C);

  /// Return a pointer to the underlying ProfileData vector that this class
  /// constructs.
  /// \return Pointer to the ProfileData vector, or nullptr if empty.
  const RawInstrProf::ProfileData<IntPtrT> *getDataPointer() const {
    return Data.empty() ? nullptr : Data.data();
  }

  /// Return the number of ProfileData elements.
  /// \return Number of ProfileData elements.
  size_t getDataSize() const { return Data.size(); }

  /// Construct a correlator implementation for \p Obj using context \p Ctx.
  /// \param Ctx Object-file context used during correlation.
  /// \param Obj Object file that supplies correlation metadata.
  /// \param FileKind Kind of correlation data to read from \p Obj.
  /// \return The correlator on success, or an error if construction fails.
  static llvm::Expected<std::unique_ptr<InstrProfCorrelatorImpl<IntPtrT>>>
  get(std::unique_ptr<InstrProfCorrelator::Context> Ctx,
      const object::ObjectFile &Obj, ProfCorrelatorKind FileKind);

protected:
  /// ProfileData records materialized during correlation.
  std::vector<RawInstrProf::ProfileData<IntPtrT>> Data;

  /// Correlate raw instrumentation data into the ProfileData vector.
  /// \param MaxWarnings the maximum number of warnings to emit (0 = no limit)
  /// \return Success, or an error if correlation fails.
  Error correlateProfileData(int MaxWarnings) override;
  /// Correlate profile data using the backend-specific probe source.
  /// \param MaxWarnings the maximum number of warnings to emit (0 = no limit)
  /// \param Data If non-null, also populate with probe metadata for dumping.
  virtual void correlateProfileDataImpl(
      int MaxWarnings,
      InstrProfCorrelator::CorrelationData *Data = nullptr) = 0;

  /// Correlate and build the function names string for this profile.
  /// \return Success, or an error if name correlation fails.
  virtual Error correlateProfileNameImpl() = 0;

  /// Dump correlated probe metadata as YAML.
  /// \param MaxWarnings the maximum number of warnings to emit (0 = no limit)
  /// \param OS Output stream that receives the YAML dump.
  /// \return Success, or an error if dumping fails.
  Error dumpYaml(int MaxWarnings, raw_ostream &OS) override;

  /// Append a ProfileData record for one instrumented function probe.
  /// \param FunctionName Name reference (hash) of the instrumented function.
  /// \param CFGHash Hash of the function control-flow graph.
  /// \param CounterOffset Section-relative address of the function counters.
  /// \param BitmapOffset Section-relative address of the function bitmap.
  /// \param FunctionPtr Address of the instrumented function, if available.
  /// \param NumCounters Number of counters for the function.
  /// \param NumBitmapBytes Number of bitmap bytes for the function.
  void addDataProbe(uint64_t FunctionName, uint64_t CFGHash,
                    IntPtrT CounterOffset, IntPtrT BitmapOffset,
                    IntPtrT FunctionPtr, uint32_t NumCounters,
                    uint32_t NumBitmapBytes);

  /// Byte-swap \p Value when the profile and host endianness differ.
  /// \param Value Value that may need byte-swapping.
  /// \return \p Value, byte-swapped when endianness differs.
  template <class T> T maybeSwap(T Value) const {
    return Ctx->ShouldSwapBytes ? llvm::byteswap(Value) : Value;
  }

private:
  InstrProfCorrelatorImpl(InstrProfCorrelatorKind Kind,
                          std::unique_ptr<InstrProfCorrelator::Context> Ctx)
      : InstrProfCorrelator(Kind, std::move(Ctx)){};
  llvm::DenseSet<IntPtrT> CounterOffsets;
  llvm::DenseSet<IntPtrT> BitmapOffsets;
};

/// DwarfInstrProfCorrelator - A child of InstrProfCorrelatorImpl that takes
/// DWARF debug info as input to correlate profiles.
template <class IntPtrT>
class DwarfInstrProfCorrelator : public InstrProfCorrelatorImpl<IntPtrT> {
public:
  /// Construct a DWARF-based correlator from \p DICtx and \p Ctx.
  /// \param DICtx DWARF context that supplies instrumentation probe DIEs.
  /// \param Ctx Object-file context used during correlation.
  DwarfInstrProfCorrelator(std::unique_ptr<DWARFContext> DICtx,
                           std::unique_ptr<InstrProfCorrelator::Context> Ctx)
      : InstrProfCorrelatorImpl<IntPtrT>(std::move(Ctx)),
        DICtx(std::move(DICtx)) {}

private:
  std::unique_ptr<DWARFContext> DICtx;

  /// Return the address of the object that the provided DIE symbolizes.
  std::optional<uint64_t> getLocation(const DWARFDie &Die) const;

  /// Returns true if the provided DIE symbolizes an instrumentation probe
  /// symbol of the necessary type.
  static bool isDIEOfProbe(const DWARFDie &Die, StringRef Prefix);

  std::optional<std::pair<InstrProfCorrelator::Probe, IntPtrT>>
  addCountersToDataProbe(const DWARFDie &Die, const bool UnlimitedWarnings,
                         int &NumSuppressedWarnings);

  std::optional<std::pair<InstrProfCorrelator::Probe, IntPtrT>>
  addBitmapToDataProbe(const DWARFDie &Die, const bool UnlimitedWarnings,
                       int &NumSuppressedWarnings);

  /// Iterate over DWARF DIEs to find those that symbolize instrumentation
  /// probes and construct the ProfileData vector and Names string.
  ///
  /// Here is some example DWARF for an instrumentation probe we are looking
  /// for:
  /// \code
  ///   DW_TAG_subprogram
  ///   DW_AT_low_pc	(0x0000000000000000)
  ///   DW_AT_high_pc	(0x0000000000000014)
  ///   DW_AT_name	("foo")
  ///     DW_TAG_variable
  ///       DW_AT_name	("__profc_foo")
  ///       DW_AT_location	(DW_OP_addr 0x0)
  ///       DW_TAG_LLVM_annotation
  ///         DW_AT_name	("Function Name")
  ///         DW_AT_const_value	("foo")
  ///       DW_TAG_LLVM_annotation
  ///         DW_AT_name	("CFG Hash")
  ///         DW_AT_const_value	(12345678)
  ///       DW_TAG_LLVM_annotation
  ///         DW_AT_name	("Num Counters")
  ///         DW_AT_const_value	(2)
  ///       NULL
  ///     NULL
  /// \endcode
  /// \param MaxWarnings the maximum number of warnings to emit (0 = no limit)
  /// \param Data if provided, populate with the correlation data found
  void correlateProfileDataImpl(
      int MaxWarnings,
      InstrProfCorrelator::CorrelationData *Data = nullptr) override;

  Error correlateProfileNameImpl() override;
};

/// BinaryInstrProfCorrelator - A child of InstrProfCorrelatorImpl that
/// takes an object file as input to correlate profiles.
template <class IntPtrT>
class BinaryInstrProfCorrelator : public InstrProfCorrelatorImpl<IntPtrT> {
public:
  /// Construct a binary-metadata correlator with context \p Ctx.
  /// \param Ctx Object-file context used during correlation.
  BinaryInstrProfCorrelator(std::unique_ptr<InstrProfCorrelator::Context> Ctx)
      : InstrProfCorrelatorImpl<IntPtrT>(std::move(Ctx)) {}

  /// Return a pointer to the names string that this class constructs.
  /// \return Pointer to the names string in the binary.
  const char *getNamesPointer() const { return this->Ctx.NameStart; }

  /// Return the number of bytes in the names string.
  /// \return Size of the names string in bytes.
  size_t getNamesSize() const { return this->Ctx.NameSize; }

private:
  void correlateProfileDataImpl(
      int MaxWarnings,
      InstrProfCorrelator::CorrelationData *Data = nullptr) override;

  Error correlateProfileNameImpl() override;
};

} // end namespace llvm

#endif // LLVM_PROFILEDATA_INSTRPROFCORRELATOR_H
