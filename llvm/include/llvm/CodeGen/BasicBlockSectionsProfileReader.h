//===-- BasicBlockSectionsProfileReader.h - BB sections profile reader pass ==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass creates the basic block cluster info by reading the basic block
// sections profile. The cluster info will be used by the basic-block-sections
// pass to arrange basic blocks in their sections.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_BASICBLOCKSECTIONSPROFILEREADER_H
#define LLVM_CODEGEN_BASICBLOCKSECTIONSPROFILEREADER_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/UniqueBBID.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

/// Cluster information for a machine basic block identified by a unique BB ID.
struct BBClusterInfo {
  /// Unique basic block ID for this cluster entry.
  UniqueBBID BBID;
  /// Cluster ID this basic block belongs to.
  unsigned ClusterID;
  /// Position of this basic block within its cluster.
  unsigned PositionInCluster;
};

/// CFG profile data for a function, including block and edge counts.
struct CFGProfile {
  /// Profile counts keyed by basic block unique ID.
  DenseMap<UniqueBBID, uint64_t> NodeCounts;
  /// Edge profile counts keyed by source then destination unique BB ID.
  DenseMap<UniqueBBID, DenseMap<UniqueBBID, uint64_t>> EdgeCounts;

  /// Hash for each original basic block, keyed by base BB ID.
  ///
  /// Hashes are stored only for original blocks (not cloned blocks), so the
  /// map key is \c unsigned instead of \c UniqueBBID.
  DenseMap<unsigned, uint64_t> BBHashes;

  /// Return the profile count for \p BBID, or zero if none exists.
  ///
  /// \param BBID Basic block whose profile count is requested.
  /// \return Profile count for \p BBID, or zero if none exists.
  uint64_t getBlockCount(const UniqueBBID &BBID) const {
    return NodeCounts.lookup(BBID);
  }

  /// Return the profile count for the edge from \p SrcBBID to \p SinkBBID, or
  /// zero if none exists.
  ///
  /// \param SrcBBID Source basic block of the edge.
  /// \param SinkBBID Destination basic block of the edge.
  /// \return Profile count for the edge, or zero if none exists.
  uint64_t getEdgeCount(const UniqueBBID &SrcBBID,
                        const UniqueBBID &SinkBBID) const {
    auto It = EdgeCounts.find(SrcBBID);
    if (It == EdgeCounts.end())
      return 0;
    return It->second.lookup(SinkBBID);
  }
};

/// Raw optimization profile for a function, including CFG and layout data.
///
/// Holds CFG data (block and edge counts) and layout directives such as
/// clustering and cloning paths.
struct FunctionOptimizationProfile {
  /// Basic block cluster information specified by unique BB IDs.
  SmallVector<BBClusterInfo> ClusterInfo;
  /// Paths to clone within the function.
  ///
  /// A path a -> b -> c -> d implies cloning b, c, and d along the edge a -> b
  /// (a is not cloned). The index of the path in this vector determines the
  /// \c UniqueBBID::CloneID of the cloned blocks in that path.
  SmallVector<SmallVector<unsigned>> ClonePaths;
  /// CFG profile data (block and edge frequencies).
  CFGProfile CFG;
  /// Code prefetch targets identified by callsite ID.
  ///
  /// The target is the code immediately following this callsite.
  SmallVector<CallsiteID> PrefetchTargets;
  /// Code prefetch hints for injection sites.
  ///
  /// Each hint is specified by the injection site ID, the target function, and
  /// the target site ID.
  SmallVector<PrefetchHint> PrefetchHints;
  /// Node counts for each basic block.
  DenseMap<UniqueBBID, uint64_t> NodeCounts;
  /// Edge counts for each edge.
  DenseMap<UniqueBBID, DenseMap<UniqueBBID, uint64_t>> EdgeCounts;
  /// Hash for each original basic block, keyed by base BB ID.
  ///
  /// Hashes are stored only for original blocks (not cloned blocks), so the
  /// map key is \c unsigned instead of \c UniqueBBID.
  DenseMap<unsigned, uint64_t> BBHashes;
};

/// Reader for basic block sections profiles used to drive BB layout.
class BasicBlockSectionsProfileReader {
public:
  friend class BasicBlockSectionsProfileReaderWrapperPass;
  /// Construct a reader that parses profile data from \p Buf.
  ///
  /// \param Buf Memory buffer containing the basic block sections profile.
  BasicBlockSectionsProfileReader(const MemoryBuffer *Buf)
      : MBuf(Buf), LineIt(*Buf, /*SkipBlanks=*/true, /*CommentMarker=*/'#'){};

  /// Construct an empty reader with no profile buffer.
  BasicBlockSectionsProfileReader() = default;

  /// Return true if function \p FuncName is hot based on the profile.
  ///
  /// \param FuncName Function name to look up in the basic block section
  /// profile.
  /// \return True if \p FuncName is hot based on the profile.
  LLVM_ABI bool isFunctionHot(StringRef FuncName) const;

