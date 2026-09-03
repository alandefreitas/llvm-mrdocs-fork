//===- IR2Vec.h - Implementation of IR2Vec ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See the LICENSE file for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the IR2Vec vocabulary analysis(IR2VecVocabAnalysis),
/// the core ir2vec::Embedder interface for generating IR embeddings,
/// and related utilities like the IR2VecPrinterPass.
///
/// Program Embeddings are typically or derived-from a learned
/// representation of the program. Such embeddings are used to represent the
/// programs as input to machine learning algorithms. IR2Vec represents the
/// LLVM IR as embeddings.
///
/// The IR2Vec algorithm is described in the following paper:
///
///   IR2Vec: LLVM IR Based Scalable Program Embeddings, S. VenkataKeerthy,
///   Rohit Aggarwal, Shalini Jain, Maunendra Sankar Desarkar, Ramakrishna
///   Upadrasta, and Y. N. Srikant, ACM Transactions on Architecture and
///   Code Optimization (TACO), 2020. https://doi.org/10.1145/3418463.
///   https://arxiv.org/abs/1909.06228
///
/// To obtain embeddings:
/// First run IR2VecVocabAnalysis to populate the vocabulary.
/// Then, use the Embedder interface to generate embeddings for the desired IR
/// entities. See the documentation for more details -
/// https://llvm.org/docs/MLGO.html#ir2vec-embeddings
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_IR2VEC_H
#define LLVM_ANALYSIS_IR2VEC_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/JSON.h"
#include <array>
#include <map>
#include <optional>

namespace llvm {

class Module;
class BasicBlock;
class Instruction;
class Function;
class Value;
class raw_ostream;
class LLVMContext;
class IR2VecVocabAnalysis;

/// Selects Symbolic or FlowAware IR2Vec embedding generation.
///
/// Symbolic embeddings capture the "syntactic" and "statistical correlation"
/// of the IR entities. Flow-aware embeddings build on top of symbolic
/// embeddings and additionally capture the flow information in the IR.
/// Note: Implementation of FlowAware embeddings is not same as the one
/// described in the paper. The current implementation is a simplified version
/// that captures the flow information (SSA-based use-defs) without tracing
/// through memory level use-defs in the embedding computation described in the
/// paper.
enum class IR2VecKind {
  /// Embeddings from syntactic and statistical correlation of IR entities.
  Symbolic,
  /// Embeddings that extend Symbolic with SSA use-def flow information.
  FlowAware
};

/// Helpers and data types for generating IR2Vec embeddings.
namespace ir2vec {

/// Command-line option category for IR2Vec-related flags.
LLVM_ABI extern llvm::cl::OptionCategory IR2VecCategory;
/// Weight applied to opcode components when forming embeddings.
LLVM_ABI extern cl::opt<float> OpcWeight;
/// Weight applied to type components when forming embeddings.
LLVM_ABI extern cl::opt<float> TypeWeight;
/// Weight applied to argument/operand components when forming embeddings.
LLVM_ABI extern cl::opt<float> ArgWeight;
/// Selects which IR2Vec embedding kind to generate.
LLVM_ABI extern cl::opt<IR2VecKind> IR2VecEmbeddingKind;
/// Path to the JSON vocabulary file used to seed embeddings.
LLVM_ABI extern cl::opt<std::string> VocabFile;

/// Fixed-dimension vector of doubles used as an IR2Vec embedding.
///
/// Embedding wraps std::vector<double> and provides arithmetic and comparison
/// operations. It is meant to be used *like* std::vector<double> but is more
/// restrictive in that it does not allow changing the size of the embedding
/// vector. The dimension is fixed at construction; elements can be modified
/// in-place.
struct Embedding {
private:
  std::vector<double> Data;

public:
  /// Construct an empty embedding.
  Embedding() = default;
  /// Construct an embedding by copying vector \p V.
  /// @param V Source values copied into this embedding.
  Embedding(const std::vector<double> &V) : Data(V) {}
  /// Construct an embedding by moving vector \p V.
  /// @param V Source values moved into this embedding.
  Embedding(std::vector<double> &&V) : Data(std::move(V)) {}
  /// Construct an embedding from initializer list \p IL.
  /// @param IL Initial values for the embedding components.
  Embedding(std::initializer_list<double> IL) : Data(IL) {}

  /// Construct a zero-filled embedding of length \p Size.
  /// @param Size Number of components in the embedding.
  explicit Embedding(size_t Size) : Data(Size, 0.0) {}
  /// Construct an embedding of length \p Size filled with \p InitialValue.
  /// @param Size Number of components in the embedding.
  /// @param InitialValue Value written to every component.
  Embedding(size_t Size, double InitialValue) : Data(Size, InitialValue) {}

  /// Return the number of components in this embedding.
  /// @return Number of components in this embedding.
  size_t size() const { return Data.size(); }
  /// Return true if this embedding has no components.
  /// @return True if this embedding has no components.
  bool empty() const { return Data.empty(); }

  /// Return a mutable reference to the component at index \p Itr.
  /// @param Itr Zero-based component index.
  /// @return Mutable reference to the component at \p Itr.
  double &operator[](size_t Itr) {
    assert(Itr < Data.size() && "Index out of bounds");
    return Data[Itr];
  }

