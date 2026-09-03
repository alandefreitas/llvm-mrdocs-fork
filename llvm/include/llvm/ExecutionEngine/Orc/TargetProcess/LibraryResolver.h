//===- LibraryResolver.h - Automatic Library Symbol Resolution -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides support for automatically searching symbols across
// dynamic libraries that have not yet been loaded.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_LIBRARYRESOLVER_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_LIBRARYRESOLVER_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ExecutionEngine/Orc/Shared/SymbolFilter.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/LibraryScanner.h"
#include "llvm/Support/Path.h"

#include <atomic>
#include <shared_mutex>

namespace llvm {
namespace orc {

class LibraryManager;

/// Load and query progress for a tracked dynamic library.
enum class LibState : uint8_t {
  /// Library path is known but not yet loaded into the process.
  Unloaded = 0,
  /// Library has been loaded (or treated as loaded) for searching.
  Loaded = 1,
  /// Symbols in the library have already been queried.
  Queried = 2
};

/// Metadata for a single dynamic library used during symbol resolution.
class LibraryInfo {
public:
  /// Deleted copy constructor; LibraryInfo is move-only via unique ownership.
  /// \param Other LibraryInfo that would be copied.
  LibraryInfo(const LibraryInfo &Other) = delete;
  /// Deleted copy assignment; LibraryInfo is move-only via unique ownership.
  /// \param Other LibraryInfo that would be copied.
  /// \return Reference to this LibraryInfo.
  LibraryInfo &operator=(const LibraryInfo &Other) = delete;

  /// Construct library metadata for \p FilePath with state \p S and kind \p K.
  /// \param FilePath Full path to the library file.
  /// \param S Initial load/query state.
  /// \param K Whether the path is a user or system library path.
  /// \param Filter Optional Bloom filter of symbols contained in the library.
  LibraryInfo(std::string FilePath, LibState S, PathType K,
              std::optional<BloomFilter> Filter = std::nullopt)
      : FilePath(std::move(FilePath)), S(S), K(K), Filter(std::move(Filter)) {}

  /// Return the directory portion of the library path.
  /// \return The directory portion of the library path.
  StringRef getBasePath() const { return sys::path::parent_path(FilePath); }
  /// Return the file name portion of the library path.
  /// \return The file name portion of the library path.
  StringRef getFileName() const { return sys::path::filename(FilePath); }

  /// Return the full library file path.
  /// \return The full library file path as a std::string.
  std::string getFullPath() const { return FilePath; }

  /// Install a Bloom filter if one is not already present.
  /// \param F Bloom filter describing symbols that may be in this library.
  void setFilter(BloomFilter F) {
    std::lock_guard<std::shared_mutex> Lock(Mtx);
    if (Filter)
      return;
    Filter.emplace(std::move(F));
  }

  /// Build and store a Bloom filter from \p Symbols if none exists yet.
  /// \param FB Builder used to construct the Bloom filter.
  /// \param Symbols Symbol names known to be defined in this library.
  void ensureFilterBuilt(const BloomFilterBuilder &FB,
                         ArrayRef<StringRef> Symbols) {
    std::lock_guard<std::shared_mutex> Lock(Mtx);
    if (Filter)
      return;
    Filter.emplace(FB.build(Symbols));
  }

  /// Return true if the Bloom filter reports that \p Symbol may be present.
  /// \param Symbol Symbol name to test against the filter.
  /// \return True if the filter reports \p Symbol may be present; false
  /// otherwise.
  bool mayContain(StringRef Symbol) const {
    assert(hasFilter());
    std::shared_lock<std::shared_mutex> Lock(Mtx);
    return Filter->mayContain(Symbol);
  }

  /// Return true if a Bloom filter has been installed for this library.
  /// \return True if a Bloom filter is installed; false otherwise.
  bool hasFilter() const {
    std::shared_lock<std::shared_mutex> Lock(Mtx);
    return Filter.has_value();
  }

  /// Return the current load/query state.
  /// \return The current LibState.
  LibState getState() const { return S.load(); }
  /// Return whether this library is from a user or system path.
  /// \return The PathType of this library.
  PathType getKind() const { return K; }

