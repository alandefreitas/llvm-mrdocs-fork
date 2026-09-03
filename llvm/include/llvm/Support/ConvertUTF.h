/*===--- ConvertUTF.h - Universal Character Names conversions ---------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *==------------------------------------------------------------------------==*/
/*
 * Copyright © 1991-2015 Unicode, Inc. All rights reserved.
 * Distributed under the Terms of Use in
 * http://www.unicode.org/copyright.html.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of the Unicode data files and any associated documentation
 * (the "Data Files") or Unicode software and any associated documentation
 * (the "Software") to deal in the Data Files or Software
 * without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, and/or sell copies of
 * the Data Files or Software, and to permit persons to whom the Data Files
 * or Software are furnished to do so, provided that
 * (a) this copyright and permission notice appear with all copies
 * of the Data Files or Software,
 * (b) this copyright and permission notice appear in associated
 * documentation, and
 * (c) there is clear notice in each modified Data File or in the Software
 * as well as in the documentation associated with the Data File(s) or
 * Software that the data or software has been modified.
 *
 * THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF
 * ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT OF THIRD PARTY RIGHTS.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS
 * NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL
 * DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THE DATA FILES OR SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder
 * shall not be used in advertising or otherwise to promote the sale,
 * use or other dealings in these Data Files or Software without prior
 * written authorization of the copyright holder.
 */

/* ---------------------------------------------------------------------

    Conversions between UTF32, UTF-16, and UTF-8.  Header file.

    Several functions are included here, forming a complete set of
    conversions between the three formats.  UTF-7 is not included
    here, but is handled in a separate source file.

    Each of these routines takes pointers to input buffers and output
    buffers.  The input buffers are const.

    Each routine converts the text between *sourceStart and sourceEnd,
    putting the result into the buffer between *targetStart and
    targetEnd. Note: the end pointers are *after* the last item: e.g.
    *(sourceEnd - 1) is the last item.

    The return result indicates whether the conversion was successful,
    and if not, whether the problem was in the source or target buffers.
    (Only the first encountered problem is indicated.)

    After the conversion, *sourceStart and *targetStart are both
    updated to point to the end of last text successfully converted in
    the respective buffers.

    Input parameters:
        sourceStart - pointer to a pointer to the source buffer.
                The contents of this are modified on return so that
                it points at the next thing to be converted.
        targetStart - similarly, pointer to pointer to the target buffer.
        sourceEnd, targetEnd - respectively pointers to the ends of the
                two buffers, for overflow checking only.

    These conversion functions take a ConversionFlags argument. When this
    flag is set to strict, both irregular sequences and isolated surrogates
    will cause an error.  When the flag is set to lenient, both irregular
    sequences and isolated surrogates are converted.

    Whether the flag is strict or lenient, all illegal sequences will cause
    an error return. This includes sequences such as: <F4 90 80 80>, <C0 80>,
    or <A0> in UTF-8, and values above 0x10FFFF in UTF-32. Conformant code
    must check for illegal sequences.

    When the flag is set to lenient, characters over 0x10FFFF are converted
    to the replacement character; otherwise (when the flag is set to strict)
    they constitute an error.

    Output parameters:
        The value "sourceIllegal" is returned from some routines if the input
        sequence is malformed.  When "sourceIllegal" is returned, the source
        value will point to the illegal value that caused the problem. E.g.,
        in UTF-8 when a sequence is malformed, it points to the start of the
        malformed sequence.

    Author: Mark E. Davis, 1994.
    Rev History: Rick McGowan, fixes & updates May 2001.
         Fixes & updates, Sept 2001.

------------------------------------------------------------------------ */

#ifndef LLVM_SUPPORT_CONVERTUTF_H
#define LLVM_SUPPORT_CONVERTUTF_H

#include "llvm/Support/Compiler.h"
#include <cstddef>
#include <string>

#if defined(_WIN32)
#include <system_error>
#endif

// Wrap everything in namespace llvm so that programs can link with llvm and
// their own version of the unicode libraries.

