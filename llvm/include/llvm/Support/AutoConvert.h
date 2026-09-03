/*===- AutoConvert.h - Auto conversion between ASCII/EBCDIC -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains functions used for auto conversion between
// ASCII/EBCDIC codepages specific to z/OS.
//
//===----------------------------------------------------------------------===*/

#ifndef LLVM_SUPPORT_AUTOCONVERT_H
#define LLVM_SUPPORT_AUTOCONVERT_H

#ifdef __MVS__
#include <_Ccsid.h>
#endif
#ifdef __cplusplus
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include <system_error>
#endif /* __cplusplus */

#define CCSID_IBM_1047 1047
#define CCSID_UTF_8 1208
#define CCSID_ISO8859_1 819

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int enablezOSAutoConversion(int FD);
int enablezOSAutoConversionCcsid(int FD, int ccsid);
int disablezOSAutoConversion(int FD);
int restorezOSStdHandleAutoConversion(int FD);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#ifdef __cplusplus
namespace llvm {

#ifdef __MVS__

/** \brief Set the tag information for a file descriptor. */
std::error_code setzOSFileTag(int FD, int CCSID, bool IsText);

/** \brief Get the the tag ccsid for a file name or a file descriptor. */
ErrorOr<__ccsid_t> getzOSFileTag(const Twine &FileName, const int FD = -1);

/** \brief Query the file tag to determine if it needs conversion to UTF-8
 *  codepage.
 */
ErrorOr<bool> needzOSConversion(const Twine &FileName, const int FD = -1);

/** Copy the tag attributes from \a source to \a destination.
 *
 * @param Source The name of the source file.
 * @param Destination The file descriptor of the destination file.
 * @returns errc::success if the tag attributes were copied successfully,
 *          otherwise returns a specific error_code.
 */
std::error_code copyFileTagAttributes(const std::string &Source,
                                      const int DestinationFD);

#endif /* __MVS__*/

/** Disable automatic ASCII/EBCDIC conversion for a file descriptor.
 *
 * On non-z/OS platforms this is a no-op that returns success.
 *
 * @param FD The file descriptor.
 * @returns A default-constructed error_code on success, otherwise the
 *          platform error from disabling conversion.
 */
inline std::error_code disableAutoConversion(int FD) {
#ifdef __MVS__
  if (::disablezOSAutoConversion(FD) == -1)
    return errnoAsErrorCode();
#endif
  return std::error_code();
}

/** Enable automatic ASCII/EBCDIC conversion for a file descriptor.
 *
 * On non-z/OS platforms this is a no-op that returns success.
 *
 * @param FD The file descriptor.
 * @returns A default-constructed error_code on success, otherwise the
 *          platform error from enabling conversion.
 */
inline std::error_code enableAutoConversion(int FD) {
#ifdef __MVS__
  if (::enablezOSAutoConversion(FD) == -1)
    return errnoAsErrorCode();
#endif
  return std::error_code();
}

/** Enable automatic conversion for a file descriptor with a specific CCSID.
 *
 * On non-z/OS platforms this is a no-op that returns success.
 *
 * @param FD The file descriptor.
 * @param ccsid The coded character set identifier for the file content.
 * @returns A default-constructed error_code on success, otherwise the
 *          platform error from enabling conversion.
 */
inline std::error_code enableAutoConversion(int FD, int ccsid) {
#ifdef __MVS__
  if (::enablezOSAutoConversionCcsid(FD, ccsid) == -1)
    return errnoAsErrorCode();
#endif
  return std::error_code();
}

/** Restore the previously saved auto-conversion mode for a standard handle.
 *
 * Applies to stdin, stdout, or stderr after \c enableAutoConversion has
 * recorded their prior conversion state. On non-z/OS platforms this is a
 * no-op that returns success.
 *
 * @param FD The standard file descriptor to restore.
 * @returns A default-constructed error_code on success, otherwise the
 *          platform error from restoring conversion.
 */
inline std::error_code restoreStdHandleAutoConversion(int FD) {
#ifdef __MVS__
  if (::restorezOSStdHandleAutoConversion(FD) == -1)
    return errnoAsErrorCode();
#endif
  return std::error_code();
}

/** Set the tag information for a file descriptor.
 *
 * On non-z/OS platforms this is a no-op that returns success.
 *
 * @param FD The file descriptor.
 * @param CCSID The coded character set identifier to assign.
 * @param IsText True if the file should be tagged as text.
 * @returns A default-constructed error_code on success, otherwise a
 *          platform error_code.
 */
inline std::error_code setFileTag(int FD, int CCSID, bool IsText) {
#ifdef __MVS__
  return setzOSFileTag(FD, CCSID, IsText);
#endif
  return std::error_code();
}

/** Query whether a file needs conversion to the UTF-8 codepage.
 *
 * On non-z/OS platforms this always reports that conversion is not needed.
 *
 * @param FileName The path used when \a FD is not available.
 * @param FD An optional open file descriptor; when not -1 it is preferred
 *           over \a FileName for reading the file tag.
 * @returns True if conversion is needed, false if not, or an error if the
 *          tag cannot be queried.
 */
inline ErrorOr<bool> needConversion(const Twine &FileName, const int FD = -1) {
#ifdef __MVS__
  return needzOSConversion(FileName, FD);
#endif
  return false;
}

} /* namespace llvm */
#endif /* __cplusplus */

#endif /* LLVM_SUPPORT_AUTOCONVERT_H */
