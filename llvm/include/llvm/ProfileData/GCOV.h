//===- GCOV.h - LLVM coverage tool ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header provides the interface to read and write coverage files that
// use 'gcov' format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_GCOV_H
#define LLVM_PROFILEDATA_GCOV_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace llvm {

class GCOVFunction;
class GCOVBlock;

/// Types and options shared by gcov note/data readers and reporters.
namespace GCOV {

/// Supported gcov file format versions.
enum GCOVVersion {
  /// gcov format from GCC 3.4.
  V304,
  /// gcov format from GCC 4.7.
  V407,
  /// gcov format from GCC 4.8.
  V408,
  /// gcov format from GCC 8.
  V800,
  /// gcov format from GCC 9.
  V900,
  /// gcov format from GCC 12.
  V1200
};

/// A struct for passing gcov options between functions.
struct Options {
  /// Construct options from individual llvm-cov gcov flags.
  ///
  /// @param A Report all basic blocks.
  /// @param B Report branch probabilities.
  /// @param C Report branch counts instead of percentages.
  /// @param F Report function-level coverage summaries.
  /// @param P Preserve path components in output filenames.
  /// @param U Include unconditional branches in branch reports.
  /// @param I Emit intermediate text output.
  /// @param L Prefixed long output filenames with the source path.
  /// @param M Demangle function names in reports.
  /// @param N Suppress writing output files.
  /// @param R Only report files under the source prefix.
  /// @param T Write reports to standard output.
  /// @param X Hash pathnames in output filenames.
  /// @param SourcePrefix Source-tree prefix used with relative-only reporting.
  Options(bool A, bool B, bool C, bool F, bool P, bool U, bool I, bool L,
          bool M, bool N, bool R, bool T, bool X, std::string SourcePrefix)
      : AllBlocks(A), BranchInfo(B), BranchCount(C), FuncCoverage(F),
        PreservePaths(P), UncondBranch(U), Intermediate(I), LongFileNames(L),
        Demangle(M), NoOutput(N), RelativeOnly(R), UseStdout(T),
        HashFilenames(X), SourcePrefix(std::move(SourcePrefix)) {}

  /// Report coverage for every basic block.
  bool AllBlocks;
  /// Include branch probability information in reports.
  bool BranchInfo;
  /// Print absolute branch counts instead of percentages.
  bool BranchCount;
  /// Include per-function coverage summaries.
  bool FuncCoverage;
  /// Keep full path components when naming output files.
  bool PreservePaths;
  /// Include unconditional branches in branch reports.
  bool UncondBranch;
  /// Emit intermediate text coverage output.
  bool Intermediate;
  /// Prefixed long output filenames with the source path.
  bool LongFileNames;
  /// Demangle C++ function names in reports.
  bool Demangle;
  /// Do not write coverage output files.
  bool NoOutput;
  /// Restrict reporting to files under \c SourcePrefix.
  bool RelativeOnly;
  /// Write coverage reports to standard output.
  bool UseStdout;
  /// Hash pathnames when forming output filenames.
  bool HashFilenames;
  /// Source-tree prefix used with relative-only reporting.
  std::string SourcePrefix;
};

} // end namespace GCOV

/// GCOVBuffer - A wrapper around MemoryBuffer to provide GCOV specific
/// read operations.
class GCOVBuffer {
public:
  /// Construct a buffer reader over the given memory buffer.
  /// @param B Memory buffer holding a .gcno or .gcda file.
  GCOVBuffer(MemoryBuffer *B) : Buffer(B) {}
  /// Destroy the buffer and consume any outstanding cursor errors.
  ~GCOVBuffer() { consumeError(cursor.takeError()); }

  /// readGCNOFormat - Check GCNO signature is valid at the beginning of buffer.
  /// @return True if the GCNO magic signature is valid.
  bool readGCNOFormat() {
    StringRef buf = Buffer->getBuffer();
    StringRef magic = buf.substr(0, 4);
    if (magic == "gcno") {
      de = DataExtractor(buf.substr(4), false);
    } else if (magic == "oncg") {
      de = DataExtractor(buf.substr(4), true);
    } else {
      errs() << "unexpected magic: " << magic << "\n";
      return false;
    }
    return true;
  }