  /// Return a const reference to the component at index \p Itr.
  /// @param Itr Zero-based component index.
  /// @return Const reference to the component at \p Itr.
  const double &operator[](size_t Itr) const {
    assert(Itr < Data.size() && "Index out of bounds");
    return Data[Itr];
  }

  /// Mutable iterator over embedding components.
  using iterator = std::vector<double>::iterator;
  /// Const iterator over embedding components.
  using const_iterator = std::vector<double>::const_iterator;

  /// Return an iterator to the first component.
  /// @return Iterator to the first component.
  iterator begin() { return Data.begin(); }
  /// Return an iterator past the last component.
  /// @return Iterator past the last component.
  iterator end() { return Data.end(); }
  /// Return a const iterator to the first component.
  /// @return Const iterator to the first component.
  const_iterator begin() const { return Data.begin(); }
  /// Return a const iterator past the last component.
  /// @return Const iterator past the last component.
  const_iterator end() const { return Data.end(); }
  /// Return a const iterator to the first component.
  /// @return Const iterator to the first component.
  const_iterator cbegin() const { return Data.cbegin(); }
  /// Return a const iterator past the last component.
  /// @return Const iterator past the last component.
  const_iterator cend() const { return Data.cend(); }

  /// Return a const reference to the underlying component storage.
  /// @return Const reference to the underlying component vector.
  const std::vector<double> &getData() const { return Data; }

  /// Add \p RHS into this embedding component-wise.
  /// @param RHS Embedding added to this one.
  /// @return Reference to this embedding after the update.
  LLVM_ABI Embedding &operator+=(const Embedding &RHS);
  /// Return the component-wise sum of this embedding and \p RHS.
  /// @param RHS Embedding added to this one.
  /// @return New embedding holding the component-wise sum.
  LLVM_ABI Embedding operator+(const Embedding &RHS) const;
  /// Subtract \p RHS from this embedding component-wise.
  /// @param RHS Embedding subtracted from this one.
  /// @return Reference to this embedding after the update.
  LLVM_ABI Embedding &operator-=(const Embedding &RHS);
  /// Return the component-wise difference of this embedding and \p RHS.
  /// @param RHS Embedding subtracted from this one.
  /// @return New embedding holding the component-wise difference.
  LLVM_ABI Embedding operator-(const Embedding &RHS) const;
  /// Scale this embedding by \p Factor in place.
  /// @param Factor Scalar multiplier applied to every component.
  /// @return Reference to this embedding after scaling.
  LLVM_ABI Embedding &operator*=(double Factor);
  /// Return this embedding scaled by \p Factor.
  /// @param Factor Scalar multiplier applied to every component.
  /// @return New embedding scaled by \p Factor.
  LLVM_ABI Embedding operator*(double Factor) const;

  /// Add \p Src scaled by \p Factor into this embedding.
  ///
  /// Equivalent to \c *this += Src * Factor.
  /// @param Src Embedding whose scaled values are added.
  /// @param Factor Scale applied to \p Src before adding.
  /// @return Reference to this embedding after the update.
  LLVM_ABI Embedding &scaleAndAdd(const Embedding &Src, float Factor);

  /// Return true if this embedding is approximately equal to \p RHS.
  /// @param RHS Embedding compared against this one.
  /// @param Tolerance Maximum absolute difference allowed per component.
  /// @return True if every component differs by at most \p Tolerance.
  LLVM_ABI bool approximatelyEquals(const Embedding &RHS,
                                    double Tolerance = 1e-4) const;

  /// Returns true if all elements of the embedding are zero.
  /// @return True if every component is zero.
  bool isZero() const {
    return llvm::all_of(Data, [](double D) { return D == 0.0; });
  }

  /// Print this embedding to stream \p OS.
  /// @param OS Output stream.
  LLVM_ABI void print(raw_ostream &OS) const;
};

/// Map from instructions to their computed embeddings.
using InstEmbeddingsMap = DenseMap<const Instruction *, Embedding>;
/// Map from basic blocks to their computed embeddings.
using BBEmbeddingsMap = DenseMap<const BasicBlock *, Embedding>;

/// Generic storage class for section-based vocabularies.
/// VocabStorage provides a generic foundation for storing and accessing
/// embeddings organized into sections.
class VocabStorage {
private:
  /// Section-based storage
  std::vector<std::vector<Embedding>> Sections;

  // Fixme: Check if these members can be made const (and delete move
  // assignment) after changing Vocabulary creation by using static factory
  // methods.
  size_t TotalSize = 0;
  unsigned Dimension = 0;

public:
  /// Default constructor creates empty storage (invalid state)
  VocabStorage() = default;

  /// Create a VocabStorage with pre-organized section data
  /// @param SectionData Per-section embedding vectors; ownership is taken.
  LLVM_ABI VocabStorage(std::vector<std::vector<Embedding>> &&SectionData);

  /// Move-construct VocabStorage from \p Other.
  /// @param Other Storage whose contents are moved into this object.
  VocabStorage(VocabStorage &&Other) = default;
  /// Move-assign VocabStorage from \p Other.
  /// @param Other Storage whose contents are moved into this object.
  /// @return Reference to this storage after the move assignment.
  VocabStorage &operator=(VocabStorage &&Other) = default;

