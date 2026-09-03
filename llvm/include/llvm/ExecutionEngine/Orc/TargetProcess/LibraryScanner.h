//===- LibraryScanner.h - Scanner for Shared Libraries ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides functionality for scanning dynamic (shared) libraries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_LIBRARYSCANNER_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_LIBRARYSCANNER_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/StringSaver.h"

#include <atomic>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>

namespace llvm {
namespace orc {

class LibraryManager;

/// Caches seen library paths and memoized filesystem lookups.
class LibraryPathCache {
  friend class PathResolver;

public:
  /// Construct an empty library path cache.
  LibraryPathCache() = default;

  /// Clear the seen-path set and optionally the realpath-related caches.
  /// @param isRealPathCache When true, also clear realpath, readlink, and
  ///        lstat caches.
  void clear(bool isRealPathCache = false) {
    std::unique_lock<std::shared_mutex> lock(Mtx);
    Seen.clear();
    if (isRealPathCache) {
      RealPathCache.clear();
#ifndef _WIN32
      ReadlinkCache.clear();
      LstatCache.clear();
#endif
    }
  }

  /// Record that \p CanonPath has been seen during scanning.
  /// @param CanonPath Canonical path to mark as seen.
  void markSeen(const std::string &CanonPath) {
    std::unique_lock<std::shared_mutex> lock(Mtx);
    Seen.insert(CanonPath);
  }

  /// Return true if \p CanonPath has already been marked as seen.
  /// @param CanonPath Canonical path to query.
  /// @return True if the path is in the seen set.
  bool hasSeen(StringRef CanonPath) const {
    std::shared_lock<std::shared_mutex> lock(Mtx);
    return Seen.contains(CanonPath);
  }

  /// Return true if \p CanonPath was already seen; otherwise mark it seen.
  /// @param CanonPath Canonical path to check or mark.
  /// @return True if the path was already in the seen set.
  bool hasSeenOrMark(StringRef CanonPath) {
    std::string s = CanonPath.str();
    {
      std::shared_lock<std::shared_mutex> lock(Mtx);
      if (Seen.contains(s))
        return true;
    }
    {
      std::unique_lock<std::shared_mutex> lock(Mtx);
      Seen.insert(s);
    }
    return false;
  }

private:
  mutable std::shared_mutex Mtx;

  struct PathInfo {
    std::string canonicalPath;
    std::error_code ErrnoCode;
  };

  void insert_realpath(StringRef Path, const PathInfo &Info) {
    std::unique_lock<std::shared_mutex> lock(Mtx);
    RealPathCache.insert({Path, Info});
  }

  std::optional<PathInfo> read_realpath(StringRef Path) const {
    std::shared_lock<std::shared_mutex> lock(Mtx);
    auto It = RealPathCache.find(Path);
    if (It != RealPathCache.end())
      return It->second;

    return std::nullopt;
  }

  StringSet<> Seen;
  StringMap<PathInfo> RealPathCache;

#ifndef _WIN32
  StringMap<std::string> ReadlinkCache;
  StringMap<mode_t> LstatCache;

  void insert_link(StringRef Path, const std::string &s) {
    std::unique_lock<std::shared_mutex> lock(Mtx);
    ReadlinkCache.insert({Path, s});
  }

  std::optional<std::string> read_link(StringRef Path) const {
    std::shared_lock<std::shared_mutex> lock(Mtx);
    auto It = ReadlinkCache.find(Path);
    if (It != ReadlinkCache.end())
      return It->second;

    return std::nullopt;
  }

  void insert_lstat(StringRef Path, mode_t m) {
    std::unique_lock<std::shared_mutex> lock(Mtx);
    LstatCache.insert({Path, m});
  }

  std::optional<mode_t> read_lstat(StringRef Path) const {
    std::shared_lock<std::shared_mutex> lock(Mtx);
    auto It = LstatCache.find(Path);
    if (It != LstatCache.end())
      return It->second;

    return std::nullopt;
  }

#endif
};

/// Resolves file system paths with optional caching of results.
///
/// Supports lstat, readlink, and realpath operations. Can resolve paths
/// relative to a base and handle symbolic links. Caches results to reduce
/// repeated system calls when enabled.
class PathResolver {
private:
  std::shared_ptr<LibraryPathCache> LibPathCache;

public:
  /// Construct a path resolver that uses the given library path cache.
  /// @param cache Shared cache for memoizing path operations.
  PathResolver(std::shared_ptr<LibraryPathCache> cache)
      : LibPathCache(std::move(cache)) {}