  /// Update the load/query state.
  /// \param s New state to store.
  void setState(LibState s) { S.store(s); }

  /// Return true if both libraries refer to the same file path.
  /// \param other Other library metadata to compare against.
  /// \return True if both libraries share the same file path; false otherwise.
  bool operator==(const LibraryInfo &other) const {
    return FilePath == other.FilePath;
  }

private:
  std::string FilePath;
  std::atomic<LibState> S;
  PathType K;
  std::optional<BloomFilter> Filter;
  mutable std::shared_mutex Mtx;
};

/// Iterates libraries that match a requested LibState.
///
/// Walks a list of LibraryInfo pointers and yields only those whose state
/// matches the cursor's target. LibraryIndex supplies the lists by PathType,
/// and the cursor filters them while iterating.
class LibraryCursor {
public:
  /// Construct a cursor over \p L that yields libraries in state \p S.
  /// \param L Libraries to walk (not owned).
  /// \param S Required LibState for a library to be considered valid.
  LibraryCursor(const std::vector<const LibraryInfo *> &L, LibState S)
      : Lists(L), S(S) {}

  /// Advance to and return the next library whose state matches, or nullptr.
  /// \return Next matching LibraryInfo, or nullptr if none remain.
  const LibraryInfo *nextValidLib() {
    while (Pos < Lists.size()) {
      const LibraryInfo *Lib = Lists[Pos++];
      if (Lib->getState() == S)
        return Lib;
    }

    return nullptr;
  }

  /// Return true if any libraries remain unvisited in the underlying list.
  /// \return True if unvisited libraries remain; false otherwise.
  bool hasMoreValidLib() const { return Pos < Lists.size(); }

private:
  const std::vector<const LibraryInfo *> &Lists;
  LibState S;
  size_t Pos = 0; // cursor position
};

/// Indexes libraries by PathType for state-filtered iteration.
///
/// Keeps libraries grouped by PathType and provides cursors that walk
/// libraries of a specific type and LibState.
class LibraryIndex {
  friend LibraryManager;

public:
  /// Return a cursor over libraries of kind \p K that are in state \p S.
  /// \param K User or system path kind to select.
  /// \param S Required LibState for yielded libraries.
  /// \return A LibraryCursor over matching libraries.
  LibraryCursor getCursor(PathType K, LibState S) const {
    static std::vector<const LibraryInfo *> Empty;
    auto It = Lists.find(K);
    if (It == Lists.end())
      return LibraryCursor(Empty, S);

    return LibraryCursor(It->second, S);
  }

  /// Return true if kind \p K has a library at or beyond index \p Idx.
  /// \param K User or system path kind to inspect.
  /// \param Idx Zero-based index into that kind's library list.
  /// \return True if a library remains at or beyond \p Idx; false otherwise.
  bool hasLibLeftFor(PathType K, uint32_t Idx) const {
    auto It = Lists.find(K);
    if (It == Lists.end())
      return false;

    const auto &L = It->second;
    return L.size() > Idx;
  }

private:
  void addLibrary(const LibraryInfo *Lib) {
    Lists[Lib->getKind()].push_back(Lib);
  }

  void clear() { Lists.clear(); }

  DenseMap<PathType, std::vector<const LibraryInfo *>> Lists;
};

/// Manages library metadata and state for symbol resolution.
///
/// Tracks libraries by load state and kind (user/system), and stores
/// associated Bloom filters and hash maps to speed up symbol lookups.
/// Thread-safe for concurrent access.
class LibraryManager {
public:
  /// A read-only view of libraries filtered by state and kind.
  ///
  /// Lets you loop over only the libraries in a map that match a given State
  /// and PathType.
  class FilteredView {
  public:
    /// Map from library path to owned LibraryInfo.
    using Map = StringMap<std::unique_ptr<LibraryInfo>>;
    /// Const iterator over the underlying path-to-info map.
    using Iterator = Map::const_iterator;
    /// Iterator that skips libraries not matching the view's state and kind.
    class FilterIterator {
    public:
      /// Construct a filter iterator starting at \p it_ and bounded by \p end_.
      /// \param it_ Current map iterator position.
      /// \param end_ One-past-the-end map iterator.
      /// \param S Required LibState for yielded libraries.
      /// \param K Required PathType for yielded libraries.
      FilterIterator(Iterator it_, Iterator end_, LibState S, PathType K)
          : it(it_), end(end_), S(S), K(K) {
        advance();
      }