  /// Return the cluster info for function \p FuncName.
  ///
  /// Returns an empty vector if the function has no cluster info.
  ///
  /// \param FuncName Function whose basic block cluster info is requested.
  /// \return Cluster info for \p FuncName, or an empty vector if none exists.
  LLVM_ABI SmallVector<BBClusterInfo>
  getClusterInfoForFunction(StringRef FuncName) const;

  /// Return the path clonings for function \p FuncName.
  ///
  /// \param FuncName Function whose clone paths are requested.
  /// \return Clone paths for \p FuncName.
  LLVM_ABI SmallVector<SmallVector<unsigned>>
  getClonePathsForFunction(StringRef FuncName) const;

  /// Return the profile count for the edge from \p SrcBBID to \p DestBBID in
  /// function \p FuncName.
  ///
  /// \param FuncName Function containing the edge.
  /// \param SrcBBID Source basic block of the edge.
  /// \param DestBBID Destination basic block of the edge.
  /// \return Profile count for the edge in \p FuncName.
  LLVM_ABI uint64_t getEdgeCount(StringRef FuncName, const UniqueBBID &SrcBBID,
                                 const UniqueBBID &DestBBID) const;

  /// Return a pointer to the CFG profile for function \p FuncName.
  ///
  /// Returns nullptr if no profile data is available for the function.
  ///
  /// \param FuncName Function whose CFG profile is requested.
  /// \return Pointer to the CFG profile for \p FuncName, or nullptr if none.
  const CFGProfile *getFunctionCFGProfile(StringRef FuncName) const {
    auto It = ProgramOptimizationProfile.find(getAliasName(FuncName));
    if (It == ProgramOptimizationProfile.end())
      return nullptr;
    return &It->second.CFG;
  }

  /// Return the prefetch targets for function \p FuncName.
  ///
  /// Targets are identified by their containing callsite IDs.
  ///
  /// \param FuncName Function whose prefetch targets are requested.
  /// \return Prefetch targets for \p FuncName, identified by callsite IDs.
  LLVM_ABI SmallVector<CallsiteID>
  getPrefetchTargetsForFunction(StringRef FuncName) const;

  /// Return the prefetch hints to be injected in function \p FuncName.
  ///
  /// \param FuncName Function whose prefetch hints are requested.
  /// \return Prefetch hints to inject in \p FuncName.
  LLVM_ABI SmallVector<PrefetchHint>
  getPrefetchHintsForFunction(StringRef FuncName) const;

private:
  StringRef getAliasName(StringRef FuncName) const {
    auto R = FuncAliasMap.find(FuncName);
    return R == FuncAliasMap.end() ? FuncName : R->second;
  }

  // Returns a profile parsing error for the current line.
  Error createProfileParseError(Twine Message) const {
    return make_error<StringError>(
        Twine("invalid profile " + MBuf->getBufferIdentifier() + " at line " +
              Twine(LineIt.line_number()) + ": " + Message),
        inconvertibleErrorCode());
  }

  // Parses a `UniqueBBID` from `S`. `S` must be in the form "<bbid>"
  // (representing an original block) or "<bbid>.<cloneid>" (representing a
  // cloned block) where bbid is a non-negative integer and cloneid is a
  // positive integer.
  Expected<UniqueBBID> parseUniqueBBID(StringRef S) const;

  // Reads the basic block sections profile for functions in this module.
  Error ReadProfile();

  // Reads version 0 profile.
  // TODO: Remove this function once version 0 is deprecated.
  Error ReadV0Profile();

  // Reads version 1 profile.
  Error ReadV1Profile();

  // This contains the basic-block-sections profile.
  const MemoryBuffer *MBuf = nullptr;

  // Iterator to the line being parsed.
  line_iterator LineIt;

  // Map from every function name in the module to its debug info filename or
  // empty string if no debug info is available.
  StringMap<SmallString<128>> FunctionNameToDIFilename;

  // This map contains the optimization profile for each function in the
  // program. A function's optimization profile consists of CFG data (node and
  // edge counts) and layout directives such as basic block clustering and
  // cloning paths.
  StringMap<FunctionOptimizationProfile> ProgramOptimizationProfile;

  // Some functions have alias names. We use this map to find the main alias
  // name which appears in ProgramOptimizationProfile as a key.
  StringMap<StringRef> FuncAliasMap;
};

/// Create a pass that parses a basic block sections profile from \p Buf.
///
/// \p Buf is a memory buffer that contains the list of functions and basic
/// block ids to selectively enable basic block sections.
///
/// \param Buf Memory buffer containing the basic block sections profile.
/// \return New immutable pass that owns a basic block sections profile reader.
LLVM_ABI ImmutablePass *
createBasicBlockSectionsProfileReaderWrapperPass(const MemoryBuffer *Buf);