  /// Deleted copy constructor.
  /// @param Other Unused; copy construction is deleted.
  VocabStorage(const VocabStorage &Other) = delete;
  /// Deleted copy assignment.
  /// @param Other Unused; copy assignment is deleted.
  VocabStorage &operator=(const VocabStorage &Other) = delete;

  /// Get total number of entries across all sections
  /// @return Total number of embeddings across all sections.
  size_t size() const { return TotalSize; }

  /// Get number of sections
  /// @return Number of sections in this storage.
  unsigned getNumSections() const {
    return static_cast<unsigned>(Sections.size());
  }

  /// Section-based access: Storage[sectionId][localIndex]
  /// @param SectionId Zero-based section index to retrieve.
  /// @return Const reference to the embeddings in section \p SectionId.
  const std::vector<Embedding> &operator[](unsigned SectionId) const {
    assert(SectionId < Sections.size() && "Invalid section ID");
    return Sections[SectionId];
  }

  /// Get vocabulary dimension
  /// @return Embedding dimension shared by entries in this storage.
  unsigned getDimension() const { return Dimension; }

  /// Check if vocabulary is valid (has data)
  /// @return True if this storage contains at least one entry.
  bool isValid() const { return TotalSize > 0; }

  /// Iterator support for section-based access
  class const_iterator {
    const VocabStorage *Storage;
    unsigned SectionId = 0;
    size_t LocalIndex = 0;

  public:
    /// Construct an iterator into \p Storage at \p SectionId / \p LocalIndex.
    /// @param Storage Vocabulary storage being iterated.
    /// @param SectionId Section that currently holds the iterator.
    /// @param LocalIndex Index within the current section.
    const_iterator(const VocabStorage *Storage, unsigned SectionId,
                   size_t LocalIndex)
        : Storage(Storage), SectionId(SectionId), LocalIndex(LocalIndex) {}

    /// Return the embedding at the current iterator position.
    /// @return Const reference to the embedding at this position.
    LLVM_ABI const Embedding &operator*() const;
    /// Advance this iterator to the next embedding.
    /// @return Reference to this iterator after advancing.
    LLVM_ABI const_iterator &operator++();
    /// Return true if this iterator equals \p Other.
    /// @param Other Iterator compared for equality.
    /// @return True if both iterators refer to the same position.
    LLVM_ABI bool operator==(const const_iterator &Other) const;
    /// Return true if this iterator differs from \p Other.
    /// @param Other Iterator compared for inequality.
    /// @return True if the iterators refer to different positions.
    LLVM_ABI bool operator!=(const const_iterator &Other) const;
  };

  /// Return a const iterator to the first embedding across all sections.
  /// @return Const iterator to the first embedding.
  const_iterator begin() const { return const_iterator(this, 0, 0); }
  /// Return a const iterator past the last embedding across all sections.
  /// @return Const iterator past the last embedding.
  const_iterator end() const {
    return const_iterator(this, getNumSections(), 0);
  }

  /// Map from vocabulary string keys to their embeddings.
  using VocabMap = std::map<std::string, Embedding>;
  /// Parse a vocabulary section from JSON and populate the target vocabulary
  /// map.
  /// @param Key JSON object key naming the section to parse.
  /// @param ParsedVocabValue JSON value expected to hold the section map.
  /// @param TargetVocab Map populated with parsed string-to-embedding entries.
  /// @param Dim Updated with the embedding dimension discovered in the section.
  /// @return Success, or an error if the section is missing or malformed.
  LLVM_ABI static Error parseVocabSection(StringRef Key,
                                          const json::Value &ParsedVocabValue,
                                          VocabMap &TargetVocab, unsigned &Dim);
};

/// Stores and looks up seed embeddings for LLVM IR entities.
///
/// The Vocabulary class manages seed embeddings for LLVM IR entities. The
/// seed embeddings are the initial learned representations of the entities
/// of LLVM IR. The IR2Vec representation for a given IR is derived from these
/// seed embeddings.
///
/// The vocabulary contains the seed embeddings for three types of entities:
/// instruction opcodes, types, and operands. Types are grouped/canonicalized
/// for better learning (e.g., all float variants map to FloatTy). The
/// vocabulary abstracts away the canonicalization effectively, the exposed APIs
/// handle all the known LLVM IR opcodes, types and operands.
///
/// This class helps populate the seed embeddings in an internal vector-based
/// ADT. It provides logic to map every IR entity to a specific slot index or
/// position in this vector, enabling O(1) embedding lookup while avoiding
/// unnecessary computations involving string based lookups while generating the
/// embeddings.
class Vocabulary {
  friend class llvm::IR2VecVocabAnalysis;