      /// Return true if this iterator does not refer to the same position as
      /// \p other.
      /// \param other Other filter iterator to compare against.
      /// \return True if the iterators differ; false if they refer to the same
      /// position.
      bool operator!=(const FilterIterator &other) const {
        return it != other.it;
      }

      /// Return a const reference to the current LibraryInfo.
      /// \return Const reference to the current LibraryInfo.
      const LibraryInfo &operator*() const { return *it->second; }

      /// Advance to the next library matching the filter criteria.
      /// \return Reference to this iterator after advancing.
      FilterIterator &operator++() {
        ++it;
        advance();
        return *this;
      }

    private:
      void advance() {
        for (; it != end; ++it)
          if (it->second->getState() == S && it->second->getKind() == K)
            break;
      }
      Iterator it;
      Iterator end;
      LibState S;
      PathType K;
    };
    /// Construct a filtered view over [\p begin, \p end) for state \p s and
    /// kind \p k.
    /// \param begin First map entry to consider.
    /// \param end One-past-the-end map entry.
    /// \param s Required LibState for included libraries.
    /// \param k Required PathType for included libraries.
    FilteredView(Iterator begin, Iterator end, LibState s, PathType k)
        : mapBegin(begin), mapEnd(end), state(s), kind(k) {}

    /// Return an iterator to the first matching library.
    /// \return A FilterIterator to the first matching library.
    FilterIterator begin() const {
      return FilterIterator(mapBegin, mapEnd, state, kind);
    }

    /// Return an end iterator for the filtered view.
    /// \return An end FilterIterator for the view.
    FilterIterator end() const {
      return FilterIterator(mapEnd, mapEnd, state, kind);
    }

  private:
    Iterator mapBegin;
    Iterator mapEnd;
    LibState state;
    PathType kind;
  };

private:
  LibraryIndex Index;
  StringMap<std::unique_ptr<LibraryInfo>> Libraries;
  mutable std::shared_mutex Mtx;

public:
  /// Callback visited for each library; return false to stop iteration.
  using LibraryVisitor = std::function<bool(const LibraryInfo &)>;

  /// Construct an empty library manager.
  LibraryManager() = default;
  /// Destroy the library manager and release owned library metadata.
  ~LibraryManager() = default;

  /// Register a library at \p Path if it is not already tracked.
  /// \param Path Full path of the library to add.
  /// \param Kind Whether \p Path is a user or system library path.
  /// \param Filter Optional Bloom filter of symbols in the library.
  /// \return True if the library was newly added; false if already present.
  bool addLibrary(std::string Path, PathType Kind,
                  std::optional<BloomFilter> Filter = std::nullopt) {
    std::unique_lock<std::shared_mutex> Lock(Mtx);
    if (Libraries.count(Path) > 0)
      return false;
    std::unique_ptr<LibraryInfo> Lib = std::make_unique<LibraryInfo>(
        Path, LibState::Unloaded, Kind, std::move(Filter));
    const LibraryInfo *Ptr = Lib.get();
    Libraries.insert({Path, std::move(Lib)});
    Index.addLibrary(Ptr);
    return true;
  }

  /// Return true if a library with path \p Path is tracked.
  /// \param Path Full library path to look up.
  /// \return True if the library is tracked; false otherwise.
  bool hasLibrary(StringRef Path) const {
    std::shared_lock<std::shared_mutex> Lock(Mtx);
    if (Libraries.count(Path) > 0)
      return true;
    return false;
  }

  /// Remove the tracked library at \p Path, if present.
  /// \param Path Full library path to remove.
  void removeLibrary(StringRef Path) {
    std::unique_lock<std::shared_mutex> Lock(Mtx);
    auto I = Libraries.find(Path);
    if (I == Libraries.end())
      return;
    Libraries.erase(I);
  }

