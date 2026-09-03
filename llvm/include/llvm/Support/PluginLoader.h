//===-- llvm/Support/PluginLoader.h - Plugin Loader for Tools ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A tool can #include this file to get a -load option that allows the user to
// load arbitrary shared objects into the tool's address space.  Note that this
// header can only be included by a program ONCE, so it should never to used by
// library authors.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PLUGINLOADER_H
#define LLVM_SUPPORT_PLUGINLOADER_H

#include "llvm/Support/Compiler.h"

#ifndef DONT_GET_PLUGIN_LOADER_OPTION
#include "llvm/Support/CommandLine.h"
#endif

#include <string>

namespace llvm {
  /// Loads shared-object plugins into a tool via the \c -load option.
  ///
  /// Used as the value type of a \c cl::opt so each \c -load argument invokes
  /// \c operator= to load the named plugin. This header may be included only
  /// once per program and is not for use by library authors.
  struct PluginLoader {
    /// Load the shared library named \p Filename into the process.
    ///
    /// On failure, prints an error and ignores the request; on success, records
    /// the path among the loaded plugins.
    ///
    /// \param Filename Path of the plugin shared object to load.
    LLVM_ABI void operator=(const std::string &Filename);
    /// Return the number of plugins successfully loaded.
    ///
    /// \return The count of plugins successfully loaded so far.
    LLVM_ABI static unsigned getNumPlugins();
    /// Return the path of the loaded plugin at index \p num.
    ///
    /// \param num Zero-based index into the list of successfully loaded
    /// plugins. Must be less than \c getNumPlugins().
    /// \return A reference to the path string of the plugin at \p num.
    LLVM_ABI static std::string &getPlugin(unsigned num);
  };

#ifndef DONT_GET_PLUGIN_LOADER_OPTION
  // This causes operator= above to be invoked for every -load option.
  static cl::opt<PluginLoader, false, cl::parser<std::string>>
      LoadOpt("load", cl::value_desc("pluginfilename"),
              cl::desc("Load the specified plugin"));
#endif
}

#endif
