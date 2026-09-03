//===-- llvm/LineEditor/LineEditor.h - line editor --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LINEEDITOR_LINEEDITOR_H
#define LLVM_LINEEDITOR_LINEEDITOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

/// Interactive line editor with history and tab completion.
class LineEditor {
public:
  /// Create a LineEditor object.
  ///
  /// \param ProgName The name of the current program. Used to form a default
  /// prompt.
  /// \param HistoryPath Path to the file in which to store history data, if
  /// possible.
  /// \param In The input stream used by the editor.
  /// \param Out The output stream used by the editor.
  /// \param Err The error stream used by the editor.
  LLVM_ABI LineEditor(StringRef ProgName, StringRef HistoryPath = "",
                      FILE *In = stdin, FILE *Out = stdout, FILE *Err = stderr);
  /// Destroy the LineEditor and release its resources.
  LLVM_ABI ~LineEditor();

  /// Reads a line.
  ///
  /// \return The line, or std::optional<std::string>() on EOF.
  LLVM_ABI std::optional<std::string> readLine() const;

  /// Persist the current command history to the history file.
  LLVM_ABI void saveHistory();
  /// Load command history from the history file.
  LLVM_ABI void loadHistory();
  /// Set the maximum number of history entries to retain.
  /// \param size Maximum history size.
  LLVM_ABI void setHistorySize(int size);

  /// Return the default history file path for the given program name.
  /// \param ProgName Program name used to form the history path.
  /// \return The default path for the history file.
  LLVM_ABI static std::string getDefaultHistoryPath(StringRef ProgName);

  /// The action to perform upon a completion request.
  struct CompletionAction {
    /// Kind of action the completer should take.
    enum ActionKind {
      /// Insert Text at the cursor position.
      AK_Insert,
      /// Show Completions, or beep if the list is empty.
      AK_ShowCompletions
    };

    /// Which action to perform.
    ActionKind Kind;

    /// The text to insert.
    std::string Text;

    /// The list of completions to show.
    std::vector<std::string> Completions;
  };

  /// A possible completion at a given cursor position.
  struct Completion {
    /// Construct an empty completion.
    Completion() = default;
    /// Construct a completion with typed and display text.
    /// \param TypedText Text to insert at the cursor.
    /// \param DisplayText Description shown for this completion.
    Completion(const std::string &TypedText, const std::string &DisplayText)
        : TypedText(TypedText), DisplayText(DisplayText) {}

    /// The text to insert. If the user has already input some of the
    /// completion, this should only include the rest of the text.
    std::string TypedText;

    /// A description of this completion. This may be the completion itself, or
    /// maybe a summary of its type or arguments.
    std::string DisplayText;
  };

  /// Set the completer for this LineEditor.
  ///
  /// A completer is a function object which takes arguments of type StringRef
  /// (the string to complete) and size_t (the zero-based cursor position in the
  /// StringRef) and returns a CompletionAction.
  /// \param Comp Completer function object to install.
  template <typename T> void setCompleter(T Comp) {
    Completer.reset(new CompleterModel<T>(Comp));
  }

  /// Set the completer for this LineEditor to the given list completer.
  ///
  /// A list completer is a function object which takes arguments of type
  /// StringRef (the string to complete) and size_t (the zero-based cursor
  /// position in the StringRef) and returns a std::vector<Completion>.
  /// \param Comp List completer function object to install.
  template <typename T> void setListCompleter(T Comp) {
    Completer.reset(new ListCompleterModel<T>(Comp));
  }

  /// Use the current completer to produce a CompletionAction for the given
  /// completion request.
  ///
  /// If the current completer is a list completer, this will return an
  /// AK_Insert CompletionAction if each completion has a common prefix, or an
  /// AK_ShowCompletions CompletionAction otherwise.
  ///
  /// \param Buffer The string to complete
  /// \param Pos The zero-based cursor position in the StringRef
  /// \return The completion action to perform for this request.
  LLVM_ABI CompletionAction getCompletionAction(StringRef Buffer,
                                                size_t Pos) const;

  /// Return the current prompt string.
  /// \return The prompt string shown before each input line.
  const std::string &getPrompt() const { return Prompt; }
  /// Set the prompt string shown before each input line.
  /// \param P New prompt string.
  void setPrompt(const std::string &P) { Prompt = P; }

  /// Internal implementation data shared with LineEditor.cpp callbacks.
  struct InternalData;

private:
  std::string Prompt;
  std::string HistoryPath;
  std::unique_ptr<InternalData> Data;

  struct LLVM_ABI CompleterConcept {
    virtual ~CompleterConcept();
    virtual CompletionAction complete(StringRef Buffer, size_t Pos) const = 0;
  };

  struct LLVM_ABI ListCompleterConcept : CompleterConcept {
    ~ListCompleterConcept() override;
    CompletionAction complete(StringRef Buffer, size_t Pos) const override;
    static std::string getCommonPrefix(const std::vector<Completion> &Comps);
    virtual std::vector<Completion> getCompletions(StringRef Buffer,
                                                   size_t Pos) const = 0;
  };

  template <typename T>
  struct CompleterModel : CompleterConcept {
    CompleterModel(T Value) : Value(Value) {}
    CompletionAction complete(StringRef Buffer, size_t Pos) const override {
      return Value(Buffer, Pos);
    }
    T Value;
  };

  template <typename T>
  struct ListCompleterModel : ListCompleterConcept {
    ListCompleterModel(T Value) : Value(std::move(Value)) {}
    std::vector<Completion> getCompletions(StringRef Buffer,
                                           size_t Pos) const override {
      return Value(Buffer, Pos);
    }
    T Value;
  };

  std::unique_ptr<const CompleterConcept> Completer;
};

}

#endif