  /// Mark the library at \p Path as Loaded.
  /// \param Path Full library path whose state is updated.
  void markLoaded(StringRef Path) {
    std::unique_lock<std::shared_mutex> Lock(Mtx);
    if (auto It = Libraries.find(Path); It != Libraries.end())
      It->second->setState(LibState::Loaded);
  }

  /// Mark the library at \p Path as Unloaded.
  /// \param Path Full library path whose state is updated.
  void markUnloaded(StringRef Path) {
    std::unique_lock<std::shared_mutex> Lock(Mtx);
    if (auto It = Libraries.find(Path); It != Libraries.end())
      It->second->setState(LibState::Unloaded);
  }

  /// Mark the library at \p Path as Queried.
  /// \param Path Full library path whose state is updated.
  void markQueried(StringRef Path) {
    std::unique_lock<std::shared_mutex> Lock(Mtx);
    if (auto It = Libraries.find(Path); It != Libraries.end())
      It->second->setState(LibState::Queried);
  }

  /// Return a pointer to the LibraryInfo for \p Path, or nullptr if absent.
  /// \param Path Full library path to look up.
  /// \return Pointer to the LibraryInfo, or nullptr if not tracked.
  const LibraryInfo *getLibrary(StringRef Path) const {
    std::shared_lock<std::shared_mutex> Lock(Mtx);
    if (auto It = Libraries.find(Path); It != Libraries.end())
      return It->second.get();
    return nullptr;
  }

  /// Return a filtered view of libraries in state \p S and kind \p K.
  /// \param S Required LibState for included libraries.
  /// \param K Required PathType for included libraries.
  /// \return A FilteredView of matching libraries.
  FilteredView getView(LibState S, PathType K) const {
    std::shared_lock<std::shared_mutex> Lock(Mtx);
    return FilteredView(Libraries.begin(), Libraries.end(), S, K);
  }

  /// Predicate that returns true when a library should be included.
  using LibraryFilterFn = std::function<bool(const LibraryInfo &)>;
  /// Append libraries matching \p S, \p K, and optional \p Filter into \p Outs.
  /// \param S Required LibState for included libraries.
  /// \param K Required PathType for included libraries.
  /// \param Outs Destination vector that receives matching library pointers.
  /// \param Filter Optional additional predicate; nullptr includes all matches.
  void getLibraries(LibState S, PathType K,
                    std::vector<const LibraryInfo *> &Outs,
                    LibraryFilterFn Filter = nullptr) const {
    std::shared_lock<std::shared_mutex> Lock(Mtx);
    for (const auto &[_, Entry] : Libraries) {
      const auto &Info = *Entry;
      if (Info.getKind() != K || Info.getState() != S)
        continue;
      if (Filter && !Filter(Info))
        continue;
      Outs.push_back(&Info);
    }
  }

  /// Return an index cursor over libraries of kind \p K in state \p S.
  /// \param K User or system path kind to select.
  /// \param S Required LibState for yielded libraries.
  /// \return A LibraryCursor over matching libraries.
  LibraryCursor getCursor(PathType K, LibState S) const {
    return Index.getCursor(K, S);
  }

  /// Invoke \p visitor for each tracked library until it returns false.
  /// \param visitor Callback receiving each LibraryInfo; false stops early.
  void forEachLibrary(const LibraryVisitor &visitor) const {
    std::shared_lock<std::shared_mutex> Lock(Mtx);
    for (const auto &[_, entry] : Libraries) {
      if (!visitor(*entry))
        break;
    }
  }

  /// Return true if the library at \p Path is in the Loaded state.
  /// \param Path Full library path to inspect.
  /// \return True if the library is Loaded; false otherwise.
  bool isLoaded(StringRef Path) const {
    std::shared_lock<std::shared_mutex> Lock(Mtx);
    if (auto It = Libraries.find(Path.str()); It != Libraries.end())
      return It->second->getState() == LibState::Loaded;
    return false;
  }

  /// Return true if the library at \p Path is in the Queried state.
  /// \param Path Full library path to inspect.
  /// \return True if the library is Queried; false otherwise.
  bool isQueried(StringRef Path) const {
    std::shared_lock<std::shared_mutex> Lock(Mtx);
    if (auto It = Libraries.find(Path.str()); It != Libraries.end())
      return It->second->getState() == LibState::Queried;
    return false;
  }