  /// Resolve \p Path to a canonical absolute path via the realpath cache.
  /// @param Path Path to resolve.
  /// @param ec Error code set on failure.
  /// @return Canonical path, or nullopt on failure.
  std::optional<std::string> resolve(StringRef Path, std::error_code &ec) {
    return realpathCached(Path, ec);
  }
#ifndef _WIN32
  /// Return the lstat mode for \p Path, caching the result.
  /// @param Path Path to lstat.
  /// @return File mode bits from lstat.
  mode_t lstatCached(StringRef Path);
  /// Return the readlink target for \p Path, caching the result.
  /// @param Path Symlink path to read.
  /// @return Link target, or nullopt on failure.
  std::optional<std::string> readlinkCached(StringRef Path);
#endif
  /// Resolve \p Path to a real path with optional base and symlink limit.
  /// @param Path Path to resolve.
  /// @param ec Error code set on failure.
  /// @param base Optional base directory for relative paths.
  /// @param baseIsResolved Whether \p base is already a resolved real path.
  /// @param symloopLevel Maximum number of symlink hops to follow.
  /// @return Canonical real path, or nullopt on failure.
  LLVM_ABI std::optional<std::string>
  realpathCached(StringRef Path, std::error_code &ec, StringRef base = "",
                 bool baseIsResolved = false, long symloopLevel = 40);
};

/// Performs placeholder substitution in dynamic library paths.
///
/// Configures known placeholders (like @loader_path) and replaces them
/// in input paths with their resolved values.
class DylibSubstitutor {
public:
  /// Configure placeholder substitutions for the given loader path.
  /// @param loaderPath Absolute path of the loading library.
  LLVM_ABI void configure(StringRef loaderPath);

  /// Apply configured placeholder substitutions to \p input.
  /// @param input Path that may contain placeholders.
  /// @return Path with known placeholders replaced.
  std::string substitute(StringRef input) const {
    for (const auto &[ph, value] : Placeholders) {
      if (input.starts_with_insensitive(ph))
        return (Twine(value) + input.drop_front(ph.size())).str();
    }
    return input.str();
  }

private:
  SmallVector<std::pair<std::string, std::string>> Placeholders;
};

/// Loads an object file and provides access to it.
///
/// Owns the underlying `ObjectFile` and ensures it is valid.
/// Any errors encountered during construction are stored and
/// returned when attempting to access the file.
class ObjectFileLoader {
public:
  /// Construct an object file loader from the given path.
  /// @param Path Filesystem path of the object or shared library to load.
  explicit ObjectFileLoader(StringRef Path) {
    auto ObjOrErr = loadObjectFileWithOwnership(Path);
    if (ObjOrErr)
      Obj = std::move(*ObjOrErr);
    else {
      consumeError(std::move(Err));
      Err = ObjOrErr.takeError();
    }
  }

  /// Deleted copy constructor; loaders are move-only.
  /// @param Other Loader that would be copied.
  ObjectFileLoader(const ObjectFileLoader &Other) = delete;
  /// Deleted copy assignment; loaders are move-only.
  /// @param Other Loader that would be copied.
  /// @return Reference to this loader.
  ObjectFileLoader &operator=(const ObjectFileLoader &Other) = delete;

  /// Move-construct from another object file loader.
  /// @param Other Loader to move from.
  ObjectFileLoader(ObjectFileLoader &&Other) = default;
  /// Move-assign from another object file loader.
  /// @param Other Loader to move from.
  /// @return Reference to this loader.
  ObjectFileLoader &operator=(ObjectFileLoader &&Other) = default;

  /// Get the loaded object file, or return an error if loading failed.
  /// @return Reference to the loaded object file, or an error.
  Expected<object::ObjectFile &> getObjectFile() {
    if (Err) {
      // allow the error to be taken only once
      if (ErrorTaken)
        return createStringError(inconvertibleErrorCode(),
                                 "error already taken");

      ErrorTaken = true;
      return std::move(Err);
    }
    return *Obj.getBinary();
  }