/// Analysis pass providing the \c BasicBlockSectionsProfileReader.
///
/// Note that this pass's result cannot be invalidated, it is immutable for the
/// life of the module.
class BasicBlockSectionsProfileReaderAnalysis
    : public AnalysisInfoMixin<BasicBlockSectionsProfileReaderAnalysis> {

public:
  /// Analysis key used to identify BasicBlockSectionsProfileReaderAnalysis.
  LLVM_ABI static AnalysisKey Key;
  /// Result type produced by this analysis.
  typedef BasicBlockSectionsProfileReader Result;
  /// Construct an analysis using target information from \p TM.
  ///
  /// \param TM Target machine associated with the module being analyzed.
  BasicBlockSectionsProfileReaderAnalysis(const TargetMachine &TM) : TM(&TM) {}

  /// Run the analysis on function \p F and return the profile reader.
  ///
  /// \param F Function being analyzed.
  /// \param AM Function analysis manager providing required analyses.
  /// \return Profile reader result for the module containing \p F.
  LLVM_ABI Result run(Function &F, FunctionAnalysisManager &AM);

private:
  const TargetMachine *TM;
};

/// Immutable wrapper pass that owns a BasicBlockSectionsProfileReader.
class LLVM_ABI BasicBlockSectionsProfileReaderWrapperPass
    : public ImmutablePass {
public:
  /// Pass identification, replacement for type ID.
  static char ID;
  /// Owned basic block sections profile reader.
  BasicBlockSectionsProfileReader BBSPR;

  /// Construct a wrapper pass that reads profile data from \p Buf.
  ///
  /// \param Buf Memory buffer containing the basic block sections profile.
  BasicBlockSectionsProfileReaderWrapperPass(const MemoryBuffer *Buf)
      : ImmutablePass(ID), BBSPR(BasicBlockSectionsProfileReader(Buf)) {}

  /// Construct a wrapper pass with an empty profile reader.
  BasicBlockSectionsProfileReaderWrapperPass()
      : ImmutablePass(ID), BBSPR(BasicBlockSectionsProfileReader()) {}

  /// Return the name of this pass.
  ///
  /// \return Name of this pass.
  StringRef getPassName() const override {
    return "Basic Block Sections Profile Reader";
  }

  /// Return true if function \p FuncName is hot based on the profile.
  ///
  /// \param FuncName Function name to look up in the basic block section
  /// profile.
  /// \return True if \p FuncName is hot based on the profile.
  bool isFunctionHot(StringRef FuncName) const;

  /// Return the cluster info for function \p FuncName.
  ///
  /// \param FuncName Function whose basic block cluster info is requested.
  /// \return Cluster info for \p FuncName.
  SmallVector<BBClusterInfo>
  getClusterInfoForFunction(StringRef FuncName) const;

  /// Return the path clonings for function \p FuncName.
  ///
  /// \param FuncName Function whose clone paths are requested.
  /// \return Clone paths for \p FuncName.
  SmallVector<SmallVector<unsigned>>
  getClonePathsForFunction(StringRef FuncName) const;

  /// Return a pointer to the CFG profile for function \p FuncName.
  ///
  /// \param FuncName Function whose CFG profile is requested.
  /// \return Pointer to the CFG profile for \p FuncName, or nullptr if none.
  const CFGProfile *getFunctionCFGProfile(StringRef FuncName) const;

  /// Return the profile count for the edge from \p SrcBBID to \p DestBBID in
  /// function \p FuncName.
  ///
  /// \param FuncName Function containing the edge.
  /// \param SrcBBID Source basic block of the edge.
  /// \param DestBBID Destination basic block of the edge.
  /// \return Profile count for the edge in \p FuncName.
  uint64_t getEdgeCount(StringRef FuncName, const UniqueBBID &SrcBBID,
                        const UniqueBBID &DestBBID) const;

  /// Return the prefetch targets for function \p FuncName.
  ///
  /// \param FuncName Function whose prefetch targets are requested.
  /// \return Prefetch targets for \p FuncName.
  SmallVector<CallsiteID>
  getPrefetchTargetsForFunction(StringRef FuncName) const;

  /// Return the prefetch hints to be injected in function \p FuncName.
  ///
  /// \param FuncName Function whose prefetch hints are requested.
  /// \return Prefetch hints to inject in \p FuncName.
  SmallVector<PrefetchHint>
  getPrefetchHintsForFunction(StringRef FuncName) const;

  /// Initialize FunctionNameToDIFilename for \p M and read matching profiles.
  ///
  /// \param M Module whose function debug-info filenames are recorded before
  /// the profile is read.
  /// \return False; this pass does not modify the module.
  bool doInitialization(Module &M) override;

  /// Return a reference to the owned profile reader.
  ///
  /// \return Reference to the owned BasicBlockSectionsProfileReader.
  BasicBlockSectionsProfileReader &getBBSPR();
};

} // namespace llvm
#endif // LLVM_CODEGEN_BASICBLOCKSECTIONSPROFILEREADER_H