  // Vocabulary Layout:
  // +----------------+------------------------------------------------------+
  // | Entity Type    | Index Range                                          |
  // +----------------+------------------------------------------------------+
  // | Opcodes        | [0 .. (MaxOpcodes-1)]                                |
  // | Canonical Types| [MaxOpcodes .. (MaxOpcodes+MaxCanonicalTypeIDs-1)]   |
  // | Operands       | [(MaxOpcodes+MaxCanonicalTypeIDs) .. NumCanEntries]  |
  // +----------------+------------------------------------------------------+
  // Note: MaxOpcodes is the number of unique opcodes supported by LLVM IR.
  //       MaxCanonicalTypeIDs is the number of canonicalized type IDs.
  //       "Similar" LLVM Types are grouped/canonicalized together. E.g., all
  //       float variants (FloatTy, DoubleTy, HalfTy, etc.) map to
  //       CanonicalTypeID::FloatTy. This helps reduce the vocabulary size
  //       and improves learning. Operands include Comparison predicates
  //       (ICmp/FCmp) along with other operand types. This can be extended to
  //       include other specializations in future.
  enum class Section : unsigned {
    Opcodes = 0,
    CanonicalTypes = 1,
    Operands = 2,
    Predicates = 3,
    MaxSections
  };

  // Use section-based storage for better organization and efficiency
  VocabStorage Storage;

  static constexpr unsigned NumICmpPredicates =
      static_cast<unsigned>(CmpInst::LAST_ICMP_PREDICATE) -
      static_cast<unsigned>(CmpInst::FIRST_ICMP_PREDICATE) + 1;
  static constexpr unsigned NumFCmpPredicates =
      static_cast<unsigned>(CmpInst::LAST_FCMP_PREDICATE) -
      static_cast<unsigned>(CmpInst::FIRST_FCMP_PREDICATE) + 1;

public:
  /// Canonical type IDs supported by IR2Vec Vocabulary
  enum class CanonicalTypeID : unsigned {
    /// Floating-point types (half, float, double, and related variants).
    FloatTy,
    /// Void type.
    VoidTy,
    /// Basic-block label type.
    LabelTy,
    /// Metadata type.
    MetadataTy,
    /// Vector types, including X86 AMX.
    VectorTy,
    /// Token type.
    TokenTy,
    /// Integer type.
    IntegerTy,
    /// Byte type.
    ByteTy,
    /// Function type.
    FunctionTy,
    /// Pointer types, including typed pointers.
    PointerTy,
    /// Struct type.
    StructTy,
    /// Array type.
    ArrayTy,
    /// Fallback for types without a dedicated canonical mapping.
    UnknownTy,
    /// Sentinel one past the last canonical type.
    MaxCanonicalType
  };

  /// Operand kinds supported by IR2Vec Vocabulary
  enum class OperandKind : unsigned {
    /// Operand that refers to a function.
    FunctionID,
    /// Operand that is a pointer value.
    PointerID,
    /// Operand that is a constant.
    ConstantID,
    /// Operand that is a non-constant variable/value.
    VariableID,
    /// Sentinel one past the last operand kind.
    MaxOperandKind
  };

  /// Capture the maximum opcode number from Instruction.def as MaxOpcodes.
  /// @param NUM Last other-instruction opcode value supplied by Instruction.def.
#define LAST_OTHER_INST(NUM) static constexpr unsigned MaxOpcodes = NUM;
#include "llvm/IR/Instruction.def"
#undef LAST_OTHER_INST

  /// Number of distinct LLVM TypeIDs recognized by the vocabulary layout.
  static constexpr unsigned MaxTypeIDs = Type::TypeID::TargetExtTyID + 1;
  /// Number of canonical type slots in the vocabulary.
  static constexpr unsigned MaxCanonicalTypeIDs =
      static_cast<unsigned>(CanonicalTypeID::MaxCanonicalType);
  /// Number of operand-kind slots in the vocabulary.
  static constexpr unsigned MaxOperandKinds =
      static_cast<unsigned>(OperandKind::MaxOperandKind);
  // CmpInst::Predicate has gaps. We want the vocabulary to be dense without
  // empty slots.
  /// Number of dense predicate slots (ICmp + FCmp) in the vocabulary.
  static constexpr unsigned MaxPredicateKinds =
      NumICmpPredicates + NumFCmpPredicates;

  /// Construct an empty (invalid) vocabulary.
  Vocabulary() = default;
  /// Construct a vocabulary that takes ownership of \p Storage.
  /// @param Storage Section-based seed embeddings moved into this vocabulary.
  Vocabulary(VocabStorage &&Storage) : Storage(std::move(Storage)) {}

  /// Deleted copy constructor.
  /// @param Other Unused; copy construction is deleted.
  Vocabulary(const Vocabulary &Other) = delete;
  /// Deleted copy assignment.
  /// @param Other Unused; copy assignment is deleted.
  Vocabulary &operator=(const Vocabulary &Other) = delete;

  /// Move-construct Vocabulary from \p Other.
  /// @param Other Vocabulary whose contents are moved into this object.
  Vocabulary(Vocabulary &&Other) = default;
  /// Deleted move assignment.
  /// @param Other Unused; move assignment is deleted.
  Vocabulary &operator=(Vocabulary &&Other) = delete;