  /// Return true if \p Obj matches the host process architecture.
  /// @param Obj Object file to check.
  /// @return True if the object file architecture is compatible.
  LLVM_ABI static bool isArchitectureCompatible(const object::ObjectFile &Obj);

private:
  object::OwningBinary<object::ObjectFile> Obj;
  Error Err = Error::success();
  bool ErrorTaken = false;

  LLVM_ABI static Expected<object::OwningBinary<object::ObjectFile>>
  loadObjectFileWithOwnership(StringRef FilePath);
};

/// Cache of loaded object files keyed by filesystem path.
class ObjFileCache {
public:
  /// Insert or replace the loader stored for \p Path.
  /// @param Path File path key.
  /// @param Loader Object file loader to store; ownership is transferred.
  void insert(StringRef Path, ObjectFileLoader &&Loader) {
    Cache.insert({Path, std::move(Loader)});
  }

  /// Take ownership of the cached loader for \p Path and remove it.
  /// @param Path File path key.
  /// @return The loader if present, otherwise nullopt.
  std::optional<ObjectFileLoader> take(StringRef Path) {
    std::unique_lock<std::shared_mutex> Lock(Mtx);
    auto It = Cache.find(Path);
    if (It == Cache.end())
      return std::nullopt;

    ObjectFileLoader L = std::move(It->second);
    Cache.erase(It);
    return std::move(L);
  }

  /// Return true if the cache holds an entry for \p Path.
  /// @param Path File path key.
  /// @return True if \p Path is present in the cache.
  bool contains(StringRef Path) const { return Cache.count(Path) != 0; }

private:
  mutable std::shared_mutex Mtx;
  StringMap<ObjectFileLoader> Cache;
};

/// Validates and normalizes dynamic library paths.
///
/// Uses a `PathResolver` to resolve paths to their canonical form and
/// checks whether they point to valid shared libraries.
class DylibPathValidator {
public:
  /// Construct a validator using the given path resolver and cache.
  /// @param PR Path resolver used to canonicalize paths.
  /// @param LC Library path cache for seen-path tracking.
  /// @param ObjCache Optional object-file cache for shared-library checks.
  DylibPathValidator(PathResolver &PR, LibraryPathCache &LC,
                     ObjFileCache *ObjCache = nullptr)
      : LibPathResolver(PR), LibPathCache(LC), ObjCache(ObjCache) {}

  /// Return true if \p Path names a shared library.
  /// @param Path Path to check.
  /// @return True if \p Path is a shared library.
  LLVM_ABI bool isSharedLibrary(StringRef Path) const;
  /// Return true if \p Path is known or verified as a shared library.
  /// @param Path Path to check.
  /// @return True if \p Path was previously seen or is a shared library.
  bool isSharedLibraryCached(StringRef Path) const {
    if (LibPathCache.hasSeen(Path))
      return true;
    return isSharedLibrary(Path);
  }

  /// Resolve \p Path to a canonical form, or nullopt on failure.
  /// @param Path Path to normalize.
  /// @return Canonical path, or nullopt if resolution fails.
  std::optional<std::string> normalize(StringRef Path) const {
    std::error_code ec;
    auto real = LibPathResolver.resolve(Path, ec);
    if (!real || ec)
      return std::nullopt;

    return real;
  }

  /// Validate the given path as a shared library.
  /// @param Path Candidate shared-library path to validate.
  /// @return Canonical path if valid, or nullopt otherwise.
  std::optional<std::string> validate(StringRef Path) const {
    if (LibPathCache.hasSeen(Path))
      return Path.str();
    auto realOpt = normalize(Path);
    if (!realOpt)
      return std::nullopt;

    if (!isSharedLibraryCached(*realOpt))
      return std::nullopt;

    return realOpt;
  }

private:
  PathResolver &LibPathResolver;
  LibraryPathCache &LibPathCache;
  mutable ObjFileCache *ObjCache;
};

/// Kind of library search path used during resolution.
enum class SearchPathType {
  /// RPATH entries embedded in the loading binary.
  RPath,
  /// User- or system-provided search directories.
  UsrOrSys,
  /// RUNPATH entries embedded in the loading binary.
  RunPath,
};

/// Configuration for a group of library search paths.
struct SearchPathConfig {
  /// Ordered list of search path directories.
  ArrayRef<StringRef> Paths;
  /// Kind of search path this configuration represents.
  SearchPathType type;
};

/// Resolves library stems against a configured list of search paths.
class SearchPathResolver {
public:
  /// Construct a resolver from the given search-path configuration.
  /// @param Cfg Search paths and their type.
  /// @param PlaceholderPrefix Optional prefix (e.g. @rpath) in library stems.
  SearchPathResolver(const SearchPathConfig &Cfg,
                     StringRef PlaceholderPrefix = "")
      : Kind(Cfg.type), PlaceholderPrefix(PlaceholderPrefix) {
    Paths.reserve(Cfg.Paths.size());
    for (auto &path : Cfg.Paths)
      Paths.emplace_back(path.str());
  }