  /// readGCDAFormat - Check GCDA signature is valid at the beginning of buffer.
  /// @return True if the GCDA magic signature is valid.
  bool readGCDAFormat() {
    StringRef buf = Buffer->getBuffer();
    StringRef magic = buf.substr(0, 4);
    if (magic == "gcda") {
      de = DataExtractor(buf.substr(4), false);
    } else if (magic == "adcg") {
      de = DataExtractor(buf.substr(4), true);
    } else {
      return false;
    }
    return true;
  }

  /// readGCOVVersion - Read GCOV version.
  /// @param version Out-parameter set to the recognized format version.
  /// @return True if a supported gcov version was recognized.
  bool readGCOVVersion(GCOV::GCOVVersion &version) {
    std::string str(de.getBytes(cursor, 4));
    if (str.size() != 4)
      return false;
    if (de.isLittleEndian())
      std::reverse(str.begin(), str.end());
    int ver = str[0] >= 'A'
                  ? (str[0] - 'A') * 100 + (str[1] - '0') * 10 + str[2] - '0'
                  : (str[0] - '0') * 10 + str[2] - '0';
    if (ver >= 120) {
      this->version = version = GCOV::V1200;
      return true;
    } else if (ver >= 90) {
      // PR gcov-profile/84846, r269678
      this->version = version = GCOV::V900;
      return true;
    } else if (ver >= 80) {
      // PR gcov-profile/48463
      this->version = version = GCOV::V800;
      return true;
    } else if (ver >= 48) {
      // r189778: the exit block moved from the last to the second.
      this->version = version = GCOV::V408;
      return true;
    } else if (ver >= 47) {
      // r173147: split checksum into cfg checksum and line checksum.
      this->version = version = GCOV::V407;
      return true;
    } else if (ver >= 34) {
      this->version = version = GCOV::V304;
      return true;
    }
    errs() << "unexpected version: " << str << "\n";
    return false;
  }

  /// Read the next 32-bit word from the buffer.
  /// @return Next 32-bit word from the buffer.
  uint32_t getWord() { return de.getU32(cursor); }
  /// Read the next gcov string as a view into the buffer.
  /// @return String view into the buffer, or empty on failure.
  StringRef getString() {
    uint32_t len;
    if (!readInt(len) || len == 0)
      return {};
    return de.getBytes(cursor, len * 4).split('\0').first;
  }

  /// Read a 32-bit integer into \p Val.
  /// @param Val Out-parameter receiving the integer value.
  /// @return True if the integer was read successfully.
  bool readInt(uint32_t &Val) {
    if (cursor.tell() + 4 > de.size()) {
      Val = 0;
      errs() << "unexpected end of memory buffer: " << cursor.tell() << "\n";
      return false;
    }
    Val = de.getU32(cursor);
    return true;
  }

  /// Read a 64-bit integer assembled from two 32-bit halves.
  /// @param Val Out-parameter receiving the integer value.
  /// @return True if both halves were read successfully.
  bool readInt64(uint64_t &Val) {
    uint32_t Lo, Hi;
    if (!readInt(Lo) || !readInt(Hi))
      return false;
    Val = ((uint64_t)Hi << 32) | Lo;
    return true;
  }

  /// Read a gcov-encoded string into \p str.
  /// @param str Out-parameter receiving the decoded string view.
  /// @return True if a string was read successfully.
  bool readString(StringRef &str) {
    uint32_t len;
    if (!readInt(len) || len == 0)
      return false;
    if (version >= GCOV::V1200)
      str = de.getBytes(cursor, len).drop_back();
    else
      str = de.getBytes(cursor, len * 4).split('\0').first;
    return bool(cursor);
  }

