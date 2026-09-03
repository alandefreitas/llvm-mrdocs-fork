//===-- llvm/Debuginfod/Debuginfod.h - Debuginfod client --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains several declarations for the debuginfod client and
/// server. The client functions are getDefaultDebuginfodUrls,
/// getCachedOrDownloadArtifact, and several convenience functions for specific
/// artifact types: getCachedOrDownloadSource, getCachedOrDownloadExecutable,
/// and getCachedOrDownloadDebuginfo. For the server, this file declares the
/// DebuginfodLogEntry and DebuginfodServer structs, as well as the
/// DebuginfodLog, DebuginfodCollection classes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFOD_DEBUGINFOD_H
#define LLVM_DEBUGINFOD_DEBUGINFOD_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/HTTP/HTTPServer.h"
#include "llvm/Object/BuildID.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/RWMutex.h"
#include "llvm/Support/Timer.h"

#include <chrono>
#include <condition_variable>
#include <optional>
#include <queue>

namespace llvm {

/// Returns false if a debuginfod lookup can be determined to have no chance of
/// succeeding.
/// \return False if a debuginfod lookup cannot succeed; true otherwise.
bool canUseDebuginfod();

/// Finds default array of Debuginfod server URLs by checking DEBUGINFOD_URLS
/// environment variable.
/// \return Default Debuginfod server URLs from DEBUGINFOD_URLS.
SmallVector<StringRef> getDefaultDebuginfodUrls();

/// Returns the cache key for a given debuginfod URL path.
/// \param UrlPath Debuginfod URL path to derive the cache key from.
/// \return Cache key derived from \p UrlPath.
std::string getDebuginfodCacheKey(StringRef UrlPath);

/// Sets the list of debuginfod server URLs to query. This overrides the
/// environment variable DEBUGINFOD_URLS.
/// \param URLs Debuginfod server URLs to use instead of DEBUGINFOD_URLS.
void setDefaultDebuginfodUrls(const SmallVector<StringRef> &URLs);

/// Finds a default local file caching directory for the debuginfod client,
/// first checking DEBUGINFOD_CACHE_PATH.
/// \return Default cache directory path, or an error on failure.
Expected<std::string> getDefaultDebuginfodCacheDirectory();

/// Finds a default timeout for debuginfod HTTP requests. Checks
/// DEBUGINFOD_TIMEOUT environment variable, default is 90 seconds (90000 ms).
/// \return Default HTTP request timeout in milliseconds.
std::chrono::milliseconds getDefaultDebuginfodTimeout();

/// Get the full URL path for a source request of a given BuildID and file
/// path.
/// \param ID Build ID of the binary that owns the source file.
/// \param SourceFilePath Path of the source file to request.
/// \return Full URL path for the source request.
std::string getDebuginfodSourceUrlPath(object::BuildIDRef ID,
                                       StringRef SourceFilePath);

/// Fetches a specified source file by searching the default local cache
/// directory and server URLs.
/// \param ID Build ID of the binary that owns the source file.
/// \param SourceFilePath Path of the source file to fetch.
/// \return Local path to the cached or downloaded source file, or an error.
Expected<std::string> getCachedOrDownloadSource(object::BuildIDRef ID,
                                                StringRef SourceFilePath);

/// Get the full URL path for an executable request of a given BuildID.
/// \param ID Build ID of the executable to request.
/// \return Full URL path for the executable request.
std::string getDebuginfodExecutableUrlPath(object::BuildIDRef ID);

/// Fetches an executable by searching the default local cache directory and
/// server URLs.
/// \param ID Build ID of the executable to fetch.
/// \return Local path to the cached or downloaded executable, or an error.
Expected<std::string> getCachedOrDownloadExecutable(object::BuildIDRef ID);

/// Get the full URL path for a debug binary request of a given BuildID.
/// \param ID Build ID of the debug binary to request.
/// \return Full URL path for the debug binary request.
std::string getDebuginfodDebuginfoUrlPath(object::BuildIDRef ID);

/// Fetches a debug binary by searching the default local cache directory and
/// server URLs.
/// \param ID Build ID of the debug binary to fetch.
/// \return Local path to the cached or downloaded debug binary, or an error.
Expected<std::string> getCachedOrDownloadDebuginfo(object::BuildIDRef ID);

/// Fetches any debuginfod artifact using the default local cache directory and
/// server URLs.
/// \param UniqueKey Key used for the local cache file when the artifact is
/// found.
/// \param UrlPath Debuginfod URL path of the artifact to fetch.
/// \return Local path to the cached or downloaded artifact, or an error.
Expected<std::string> getCachedOrDownloadArtifact(StringRef UniqueKey,
                                                  StringRef UrlPath);

/// Fetches any debuginfod artifact from a cache or remote servers.
///
/// Uses the specified local cache directory, server URLs, and request timeout
/// (in milliseconds). If the artifact is found, uses the UniqueKey for the
/// local cache file.
/// \param UniqueKey Key used for the local cache file when the artifact is
/// found.
/// \param UrlPath Debuginfod URL path of the artifact to fetch.
/// \param CacheDirectoryPath Local directory used to cache downloaded
/// artifacts.
/// \param DebuginfodUrls Debuginfod server URLs to query.
/// \param Timeout HTTP request timeout in milliseconds.
/// \return Local path to the cached or downloaded artifact, or an error.
Expected<std::string> getCachedOrDownloadArtifact(
    StringRef UniqueKey, StringRef UrlPath, StringRef CacheDirectoryPath,
    ArrayRef<StringRef> DebuginfodUrls, std::chrono::milliseconds Timeout);

class ThreadPoolInterface;

/// A single log message produced by the debuginfod server or collection.
struct DebuginfodLogEntry {
  /// Text of the log message.
  std::string Message;
  /// Constructs an empty log entry.
  DebuginfodLogEntry() = default;
  /// Constructs a log entry from \p Message.
  /// \param Message Log message text.
  DebuginfodLogEntry(const Twine &Message);
};

/// Thread-safe queue of debuginfod log entries.
class DebuginfodLog {
  std::mutex QueueMutex;
  std::condition_variable QueueCondition;
  std::queue<DebuginfodLogEntry> LogEntryQueue;

public:
  /// Adds a log entry to the end of the queue.
  /// \param Entry Log entry to enqueue.
  void push(DebuginfodLogEntry Entry);
  /// Adds a log entry for \p Message to the end of the queue.
  /// \param Message Log message text to enqueue.
  void push(const Twine &Message);
  /// Pops and returns the first log entry, blocking until one is available.
  /// \return The first log entry from the queue.
  DebuginfodLogEntry pop();
};

/// Tracks a collection of debuginfod artifacts on the local filesystem.
class DebuginfodCollection {
  SmallVector<std::string, 1> Paths;
  sys::RWMutex BinariesMutex;
  StringMap<std::string> Binaries;
  sys::RWMutex DebugBinariesMutex;
  StringMap<std::string> DebugBinaries;
  Error findBinaries(StringRef Path);
  Expected<std::optional<std::string>> getDebugBinaryPath(object::BuildIDRef);
  Expected<std::optional<std::string>> getBinaryPath(object::BuildIDRef);
  // If the collection has not been updated since MinInterval, call update() and
  // return true. Otherwise return false. If update returns an error, return the
  // error.
  Expected<bool> updateIfStale();
  DebuginfodLog &Log;
  ThreadPoolInterface &Pool;
  Timer UpdateTimer;
  sys::Mutex UpdateMutex;