  /// Resolve a library stem to an absolute path.
  /// @param libStem Library name or stem to resolve.
  /// @param Subst Substitutor for path placeholders.
  /// @param Validator Validator for candidate shared-library paths.
  /// @return Absolute path of the resolved library, or nullopt if not found.
  LLVM_ABI std::optional<std::string>
  resolve(StringRef libStem, const DylibSubstitutor &Subst,
          DylibPathValidator &Validator) const;
  /// Return the search-path kind used by this resolver.
  /// @return The \c SearchPathType for this resolver.
  SearchPathType searchPathType() const { return Kind; }

private:
  std::vector<std::string> Paths;
  SearchPathType Kind;
  std::string PlaceholderPrefix;
};

/// Internal helper that resolves libraries via substitutor and search paths.
class DylibResolverImpl {
public:
  /// Construct an implementation with substitutor, validator, and resolvers.
  /// @param Substitutor Placeholder substitutor for library paths.
  /// @param Validator Validator for candidate shared-library paths.
  /// @param Resolvers Ordered search-path resolvers to consult.
  DylibResolverImpl(DylibSubstitutor Substitutor, DylibPathValidator &Validator,
                    std::vector<SearchPathResolver> Resolvers)
      : Substitutor(std::move(Substitutor)), Validator(Validator),
        Resolvers(std::move(Resolvers)) {}

  /// Resolve \p Stem to an absolute library path.
  /// @param Stem Library name or stem to resolve.
  /// @param VariateLibStem When true, try alternate stem spellings/extensions.
  /// @return Absolute path of the resolved library, or nullopt if not found.
  LLVM_ABI std::optional<std::string>
  resolve(StringRef Stem, bool VariateLibStem = false) const;

private:
  std::optional<std::string> tryWithExtensions(StringRef libstem) const;

  DylibSubstitutor Substitutor;
  DylibPathValidator &Validator;
  std::vector<SearchPathResolver> Resolvers;
};

/// Resolves dynamic library names to absolute filesystem paths.
class DylibResolver {
public:
  /// Construct a resolver that validates candidates with \p Validator.
  /// @param Validator Validator used for candidate shared-library paths.
  DylibResolver(DylibPathValidator &Validator) : Validator(Validator) {}

  /// Configure placeholder substitution and search-path resolvers.
  /// @param loaderPath Absolute path of the loading library.
  /// @param SearchPathCfg Search-path configurations to consult, in order.
  void configure(StringRef loaderPath,
                 ArrayRef<SearchPathConfig> SearchPathCfg) {
    DylibSubstitutor Substitutor;
    Substitutor.configure(loaderPath);

    std::vector<SearchPathResolver> Resolvers;
    for (const auto &cfg : SearchPathCfg) {
      Resolvers.emplace_back(cfg,
                             cfg.type == SearchPathType::RPath ? "@rpath" : "");
    }

    impl_ = std::make_unique<DylibResolverImpl>(
        std::move(Substitutor), Validator, std::move(Resolvers));
  }

  /// Resolve \p libStem to an absolute library path.
  /// @param libStem Library name or stem to resolve.
  /// @param VariateLibStem When true, try alternate stem spellings/extensions.
  /// @return Absolute path of the resolved library, or nullopt if not found.
  std::optional<std::string> resolve(StringRef libStem,
                                     bool VariateLibStem = false) const {
    if (!impl_)
      return std::nullopt;
    return impl_->resolve(libStem, VariateLibStem);
  }