  /// Create a Vocabulary by loading embeddings from a JSON file.
  ///
  /// This is the primary entry point for programmatic vocabulary creation,
  /// suitable for use in Python bindings or other contexts where command-line
  /// options are not available. Weights are applied to scale the embeddings
  /// for opcodes, types, and arguments respectively.
  /// @param VocabFilePath Path to the JSON vocabulary file.
  /// @param OpcWeight Scale applied to opcode embeddings.
  /// @param TypeWeight Scale applied to type embeddings.
  /// @param ArgWeight Scale applied to argument/operand embeddings.
  /// @return Loaded vocabulary, or an error if the file cannot be parsed.
  LLVM_ABI static Expected<Vocabulary> fromFile(StringRef VocabFilePath,
                                                float OpcWeight = 1.0,
                                                float TypeWeight = 0.5,
                                                float ArgWeight = 0.2);

  /// Return true if this vocabulary has the expected number of entries.
  /// @return True if storage size equals the canonical entry count.
  bool isValid() const { return Storage.size() == NumCanonicalEntries; }

  /// Return the embedding dimension of this vocabulary.
  /// @return Embedding dimension of the seed vocabulary.
  unsigned getDimension() const {
    assert(isValid() && "IR2Vec Vocabulary is invalid");
    return Storage.getDimension();
  }

  /// Total number of entries (opcodes + canonicalized types + operand kinds +
  /// predicates)
  /// @return Canonical vocabulary size across all entity sections.
  static constexpr size_t getCanonicalSize() { return NumCanonicalEntries; }

  /// Function to get vocabulary key for a given Opcode
  /// @param Opcode LLVM instruction opcode whose vocabulary key is requested.
  /// @return String key used for \p Opcode in the vocabulary.
  LLVM_ABI static StringRef getVocabKeyForOpcode(unsigned Opcode);

  /// Function to get vocabulary key for a given TypeID
  /// @param TypeID LLVM type ID whose vocabulary key is requested.
  /// @return String key for the canonical type mapped from \p TypeID.
  static StringRef getVocabKeyForTypeID(Type::TypeID TypeID) {
    return getVocabKeyForCanonicalTypeID(getCanonicalTypeID(TypeID));
  }

  /// Function to get vocabulary key for a given OperandKind
  /// @param Kind Operand classification whose vocabulary key is requested.
  /// @return String key used for \p Kind in the vocabulary.
  static StringRef getVocabKeyForOperandKind(OperandKind Kind) {
    unsigned Index = static_cast<unsigned>(Kind);
    assert(Index < MaxOperandKinds && "Invalid OperandKind");
    return OperandKindNames[Index];
  }

  /// Function to classify an operand into OperandKind
  /// @param Op Value classified into an OperandKind.
  /// @return OperandKind classification for \p Op.
  LLVM_ABI static OperandKind getOperandKind(const Value *Op);

  /// Function to get vocabulary key for a given predicate
  /// @param P Comparison predicate whose vocabulary key is requested.
  /// @return String key used for \p P in the vocabulary.
  LLVM_ABI static StringRef getVocabKeyForPredicate(CmpInst::Predicate P);

  /// Functions to return flat index
  /// Return the flat vocabulary index for instruction opcode \p Opcode.
  /// @param Opcode LLVM instruction opcode (1-based).
  /// @return Zero-based flat index for \p Opcode.
  static unsigned getIndex(unsigned Opcode) {
    assert(Opcode >= 1 && Opcode <= MaxOpcodes && "Invalid opcode");
    return Opcode - 1; // Convert to zero-based index
  }

  /// Return the flat vocabulary index for type ID \p TypeID.
  /// @param TypeID LLVM type ID to map into the canonical-types section.
  /// @return Flat index of the canonical type for \p TypeID.
  static unsigned getIndex(Type::TypeID TypeID) {
    assert(static_cast<unsigned>(TypeID) < MaxTypeIDs && "Invalid type ID");
    return MaxOpcodes + static_cast<unsigned>(getCanonicalTypeID(TypeID));
  }

  /// Return the flat vocabulary index for operand value \p Op.
  /// @param Op Value whose operand-kind slot is requested.
  /// @return Flat index of the operand-kind slot for \p Op.
  static unsigned getIndex(const Value &Op) {
    unsigned Index = static_cast<unsigned>(getOperandKind(&Op));
    assert(Index < MaxOperandKinds && "Invalid OperandKind");
    return OperandBaseOffset + Index;
  }

  /// Return the flat vocabulary index for comparison predicate \p P.
  /// @param P Comparison predicate whose slot is requested.
  /// @return Flat index of the predicate slot for \p P.
  static unsigned getIndex(CmpInst::Predicate P) {
    return PredicateBaseOffset + getPredicateLocalIndex(P);
  }

  /// Accessors to get the embedding for a given entity.
  /// Return the seed embedding for instruction opcode \p Opcode.
  /// @param Opcode LLVM instruction opcode (1-based).
  /// @return Const reference to the seed embedding for \p Opcode.
  const ir2vec::Embedding &operator[](unsigned Opcode) const {
    assert(Opcode >= 1 && Opcode <= MaxOpcodes && "Invalid opcode");
    return Storage[static_cast<unsigned>(Section::Opcodes)][Opcode - 1];
  }