  // Minimum update interval, in seconds, for on-demand updates triggered when a
  // build-id is not found.
  double MinInterval;

public:
  /// Constructs a collection that scans \p Paths for debuginfod artifacts.
  /// \param Paths Filesystem paths to search for binaries and debug binaries.
  /// \param Log Log that receives collection status messages.
  /// \param Pool Thread pool used for parallel directory scanning.
  /// \param MinInterval Minimum seconds between on-demand updates when a
  /// build-id is not found.
  DebuginfodCollection(ArrayRef<StringRef> Paths, DebuginfodLog &Log,
                       ThreadPoolInterface &Pool, double MinInterval);
  /// Scans the configured paths and refreshes the binary and debug-binary maps.
  /// \return Success, or an error if scanning fails.
  Error update();
  /// Repeatedly calls update(), sleeping for \p Interval between iterations.
  /// \param Interval Delay between successive update() calls.
  /// \return An error if an update fails; otherwise does not return.
  Error updateForever(std::chrono::milliseconds Interval);
  /// Finds the local path of a debug binary for \p ID, updating if stale.
  /// \param ID Build ID of the debug binary to locate.
  /// \return Local path of the debug binary, or an error.
  Expected<std::string> findDebugBinaryPath(object::BuildIDRef ID);
  /// Finds the local path of a binary for \p ID, updating if stale.
  /// \param ID Build ID of the binary to locate.
  /// \return Local path of the binary, or an error.
  Expected<std::string> findBinaryPath(object::BuildIDRef ID);
};

/// Debuginfod HTTP server that serves artifacts from a DebuginfodCollection.
struct DebuginfodServer {
  /// Underlying HTTP server that handles incoming requests.
  HTTPServer Server;
  /// Log that receives server request and status messages.
  DebuginfodLog &Log;
  /// Collection of local artifacts served by this server.
  DebuginfodCollection &Collection;
  /// Constructs a server that serves artifacts from \p Collection.
  /// \param Log Log that receives server request and status messages.
  /// \param Collection Local artifact collection to serve.
  DebuginfodServer(DebuginfodLog &Log, DebuginfodCollection &Collection);
};

} // end namespace llvm

#endif
