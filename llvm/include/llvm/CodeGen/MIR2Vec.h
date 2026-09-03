//===- MIR2Vec.h - Implementation of MIR2Vec ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See the LICENSE file for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the MIR2Vec framework for generating Machine IR
/// embeddings.
///
/// Design Overview:
/// ----------------------
/// 1. MIR2VecVocabProvider - Core vocabulary loading logic (no PM dependency)
///    - Can be used standalone or wrapped by the pass manager
///    - Requires MachineModuleInfo with parsed machine functions
///
/// 2. MIR2VecVocabLegacyAnalysis - Pass manager wrapper (ImmutablePass)
///    - Integrated and used by llc -print-mir2vec
///
/// 3. MIREmbedder - Generates embeddings from vocabulary
///    - SymbolicMIREmbedder: MIR2Vec embedding implementation
///
/// MIR2Vec extends IR2Vec to support Machine IR embeddings. It represents the
/// LLVM Machine IR as embeddings which can be used as input to machine learning
/// algorithms.
///
/// The original idea of MIR2Vec is described in the following paper:
///
/// RL4ReAl: Reinforcement Learning for Register Allocation. S. VenkataKeerthy,
/// Siddharth Jain, Anilava Kundu, Rohit Aggarwal, Albert Cohen, and Ramakrishna
/// Upadrasta. 2023. RL4ReAl: Reinforcement Learning for Register Allocation.
/// Proceedings of the 32nd ACM SIGPLAN International Conference on Compiler
/// Construction (CC 2023). https://doi.org/10.1145/3578360.3580273.
/// https://arxiv.org/abs/2204.02013
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MIR2VEC_H
#define LLVM_CODEGEN_MIR2VEC_H

#include "llvm/Analysis/IR2Vec.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include <map>
#include <optional>
#include <set>
#include <string>

namespace llvm {

class Module;
class raw_ostream;
class LLVMContext;
class MIR2VecVocabLegacyAnalysis;
class TargetInstrInfo;

/// Kind of MIR2Vec embedding algorithm to use.
enum class MIR2VecKind {
  Symbolic ///< Symbolic entity-level MIR embeddings.
};

/// MIR2Vec embedding helpers, vocabulary, and embedder types.
namespace mir2vec {

// Forward declarations
class MIREmbedder;
class SymbolicMIREmbedder;

/// Command-line option category for MIR2Vec-related flags.
LLVM_ABI extern llvm::cl::OptionCategory MIR2VecCategory;
/// Weight applied to opcode embeddings when combining entity vectors.
LLVM_ABI extern cl::opt<float> OpcWeight;
/// Weight applied to common (non-register) operand embeddings.
LLVM_ABI extern cl::opt<float> CommonOperandWeight;
/// Weight applied to register operand embeddings.
LLVM_ABI extern cl::opt<float> RegOperandWeight;

/// Embedding vector type shared with IR2Vec.
using Embedding = ir2vec::Embedding;
/// Map from machine instructions to their embeddings.
using MachineInstEmbeddingsMap = DenseMap<const MachineInstr *, Embedding>;
/// Map from machine basic blocks to their embeddings.
using MachineBlockEmbeddingsMap =
    DenseMap<const MachineBasicBlock *, Embedding>;

/// Class for storing and accessing the MIR2Vec vocabulary.
/// The MIRVocabulary class manages seed embeddings for LLVM Machine IR
class MIRVocabulary {
  friend class llvm::MIR2VecVocabLegacyAnalysis;
  using VocabMap = std::map<std::string, ir2vec::Embedding>;

  // MIRVocabulary Layout:
  // +-------------------+-----------------------------------------------------+
  // | Entity Type       | Description                                         |
  // +-------------------+-----------------------------------------------------+
  // | 1. Opcodes        | Target specific opcodes derived from TII, grouped   |
  // |                   | by instruction semantics.                           |
  // | 2. Common Operands| All common operand types, except register operands, |
  // |                   | defined by MachineOperand::MachineOperandType enum. |
  // | 3. Physical       | Register classes defined by the target, specialized |
  // |    Reg classes    | by physical registers.                              |
  // | 4. Virtual        | Register classes defined by the target, specialized |
  // |    Reg classes    | by virtual and physical registers.                  |
  // +-------------------+-----------------------------------------------------+