  /// Data extractor positioned after the gcov magic word.
  DataExtractor de{ArrayRef<uint8_t>{}, false};
  /// Read cursor advanced through \c de.
  DataExtractor::Cursor cursor{0};

private:
  MemoryBuffer *Buffer;
  GCOV::GCOVVersion version{};
};

/// GCOVFile - Collects coverage information for one pair of coverage file
/// (.gcno and .gcda).
class GCOVFile {
public:
  /// Construct an empty coverage file with no note or data content.
  GCOVFile() = default;

  /// Read notes (.gcno) records into this file.
  /// @param Buffer Buffer positioned at a GCNO stream.
  /// @return True if the notes file was read successfully.
  LLVM_ABI bool readGCNO(GCOVBuffer &Buffer);
  /// Read data (.gcda) records into this file.
  /// @param Buffer Buffer positioned at a GCDA stream.
  /// @return True if the data file was read successfully.
  LLVM_ABI bool readGCDA(GCOVBuffer &Buffer);
  /// Return the gcov format version read from the notes file.
  /// @return Gcov format version from the notes file.
  GCOV::GCOVVersion getVersion() const { return version; }
  /// Print a textual summary of this coverage file to \p OS.
  /// @param OS Output stream receiving the summary.
  LLVM_ABI void print(raw_ostream &OS) const;
  /// Dump a textual summary of this coverage file to standard error.
  LLVM_ABI void dump() const;

  /// Source filenames referenced by the notes file.
  std::vector<std::string> filenames;
  /// Map from source filename to index in \c filenames.
  StringMap<unsigned> filenameToIdx;

public:
  /// True once a notes (.gcno) file has been successfully read.
  bool GCNOInitialized = false;
  /// Gcov format version from the notes file.
  GCOV::GCOVVersion version{};
  /// Checksum from the notes file header.
  uint32_t checksum = 0;
  /// Working directory recorded in the notes file, if present.
  StringRef cwd;
  /// Functions described by the notes and data files.
  SmallVector<std::unique_ptr<GCOVFunction>, 16> functions;
  /// Map from function identifier to the corresponding \c GCOVFunction.
  std::map<uint32_t, GCOVFunction *> identToFunction;
  /// Number of profile runs accumulated in the data file.
  uint32_t runCount = 0;
  /// Number of programs recorded in the data file.
  uint32_t programCount = 0;

  /// Iterator over the functions owned by this file.
  using iterator = pointee_iterator<
      SmallVectorImpl<std::unique_ptr<GCOVFunction>>::const_iterator>;
  /// Return an iterator to the first function.
  /// @return Iterator to the first function.
  iterator begin() const { return iterator(functions.begin()); }
  /// Return an iterator past the last function.
  /// @return Iterator past the last function.
  iterator end() const { return iterator(functions.end()); }

private:
  unsigned addNormalizedPathToMap(StringRef filename);
};

/// Directed edge between two basic blocks in a gcov CFG.
struct GCOVArc {
  /// Construct an arc from \p src to \p dst with the given gcov flags.
  /// @param src Source basic block.
  /// @param dst Destination basic block.
  /// @param flags Gcov arc flags (for example on-tree).
  GCOVArc(GCOVBlock &src, GCOVBlock &dst, uint32_t flags)
      : src(src), dst(dst), flags(flags) {}
  /// Return true if this arc belongs to the spanning tree.
  /// @return True if this arc belongs to the spanning tree.
  LLVM_ABI bool onTree() const;

  /// Source basic block of this arc.
  GCOVBlock &src;
  /// Destination basic block of this arc.
  GCOVBlock &dst;
  /// Gcov arc flags from the notes file.
  uint32_t flags;
  /// Execution count for this arc.
  uint64_t count = 0;
  /// Cycle count contribution attributed to this arc.
  uint64_t cycleCount = 0;
};

/// GCOVFunction - Collects function information.
class GCOVFunction {
public:
  /// Iterator over the basic blocks owned by this function.
  using BlockIterator = pointee_iterator<
      SmallVectorImpl<std::unique_ptr<GCOVBlock>>::const_iterator>;