  /// Return the seed embedding for type ID \p TypeID.
  /// @param TypeID LLVM type ID whose canonical embedding is requested.
  /// @return Const reference to the canonical seed embedding for \p TypeID.
  const ir2vec::Embedding &operator[](Type::TypeID TypeID) const {
    assert(static_cast<unsigned>(TypeID) < MaxTypeIDs && "Invalid type ID");
    unsigned LocalIndex = static_cast<unsigned>(getCanonicalTypeID(TypeID));
    return Storage[static_cast<unsigned>(Section::CanonicalTypes)][LocalIndex];
  }

  /// Return the seed embedding for operand value \p Arg.
  /// @param Arg Value whose operand-kind embedding is requested.
  /// @return Const reference to the operand-kind seed embedding for \p Arg.
  const ir2vec::Embedding &operator[](const Value &Arg) const {
    unsigned LocalIndex = static_cast<unsigned>(getOperandKind(&Arg));
    assert(LocalIndex < MaxOperandKinds && "Invalid OperandKind");
    return Storage[static_cast<unsigned>(Section::Operands)][LocalIndex];
  }

  /// Return the seed embedding for comparison predicate \p P.
  /// @param P Comparison predicate whose embedding is requested.
  /// @return Const reference to the seed embedding for \p P.
  const ir2vec::Embedding &operator[](CmpInst::Predicate P) const {
    unsigned LocalIndex = getPredicateLocalIndex(P);
    return Storage[static_cast<unsigned>(Section::Predicates)][LocalIndex];
  }

  /// Const Iterator type aliases
  using const_iterator = VocabStorage::const_iterator;

  /// Return a const iterator to the first vocabulary embedding.
  /// @return Const iterator to the first vocabulary embedding.
  const_iterator begin() const {
    assert(isValid() && "IR2Vec Vocabulary is invalid");
    return Storage.begin();
  }

  /// Return a const iterator to the first vocabulary embedding.
  /// @return Const iterator to the first vocabulary embedding.
  const_iterator cbegin() const { return begin(); }

  /// Return a const iterator past the last vocabulary embedding.
  /// @return Const iterator past the last vocabulary embedding.
  const_iterator end() const {
    assert(isValid() && "IR2Vec Vocabulary is invalid");
    return Storage.end();
  }

  /// Return a const iterator past the last vocabulary embedding.
  /// @return Const iterator past the last vocabulary embedding.
  const_iterator cend() const { return end(); }

  /// Return the string key for vocabulary flat index \p Pos.
  ///
  /// This is useful for debugging or printing the vocabulary. Do not use this
  /// for embedding generation as string based lookups are inefficient.
  /// @param Pos Flat zero-based index into the vocabulary layout.
  /// @return String key corresponding to flat index \p Pos.
  LLVM_ABI static StringRef getStringKey(unsigned Pos);

  /// Create a dummy vocabulary for testing purposes.
  /// @param Dim Embedding dimension used for the dummy entries.
  /// @return Dummy VocabStorage filled for testing.
  LLVM_ABI static VocabStorage createDummyVocabForTest(unsigned Dim = 1);

  /// Invalidate this analysis result when module analyses are invalidated.
  /// @param M Module associated with this analysis result.
  /// @param PA Set of analyses preserved by the transformation.
  /// @param Inv Invalidator used to invalidate dependent analyses.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(Module &M, const PreservedAnalyses &PA,
                           ModuleAnalysisManager::Invalidator &Inv) const;

private:
  constexpr static unsigned NumCanonicalEntries =
      MaxOpcodes + MaxCanonicalTypeIDs + MaxOperandKinds + MaxPredicateKinds;

  // Base offsets for flat index computation
  constexpr static unsigned OperandBaseOffset =
      MaxOpcodes + MaxCanonicalTypeIDs;
  constexpr static unsigned PredicateBaseOffset =
      OperandBaseOffset + MaxOperandKinds;

  /// Functions for predicate index calculations
  LLVM_ABI static unsigned getPredicateLocalIndex(CmpInst::Predicate P);
  LLVM_ABI static CmpInst::Predicate
  getPredicateFromLocalIndex(unsigned LocalIndex);

  /// String mappings for CanonicalTypeID values
  static constexpr StringLiteral CanonicalTypeNames[] = {
      "FloatTy",  "VoidTy",    "LabelTy",  "MetadataTy", "VectorTy",
      "TokenTy",  "IntegerTy", "ByteTy",   "FunctionTy", "PointerTy",
      "StructTy", "ArrayTy",   "UnknownTy"};
  static_assert(std::size(CanonicalTypeNames) ==
                    static_cast<unsigned>(CanonicalTypeID::MaxCanonicalType),
                "CanonicalTypeNames array size must match MaxCanonicalType");

  /// String mappings for OperandKind values
  static constexpr StringLiteral OperandKindNames[] = {"Function", "Pointer",
                                                       "Constant", "Variable"};
  static_assert(std::size(OperandKindNames) ==
                    static_cast<unsigned>(OperandKind::MaxOperandKind),
                "OperandKindNames array size must match MaxOperandKind");