  /// Remove all tracked libraries and clear the index.
  void clear() {
    std::unique_lock<std::shared_mutex> Lock(Mtx);
    Libraries.clear();
  }
};

/// One step in a symbol search plan: which state and path kind to search next.
struct SearchPlanEntry {
  /// Library load/query state to search at this step.
  LibState State; // Loaded, Queried, Unloaded
  /// User or system path kind to search at this step.
  PathType Type;  // User, System
};

/// Ordered plan of library states and kinds to search when resolving symbols.
struct SearchPolicy {
  /// Sequence of search steps to try, in order.
  std::vector<SearchPlanEntry> Plan;

  /// Return the default search order over user then system libraries.
  /// \return A SearchPolicy with the default user-then-system order.
  static SearchPolicy defaultPlan() {
    return {{{LibState::Loaded, PathType::User},
             {LibState::Queried, PathType::User},
             {LibState::Unloaded, PathType::User},
             {LibState::Loaded, PathType::System},
             {LibState::Queried, PathType::System},
             {LibState::Unloaded, PathType::System}}};
  }
};

/// Options controlling which symbols are yielded during enumeration.
struct SymbolEnumeratorOptions {
  /// Bit flags selecting symbol categories to ignore while enumerating.
  enum Filter : uint32_t {
    /// Include all symbols; apply no category filters.
    None = 0,
    /// Skip undefined symbols.
    IgnoreUndefined = 1 << 0,
    /// Skip weak symbols.
    IgnoreWeak = 1 << 1,
    /// Skip indirect symbols.
    IgnoreIndirect = 1 << 2,
    /// Skip symbols that are not global.
    IgnoreNonGlobal = 1 << 3,
    /// Skip hidden symbols.
    IgnoreHidden = 1 << 4,
    /// Skip symbols that are not exported.
    IgnoreNonExported = 1 << 5
  };

  /// Return the default filter flags used for symbol enumeration.
  /// \return Default SymbolEnumeratorOptions with common ignore flags.
  static SymbolEnumeratorOptions defaultOptions() {
    return {Filter::IgnoreUndefined | Filter::IgnoreHidden |
            Filter::IgnoreNonGlobal};
  }
  /// Combined Filter bit flags applied during enumeration.
  uint32_t FilterFlags = Filter::None;
};

/// Combined search policy and enumerator options for a symbol search.
struct SearchConfig {
  /// Ordered plan of library states and kinds to search.
  SearchPolicy Policy;
  /// Filters applied while enumerating symbols in each library.
  SymbolEnumeratorOptions Options;

  /// Construct a SearchConfig with the default policy and enumerator options.
  SearchConfig()
      : Policy(SearchPolicy::defaultPlan()), // default plan
        Options(SymbolEnumeratorOptions::defaultOptions()) {}
};

/// Scans libraries and resolves Symbols across user and system paths.
///
/// Supports symbol enumeration and filtering via SymbolEnumerator, and tracks
/// symbol resolution results through SymbolQuery. Thread-safe and uses
/// LibraryScanHelper for efficient path resolution and caching.
class LibraryResolver {
  friend class LibraryResolutionDriver;

public:
  /// Enumerates symbols from object files or libraries on disk.
  class SymbolEnumerator {
  public:
    /// Result of a per-symbol enumeration callback.
    enum class EnumerateResult {
      /// Continue enumerating remaining symbols.
      Continue,
      /// Stop enumeration successfully without treating it as an error.
      Stop,
      /// Abort enumeration and report failure to the caller.
      Error
    };

    /// Callback invoked for each enumerated symbol name.
    using OnEachSymbolFn = std::function<EnumerateResult(StringRef Sym)>;