namespace llvm {

/* ---------------------------------------------------------------------
    The following 4 definitions are compiler-specific.
    The C standard does not guarantee that wchar_t has at least
    16 bits, so wchar_t is no less portable than unsigned short!
    All should be unsigned values to avoid sign extension during
    bit mask & shift operations.
------------------------------------------------------------------------ */

/// Unicode UTF-32 code unit type; at least 32 bits.
using UTF32 = unsigned int;
/// Unicode UTF-16 code unit type; at least 16 bits.
using UTF16 = unsigned short;
/// Unicode UTF-8 code unit type; typically 8 bits.
using UTF8 = unsigned char;
/// Boolean type used by the ConvertUTF routines; 0 or 1.
using Boolean = unsigned char;

/* Some fundamental constants */
#define UNI_REPLACEMENT_CHAR (UTF32)0x0000FFFD
#define UNI_MAX_BMP (UTF32)0x0000FFFF
#define UNI_MAX_UTF16 (UTF32)0x0010FFFF
#define UNI_MAX_UTF32 (UTF32)0x7FFFFFFF
#define UNI_MAX_LEGAL_UTF32 (UTF32)0x0010FFFF

#define UNI_MAX_UTF8_BYTES_PER_CODE_POINT 4

#define UNI_UTF16_BYTE_ORDER_MARK_NATIVE  0xFEFF
#define UNI_UTF16_BYTE_ORDER_MARK_SWAPPED 0xFFFE

#define UNI_UTF32_BYTE_ORDER_MARK_NATIVE 0x0000FEFF
#define UNI_UTF32_BYTE_ORDER_MARK_SWAPPED 0xFFFE0000

/// Result of a ConvertUTF* conversion between UTF encodings.
enum ConversionResult {
  /// Conversion completed successfully.
  conversionOK,
  /// Partial character in source, but hit end of input.
  sourceExhausted,
  /// Insufficient room in the target buffer for conversion.
  targetExhausted,
  /// Source sequence is illegal or malformed.
  sourceIllegal
};

/// Controls whether conversion is strict or lenient about irregular sequences.
enum ConversionFlags {
  /// Reject irregular sequences and isolated surrogates as errors.
  strictConversion = 0,
  /// Convert irregular sequences and isolated surrogates when possible.
  lenientConversion
};

/**
 * Convert a UTF-8 sequence to UTF-16.
 *
 * \param [in,out] sourceStart Pointer to the start of the UTF-8 source; updated
 * past the last converted code unit on return.
 * \param sourceEnd Pointer just past the end of the source buffer.
 * \param [in,out] targetStart Pointer to the start of the UTF-16 target; updated
 * past the last written code unit on return.
 * \param targetEnd Pointer just past the end of the target buffer.
 * \param flags Whether conversion is strict or lenient.
 * \returns A \c ConversionResult indicating success or the failure reason.
 */
LLVM_ABI ConversionResult ConvertUTF8toUTF16(const UTF8 **sourceStart,
                                             const UTF8 *sourceEnd,
                                             UTF16 **targetStart,
                                             UTF16 *targetEnd,
                                             ConversionFlags flags);

/**
 * Convert a partial UTF8 sequence to UTF32.  If the sequence ends in an
 * incomplete code unit sequence, returns \c sourceExhausted.
 *
 * \param [in,out] sourceStart Pointer to the start of the UTF-8 source; updated
 * past the last converted code unit on return.
 * \param sourceEnd Pointer just past the end of the source buffer.
 * \param [in,out] targetStart Pointer to the start of the UTF-32 target; updated
 * past the last written code unit on return.
 * \param targetEnd Pointer just past the end of the target buffer.
 * \param flags Whether conversion is strict or lenient.
 * \returns A \c ConversionResult indicating success or the failure reason.
 */
LLVM_ABI ConversionResult ConvertUTF8toUTF32Partial(const UTF8 **sourceStart,
                                                    const UTF8 *sourceEnd,
                                                    UTF32 **targetStart,
                                                    UTF32 *targetEnd,
                                                    ConversionFlags flags);

/**
 * Convert a partial UTF8 sequence to UTF32.  If the sequence ends in an
 * incomplete code unit sequence, returns \c sourceIllegal.
 *
 * \param [in,out] sourceStart Pointer to the start of the UTF-8 source; updated
 * past the last converted code unit on return.
 * \param sourceEnd Pointer just past the end of the source buffer.
 * \param [in,out] targetStart Pointer to the start of the UTF-32 target; updated
 * past the last written code unit on return.
 * \param targetEnd Pointer just past the end of the target buffer.
 * \param flags Whether conversion is strict or lenient.
 * \returns A \c ConversionResult indicating success or the failure reason.
 */
LLVM_ABI ConversionResult ConvertUTF8toUTF32(const UTF8 **sourceStart,
                                             const UTF8 *sourceEnd,
                                             UTF32 **targetStart,
                                             UTF32 *targetEnd,
                                             ConversionFlags flags);

/**
 * Convert a UTF-16 sequence to UTF-8.
 *
 * \param [in,out] sourceStart Pointer to the start of the UTF-16 source; updated
 * past the last converted code unit on return.
 * \param sourceEnd Pointer just past the end of the source buffer.
 * \param [in,out] targetStart Pointer to the start of the UTF-8 target; updated
 * past the last written code unit on return.
 * \param targetEnd Pointer just past the end of the target buffer.
 * \param flags Whether conversion is strict or lenient.
 * \returns A \c ConversionResult indicating success or the failure reason.
 */
LLVM_ABI ConversionResult ConvertUTF16toUTF8(const UTF16 **sourceStart,
                                             const UTF16 *sourceEnd,
                                             UTF8 **targetStart,
                                             UTF8 *targetEnd,
                                             ConversionFlags flags);

/**
 * Convert a UTF-32 sequence to UTF-8.
 *
 * \param [in,out] sourceStart Pointer to the start of the UTF-32 source; updated
 * past the last converted code unit on return.
 * \param sourceEnd Pointer just past the end of the source buffer.
 * \param [in,out] targetStart Pointer to the start of the UTF-8 target; updated
 * past the last written code unit on return.
 * \param targetEnd Pointer just past the end of the target buffer.
 * \param flags Whether conversion is strict or lenient.
 * \returns A \c ConversionResult indicating success or the failure reason.
 */
LLVM_ABI ConversionResult ConvertUTF32toUTF8(const UTF32 **sourceStart,
                                             const UTF32 *sourceEnd,
                                             UTF8 **targetStart,
                                             UTF8 *targetEnd,
                                             ConversionFlags flags);

/**
 * Convert a UTF-16 sequence to UTF-32.
 *
 * \param [in,out] sourceStart Pointer to the start of the UTF-16 source; updated
 * past the last converted code unit on return.
 * \param sourceEnd Pointer just past the end of the source buffer.
 * \param [in,out] targetStart Pointer to the start of the UTF-32 target; updated
 * past the last written code unit on return.
 * \param targetEnd Pointer just past the end of the target buffer.
 * \param flags Whether conversion is strict or lenient.
 * \returns A \c ConversionResult indicating success or the failure reason.
 */
LLVM_ABI ConversionResult ConvertUTF16toUTF32(const UTF16 **sourceStart,
                                              const UTF16 *sourceEnd,
                                              UTF32 **targetStart,
                                              UTF32 *targetEnd,
                                              ConversionFlags flags);

/**
 * Convert a UTF-32 sequence to UTF-16.
 *
 * \param [in,out] sourceStart Pointer to the start of the UTF-32 source; updated
 * past the last converted code unit on return.
 * \param sourceEnd Pointer just past the end of the source buffer.
 * \param [in,out] targetStart Pointer to the start of the UTF-16 target; updated
 * past the last written code unit on return.
 * \param targetEnd Pointer just past the end of the target buffer.
 * \param flags Whether conversion is strict or lenient.
 * \returns A \c ConversionResult indicating success or the failure reason.
 */
LLVM_ABI ConversionResult ConvertUTF32toUTF16(const UTF32 **sourceStart,
                                              const UTF32 *sourceEnd,
                                              UTF16 **targetStart,
                                              UTF16 *targetEnd,
                                              ConversionFlags flags);

/**
 * Returns true if the UTF-8 sequence starting at \p source is well-formed.
 *
 * \param source Pointer to the first byte of the candidate UTF-8 sequence.
 * \param sourceEnd Pointer just past the end of the available buffer.
 * \returns true if the sequence is well-formed.
 */
LLVM_ABI Boolean isLegalUTF8Sequence(const UTF8 *source, const UTF8 *sourceEnd);

/**
 * Returns true if the UTF-8 string between \p *source and \p sourceEnd is legal.
 *
 * On failure, \p *source is left pointing at the first illegal sequence.
 *
 * \param [in,out] source Pointer to the start of the UTF-8 string; advanced
 * through the string, or left at the first illegal sequence on failure.
 * \param sourceEnd Pointer just past the end of the string.
 * \returns true if the string is a legal UTF-8 sequence.
 */
LLVM_ABI Boolean isLegalUTF8String(const UTF8 **source, const UTF8 *sourceEnd);

/**
 * Returns the byte length of the first UTF-8 sequence, or 0 if it is invalid.
 *
 * \param source Pointer to the first byte of the candidate UTF-8 sequence.
 * \param sourceEnd Pointer just past the end of the available buffer.
 * \returns the byte length of the sequence, or 0 if it is invalid.
 */
LLVM_ABI unsigned getUTF8SequenceSize(const UTF8 *source,
                                      const UTF8 *sourceEnd);

/**
 * Returns the length of the maximal subpart of an ill-formed UTF-8 sequence.
 *
 * Implements Unicode 6.3.0 D93b: the longest initial subsequence that is either
 * a prefix of a well-formed sequence, or a single code unit. The input must not
 * be a legal UTF-8 sequence.
 *
 * \param source Pointer to the start of the ill-formed UTF-8 subsequence.
 * \param sourceEnd Pointer just past the end of the available buffer.
 * \returns the length in bytes of the maximal subpart.
 */
LLVM_ABI unsigned
findMaximalSubpartOfIllFormedUTF8Sequence(const UTF8 *source,
                                          const UTF8 *sourceEnd);

/**
 * Returns the expected byte length of a UTF-8 sequence from its first byte.
 *
 * Does not validate that a complete legal sequence follows; use
 * \c isLegalUTF8Sequence or \c getUTF8SequenceSize for that.
 *
 * \param firstByte First byte of a UTF-8 code unit sequence.
 * \returns the expected number of bytes in the UTF-8 sequence.
 */
LLVM_ABI unsigned getNumBytesForUTF8(UTF8 firstByte);

/*************************************************************************/
/* Below are LLVM-specific wrappers of the functions above. */

template <typename T> class ArrayRef;
template <typename T> class SmallVectorImpl;
class StringRef;

/**
 * Convert a UTF-8 StringRef to UTF-8, UTF-16, or UTF-32 by wide character width.
 *
 * The converted data is written to ResultPtr, which needs to point to at least
 * WideCharWidth * (Source.Size() + 1) bytes. On success, ResultPtr will point
 * one after the end of the copied string. On failure, ResultPtr will not be
 * changed, and ErrorPtr will be set to the location of the first character
 * which could not be converted.
 *
 * \param WideCharWidth Width in bytes of each wide character (1, 2, or 4).
 * \param Source UTF-8 encoded input string.
 * \param [in,out] ResultPtr Pointer to the output buffer; advanced past the
 * converted data on success.
 * \param [out] ErrorPtr Set to the first unconvertible character on failure.
 * \return true on success.
 */
LLVM_ABI bool ConvertUTF8toWide(unsigned WideCharWidth, llvm::StringRef Source,
                                char *&ResultPtr, const UTF8 *&ErrorPtr);

/**
 * Converts a UTF-8 StringRef to a std::wstring.
 *
 * \param Source UTF-8 encoded input string.
 * \param [out] Result Converted wide string is stored here on success.
 * \return true on success.
 */
LLVM_ABI bool ConvertUTF8toWide(llvm::StringRef Source, std::wstring &Result);

/**
 * Converts a UTF-8 C-string to a std::wstring.
 *
 * \param Source Null-terminated UTF-8 encoded input string.
 * \param [out] Result Converted wide string is stored here on success.
 * \return true on success.
 */
LLVM_ABI bool ConvertUTF8toWide(const char *Source, std::wstring &Result);

/**
 * Converts a wide string view to a UTF-8 encoded std::string.
 *
 * \param Source Wide-character input string.
 * \param [out] Result Converted UTF-8 is stored here on success.
 * \return true on success.
 */
LLVM_ABI bool convertWideToUTF8(std::wstring_view Source, std::string &Result);

/**
 * Convert an Unicode code point to UTF8 sequence.
 *
 * \param Source a Unicode code point.
 * \param [in,out] ResultPtr pointer to the output buffer, needs to be at least
 * \c UNI_MAX_UTF8_BYTES_PER_CODE_POINT bytes.  On success \c ResultPtr is
 * updated one past end of the converted sequence.
 *
 * \returns true on success.
 */
LLVM_ABI bool ConvertCodePointToUTF8(unsigned Source, char *&ResultPtr);

/**
 * Convert the first UTF8 sequence in the given source buffer to a UTF32
 * code point.
 *
 * \param [in,out] source A pointer to the source buffer. If the conversion
 * succeeds, this pointer will be updated to point to the byte just past the
 * end of the converted sequence.
 * \param sourceEnd A pointer just past the end of the source buffer.
 * \param [out] target The converted code
 * \param flags Whether the conversion is strict or lenient.
 *
 * \returns conversionOK on success
 *
 * \sa ConvertUTF8toUTF32
 */
inline ConversionResult convertUTF8Sequence(const UTF8 **source,
                                            const UTF8 *sourceEnd,
                                            UTF32 *target,
                                            ConversionFlags flags) {
  if (*source == sourceEnd)
    return sourceExhausted;
  unsigned size = getNumBytesForUTF8(**source);
  if ((ptrdiff_t)size > sourceEnd - *source)
    return sourceExhausted;
  return ConvertUTF8toUTF32(source, *source + size, &target, target + 1, flags);
}

/**
 * Returns true if a blob of text starts with a UTF-16 big or little endian byte
 * order mark.
 *
 * \param SrcBytes Raw bytes to inspect for a leading UTF-16 BOM.
 * \returns true if \p SrcBytes starts with a UTF-16 BOM.
 */
LLVM_ABI bool hasUTF16ByteOrderMark(ArrayRef<char> SrcBytes);

/**
 * Converts a stream of raw bytes assumed to be UTF16 into a UTF8 std::string.
 *
 * \param [in] SrcBytes A buffer of what is assumed to be UTF-16 encoded text.
 * \param [out] Out Converted UTF-8 is stored here on success.
 * \returns true on success
 */
LLVM_ABI bool convertUTF16ToUTF8String(ArrayRef<char> SrcBytes,
                                       std::string &Out);

/**
* Converts a UTF16 string into a UTF8 std::string.
*
* \param [in] Src A buffer of UTF-16 encoded text.
* \param [out] Out Converted UTF-8 is stored here on success.
* \returns true on success
*/
LLVM_ABI bool convertUTF16ToUTF8String(ArrayRef<UTF16> Src, std::string &Out);

/**
 * Converts a stream of raw bytes assumed to be UTF32 into a UTF8 std::string.
 *
 * \param [in] SrcBytes A buffer of what is assumed to be UTF-32 encoded text.
 * \param [out] Out Converted UTF-8 is stored here on success.
 * \returns true on success
 */
LLVM_ABI bool convertUTF32ToUTF8String(ArrayRef<char> SrcBytes,
                                       std::string &Out);

/**
 * Converts a UTF32 string into a UTF8 std::string.
 *
 * \param [in] Src A buffer of UTF-32 encoded text.
 * \param [out] Out Converted UTF-8 is stored here on success.
 * \returns true on success
 */
LLVM_ABI bool convertUTF32ToUTF8String(ArrayRef<UTF32> Src, std::string &Out);

/**
 * Converts a UTF-8 string into a UTF-16 string with native endianness.
 *
 * \param SrcUTF8 UTF-8 encoded input string.
 * \param [out] DstUTF16 Converted UTF-16 is stored here on success.
 * \returns true on success
 */
LLVM_ABI bool convertUTF8ToUTF16String(StringRef SrcUTF8,
                                       SmallVectorImpl<UTF16> &DstUTF16);

/**
 * Returns true if \p Codepoint fits in a single UTF-8 code unit (ASCII).
 *
 * \param Codepoint Unicode code point to test.
 * \returns true if \p Codepoint fits in a single UTF-8 code unit.
 */
LLVM_ABI bool IsSingleCodeUnitUTF8Codepoint(unsigned Codepoint);

/**
 * Returns true if \p Codepoint fits in a single UTF-16 code unit.
 *
 * Surrogate code points are excluded; only BMP non-surrogate scalars qualify.
 *
 * \param Codepoint Unicode code point to test.
 * \returns true if \p Codepoint fits in a single UTF-16 code unit.
 */
LLVM_ABI bool IsSingleCodeUnitUTF16Codepoint(unsigned Codepoint);

/**
 * Returns true if \p Codepoint is a valid UTF-32 scalar value.
 *
 * Surrogate code points are excluded. Every legal Unicode scalar fits in one
 * UTF-32 code unit.
 *
 * \param Codepoint Unicode code point to test.
 * \returns true if \p Codepoint is a valid UTF-32 scalar value.
 */
LLVM_ABI bool IsSingleCodeUnitUTF32Codepoint(unsigned Codepoint);

#if defined(_WIN32)
namespace sys {
namespace windows {
LLVM_ABI std::error_code UTF8ToUTF16(StringRef utf8,
                                     SmallVectorImpl<wchar_t> &utf16);
/// Convert to UTF16 from the current code page used in the system
LLVM_ABI std::error_code CurCPToUTF16(StringRef utf8,
                                      SmallVectorImpl<wchar_t> &utf16);
LLVM_ABI std::error_code UTF16ToUTF8(const wchar_t *utf16, size_t utf16_len,
                                     SmallVectorImpl<char> &utf8);
/// Convert from UTF16 to the current code page used in the system
LLVM_ABI std::error_code UTF16ToCurCP(const wchar_t *utf16, size_t utf16_len,
                                      SmallVectorImpl<char> &utf8);
} // namespace windows
} // namespace sys
#endif

} /* end namespace llvm */

#endif