  /// Every known TypeID defined in llvm/IR/Type.h is expected to have a
  /// corresponding mapping here in the same order as enum Type::TypeID.
  static constexpr std::array<CanonicalTypeID, MaxTypeIDs> TypeIDMapping = {{
      CanonicalTypeID::FloatTy,    // HalfTyID = 0
      CanonicalTypeID::FloatTy,    // BFloatTyID
      CanonicalTypeID::FloatTy,    // FloatTyID
      CanonicalTypeID::FloatTy,    // DoubleTyID
      CanonicalTypeID::FloatTy,    // X86_FP80TyID
      CanonicalTypeID::FloatTy,    // FP128TyID
      CanonicalTypeID::FloatTy,    // PPC_FP128TyID
      CanonicalTypeID::VoidTy,     // VoidTyID
      CanonicalTypeID::LabelTy,    // LabelTyID
      CanonicalTypeID::MetadataTy, // MetadataTyID
      CanonicalTypeID::VectorTy,   // X86_AMXTyID
      CanonicalTypeID::TokenTy,    // TokenTyID
      CanonicalTypeID::IntegerTy,  // IntegerTyID
      CanonicalTypeID::ByteTy,     // ByteTyID
      CanonicalTypeID::FunctionTy, // FunctionTyID
      CanonicalTypeID::PointerTy,  // PointerTyID
      CanonicalTypeID::StructTy,   // StructTyID
      CanonicalTypeID::ArrayTy,    // ArrayTyID
      CanonicalTypeID::VectorTy,   // FixedVectorTyID
      CanonicalTypeID::VectorTy,   // ScalableVectorTyID
      CanonicalTypeID::PointerTy,  // TypedPointerTyID
      CanonicalTypeID::UnknownTy   // TargetExtTyID
  }};
  static_assert(TypeIDMapping.size() == MaxTypeIDs,
                "TypeIDMapping must cover all Type::TypeID values");

  /// Function to get vocabulary key for canonical type by enum
  static StringRef getVocabKeyForCanonicalTypeID(CanonicalTypeID CType) {
    unsigned Index = static_cast<unsigned>(CType);
    assert(Index < MaxCanonicalTypeIDs && "Invalid CanonicalTypeID");
    return CanonicalTypeNames[Index];
  }

  /// Function to convert TypeID to CanonicalTypeID
  static CanonicalTypeID getCanonicalTypeID(Type::TypeID TypeID) {
    unsigned Index = static_cast<unsigned>(TypeID);
    assert(Index < MaxTypeIDs && "Invalid TypeID");
    return TypeIDMapping[Index];
  }

  /// Function to get the predicate enum value for a given index. Index is
  /// relative to the predicates section of the vocabulary. E.g., Index 0
  /// corresponds to the first predicate.
  LLVM_ABI static CmpInst::Predicate getPredicate(unsigned Index) {
    assert(Index < MaxPredicateKinds && "Invalid predicate index");
    return getPredicateFromLocalIndex(Index);
  }

  using VocabMap = std::map<std::string, Embedding>;

  /// Generate VocabStorage from vocabulary maps.
  static VocabStorage buildVocabStorage(const VocabMap &OpcVocab,
                                        const VocabMap &TypeVocab,
                                        const VocabMap &ArgVocab);
};

/// Interface for generating IR2Vec embeddings from a function and vocabulary.
///
/// Embedder provides the interface to generate embeddings (vector
/// representations) for instructions, basic blocks, and functions. The
/// vector representations are generated using IR2Vec algorithms.
///
/// The Embedder class is an abstract class and it is intended to be
/// subclassed for different IR2Vec algorithms like Symbolic and Flow-aware.
class Embedder {
protected:
  /// Function whose embeddings are being computed.
  const Function &F;
  /// Vocabulary supplying seed embeddings for IR entities.
  const Vocabulary &Vocab;

  /// Dimension of the vector representation; captured from the input vocabulary
  const unsigned Dimension;

  /// Weights for different entities (like opcode, arguments, types)
  /// in the IR instructions to generate the vector representation.
  const float OpcWeight, TypeWeight, ArgWeight;

  /// Construct an embedder for function \p F using vocabulary \p Vocab.
  /// @param F Function whose IR is embedded.
  /// @param Vocab Vocabulary providing seed embeddings.
  Embedder(const Function &F, const Vocabulary &Vocab)
      : F(F), Vocab(Vocab), Dimension(Vocab.getDimension()),
        OpcWeight(ir2vec::OpcWeight), TypeWeight(ir2vec::TypeWeight),
        ArgWeight(ir2vec::ArgWeight) {}

  /// Function to compute embeddings.
  /// @return Embedding for the whole function \c F.
  LLVM_ABI Embedding computeEmbeddings() const;

  /// Function to compute the embedding for a given basic block.
  /// @param BB Basic block whose embedding is computed.
  /// @return Embedding for \p BB.
  LLVM_ABI Embedding computeEmbeddings(const BasicBlock &BB) const;

  /// Function to compute the embedding for a given instruction.
  /// Specific to the kind of embeddings being computed.
  /// @param I Instruction whose embedding is computed.
  /// @return Embedding for \p I.
  virtual Embedding computeEmbeddings(const Instruction &I) const = 0;

public:
  /// Destroy this embedder.
  virtual ~Embedder() = default;

  /// Factory method to create an Embedder object.
  /// @param Mode Embedding algorithm to instantiate.
  /// @param F Function whose IR is embedded.
  /// @param Vocab Vocabulary providing seed embeddings.
  /// @return Unique pointer to an Embedder for \p Mode.
  LLVM_ABI static std::unique_ptr<Embedder>
  create(IR2VecKind Mode, const Function &F, const Vocabulary &Vocab);