    /// Enumerate symbols in \p Obj, invoking \p OnEach for each accepted name.
    /// \param Obj Object file whose symbols are enumerated.
    /// \param OnEach Callback receiving each symbol; controls continuation.
    /// \param Opts Filters controlling which symbols are yielded.
    /// \return True on success; false if enumeration failed or OnEach erred.
    LLVM_ABI static bool enumerateSymbols(object::ObjectFile *Obj,
                                          OnEachSymbolFn OnEach,
                                          const SymbolEnumeratorOptions &Opts);
    /// Enumerate symbols in the library at \p Path, invoking \p OnEach.
    /// \param Path Path to a library or object file on disk.
    /// \param OnEach Callback receiving each symbol; controls continuation.
    /// \param Opts Filters controlling which symbols are yielded.
    /// \return True on success; false if enumeration failed or OnEach erred.
    LLVM_ABI static bool enumerateSymbols(StringRef Path, OnEachSymbolFn OnEach,
                                          const SymbolEnumeratorOptions &Opts);
  };

  /// Tracks a set of symbols and the libraries where they are resolved.
  ///
  /// SymbolQuery is used to keep track of which symbols have been resolved
  /// to which libraries. It supports concurrent read/write access using a
  /// shared mutex, allowing multiple readers or a single writer at a time.
  class SymbolQuery {
  public:
    /// Holds the result for a single symbol.
    struct Entry {
      /// Unresolved or resolved symbol name.
      std::string Name;
      /// Full path of the library that resolved the symbol, or empty if not.
      std::string ResolvedLibPath;
    };

  private:
    mutable std::shared_mutex Mtx;
    SmallVector<Entry, 24> Entries;
    std::atomic<size_t> ResolvedCount = 0;

  public:
    /// Construct a query tracking each unique name in \p Symbols.
    /// \param Symbols Symbol names to resolve.
    explicit SymbolQuery(ArrayRef<StringRef> Symbols) {
      for (const auto &S : Symbols) {
        if (!contains(S))
          Entries.push_back({S.str(), ""});
      }
    }

    /// Return true if \p Name is among the symbols tracked by this query.
    /// \param Name Symbol name to look up.
    /// \return True if \p Name is tracked; false otherwise.
    bool contains(StringRef Name) const {
      return llvm::any_of(Entries,
                          [&](const Entry &E) { return E.Name == Name; });
    }

    /// Predicate that returns true when an unresolved symbol should be listed.
    using SymbolFilterFn = unique_function<bool(StringRef)>;
    /// Append unresolved symbol names accepted by \p Allow into \p Unresolved.
    /// \param Unresolved Destination that receives unresolved symbol names.
    /// \param Allow Predicate selecting which unresolved names to include.
    void getUnresolvedSymbols(SmallVectorImpl<StringRef> &Unresolved,
                              SymbolFilterFn Allow) const {
      std::shared_lock<std::shared_mutex> Lock(Mtx);
      for (const auto &E : Entries) {
        if (E.ResolvedLibPath.empty() && Allow(E.Name))
          Unresolved.push_back(E.Name);
      }
    }

    /// Record that \p Sym was found in the library at \p LibPath.
    /// \param Sym Symbol name that was resolved.
    /// \param LibPath Full path of the library that defines \p Sym.
    void resolve(StringRef Sym, const std::string &LibPath) {
      std::unique_lock<std::shared_mutex> Lock(Mtx);
      for (auto &E : Entries) {
        if (E.Name == Sym && E.ResolvedLibPath.empty()) {
          E.ResolvedLibPath = LibPath;
          ResolvedCount.fetch_add(1, std::memory_order_relaxed);
          return;
        }
      }
    }

    /// Return true if every tracked symbol has been resolved to a library.
    /// \return True if all tracked symbols are resolved; false otherwise.
    bool allResolved() const {
      return ResolvedCount.load(std::memory_order_relaxed) == Entries.size();
    }

    /// Return true if at least one tracked symbol is still unresolved.
    /// \return True if any tracked symbol is unresolved; false otherwise.
    bool hasUnresolved() const {
      return ResolvedCount.load(std::memory_order_relaxed) < Entries.size();
    }

    /// Return the library path that resolved \p Sym, if any.
    /// \param Sym Symbol name whose resolved library is requested.
    /// \return The resolving library path, or std::nullopt if unresolved.
    std::optional<StringRef> getResolvedLib(StringRef Sym) const {
      std::shared_lock<std::shared_mutex> Lock(Mtx);
      for (const auto &E : Entries)
        if (E.Name == Sym && !E.ResolvedLibPath.empty())
          return E.ResolvedLibPath;
      return std::nullopt;
    }