  /// Layout information for the MIR vocabulary. Defines the starting index
  /// and size of each section in the vocabulary.
  struct {
    size_t OpcodeBase = 0;
    size_t CommonOperandBase = 0;
    size_t PhyRegBase = 0;
    size_t VirtRegBase = 0;
    size_t TotalEntries = 0;
  } Layout;

  // TODO: See if we can have only one reg classes section instead of physical
  // and virtual separate sections in the vocabulary. This would reduce the
  // number of vocabulary entities significantly.
  // We can potentially distinguish physical and virtual registers by
  // considering them as a separate feature.
  enum class Section : unsigned {
    Opcodes = 0,
    CommonOperands = 1,
    PhyRegisters = 2,
    VirtRegisters = 3,
    MaxSections
  };

  ir2vec::VocabStorage Storage;
  std::set<std::string> UniqueBaseOpcodeNames;
  SmallVector<std::string, 24> RegisterOperandNames;

  // Some instructions have optional register operands that may be NoRegister.
  // We return a zero vector in such cases.
  Embedding ZeroEmbedding;

  // We have specialized MO_Register handling in the Register operand section,
  // so we don't include it here. Also, no MO_DbgInstrRef for now.
  static constexpr StringLiteral CommonOperandNames[] = {
      "Immediate",       "CImmediate",        "FPImmediate",  "MBB",
      "FrameIndex",      "ConstantPoolIndex", "TargetIndex",  "JumpTableIndex",
      "ExternalSymbol",  "GlobalAddress",     "BlockAddress", "RegisterMask",
      "RegisterLiveOut", "Metadata",          "MCSymbol",     "CFIIndex",
      "IntrinsicID",     "Predicate",         "ShuffleMask",  "LaneMask"};
  static_assert(std::size(CommonOperandNames) == MachineOperand::MO_Last - 1 &&
                "Common operand names size changed, update accordingly");

  const TargetInstrInfo &TII;
  const TargetRegisterInfo &TRI;
  const MachineRegisterInfo &MRI;

  void generateStorage(const VocabMap &OpcodeMap,
                       const VocabMap &CommonOperandMap,
                       const VocabMap &PhyRegMap, const VocabMap &VirtRegMap);
  void buildCanonicalOpcodeMapping();
  void buildRegisterOperandMapping();

  /// Get canonical index for a machine opcode
  LLVM_ABI unsigned getCanonicalOpcodeIndex(unsigned Opcode) const;

  /// Get index for a common (non-register) machine operand
  LLVM_ABI unsigned
  getCommonOperandIndex(MachineOperand::MachineOperandType OperandType) const;

  /// Get index for a register machine operand. Returns std::nullopt if Reg
  /// belongs to no register class, which is a valid outcome for some target
  /// physical registers.
  LLVM_ABI std::optional<unsigned> getRegisterOperandIndex(Register Reg) const;

  // Accessors for operand types
  const Embedding &
  operator[](MachineOperand::MachineOperandType OperandType) const {
    unsigned LocalIndex = getCommonOperandIndex(OperandType);
    return Storage[static_cast<unsigned>(Section::CommonOperands)][LocalIndex];
  }

  const Embedding &operator[](Register Reg) const {
    // Reg is sometimes NoRegister (0) for optional operands. We return a zero
    // vector in this case.
    if (!Reg.isValid())
      return ZeroEmbedding;
    // TODO: Implement proper stack slot handling for MIR2Vec embeddings.
    // Stack slots represent frame indices and should have their own
    // embedding strategy rather than defaulting to register class 0.
    // Consider: 1) Separate vocabulary section for stack slots
    //          2) Stack slot size/alignment based embeddings
    //          3) Frame index based categorization
    if (Reg.isStack())
      return ZeroEmbedding;

    // Registers that belong to no register class have no vocabulary entry;
    // treat them like the other unmapped cases above.
    std::optional<unsigned> LocalIndex = getRegisterOperandIndex(Reg);
    if (!LocalIndex)
      return ZeroEmbedding;
    auto SectionID =
        Reg.isPhysical() ? Section::PhyRegisters : Section::VirtRegisters;
    return Storage[static_cast<unsigned>(SectionID)][*LocalIndex];
  }

