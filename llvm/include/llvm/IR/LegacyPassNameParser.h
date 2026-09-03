//===- LegacyPassNameParser.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the PassNameParser and FilteredPassNameParser<> classes,
// which are used to add command line arguments to a utility for all of the
// passes that have been registered into the system.
//
// The PassNameParser class adds ALL passes linked into the system (that are
// creatable) as command line arguments to the tool (when instantiated with the
// appropriate command line option template).  The FilteredPassNameParser<>
// template is used for the same purposes as PassNameParser, except that it only
// includes passes that have a PassType that are compatible with the filter
// (which is the template argument).
//
// Note that this is part of the legacy pass manager infrastructure and will be
// (eventually) going away.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_LEGACYPASSNAMEPARSER_H
#define LLVM_IR_LEGACYPASSNAMEPARSER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>

namespace llvm {

//===----------------------------------------------------------------------===//
/// Command-line parser that adds a selectable option for each registered pass.
///
/// Makes use of the pass registration mechanism to automatically add a command
/// line argument to opt for each pass.
class LLVM_ABI PassNameParser : public PassRegistrationListener,
                                public cl::parser<const PassInfo *> {
public:
  /// Construct a pass-name parser bound to the given command-line option.
  /// \param O Command-line option that owns this parser.
  PassNameParser(cl::Option &O);
  /// Destroy the parser and unregister as a pass registration listener.
  ~PassNameParser() override;

  /// Initialize the base parser and add all passes registered so far.
  void initialize() {
    cl::parser<const PassInfo*>::initialize();

    // Add all of the passes to the map that got initialized before 'this' did.
    enumeratePasses();
  }

  /// Return true if \p P should be omitted from the option list.
  ///
  /// Subclasses can override this to refine which passes are included.
  /// \param P Pass registration info to consider.
  /// \return True if the pass should be omitted from the option list.
  virtual bool ignorablePassImpl(const PassInfo *P) const { return false; }

  /// Return true if \p P is not selectable, not constructible, or ignored by
  /// \c ignorablePassImpl.
  /// \param P Pass registration info to consider.
  /// \return True if the pass should be omitted from the option list.
  inline bool ignorablePass(const PassInfo *P) const {
    // Ignore non-selectable and non-constructible passes!  Ignore
    // non-optimizations.
    return P->getPassArgument().empty() || P->getNormalCtor() == nullptr ||
           ignorablePassImpl(P);
  }

  /// Add \p P as a literal option when a new pass is registered.
  /// \param P Pass registration info for the newly registered pass.
  void passRegistered(const PassInfo *P) override {
    if (ignorablePass(P)) return;
    if (findOption(P->getPassArgument().data()) != getNumOptions()) {
      errs() << "Two passes with the same argument (-"
           << P->getPassArgument() << ") attempted to be registered!\n";
      llvm_unreachable(nullptr);
    }
    addLiteralOption(P->getPassArgument().data(), P, P->getPassName().data());
  }
  /// Add \p P during pass enumeration by forwarding to \c passRegistered.
  /// \param P Pass registration info being enumerated.
  void passEnumerate(const PassInfo *P) override { passRegistered(P); }

  /// Print this option's help text after sorting the pass name table.
  /// \param O Command-line option whose help is being printed.
  /// \param GlobalWidth Column width shared across option help output.
  void printOptionInfo(const cl::Option &O, size_t GlobalWidth) const override {
    PassNameParser *PNP = const_cast<PassNameParser*>(this);
    array_pod_sort(PNP->Values.begin(), PNP->Values.end(), ValCompare);
    cl::parser<const PassInfo*>::printOptionInfo(O, GlobalWidth);
  }

private:
  // ValCompare - Provide a sorting comparator for Values elements...
  static int ValCompare(const PassNameParser::OptionInfo *VT1,
                        const PassNameParser::OptionInfo *VT2) {
    return VT1->Name.compare(VT2->Name);
  }
};

} // End llvm namespace

#endif