  /// Substitute linker-style placeholders in \p libStem for \p loaderPath.
  /// @param libStem Library path that may contain placeholders.
  /// @param loaderPath Absolute path of the loading library.
  /// @return \p libStem after placeholder substitution.
  static std::string resolvelinkerFlag(StringRef libStem,
                                       StringRef loaderPath) {
    DylibSubstitutor Substitutor;
    Substitutor.configure(loaderPath);
    return Substitutor.substitute(libStem);
  }

private:
  DylibPathValidator &Validator;
  std::unique_ptr<DylibResolverImpl> impl_;
};

/// Classification of a library search-path base directory.
enum class PathType : uint8_t {
  /// User-provided or application-local search path.
  User,
  /// System search path.
  System,
  /// Path whose origin has not been classified.
  Unknown
};

/// Progress of scanning a library search-path base directory.
enum class ScanState : uint8_t {
  /// Directory has not been scanned yet.
  NotScanned,
  /// Directory is currently being scanned.
  Scanning,
  /// Directory has finished scanning.
  Scanned
};

/// A tracked base directory used when scanning for shared libraries.
struct LibrarySearchPath {
  /// Canonical base directory path.
  std::string BasePath; // Canonical base directory path
  /// Whether this path is user, system, or unclassified.
  PathType Kind;        // User or System
  /// Current scan progress for this base path.
  std::atomic<ScanState> State;

  /// Construct a search path for \p Base with classification \p K.
  /// @param Base Canonical base directory path.
  /// @param K Path classification (user, system, or unknown).
  LibrarySearchPath(std::string Base, PathType K)
      : BasePath(std::move(Base)), Kind(K), State(ScanState::NotScanned) {}
};

/// Scans and tracks libraries for symbol resolution.
///
/// Maintains a list of library paths to scan, caches scanned units,
/// and resolves paths canonically for consistent tracking.
class LibraryScanHelper {
public:
  /// Construct a helper that tracks the given base search paths.
  /// @param SPaths Initial base directories to register for scanning.
  /// @param LibPathCache Shared cache of seen/canonicalized paths.
  /// @param LibPathResolver Shared path resolver used for canonicalization.
  explicit LibraryScanHelper(const std::vector<std::string> &SPaths,
                             std::shared_ptr<LibraryPathCache> LibPathCache,
                             std::shared_ptr<PathResolver> LibPathResolver)
      : LibPathCache(std::move(LibPathCache)),
        LibPathResolver(std::move(LibPathResolver)) {
    DEBUG_WITH_TYPE(
        "orc", dbgs() << "LibraryScanHelper::LibraryScanHelper: base paths : "
                      << SPaths.size() << "\n";);
    for (const auto &p : SPaths)
      addBasePath(p);
  }

  /// Add a canonical directory for scanning.
  /// @param P Base directory path to track.
  /// @param Kind Classification of the path, or Unknown to infer later.
  LLVM_ABI void
  addBasePath(const std::string &P,
              PathType Kind =
                  PathType::Unknown); // Add a canonical directory for scanning

  /// Fill \p Out with up to \p batchSize unscanned paths of kind \p Kind.
  /// @param Kind Path classification to draw the next batch from.
  /// @param batchSize Maximum number of search paths to return.
  /// @param Out Destination for pointers to the selected search paths.
  LLVM_ABI void getNextBatch(PathType Kind, size_t batchSize,
                             SmallVectorImpl<const LibrarySearchPath *> &Out);

  /// Return true if any path of kind \p K still needs scanning.
  /// @param K Path classification to query.
  /// @return True if unscanned paths of that kind remain.
  LLVM_ABI bool leftToScan(PathType K) const;
  /// Reset all tracked paths to the unscanned state.
  LLVM_ABI void resetToScan();

  /// Return true if \p P is a tracked base search path.
  /// @param P Path to query.
  /// @return True if \p P is registered as a base path.
  LLVM_ABI bool isTrackedBasePath(StringRef P) const;
  /// Return true if at least one base search path is registered.
  /// @return True when the tracked search-path list is non-empty.
  bool hasSearchPath() const { return !LibSearchPaths.empty(); }

  /// Return the canonical base paths currently being tracked.
  /// @return Snapshot of tracked base directory paths.
  SmallVector<StringRef> getSearchPaths() const {
    SmallVector<StringRef> SearchPaths;
    for (const auto &[_, SP] : LibSearchPaths)
      SearchPaths.push_back(SP->BasePath);
    return SearchPaths;
  }

  /// Return the path resolver used by this helper.
  /// @return Reference to the shared path resolver.
  PathResolver &getPathResolver() const { return *LibPathResolver; }