  /// Get entity ID (flat index) for a common operand type
  /// This is used for triplet generation
  unsigned getEntityIDForCommonOperand(
      MachineOperand::MachineOperandType OperandType) const {
    return Layout.CommonOperandBase + getCommonOperandIndex(OperandType);
  }

  /// Get entity ID (flat index) for a register
  /// This is used for triplet generation
  unsigned getEntityIDForRegister(Register Reg) const {
    if (!Reg.isValid() || Reg.isStack())
      return Layout
          .VirtRegBase; // Return VirtRegBase for invalid/stack registers
    std::optional<unsigned> LocalIndex = getRegisterOperandIndex(Reg);
    // Registers without a register class share the invalid/stack fallback.
    if (!LocalIndex)
      return Layout.VirtRegBase;
    size_t BaseOffset =
        Reg.isPhysical() ? Layout.PhyRegBase : Layout.VirtRegBase;
    return BaseOffset + *LocalIndex;
  }

public:
  /// Static method for extracting base opcode names (public for testing)
  ///
  /// \param InstrName Full instruction name from which the base opcode is
  ///        extracted.
  /// \return The extracted base opcode name.
  LLVM_ABI static std::string extractBaseOpcodeName(StringRef InstrName);

  /// Get indices from opcode or operand names. These are public for testing.
  /// String based lookups are inefficient and should be avoided in general.
  ///
  /// \param BaseName Canonical base opcode name to look up.
  /// \return The canonical vocabulary index for \p BaseName.
  LLVM_ABI unsigned getCanonicalIndexForBaseName(StringRef BaseName) const;
  /// Return the canonical vocabulary index for a common operand name.
  ///
  /// \param OperandName Operand type name to look up.
  /// \return The canonical vocabulary index for \p OperandName.
  LLVM_ABI unsigned
  getCanonicalIndexForOperandName(StringRef OperandName) const;
  /// Return the canonical vocabulary index for a register class name.
  ///
  /// \param RegName Register class name to look up.
  /// \param IsPhysical True to search physical register classes; false for
  ///        virtual.
  /// \return The canonical vocabulary index for \p RegName.
  LLVM_ABI unsigned
  getCanonicalIndexForRegisterClass(StringRef RegName,
                                    bool IsPhysical = true) const;

  /// Get the string key for a vocabulary entry at the given position
  ///
  /// \param Pos Flat vocabulary position whose key is returned.
  /// \return The string key for the entry at \p Pos.
  LLVM_ABI std::string getStringKey(unsigned Pos) const;

  /// Return the embedding dimension used by this vocabulary.
  ///
  /// \return The embedding vector dimension.
  unsigned getDimension() const { return Storage.getDimension(); }

  /// Get entity ID (flat index) for an opcode
  /// This is used for triplet generation
  ///
  /// \param Opcode Machine opcode whose flat entity ID is returned.
  /// \return The flat vocabulary entity ID for \p Opcode.
  unsigned getEntityIDForOpcode(unsigned Opcode) const {
    return Layout.OpcodeBase + getCanonicalOpcodeIndex(Opcode);
  }

  /// Get entity ID (flat index) for a machine operand
  /// This is used for triplet generation
  ///
  /// \param MO Machine operand whose flat entity ID is returned.
  /// \return The flat vocabulary entity ID for \p MO.
  unsigned getEntityIDForMachineOperand(const MachineOperand &MO) const {
    if (MO.getType() == MachineOperand::MO_Register)
      return getEntityIDForRegister(MO.getReg());
    return getEntityIDForCommonOperand(MO.getType());
  }

  /// Return the seed embedding for machine opcode \p Opcode.
  ///
  /// \param Opcode Machine opcode to look up.
  /// \return The seed embedding for \p Opcode.
  const Embedding &operator[](unsigned Opcode) const {
    unsigned LocalIndex = getCanonicalOpcodeIndex(Opcode);
    return Storage[static_cast<unsigned>(Section::Opcodes)][LocalIndex];
  }

  /// Return the seed embedding for machine operand \p Operand.
  ///
  /// \param Operand Machine operand whose embedding is returned.
  /// \return The seed embedding for \p Operand.
  const Embedding &operator[](MachineOperand Operand) const {
    auto OperandType = Operand.getType();
    if (OperandType == MachineOperand::MO_Register)
      return operator[](Operand.getReg());
    else
      return operator[](OperandType);
  }