    /// Return true if \p Sym has been resolved to a library.
    /// \param Sym Symbol name to inspect.
    /// \return True if \p Sym is resolved; false otherwise.
    bool isResolved(StringRef Sym) const {
      std::shared_lock<std::shared_mutex> Lock(Mtx);
      for (const auto &E : Entries)
        if (E.Name == Sym && !E.ResolvedLibPath.empty())
          return true;
      return false;
    }

    /// Return pointers to every tracked symbol entry (resolved or not).
    /// \return Vector of pointers to all tracked symbol entries.
    std::vector<const Entry *> getAllResults() const {
      std::shared_lock<std::shared_mutex> Lock(Mtx);
      std::vector<const Entry *> Out;
      Out.reserve(Entries.size());
      for (const auto &E : Entries)
        Out.push_back(&E);
      return Out;
    }
  };

  /// Configuration used to construct a LibraryResolver.
  struct Setup {
    /// Root directories to scan for libraries.
    std::vector<std::string> BasePaths;
    // std::shared_ptr<LibraryPathCache> Cache;
    // std::shared_ptr<PathResolver> PResolver;

    /// Maximum number of libraries to scan per batch; 0 means unbounded.
    size_t ScanBatchSize = 0;

    /// Predicate deciding whether a candidate path should be scanned.
    LibraryScanner::ShouldScanFn ShouldScanCall = [](StringRef) {
      return true;
    };

    /// Builder used to construct per-library Bloom filters.
    BloomFilterBuilder FilterBuilder = BloomFilterBuilder();

    /// Create a Setup for \p BasePaths with an optional custom scan predicate.
    /// \param BasePaths Root directories to scan for libraries.
    /// \param customShouldScan Optional predicate; nullptr keeps the default.
    /// \return A Setup configured with the given base paths and scan predicate.
    static Setup
    create(std::vector<std::string> BasePaths,
           //  std::shared_ptr<LibraryPathCache> existingCache = nullptr,
           //  std::shared_ptr<PathResolver> existingResolver = nullptr,
           LibraryScanner::ShouldScanFn customShouldScan = nullptr) {
      Setup S;
      S.BasePaths = std::move(BasePaths);

      // S.Cache =
      //     existingCache ? existingCache :
      //     std::make_shared<LibraryPathCache>();

      // S.PResolver = existingResolver ? existingResolver
      //                                :
      //                                std::make_shared<PathResolver>(S.Cache);

      if (customShouldScan)
        S.ShouldScanCall = std::move(customShouldScan);

      return S;
    }
  };

  /// Deleted default constructor; use the Setup-based constructor.
  LibraryResolver() = delete;
  /// Construct a LibraryResolver from configuration \p S.
  /// \param S Setup describing scan roots, batch size, and filters.
  LLVM_ABI explicit LibraryResolver(const Setup &S);
  /// Destroy the LibraryResolver and release owned scan state.
  ~LibraryResolver() = default;

  /// Callback invoked when a symbol search completes, with the filled query.
  using OnSearchComplete = unique_function<void(SymbolQuery &)>;

  /// Print tracked libraries and their kinds/states to the debug stream.
  void dump() {
    int i = 0;
    LibMgr.forEachLibrary([&](const LibraryInfo &Lib) -> bool {
      dbgs() << ++i << ". Library Path : " << Lib.getFullPath() << " -> \n\t\t:"
             << " ({Type : ("
             << (Lib.getKind() == PathType::User ? "User" : "System")
             << ") }, { State : "
             << (Lib.getState() == LibState::Loaded ? "Loaded" : "Unloaded")
             << "})\n";
      return true;
    });
  }