  /// Construct a function belonging to \p file.
  /// @param file Coverage file that owns this function.
  GCOVFunction(GCOVFile &file) : file(file) {}

  /// Return the function name, optionally demangled.
  /// @param demangle When true, demangle C++ names before returning.
  /// @return Function name, demangled when \p demangle is true.
  LLVM_ABI StringRef getName(bool demangle) const;
  /// Return the source filename associated with this function.
  /// @return Source filename associated with this function.
  LLVM_ABI StringRef getFilename() const;
  /// Return the execution count of the function entry block.
  /// @return Execution count of the function entry block.
  LLVM_ABI uint64_t getEntryCount() const;
  /// Return the function exit block.
  /// @return Reference to the function exit block.
  LLVM_ABI GCOVBlock &getExitBlock() const;

  /// Return a range over the basic blocks of this function.
  /// @return Iterator range over the function's basic blocks.
  iterator_range<BlockIterator> blocksRange() const {
    return make_range(blocks.begin(), blocks.end());
  }

  /// Propagate arc counts through the CFG starting from block \p v.
  /// @param v Block at which count propagation begins.
  /// @param pred Predecessor arc used to seed propagation; may be null.
  LLVM_ABI void propagateCounts(const GCOVBlock &v, GCOVArc *pred);
  /// Print a textual summary of this function to \p OS.
  /// @param OS Output stream receiving the summary.
  LLVM_ABI void print(raw_ostream &OS) const;
  /// Dump a textual summary of this function to standard error.
  LLVM_ABI void dump() const;

  /// Coverage file that owns this function.
  GCOVFile &file;
  /// Function identifier from the notes file.
  uint32_t ident = 0;
  /// Line-number checksum from the notes file.
  uint32_t linenoChecksum;
  /// CFG checksum from the notes file.
  uint32_t cfgChecksum = 0;
  /// Starting source line of the function.
  uint32_t startLine = 0;
  /// Starting source column of the function.
  uint32_t startColumn = 0;
  /// Ending source line of the function.
  uint32_t endLine = 0;
  /// Ending source column of the function.
  uint32_t endColumn = 0;
  /// Non-zero when the function is compiler-generated (artificial).
  uint8_t artificial = 0;
  /// Mangled function name from the notes file.
  StringRef Name;
  /// Cached demangled form of \c Name.
  mutable SmallString<0> demangled;
  /// Index into the owning file's filename list for this function.
  unsigned srcIdx;
  /// Basic blocks belonging to this function.
  SmallVector<std::unique_ptr<GCOVBlock>, 0> blocks;
  SmallVector<std::unique_ptr<GCOVArc>, 0>
      arcs,     ///< Non-tree arcs of this function.
      treeArcs; ///< Spanning-tree arcs of this function.
  /// Blocks already visited during count propagation.
  DenseSet<const GCOVBlock *> visited;
};

/// Represent file of lines same with block_location_info in gcc.
struct GCOVBlockLocation {
  /// Construct a location for the source file at index \p idx.
  /// @param idx Index into the owning file's filename list.
  GCOVBlockLocation(unsigned idx) : srcIdx(idx) {}

  /// Index into the owning file's filename list.
  unsigned srcIdx;
  /// Source line numbers associated with this location.
  SmallVector<uint32_t, 4> lines;
};

/// GCOVBlock - Collects block information.
class GCOVBlock {
public:
  /// Const iterator over arcs incident to this block.
  using EdgeIterator = SmallVectorImpl<GCOVArc *>::const_iterator;
  /// List of basic-block pointers.
  using BlockVector = SmallVector<const GCOVBlock *, 1>;
  /// List of basic-block lists, used when computing cycles.
  using BlockVectorLists = SmallVector<BlockVector, 4>;
  /// List of arc pointers.
  using Edges = SmallVector<GCOVArc *, 4>;