  /// Const iterator over vocabulary embeddings.
  using const_iterator = ir2vec::VocabStorage::const_iterator;
  /// Return an iterator to the first vocabulary embedding.
  ///
  /// \return A const iterator to the first embedding.
  const_iterator begin() const { return Storage.begin(); }

  /// Return an iterator past the last vocabulary embedding.
  ///
  /// \return A const iterator one past the last embedding.
  const_iterator end() const { return Storage.end(); }

  /// Default construction is deleted; use create() or createDummyVocabForTest().
  MIRVocabulary() = delete;

  /// Factory method to create MIRVocabulary from vocabulary map
  ///
  /// \param OpcMap Opcode name to embedding map.
  /// \param CommonOperandsMap Common operand name to embedding map.
  /// \param PhyRegMap Physical register-class name to embedding map.
  /// \param VirtRegMap Virtual register-class name to embedding map.
  /// \param TII Target instruction info used to map opcodes.
  /// \param TRI Target register info used to map register classes.
  /// \param MRI Machine register info for the current function.
  /// \return The constructed MIRVocabulary, or an error on failure.
  LLVM_ABI static Expected<MIRVocabulary>
  create(VocabMap &&OpcMap, VocabMap &&CommonOperandsMap, VocabMap &&PhyRegMap,
         VocabMap &&VirtRegMap, const TargetInstrInfo &TII,
         const TargetRegisterInfo &TRI, const MachineRegisterInfo &MRI);

  /// Create a dummy vocabulary for testing purposes.
  ///
  /// \param TII Target instruction info used to map opcodes.
  /// \param TRI Target register info used to map register classes.
  /// \param MRI Machine register info for the current function.
  /// \param Dim Embedding dimension for the dummy vocabulary.
  /// \return A dummy MIRVocabulary suitable for tests, or an error on failure.
  LLVM_ABI static Expected<MIRVocabulary>
  createDummyVocabForTest(const TargetInstrInfo &TII,
                          const TargetRegisterInfo &TRI,
                          const MachineRegisterInfo &MRI, unsigned Dim = 1);

  /// Total number of entries in the vocabulary
  ///
  /// \return The number of entries in the vocabulary storage.
  size_t getCanonicalSize() const { return Storage.size(); }

private:
  MIRVocabulary(VocabMap &&OpcMap, VocabMap &&CommonOperandsMap,
                VocabMap &&PhyRegMap, VocabMap &&VirtRegMap,
                const TargetInstrInfo &TII, const TargetRegisterInfo &TRI,
                const MachineRegisterInfo &MRI);
};

/// Base class for MIR embedders
class MIREmbedder {
protected:
  /// Machine function whose embeddings are computed.
  const MachineFunction &MF;
  /// Vocabulary providing seed embeddings for MIR entities.
  const MIRVocabulary &Vocab;

  /// Dimension of the embeddings; Captured from the vocabulary
  const unsigned Dimension;

  /// Weight for opcode embeddings
  const float OpcWeight, CommonOperandWeight, RegOperandWeight;

  /// Construct an embedder for machine function \p MF using vocabulary \p Vocab.
  ///
  /// \param MF Machine function to embed.
  /// \param Vocab MIR2Vec vocabulary providing seed embeddings.
  MIREmbedder(const MachineFunction &MF, const MIRVocabulary &Vocab)
      : MF(MF), Vocab(Vocab), Dimension(Vocab.getDimension()),
        OpcWeight(mir2vec::OpcWeight),
        CommonOperandWeight(mir2vec::CommonOperandWeight),
        RegOperandWeight(mir2vec::RegOperandWeight) {}

  /// Function to compute embeddings.
  ///
  /// \return The embedding vector for the current machine function.
  LLVM_ABI Embedding computeEmbeddings() const;

  /// Function to compute the embedding for a given machine basic block.
  ///
  /// \param MBB Machine basic block whose embedding is computed.
  /// \return The embedding vector for \p MBB.
  LLVM_ABI Embedding computeEmbeddings(const MachineBasicBlock &MBB) const;

