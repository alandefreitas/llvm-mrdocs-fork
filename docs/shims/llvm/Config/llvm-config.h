#ifndef LLVM_CONFIG_H
#define LLVM_CONFIG_H

// Documentation-build shim for LLVM's generated llvm-config.h. Only the macros
// the public ADT/Support headers consult are defined; values are the common
// 64-bit-Unix defaults and do not affect the rendered documentation.
#define LLVM_VERSION_MAJOR 20
#define LLVM_VERSION_MINOR 0
#define LLVM_VERSION_PATCH 0
#define LLVM_VERSION_STRING "20.0.0git"
#define LLVM_ON_UNIX 1
#define LLVM_ENABLE_THREADS 1
#define LLVM_HAS_ATOMICS 1
#define LLVM_DEFAULT_TARGET_TRIPLE "x86_64-unknown-linux-gnu"
#define LLVM_HOST_TRIPLE "x86_64-unknown-linux-gnu"
#define LLVM_ENABLE_DUMP 0
#define HAVE_SYSEXITS_H 1

#endif
