//===- WithColor.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_WITHCOLOR_H
#define LLVM_SUPPORT_WITHCOLOR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class Error;
class StringRef;

namespace cl {
/// Command-line option category grouping related flags.
class OptionCategory;
}

/// Return the command-line option category for color-related flags.
///
/// \return The OptionCategory for color-related command-line flags.
LLVM_ABI extern cl::OptionCategory &getColorCategory();

/// Symbolic highlight roles used when coloring diagnostic and dump output.
enum class HighlightColor {
  /// Memory or numeric address tokens.
  Address,
  /// String literals.
  String,
  /// Markup or IR-style tags.
  Tag,
  /// Named attributes.
  Attribute,
  /// Enumeration constants.
  Enumerator,
  /// Macro names.
  Macro,
  /// Error-severity text.
  Error,
  /// Warning-severity text.
  Warning,
  /// Note-severity text.
  Note,
  /// Remark-severity text.
  Remark
};

/// Controls whether WithColor applies ANSI colors to a stream.
enum class ColorMode {
  /// Decide from the command line and whether the stream supports color.
  Auto,
  /// Force colors on when the stream can display them.
  Enable,
  /// Force colors off.
  Disable,
};

/// An RAII object that temporarily switches an output stream to a specific
/// color.
class WithColor {
public:
  /// Predicate that decides whether colors should be auto-enabled for a stream.
  using AutoDetectFunctionType = bool (*)(const raw_ostream &OS);

  /// To be used like this: WithColor(OS, HighlightColor::String) << "text";
  /// @param OS The output stream
  /// @param S Symbolic name for syntax element to color
  /// @param Mode Enable, disable or compute whether to use colors.
  LLVM_CTOR_NODISCARD LLVM_ABI WithColor(raw_ostream &OS, HighlightColor S,
                                         ColorMode Mode = ColorMode::Auto);
  /// To be used like this: WithColor(OS, raw_ostream::BLACK) << "text";
  /// @param OS The output stream
  /// @param Color ANSI color to use, the special SAVEDCOLOR can be used to
  /// change only the bold attribute, and keep colors untouched
  /// @param Bold Bold/brighter text, default false
  /// @param BG If true, change the background, default: change foreground
  /// @param Mode Enable, disable or compute whether to use colors.
  ///
  /// FIXME: If Color == SAVEDCOLOR, Bold == false is currently ignored.
  LLVM_CTOR_NODISCARD WithColor(
      raw_ostream &OS, raw_ostream::Colors Color = raw_ostream::SAVEDCOLOR,
      bool Bold = false, bool BG = false, ColorMode Mode = ColorMode::Auto)
      : OS(OS), Mode(Mode) {
    changeColor(Color, Bold, BG);
  }
  /// Restore the stream's previous color state.
  LLVM_ABI ~WithColor();

  /// Return the underlying output stream.
  ///
  /// \return The underlying output stream.
  raw_ostream &get() { return OS; }
  /// Convert to the underlying output stream.
  ///
  /// \return The underlying output stream.
  operator raw_ostream &() { return OS; }
  /// Write \p O to the colored stream and return this wrapper.
  ///
  /// \param O Value inserted into the stream.
  /// \return This colored stream wrapper.
  template <typename T> WithColor &operator<<(T &O) {
    OS << O;
    return *this;
  }
  /// Write \p O to the colored stream and return this wrapper.
  ///
  /// \param O Value inserted into the stream.
  /// \return This colored stream wrapper.
  template <typename T> WithColor &operator<<(const T &O) {
    OS << O;
    return *this;
  }

  /// Convenience method for printing "error: " to stderr.
  ///
  /// \return stderr with the error severity prefix applied.
  LLVM_ABI static raw_ostream &error();
  /// Convenience method for printing "warning: " to stderr.
  ///
  /// \return stderr with the warning severity prefix applied.
  LLVM_ABI static raw_ostream &warning();
  /// Convenience method for printing "note: " to stderr.
  ///
  /// \return stderr with the note severity prefix applied.
  LLVM_ABI static raw_ostream &note();
  /// Convenience method for printing "remark: " to stderr.
  ///
  /// \return stderr with the remark severity prefix applied.
  LLVM_ABI static raw_ostream &remark();