  /// Function to compute the embedding for a given machine instruction.
  /// Specific to the kind of embeddings being computed.
  ///
  /// \param MI Machine instruction whose embedding is computed.
  /// \return The embedding vector for \p MI.
  virtual Embedding computeEmbeddings(const MachineInstr &MI) const = 0;

public:
  /// Destroy this MIR embedder.
  virtual ~MIREmbedder() = default;

  /// Factory method to create an Embedder object of the specified kind
  /// Returns nullptr if the requested kind is not supported.
  ///
  /// \param Mode Embedding algorithm kind to instantiate.
  /// \param MF Machine function to embed.
  /// \param Vocab MIR2Vec vocabulary providing seed embeddings.
  /// \return A unique pointer to the created embedder, or nullptr if \p Mode
  ///         is unsupported.
  LLVM_ABI static std::unique_ptr<MIREmbedder>
  create(MIR2VecKind Mode, const MachineFunction &MF,
         const MIRVocabulary &Vocab);

  /// Computes and returns the embedding for a given machine instruction MI in
  /// the machine function MF.
  ///
  /// \param MI Machine instruction whose embedding is returned.
  /// \return The embedding vector for \p MI.
  Embedding getMInstVector(const MachineInstr &MI) const {
    return computeEmbeddings(MI);
  }

  /// Computes and returns the embedding for a given machine basic block in the
  /// machine function MF.
  ///
  /// \param MBB Machine basic block whose embedding is returned.
  /// \return The embedding vector for \p MBB.
  Embedding getMBBVector(const MachineBasicBlock &MBB) const {
    return computeEmbeddings(MBB);
  }

  /// Computes and returns the embedding for the current machine function.
  ///
  /// \return The embedding vector for the current machine function.
  Embedding getMFunctionVector() const {
    // Currently, we always (re)compute the embeddings for the function. This is
    // cheaper than caching the vector.
    return computeEmbeddings();
  }
};

/// Class for computing Symbolic embeddings
/// Symbolic embeddings are constructed based on the entity-level
/// representations obtained from the MIR Vocabulary.
class LLVM_ABI SymbolicMIREmbedder : public MIREmbedder {
private:
  Embedding computeEmbeddings(const MachineInstr &MI) const override;

public:
  /// Construct a symbolic MIR embedder for \p F using \p Vocab.
  ///
  /// \param F Machine function to embed.
  /// \param Vocab MIR2Vec vocabulary providing seed embeddings.
  SymbolicMIREmbedder(const MachineFunction &F, const MIRVocabulary &Vocab);
  /// Create a symbolic MIR embedder for \p MF using \p Vocab.
  ///
  /// \param MF Machine function to embed.
  /// \param Vocab MIR2Vec vocabulary providing seed embeddings.
  /// \return A unique pointer to the created SymbolicMIREmbedder.
  static std::unique_ptr<SymbolicMIREmbedder>
  create(const MachineFunction &MF, const MIRVocabulary &Vocab);
};

} // namespace mir2vec

/// Vocabulary provider used by pass managers and standalone tools.
///
/// This class encapsulates the core vocabulary loading logic and can be used
/// independently of the pass manager infrastructure. For pass-based usage,
/// see MIR2VecVocabLegacyAnalysis.
///
/// Note: This provider pattern makes new PM migration straightforward when
/// needed. A new PM analysis wrapper can be added that delegates to this
/// provider, similar to how MIR2VecVocabLegacyAnalysis currently wraps it.
class MIR2VecVocabProvider {
  using VocabMap = std::map<std::string, mir2vec::Embedding>;

public:
  /// Construct a vocabulary provider using machine module info \p MMI.
  ///
  /// \param MMI Machine module info providing parsed machine functions.
  MIR2VecVocabProvider(const MachineModuleInfo &MMI) : MMI(MMI) {}

  /// Load and return the MIR2Vec vocabulary for module \p M.
  ///
  /// \param M Module whose MIR2Vec vocabulary is requested.
  /// \return The MIR2Vec vocabulary, or an error on failure.
  LLVM_ABI Expected<mir2vec::MIRVocabulary> getVocabulary(const Module &M);

private:
  Error readVocabulary(VocabMap &OpcVocab, VocabMap &CommonOperandVocab,
                       VocabMap &PhyRegVocabMap, VocabMap &VirtRegVocabMap);
  const MachineModuleInfo &MMI;
};

/// Pass to analyze and populate MIR2Vec vocabulary from a module
class LLVM_ABI MIR2VecVocabLegacyAnalysis : public ImmutablePass {
  using VocabVector = std::vector<mir2vec::Embedding>;
  using VocabMap = std::map<std::string, mir2vec::Embedding>;