  /// Return the library path cache used by this helper.
  /// @return Reference to the shared library path cache.
  LibraryPathCache &getCache() const { return *LibPathCache; }

  /// Return true if \p P was already seen; otherwise mark it seen.
  /// @param P Path to check or mark.
  /// @return True if the path was already in the seen set.
  bool hasSeenOrMark(StringRef P) const {
    return LibPathCache->hasSeenOrMark(P);
  }

  /// Resolve \p P to a canonical absolute path.
  /// @param P Path to resolve.
  /// @param ec Error code set on failure.
  /// @return Canonical path, or nullopt on failure.
  std::optional<std::string> resolve(StringRef P, std::error_code &ec) const {
    return LibPathResolver->resolve(P.str(), ec);
  }

private:
  std::string resolveCanonical(StringRef P, std::error_code &ec) const;
  PathType classifyKind(StringRef P) const;

  mutable std::shared_mutex Mtx;
  std::shared_ptr<LibraryPathCache> LibPathCache;
  std::shared_ptr<PathResolver> LibPathResolver;

  StringMap<std::unique_ptr<LibrarySearchPath>>
      LibSearchPaths; // key: canonical path
  std::deque<StringRef> UnscannedUsr;
  std::deque<StringRef> UnscannedSys;
};

/// Scans libraries, resolves dependencies, and registers them.
class LibraryScanner {
public:
  /// Predicate that returns true when a library path should be scanned.
  using ShouldScanFn = std::function<bool(StringRef)>;

  /// Construct a scanner over \p H that registers libraries with \p LibMgr.
  /// @param H Helper that supplies base paths and path resolution.
  /// @param LibMgr Manager that receives discovered libraries.
  /// @param ShouldScanCall Optional filter; defaults to scanning every path.
  LibraryScanner(
      LibraryScanHelper &H, LibraryManager &LibMgr,
      ShouldScanFn ShouldScanCall = [](StringRef path) { return true; })
      : ObjCache(ObjFileCache()), ScanHelper(H), LibMgr(LibMgr),
        Validator(ScanHelper.getPathResolver(), ScanHelper.getCache(),
                  &ObjCache),
        ShouldScanCall(std::move(ShouldScanCall)) {}

  /// Scan the next batch of libraries of the given path kind.
  /// @param Kind Path classification to scan from.
  /// @param batchSize Number of base directories to process in this call.
  LLVM_ABI void scanNext(PathType Kind, size_t batchSize = 1);

  /// Dependency info for a library.
  struct LibraryDepsInfo {
    /// Allocator backing saved dependency and path strings.
    llvm::BumpPtrAllocator Alloc;
    /// String saver that stores copies in \c Alloc.
    llvm::StringSaver Saver{Alloc};

    /// RPATH entries extracted from the library.
    SmallVector<StringRef, 2> rpath;
    /// RUNPATH entries extracted from the library.
    SmallVector<StringRef, 2> runPath;
    /// Dependent library names or paths extracted from the library.
    SmallVector<StringRef, 4> deps;
    /// True if the library is a position-independent executable.
    bool isPIE = false;

    /// Append an RPATH entry, saving a copy of \p s.
    /// @param s RPATH entry to record.
    void addRPath(StringRef s) { rpath.push_back(Saver.save(s)); }

    /// Append a RUNPATH entry, saving a copy of \p s.
    /// @param s RUNPATH entry to record.
    void addRunPath(StringRef s) { runPath.push_back(Saver.save(s)); }

    /// Append a dependent library, saving a copy of \p s.
    /// @param s Dependent library name or path to record.
    void addDep(StringRef s) { deps.push_back(Saver.save(s)); }
  };

private:
  ObjFileCache ObjCache;
  LibraryScanHelper &ScanHelper;
  LibraryManager &LibMgr;
  DylibPathValidator Validator;
  ShouldScanFn ShouldScanCall;

  bool shouldScan(StringRef FilePath, bool IsResolvingDep = false);

  Expected<LibraryDepsInfo> extractDeps(StringRef FilePath);

  void handleLibrary(StringRef FilePath, PathType K, int level = 0);

  void scanBaseDir(LibrarySearchPath *U);
};

/// Alias for \c LibraryScanner::LibraryDepsInfo.
using LibraryDepsInfo = LibraryScanner::LibraryDepsInfo;

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_LIBRARYSCANNER_H