  /// Convenience method for printing "error: " to the given stream.
  ///
  /// \param OS The output stream.
  /// \param Prefix Optional tool or context name printed before the severity.
  /// \param DisableColors When true, force colors off for this message.
  /// \return \p OS with the error severity prefix applied.
  LLVM_ABI static raw_ostream &error(raw_ostream &OS, StringRef Prefix = "",
                                     bool DisableColors = false);
  /// Convenience method for printing "warning: " to the given stream.
  ///
  /// \param OS The output stream.
  /// \param Prefix Optional tool or context name printed before the severity.
  /// \param DisableColors When true, force colors off for this message.
  /// \return \p OS with the warning severity prefix applied.
  LLVM_ABI static raw_ostream &warning(raw_ostream &OS, StringRef Prefix = "",
                                       bool DisableColors = false);
  /// Convenience method for printing "note: " to the given stream.
  ///
  /// \param OS The output stream.
  /// \param Prefix Optional tool or context name printed before the severity.
  /// \param DisableColors When true, force colors off for this message.
  /// \return \p OS with the note severity prefix applied.
  LLVM_ABI static raw_ostream &note(raw_ostream &OS, StringRef Prefix = "",
                                    bool DisableColors = false);
  /// Convenience method for printing "remark: " to the given stream.
  ///
  /// \param OS The output stream.
  /// \param Prefix Optional tool or context name printed before the severity.
  /// \param DisableColors When true, force colors off for this message.
  /// \return \p OS with the remark severity prefix applied.
  LLVM_ABI static raw_ostream &remark(raw_ostream &OS, StringRef Prefix = "",
                                      bool DisableColors = false);

  /// Determine whether colors are displayed.
  ///
  /// \return True if colors will be displayed on the stream.
  LLVM_ABI bool colorsEnabled();

  /// Change the color of text that will be output from this point forward.
  /// @param Color ANSI color to use, the special SAVEDCOLOR can be used to
  /// change only the bold attribute, and keep colors untouched
  /// @param Bold Bold/brighter text, default false
  /// @param BG If true, change the background, default: change foreground
  ///
  /// FIXME: If Color == SAVEDCOLOR, Bold == false is currently ignored.
  /// \return This colored stream wrapper.
  LLVM_ABI WithColor &changeColor(raw_ostream::Colors Color, bool Bold = false,
                                  bool BG = false);

  /// Reset the colors to terminal defaults. Call this when you are done
  /// outputting colored text, or before program exit.
  ///
  /// \return This colored stream wrapper.
  LLVM_ABI WithColor &resetColor();

  /// Implement default handling for Error.
  /// Print "error: " to stderr.
  ///
  /// \param Err Error whose messages are printed as errors.
  LLVM_ABI static void defaultErrorHandler(Error Err);

  /// Implement default handling for Warning.
  /// Print "warning: " to stderr.
  ///
  /// \param Warning Error whose messages are printed as warnings.
  LLVM_ABI static void defaultWarningHandler(Error Warning);

  /// Retrieve the default color auto detection function.
  ///
  /// \return The default predicate used for color auto-detection.
  LLVM_ABI static AutoDetectFunctionType defaultAutoDetectFunction();

  /// Change the global auto detection function.
  ///
  /// \param NewAutoDetectFunction Predicate used to decide color auto-enable.
  LLVM_ABI static void
  setAutoDetectFunction(AutoDetectFunctionType NewAutoDetectFunction);

private:
  raw_ostream &OS;
  ColorMode Mode;

  static AutoDetectFunctionType AutoDetectFunction;
};

} // end namespace llvm

#endif // LLVM_SUPPORT_WITHCOLOR_H