  StringRef getPassName() const override;

protected:
  /// Require MachineModuleInfo and preserve all analyses.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    AU.setPreservesAll();
  }
  /// Lazily created vocabulary provider backed by MachineModuleInfo.
  std::unique_ptr<MIR2VecVocabProvider> Provider;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct the MIR2Vec vocabulary legacy analysis pass.
  MIR2VecVocabLegacyAnalysis() : ImmutablePass(ID) {}

  /// Return the MIR2Vec vocabulary for module \p M.
  ///
  /// \param M Module whose MIR2Vec vocabulary is requested.
  /// \return The MIR2Vec vocabulary, or an error on failure.
  Expected<mir2vec::MIRVocabulary> getMIR2VecVocabulary(const Module &M) {
    MachineModuleInfo &MMI =
        getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
    if (!Provider)
      Provider = std::make_unique<MIR2VecVocabProvider>(MMI);
    return Provider->getVocabulary(M);
  }

  /// Return the underlying vocabulary provider.
  ///
  /// The provider must already have been initialized by a prior vocabulary
  /// request.
  ///
  /// \return A reference to the initialized MIR2VecVocabProvider.
  MIR2VecVocabProvider &getProvider() {
    assert(Provider && "Provider not initialized");
    return *Provider;
  }
};

/// This pass prints the embeddings in the MIR2Vec vocabulary
class LLVM_ABI MIR2VecVocabPrinterLegacyPass : public MachineFunctionPass {
  raw_ostream &OS;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct a vocabulary printer that writes to \p OS.
  ///
  /// \param OS Output stream that receives the printed vocabulary.
  explicit MIR2VecVocabPrinterLegacyPass(raw_ostream &OS)
      : MachineFunctionPass(ID), OS(OS) {}

  /// Print MIR2Vec vocabulary information for machine function \p MF.
  ///
  /// \param MF Machine function being processed.
  /// \return False; this pass does not modify the machine function.
  bool runOnMachineFunction(MachineFunction &MF) override;
  /// Print remaining vocabulary data after all machine functions are processed.
  ///
  /// \param M Module being finalized.
  /// \return False; this pass does not modify the module.
  bool doFinalization(Module &M) override;
  /// Require MIR2VecVocabLegacyAnalysis and preserve all analyses.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MIR2VecVocabLegacyAnalysis>();
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  /// Return the name of this pass.
  ///
  /// \return The pass name string.
  StringRef getPassName() const override {
    return "MIR2Vec Vocabulary Printer Pass";
  }
};

/// This pass prints the MIR2Vec embeddings for machine functions, basic blocks,
/// and instructions
class LLVM_ABI MIR2VecPrinterLegacyPass : public MachineFunctionPass {
  raw_ostream &OS;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct an embedding printer that writes to \p OS.
  ///
  /// \param OS Output stream that receives the printed embeddings.
  explicit MIR2VecPrinterLegacyPass(raw_ostream &OS)
      : MachineFunctionPass(ID), OS(OS) {}

  /// Print MIR2Vec embeddings for machine function \p MF.
  ///
  /// \param MF Machine function whose embeddings are printed.
  /// \return False; this pass does not modify the machine function.
  bool runOnMachineFunction(MachineFunction &MF) override;
  /// Require MIR2VecVocabLegacyAnalysis and preserve all analyses.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MIR2VecVocabLegacyAnalysis>();
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  /// Return the name of this pass.
  ///
  /// \return The pass name string.
  StringRef getPassName() const override {
    return "MIR2Vec Embedder Printer Pass";
  }
};

/// Create a machine pass that prints MIR2Vec embeddings
///
/// \param OS Output stream that receives the printed embeddings.
/// \return The newly created MachineFunctionPass.
LLVM_ABI MachineFunctionPass *createMIR2VecPrinterLegacyPass(raw_ostream &OS);

} // namespace llvm

#endif // LLVM_CODEGEN_MIR2VEC_H
