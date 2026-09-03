#ifndef LLVM_SUPPORT_LOCALE_H
#define LLVM_SUPPORT_LOCALE_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class StringRef;

namespace sys {
/// Utilities for measuring display width and testing printable characters.
namespace locale {

/// Returns the display column width of the UTF-8 string \p s.
///
/// \param s UTF-8 text whose terminal column width is measured.
/// \returns The column width, or a negative error code for invalid or
/// non-printable input (see unicode::columnWidthUTF8).
LLVM_ABI int columnWidth(StringRef s);

/// Returns whether character \p c is considered printable for display.
///
/// \param c Unicode code point to test.
/// \returns true if the character is considered printable.
LLVM_ABI bool isPrint(int c);
}
}
}

#endif // LLVM_SUPPORT_LOCALE_H