  /// Search configured libraries for each name in \p SymList.
  /// \param SymList Symbol names to resolve.
  /// \param OnComplete Callback invoked with the completed SymbolQuery.
  /// \param Config Search policy and enumerator options to apply.
  LLVM_ABI void
  searchSymbolsInLibraries(ArrayRef<StringRef> SymList,
                           OnSearchComplete OnComplete,
                           const SearchConfig &Config = SearchConfig());

private:
  LLVM_ABI bool scanLibrariesIfNeeded(PathType K, size_t BatchSize = 0);
  bool scanForNewLibraries(PathType K, LibraryCursor &Cur);
  void resolveSymbolsInLibrary(LibraryInfo *Lib, SymbolQuery &Q,
                               const SymbolEnumeratorOptions &Opts);

  LibraryManager LibMgr;
  std::shared_ptr<LibraryPathCache> LibPathCache;
  std::shared_ptr<PathResolver> LibPathResolver;
  LibraryScanHelper ScanHelper;
  BloomFilterBuilder FB;
  LibraryScanner::ShouldScanFn ShouldScanCall;
  size_t scanBatchSize;
};

/// Alias for LibraryResolver::SymbolEnumerator.
using SymbolEnumerator = LibraryResolver::SymbolEnumerator;
/// Alias for LibraryResolver::SymbolQuery.
using SymbolQuery = LibraryResolver::SymbolQuery;
/// Alias for SymbolEnumerator::EnumerateResult.
using EnumerateResult = SymbolEnumerator::EnumerateResult;

/// High-level driver that owns a LibraryResolver and drives scan/resolve.
class LibraryResolutionDriver {
public:
  /// Create a driver configured from LibraryResolver setup \p S.
  /// \param S Setup describing scan roots, batch size, and filters.
  /// \return A unique pointer to the new LibraryResolutionDriver.
  LLVM_ABI static std::unique_ptr<LibraryResolutionDriver>
  create(const LibraryResolver::Setup &S);

  /// Add \p Path as a scan root of kind \p Kind.
  /// \param Path Directory or library path to include in scanning.
  /// \param Kind Whether \p Path is a user or system path.
  LLVM_ABI void addScanPath(const std::string &Path, PathType Kind);
  /// Mark the library at \p Path as loaded.
  /// \param Path Full library path whose state is updated.
  LLVM_ABI void markLibraryLoaded(StringRef Path);
  /// Mark the library at \p Path as unloaded.
  /// \param Path Full library path whose state is updated.
  LLVM_ABI void markLibraryUnLoaded(StringRef Path);
  /// Return true if the library at \p Path is marked loaded.
  /// \param Path Full library path to inspect.
  /// \return True if the library is marked loaded; false otherwise.
  bool isLibraryLoaded(StringRef Path) const {
    return LR->LibMgr.isLoaded(Path);
  }

  /// Clear tracked libraries and reset scan/path caches.
  void resetAll() {
    LR->LibMgr.clear();
    LR->ScanHelper.resetToScan();
    LR->LibPathCache->clear();
  }

  /// Scan both user and system library paths, optionally in batches.
  /// \param BatchSize Max libraries to scan per kind; 0 means unbounded.
  void scanAll(size_t BatchSize = 0) {
    LR->scanLibrariesIfNeeded(PathType::User, BatchSize);
    LR->scanLibrariesIfNeeded(PathType::System, BatchSize);
  }

  /// Scan libraries of path kind \p PK, optionally in batches.
  /// \param PK User or system path kind to scan.
  /// \param BatchSize Max libraries to scan; 0 means unbounded.
  void scan(PathType PK, size_t BatchSize = 0) {
    LR->scanLibrariesIfNeeded(PK, BatchSize);
  }

  /// Resolve \p Symbols using the owned LibraryResolver.
  /// \param Symbols Symbol names to resolve.
  /// \param OnCompletion Callback invoked with the completed SymbolQuery.
  /// \param Config Search policy and enumerator options to apply.
  LLVM_ABI void resolveSymbols(ArrayRef<StringRef> Symbols,
                               LibraryResolver::OnSearchComplete OnCompletion,
                               const SearchConfig &Config = SearchConfig());

  /// Destroy the driver and the owned LibraryResolver.
  ~LibraryResolutionDriver() = default;

private:
  LibraryResolutionDriver(std::unique_ptr<LibraryResolver> L)
      : LR(std::move(L)) {}

  std::unique_ptr<LibraryResolver> LR;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_LIBRARYRESOLVER_H