  /// Construct a block with the given block number.
  /// @param N Block number from the notes file.
  GCOVBlock(uint32_t N) : number(N) {}

  /// Append source line \p N to the current file location.
  /// @param N Source line number to record.
  void addLine(uint32_t N) {
    locations.back().lines.push_back(N);
    lastLine = N;
  }
  /// Start a new file location for source file index \p fileIdx.
  /// @param fileIdx Index into the owning file's filename list.
  void addFile(unsigned fileIdx) { locations.emplace_back(fileIdx); }

  /// Return the most recently recorded source line number.
  /// @return Most recently recorded source line for this block.
  uint32_t getLastLine() const { return lastLine; }
  /// Return the execution count of this block.
  /// @return Execution count of this block.
  uint64_t getCount() const { return count; }

  /// Record an incoming arc to this block.
  /// @param Edge Predecessor arc ending at this block.
  void addSrcEdge(GCOVArc *Edge) { pred.push_back(Edge); }

  /// Record an outgoing arc from this block.
  /// @param Edge Successor arc starting at this block.
  void addDstEdge(GCOVArc *Edge) { succ.push_back(Edge); }

  /// Return a range over the predecessor arcs of this block.
  /// @return Iterator range over incoming arcs.
  iterator_range<EdgeIterator> srcs() const {
    return make_range(pred.begin(), pred.end());
  }

  /// Return a range over the successor arcs of this block.
  /// @return Iterator range over outgoing arcs.
  iterator_range<EdgeIterator> dsts() const {
    return make_range(succ.begin(), succ.end());
  }

  /// Print a textual summary of this block to \p OS.
  /// @param OS Output stream receiving the summary.
  LLVM_ABI void print(raw_ostream &OS) const;
  /// Dump a textual summary of this block to standard error.
  LLVM_ABI void dump() const;

  /// Augment cycle counts along one cycle found from \p src.
  /// @param src Block used as the search root for a cycle.
  /// @param stack Work stack of blocks and successor indices.
  /// @return Cycle count discovered for one cycle, or zero if none.
  LLVM_ABI static uint64_t
  augmentOneCycle(GCOVBlock *src,
                  std::vector<std::pair<GCOVBlock *, size_t>> &stack);
  /// Return the total cycle count over the given blocks.
  /// @param blocks Blocks whose cycle counts are summed.
  /// @return Sum of cycle counts over \p blocks.
  LLVM_ABI static uint64_t getCyclesCount(const BlockVector &blocks);
  /// Return the number of source lines covered by the given blocks.
  /// @param Blocks Blocks whose line coverage is counted.
  /// @return Number of source lines covered by \p Blocks.
  LLVM_ABI static uint64_t getLineCount(const BlockVector &Blocks);

public:
  /// Block number from the notes file.
  uint32_t number;
  /// Execution count of this block.
  uint64_t count = 0;
  /// Incoming arcs to this block.
  SmallVector<GCOVArc *, 2> pred;
  /// Outgoing arcs from this block.
  SmallVector<GCOVArc *, 2> succ;
  /// Source file locations and lines belonging to this block.
  SmallVector<GCOVBlockLocation> locations;
  /// Most recently recorded source line for this block.
  uint32_t lastLine = 0;
  /// True when this block may still be traversed for cycle detection.
  bool traversable = false;
  /// Incoming arc used while detecting cycles; may be null.
  GCOVArc *incoming = nullptr;
};

/// Process one input coverage file pair and emit reports per \p options.
/// @param options Reporting options controlling output.
/// @param filename Display name of the coverage input.
/// @param gcno Path to the notes (.gcno) file.
/// @param gcda Path to the data (.gcda) file.
/// @param file Coverage file object to populate and report.
LLVM_ABI void gcovOneInput(const GCOV::Options &options, StringRef filename,
                           StringRef gcno, StringRef gcda, GCOVFile &file);

} // end namespace llvm

#endif // LLVM_PROFILEDATA_GCOV_H