  /// Computes and returns the embedding for a given instruction in the function
  /// F
  /// @param I Instruction in \c F whose embedding is requested.
  /// @return Embedding for instruction \p I.
  Embedding getInstVector(const Instruction &I) const {
    return computeEmbeddings(I);
  }

  /// Computes and returns the embedding for a given basic block in the function
  /// F
  /// @param BB Basic block in \c F whose embedding is requested.
  /// @return Embedding for basic block \p BB.
  Embedding getBBVector(const BasicBlock &BB) const {
    return computeEmbeddings(BB);
  }

  /// Computes and returns the embedding for the current function.
  /// @return Embedding for function \c F.
  Embedding getFunctionVector() const { return computeEmbeddings(); }

  /// Invalidate any embeddings cached by this embedder.
  ///
  /// The embeddings may not be relevant anymore when the IR changes due to
  /// transformations. In such cases, the cached embeddings should be
  /// invalidated to ensure correctness/recomputation. This is a no-op for
  /// SymbolicEmbedder but removes all the cached entries in FlowAwareEmbedder.
  virtual void invalidateEmbeddings() {}
};

/// Computes Symbolic IR2Vec embeddings for a function.
///
/// Symbolic embeddings are constructed based on the entity-level
/// representations obtained from the Vocabulary.
class LLVM_ABI SymbolicEmbedder : public Embedder {
private:
  Embedding computeEmbeddings(const Instruction &I) const override;

public:
  /// Construct a Symbolic embedder for \p F using \p Vocab.
  /// @param F Function whose IR is embedded.
  /// @param Vocab Vocabulary providing seed embeddings.
  SymbolicEmbedder(const Function &F, const Vocabulary &Vocab)
      : Embedder(F, Vocab) {}
};

/// Computes FlowAware IR2Vec embeddings for a function.
///
/// Flow-aware embeddings build on the vocabulary, just like Symbolic
/// embeddings, and additionally capture the flow information in the IR.
class LLVM_ABI FlowAwareEmbedder : public Embedder {
private:
  // FlowAware embeddings would benefit from caching instruction embeddings as
  // they are reused while computing the embeddings of other instructions.
  mutable InstEmbeddingsMap InstVecMap;
  Embedding computeEmbeddings(const Instruction &I) const override;

public:
  /// Construct a FlowAware embedder for \p F using \p Vocab.
  /// @param F Function whose IR is embedded.
  /// @param Vocab Vocabulary providing seed embeddings.
  FlowAwareEmbedder(const Function &F, const Vocabulary &Vocab)
      : Embedder(F, Vocab) {}
  /// Clear cached instruction embeddings for this embedder.
  void invalidateEmbeddings() override { InstVecMap.clear(); }
};

} // namespace ir2vec

/// Analysis that produces the IR2Vec vocabulary for a module.
///
/// The vocabulary provides a mapping between an entity of the IR (like opcode,
/// type, argument, etc.) and its corresponding embedding.
class IR2VecVocabAnalysis : public AnalysisInfoMixin<IR2VecVocabAnalysis> {
  std::optional<ir2vec::VocabStorage> Vocab;

  void emitError(Error Err);

public:
  /// Analysis key used to identify this analysis in the pass manager.
  LLVM_ABI static AnalysisKey Key;
  /// Construct an analysis that loads vocabulary from command-line options.
  IR2VecVocabAnalysis() = default;
  /// Construct an analysis seeded with prebuilt vocabulary storage \p Vocab.
  /// @param Vocab Vocabulary storage moved into this analysis.
  explicit IR2VecVocabAnalysis(ir2vec::VocabStorage &&Vocab)
      : Vocab(std::move(Vocab)) {}
  /// Result type produced by this analysis.
  using Result = ir2vec::Vocabulary;
  /// Run this analysis on module \p M.
  /// @param M Module whose vocabulary is requested.
  /// @param MAM Module analysis manager providing dependent analyses.
  /// @return Vocabulary for module \p M.
  LLVM_ABI Result run(Module &M, ModuleAnalysisManager &MAM);
};

/// This pass prints the IR2Vec embeddings for instructions, basic blocks, and
/// functions.
class IR2VecPrinterPass : public RequiredPassInfoMixin<IR2VecPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes embeddings to \p OS.
  /// @param OS Output stream that receives printed embeddings.
  explicit IR2VecPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print IR2Vec embeddings for module \p M.
  /// @param M Module whose embeddings are printed.
  /// @param MAM Module analysis manager used to fetch vocabulary/embeddings.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

/// This pass prints the embeddings in the vocabulary
class IR2VecVocabPrinterPass
    : public RequiredPassInfoMixin<IR2VecVocabPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes vocabulary embeddings to \p OS.
  /// @param OS Output stream that receives printed vocabulary entries.
  explicit IR2VecVocabPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print the IR2Vec vocabulary for module \p M.
  /// @param M Module whose vocabulary is printed.
  /// @param MAM Module analysis manager used to fetch the vocabulary.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};

} // namespace llvm

#endif // LLVM_ANALYSIS_IR2VEC_H
