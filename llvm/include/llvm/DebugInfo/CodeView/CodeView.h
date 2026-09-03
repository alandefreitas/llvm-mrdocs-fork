//===- CodeView.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines constants and basic types describing CodeView debug information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CODEVIEW_H
#define LLVM_DEBUGINFO_CODEVIEW_CODEVIEW_H

#include "llvm/Support/Compiler.h"
#include <cinttypes>

#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/Support/Endian.h"

namespace llvm {
namespace codeview {

/// Distinguishes individual records in .debug$T or .debug$P section or PDB type
/// stream. The documentation and headers talk about this as the "leaf" type.
enum class TypeRecordKind : uint16_t {
#define TYPE_RECORD(lf_ename, value, name) name = value,
#include "CodeViewTypes.def"
};

/// Duplicate copy of the above enum, but using the official CV names. Useful
/// for reference purposes and when dealing with unknown record types.
enum TypeLeafKind : uint16_t {
#define CV_TYPE(name, val) name = val,
#include "CodeViewTypes.def"
};

/// Distinguishes individual records in the Symbols subsection of a .debug$S
/// section. Equivalent to SYM_ENUM_e in cvinfo.h.
enum class SymbolRecordKind : uint16_t {
#define SYMBOL_RECORD(lf_ename, value, name) name = value,
#include "CodeViewSymbols.def"
};

/// Duplicate copy of the above enum, but using the official CV names. Useful
/// for reference purposes and when dealing with unknown record types.
enum SymbolKind : uint16_t {
#define CV_SYMBOL(name, val) name = val,
#include "CodeViewSymbols.def"
};

#define CV_DEFINE_ENUM_CLASS_FLAGS_OPERATORS(Class)                            \
  inline Class operator|(Class a, Class b) {                                   \
    return static_cast<Class>(llvm::to_underlying(a) |                         \
                              llvm::to_underlying(b));                         \
  }                                                                            \
  inline Class operator&(Class a, Class b) {                                   \
    return static_cast<Class>(llvm::to_underlying(a) &                         \
                              llvm::to_underlying(b));                         \
  }                                                                            \
  inline Class operator~(Class a) {                                            \
    return static_cast<Class>(~llvm::to_underlying(a));                        \
  }                                                                            \
  inline Class &operator|=(Class &a, Class b) {                                \
    a = a | b;                                                                 \
    return a;                                                                  \
  }                                                                            \
  inline Class &operator&=(Class &a, Class b) {                                \
    a = a & b;                                                                 \
    return a;                                                                  \
  }

/// Target CPU type recorded in CodeView compile symbols (CV_CPU_TYPE_e).
///
/// These values correspond to the CV_CPU_TYPE_e enumeration, and are documented
/// here: https://msdn.microsoft.com/en-us/library/b2fc64ek.aspx
enum class CPUType : uint16_t {
  Intel8080 = 0x0, ///< Intel 8080 processor.
  Intel8086 = 0x1, ///< Intel 8086 processor.
  Intel80286 = 0x2, ///< Intel 80286 processor.
  Intel80386 = 0x3,  ///< Intel 80386 processor.
  Intel80486 = 0x4,  ///< Intel 80486 processor.
  Pentium = 0x5, ///< Intel Pentium processor.
  PentiumPro = 0x6, ///< Pentium Pro and Pentium II processors.
  Pentium3 = 0x7, ///< Intel Pentium III processor.
  MIPS = 0x10, ///< MIPS processor.
  MIPS16 = 0x11, ///< MIPS16 processor.
  MIPS32 = 0x12, ///< MIPS32 processor.
  MIPS64 = 0x13, ///< MIPS64 processor.
  MIPSI = 0x14, ///< MIPS I processor.
  MIPSII = 0x15, ///< MIPS II processor.
  MIPSIII = 0x16, ///< MIPS III processor.
  MIPSIV = 0x17, ///< MIPS IV processor.
  MIPSV = 0x18, ///< MIPS V processor.
  M68000 = 0x20, ///< Motorola 68000 processor.
  M68010 = 0x21, ///< Motorola 68010 processor.
  M68020 = 0x22, ///< Motorola 68020 processor.
  M68030 = 0x23, ///< Motorola 68030 processor.
  M68040 = 0x24, ///< Motorola 68040 processor.
  Alpha = 0x30, ///< DEC Alpha processor.
  Alpha21164 = 0x31, ///< DEC Alpha 21164 processor.
  Alpha21164A = 0x32, ///< DEC Alpha 21164A processor.
  Alpha21264 = 0x33, ///< DEC Alpha 21264 processor.
  Alpha21364 = 0x34, ///< DEC Alpha 21364 processor.
  PPC601 = 0x40, ///< PowerPC 601 processor.
  PPC603 = 0x41, ///< PowerPC 603 processor.
  PPC604 = 0x42, ///< PowerPC 604 processor.
  PPC620 = 0x43, ///< PowerPC 620 processor.
  PPCFP = 0x44, ///< PowerPC with floating-point unit.
  PPCBE = 0x45, ///< PowerPC big-endian.
  SH3 = 0x50, ///< SuperH SH3 processor.
  SH3E = 0x51, ///< SuperH SH3E processor.
  SH3DSP = 0x52, ///< SuperH SH3-DSP processor.
  SH4 = 0x53, ///< SuperH SH4 processor.
  SHMedia = 0x54, ///< SuperH SHMedia processor.
  ARM3 = 0x60, ///< ARM3 processor.
  ARM4 = 0x61, ///< ARM4 processor.
  ARM4T = 0x62, ///< ARM4T (Thumb) processor.
  ARM5 = 0x63, ///< ARM5 processor.
  ARM5T = 0x64, ///< ARM5T processor.
  ARM6 = 0x65, ///< ARM6 processor.
  ARM_XMAC = 0x66, ///< ARM with XMAC extensions.
  ARM_WMMX = 0x67, ///< ARM with Wireless MMX extensions.
  ARM7 = 0x68, ///< ARM7 processor.
  Omni = 0x70, ///< Omni processor.
  Ia64 = 0x80, ///< Intel Itanium (IA-64) processor.
  Ia64_2 = 0x81, ///< Intel Itanium 2 processor.
  CEE = 0x90, ///< Common Execution Environment (.NET).
  AM33 = 0xa0, ///< Panasonic AM33 processor.
  M32R = 0xb0, ///< Mitsubishi M32R processor.
  TriCore = 0xc0, ///< Infineon TriCore processor.
  X64 = 0xd0, ///< AMD64 / Intel x64 processor.
  EBC = 0xe0, ///< EFI Byte Code.
  Thumb = 0xf0, ///< ARM Thumb (16-bit) instruction set.
  ARMNT = 0xf4, ///< ARM Windows NT (Thumb-2) processor.
  ARM64 = 0xf6, ///< ARM64 (AArch64) processor.
  HybridX86ARM64 = 0xf7, ///< Hybrid x86 / ARM64 processor.
  ARM64EC = 0xf8, ///< ARM64EC (Emulation Compatible) processor.
  ARM64X = 0xf9, ///< ARM64X processor.
  Unknown = 0xff, ///< Unknown or unspecified CPU type.
  D3D11_Shader = 0x100, ///< Direct3D 11 shader.
};

/// Source language recorded in a compile symbol (CV_CFL_LANG).
///
/// These values correspond to the CV_CFL_LANG enumeration in the Microsoft
/// Debug Interface Access SDK, and are documented here:
/// https://learn.microsoft.com/en-us/visualstudio/debugger/debug-interface-access/cv-cfl-lang
enum SourceLanguage : uint8_t {
#define CV_LANGUAGE(NAME, ID) NAME = ID,
#include "CodeViewLanguages.def"
};

/// Calling convention for a procedure or member function (CV_call_e).
///
/// These values correspond to the CV_call_e enumeration, and are documented
/// at the following locations:
///   https://msdn.microsoft.com/en-us/library/b2fc64ek.aspx
///   https://msdn.microsoft.com/en-us/library/windows/desktop/ms680207(v=vs.85).aspx
enum class CallingConvention : uint8_t {
  NearC = 0x00,       ///< Near right-to-left push; caller pops the stack.
  FarC = 0x01,        ///< Far right-to-left push; caller pops the stack.
  NearPascal = 0x02,  ///< Near left-to-right push; callee pops the stack.
  FarPascal = 0x03,   ///< Far left-to-right push; callee pops the stack.
  NearFast = 0x04,    ///< Near left-to-right push with regs; callee pops stack.
  FarFast = 0x05,     ///< Far left-to-right push with regs; callee pops stack.
  NearStdCall = 0x07, ///< Near __stdcall convention (callee pops stack).
  FarStdCall = 0x08,  ///< Far standard call.
  NearSysCall = 0x09, ///< Near system call.
  FarSysCall = 0x0a,  ///< Far system call.
  ThisCall = 0x0b,    ///< \c this call (\c this passed in a register).
  MipsCall = 0x0c,    ///< MIPS calling convention.
  Generic = 0x0d,     ///< Generic call sequence.
  AlphaCall = 0x0e,   ///< Alpha calling convention.
  PpcCall = 0x0f,     ///< PowerPC calling convention.
  SHCall = 0x10,      ///< Hitachi SuperH calling convention.
  ArmCall = 0x11,     ///< ARM calling convention.
  AM33Call = 0x12,    ///< AM33 call
  TriCall = 0x13,     ///< Infineon TriCore calling convention.
  SH5Call = 0x14,     ///< Hitachi SuperH-5 calling convention.
  M32RCall = 0x15,    ///< M32R calling convention.
  ClrCall = 0x16,     ///< .NET CLR calling convention.
  Inline = 0x17, ///< Marker for routines always inlined and thus lacking a convention.
  NearVector = 0x18, ///< Near vectorcall-style left-to-right push with regs.
  Swift = 0x19,      ///< Swift calling convention.
};

/// Attribute flags for class, struct, union, and enum type records (CV_CLASS_e).
enum class ClassOptions : uint16_t {
  None = 0x0000,   ///< No special class attributes.
  Packed = 0x0001, ///< Struct/class uses 1-byte packing (\c \#pragma pack(1)).
  HasConstructorOrDestructor = 0x0002, ///< Type has a user-defined ctor or dtor.
  HasOverloadedOperator = 0x0004, ///< Type has overloaded operators.
  Nested = 0x0008, ///< Type is nested inside another type.
  ContainsNestedClass = 0x0010, ///< Type contains nested class/struct/union types.
  HasOverloadedAssignmentOperator = 0x0020, ///< Type overloads assignment.
  HasConversionOperator = 0x0040, ///< Type has a conversion operator.
  ForwardReference = 0x0080, ///< Record is a forward declaration.
  Scoped = 0x0100, ///< Type is scoped (defined inside a function or other local scope).
  HasUniqueName = 0x0200, ///< Type has a unique decorated name.
  Sealed = 0x0400, ///< Type cannot be used as a base class.
  Intrinsic = 0x2000 ///< Intrinsic / built-in type encoding.
};
/// Bitwise OR combining \c ClassOptions flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Combined flag bits of \p a and \p b.
inline ClassOptions operator|(ClassOptions a, ClassOptions b) {
  return static_cast<ClassOptions>(llvm::to_underlying(a) |
                                   llvm::to_underlying(b));
}
/// Bitwise AND combining \c ClassOptions flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Intersection of flag bits in \p a and \p b.
inline ClassOptions operator&(ClassOptions a, ClassOptions b) {
  return static_cast<ClassOptions>(llvm::to_underlying(a) &
                                   llvm::to_underlying(b));
}
/// Bitwise complement of \c ClassOptions flag bits.
/// \param a Operand to complement.
/// \returns Bitwise complement of \p a.
inline ClassOptions operator~(ClassOptions a) {
  return static_cast<ClassOptions>(~llvm::to_underlying(a));
}
/// In-place bitwise OR of \c ClassOptions flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline ClassOptions &operator|=(ClassOptions &a, ClassOptions b) {
  a = a | b;
  return a;
}
/// In-place bitwise AND of \c ClassOptions flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline ClassOptions &operator&=(ClassOptions &a, ClassOptions b) {
  a = a & b;
  return a;
}

/// Flags describing a procedure frame (S_FRAMEPROC).
enum class FrameProcedureOptions : uint32_t {
  None = 0x00000000, ///< No frame-procedure options set.
  HasAlloca = 0x00000001, ///< Frame uses \c alloca (or equivalent).
  HasSetJmp = 0x00000002, ///< Frame contains \c setjmp.
  HasLongJmp = 0x00000004, ///< Frame contains \c longjmp.
  HasInlineAssembly = 0x00000008, ///< Frame contains inline assembly.
  HasExceptionHandling = 0x00000010, ///< Frame has C++ exception handling.
  MarkedInline = 0x00000020, ///< Function was marked \c inline.
  HasStructuredExceptionHandling = 0x00000040, ///< Frame has SEH.
  Naked = 0x00000080, ///< Function is \c __declspec(naked).
  SecurityChecks = 0x00000100, ///< Frame compiled with buffer security checks (/GS).
  AsynchronousExceptionHandling = 0x00000200, ///< Frame uses asynchronous EH (/EHa).
  NoStackOrderingForSecurityChecks = 0x00000400, ///< Stack ordering for /GS disabled.
  Inlined = 0x00000800, ///< Function was inlined into a caller.
  StrictSecurityChecks = 0x00001000, ///< Frame uses strict buffer security checks (e.g. required stack protection).
  SafeBuffers = 0x00002000, ///< Function compiled with safe buffers.
  EncodedLocalBasePointerMask = 0x0000C000, ///< Mask for encoded local base pointer register.
  EncodedParamBasePointerMask = 0x00030000, ///< Mask for encoded parameter base pointer register.
  ProfileGuidedOptimization = 0x00040000, ///< Frame compiled with profile-guided optimization.
  ValidProfileCounts = 0x00080000, ///< Frame has valid profile counts.
  OptimizedForSpeed = 0x00100000, ///< Frame optimized for speed.
  GuardCfg = 0x00200000, ///< Control Flow Guard (/guard:cf) enabled.
  GuardCfw = 0x00400000 ///< Control Flow Guard with write checks (/guard:cf,cfw).
};
/// Bitwise OR combining \c FrameProcedureOptions flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Combined flag bits of \p a and \p b.
inline FrameProcedureOptions operator|(FrameProcedureOptions a,
                                       FrameProcedureOptions b) {
  return static_cast<FrameProcedureOptions>(llvm::to_underlying(a) |
                                            llvm::to_underlying(b));
}
/// Bitwise AND combining \c FrameProcedureOptions flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Intersection of flag bits in \p a and \p b.
inline FrameProcedureOptions operator&(FrameProcedureOptions a,
                                       FrameProcedureOptions b) {
  return static_cast<FrameProcedureOptions>(llvm::to_underlying(a) &
                                            llvm::to_underlying(b));
}
/// Bitwise complement of \c FrameProcedureOptions flag bits.
/// \param a Operand to complement.
/// \returns Bitwise complement of \p a.
inline FrameProcedureOptions operator~(FrameProcedureOptions a) {
  return static_cast<FrameProcedureOptions>(~llvm::to_underlying(a));
}
/// In-place bitwise OR of \c FrameProcedureOptions flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline FrameProcedureOptions &operator|=(FrameProcedureOptions &a,
                                         FrameProcedureOptions b) {
  a = a | b;
  return a;
}
/// In-place bitwise AND of \c FrameProcedureOptions flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline FrameProcedureOptions &operator&=(FrameProcedureOptions &a,
                                         FrameProcedureOptions b) {
  a = a & b;
  return a;
}

/// Options for function type records (LF_PROCEDURE / LF_MFUNCTION).
enum class FunctionOptions : uint8_t {
  None = 0x00, ///< No function options set.
  CxxReturnUdt = 0x01, ///< Returns a C++ UDT via a hidden pointer.
  Constructor = 0x02, ///< Function is a C++ constructor.
  ConstructorWithVirtualBases = 0x04 ///< Constructor initializes virtual bases.
};
/// Bitwise OR combining \c FunctionOptions flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Combined flag bits of \p a and \p b.
inline FunctionOptions operator|(FunctionOptions a, FunctionOptions b) {
  return static_cast<FunctionOptions>(llvm::to_underlying(a) |
                                      llvm::to_underlying(b));
}
/// Bitwise AND combining \c FunctionOptions flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Intersection of flag bits in \p a and \p b.
inline FunctionOptions operator&(FunctionOptions a, FunctionOptions b) {
  return static_cast<FunctionOptions>(llvm::to_underlying(a) &
                                      llvm::to_underlying(b));
}
/// Bitwise complement of \c FunctionOptions flag bits.
/// \param a Operand to complement.
/// \returns Bitwise complement of \p a.
inline FunctionOptions operator~(FunctionOptions a) {
  return static_cast<FunctionOptions>(~llvm::to_underlying(a));
}
/// In-place bitwise OR of \c FunctionOptions flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline FunctionOptions &operator|=(FunctionOptions &a, FunctionOptions b) {
  a = a | b;
  return a;
}
/// In-place bitwise AND of \c FunctionOptions flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline FunctionOptions &operator&=(FunctionOptions &a, FunctionOptions b) {
  a = a & b;
  return a;
}

/// Homogeneous floating-point aggregate kind for ARM calling conventions.
enum class HfaKind : uint8_t {
  None = 0x00,   ///< Not an HFA.
  Float = 0x01,  ///< HFA of single-precision floats.
  Double = 0x02, ///< HFA of double-precision floats.
  Other = 0x03   ///< Other HFA element type.
};

/// Source-level access specifier. (CV_access_e)
enum class MemberAccess : uint8_t {
  None = 0,      ///< No access specifier / unspecified.
  Private = 1,   ///< Private member.
  Protected = 2, ///< Protected member.
  Public = 3     ///< Public member.
};

/// Part of member attribute flags. (CV_methodprop_e)
enum class MethodKind : uint8_t {
  Vanilla = 0x00,               ///< Ordinary non-virtual method.
  Virtual = 0x01,               ///< Virtual method (not introducing).
  Static = 0x02, ///< Static member function.
  Friend = 0x03,                ///< Friend function.
  IntroducingVirtual = 0x04,    ///< Introducing virtual method.
  PureVirtual = 0x05,           ///< Pure virtual method.
  PureIntroducingVirtual = 0x06 ///< Pure introducing virtual method.
};

/// Equivalent to CV_fldattr_t bitfield.
enum class MethodOptions : uint16_t {
  None = 0x0000,             ///< No method attribute flags set.
  AccessMask = 0x0003, ///< Low two bits encode \c MemberAccess.
  MethodKindMask = 0x001c,   ///< Bits encoding \c MethodKind.
  Pseudo = 0x0020,           ///< Compiler-synthesized pseudo method.
  NoInherit = 0x0040, ///< Member cannot be inherited by derived classes.
  NoConstruct = 0x0080,      ///< Does not participate in construction.
  CompilerGenerated = 0x0100, ///< Method was generated by the compiler.
  Sealed = 0x0200            ///< Method cannot be overridden.
};
/// Bitwise OR combining \c MethodOptions flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Combined flag bits of \p a and \p b.
inline MethodOptions operator|(MethodOptions a, MethodOptions b) {
  return static_cast<MethodOptions>(llvm::to_underlying(a) |
                                    llvm::to_underlying(b));
}
/// Bitwise AND combining \c MethodOptions flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Intersection of flag bits in \p a and \p b.
inline MethodOptions operator&(MethodOptions a, MethodOptions b) {
  return static_cast<MethodOptions>(llvm::to_underlying(a) &
                                    llvm::to_underlying(b));
}
/// Bitwise complement of \c MethodOptions flag bits.
/// \param a Operand to complement.
/// \returns Bitwise complement of \p a.
inline MethodOptions operator~(MethodOptions a) {
  return static_cast<MethodOptions>(~llvm::to_underlying(a));
}
/// In-place bitwise OR of \c MethodOptions flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline MethodOptions &operator|=(MethodOptions &a, MethodOptions b) {
  a = a | b;
  return a;
}
/// In-place bitwise AND of \c MethodOptions flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline MethodOptions &operator&=(MethodOptions &a, MethodOptions b) {
  a = a & b;
  return a;
}

/// Equivalent to CV_LABEL_TYPE_e.
enum class LabelType : uint16_t {
  Near = 0x0, ///< Near (offset-only) label.
  Far = 0x4,  ///< Far (segmented) label.
};

/// Equivalent to CV_modifier_t.
/// TODO: Add flag for _Atomic modifier
enum class ModifierOptions : uint16_t {
  None = 0x0000,      ///< No type modifiers.
  Const = 0x0001,     ///< Type is declared \c const.
  Volatile = 0x0002, ///< Type is declared \c volatile.
  Unaligned = 0x0004  ///< Type is unaligned.
};
/// Bitwise OR combining \c ModifierOptions flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Combined flag bits of \p a and \p b.
inline ModifierOptions operator|(ModifierOptions a, ModifierOptions b) {
  return static_cast<ModifierOptions>(llvm::to_underlying(a) |
                                      llvm::to_underlying(b));
}
/// Bitwise AND combining \c ModifierOptions flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Intersection of flag bits in \p a and \p b.
inline ModifierOptions operator&(ModifierOptions a, ModifierOptions b) {
  return static_cast<ModifierOptions>(llvm::to_underlying(a) &
                                      llvm::to_underlying(b));
}
/// Bitwise complement of \c ModifierOptions flag bits.
/// \param a Operand to complement.
/// \returns Bitwise complement of \p a.
inline ModifierOptions operator~(ModifierOptions a) {
  return static_cast<ModifierOptions>(~llvm::to_underlying(a));
}
/// In-place bitwise OR of \c ModifierOptions flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline ModifierOptions &operator|=(ModifierOptions &a, ModifierOptions b) {
  a = a | b;
  return a;
}
/// In-place bitwise AND of \c ModifierOptions flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline ModifierOptions &operator&=(ModifierOptions &a, ModifierOptions b) {
  a = a & b;
  return a;
}

/// Flags that can be OR'd into a debug subsection kind.
enum : uint32_t {
  SubsectionIgnoreFlag = 0x80000000 ///< If set on a subsection kind, the linker should ignore that subsection.
};

/// Kind of a CodeView debug subsection in .debug$S (or PDB equivalents).
enum class DebugSubsectionKind : uint32_t {
  None = 0,                 ///< No subsection / invalid kind.
  Symbols = 0xf1,           ///< Symbol records subsection.
  Lines = 0xf2,             ///< Source line mapping subsection.
  StringTable = 0xf3,       ///< String table subsection.
  FileChecksums = 0xf4,     ///< File checksums subsection.
  FrameData = 0xf5,         ///< Frame data subsection.
  InlineeLines = 0xf6,      ///< Inlinee source line subsection.
  CrossScopeImports = 0xf7, ///< Cross-module import ID mapping subsection.
  CrossScopeExports = 0xf8, ///< Cross-module export ID mapping subsection.

  // These appear to relate to .Net assembly info.
  ILLines = 0xf9, ///< .NET IL source line mapping subsection.
  FuncMDTokenMap = 0xfa,      ///< Function metadata token map subsection.
  TypeMDTokenMap = 0xfb,      ///< Type metadata token map subsection.
  MergedAssemblyInput = 0xfc, ///< Merged .NET assembly input subsection.

  CoffSymbolRVA = 0xfd, ///< COFF symbol RVA list subsection.

  XfgHashType = 0xff, ///< eXtended Flow Guard (XFG) type-hash subsection.
  XfgHashVirtual = 0x100, ///< XFG virtual-function hash subsection.
};

/// Equivalent to CV_ptrtype_e.
enum class PointerKind : uint8_t {
  Near16 = 0x00,                ///< 16-bit near pointer (offset only).
  Far16 = 0x01,                 ///< 16:16 far pointer.
  Huge16 = 0x02,                ///< 16:16 huge pointer (normalized, >64KB).
  BasedOnSegment = 0x03,        ///< Pointer based on a segment register.
  BasedOnValue = 0x04,          ///< Pointer based on the value of its base.
  BasedOnSegmentValue = 0x05,   ///< Pointer based on the segment value of its base.
  BasedOnAddress = 0x06,        ///< Pointer based on the address of its base.
  BasedOnSegmentAddress = 0x07, ///< Pointer to the segment address of its base.
  BasedOnType = 0x08,           ///< Pointer based on a type.
  BasedOnSelf = 0x09,           ///< Pointer based on itself.
  Near32 = 0x0a,                ///< 32-bit near pointer.
  Far32 = 0x0b,                 ///< 16:32 far pointer.
  Near64 = 0x0c                 ///< 64-bit near pointer.
};

/// Equivalent to CV_ptrmode_e.
enum class PointerMode : uint8_t {
  Pointer = 0x00,                 ///< Ordinary pointer.
  LValueReference = 0x01,         ///< L-value reference (classic C++ reference).
  PointerToDataMember = 0x02,     ///< Pointer to a non-static data member.
  PointerToMemberFunction = 0x03, ///< Pointer to a non-static member function.
  RValueReference = 0x04          ///< R-value reference.
};

/// Equivalent to misc lfPointerAttr bitfields.
enum class PointerOptions : uint32_t {
  None = 0x00000000,              ///< No pointer attribute flags set.
  Flat32 = 0x00000100,            ///< 32-bit flat pointer model.
  Volatile = 0x00000200,          ///< Pointer is declared \c volatile.
  Const = 0x00000400,             ///< Pointer is declared \c const.
  Unaligned = 0x00000800,         ///< Pointer is unaligned.
  Restrict = 0x00001000,          ///< Pointer is declared \c __restrict.
  WinRTSmartPointer = 0x00080000, ///< Windows Runtime smart pointer.
  LValueRefThisPointer = 0x00100000, ///< \c this is an l-value reference.
  RValueRefThisPointer = 0x00200000  ///< \c this is an r-value reference.
};
/// Bitwise OR combining \c PointerOptions flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Combined flag bits of \p a and \p b.
inline PointerOptions operator|(PointerOptions a, PointerOptions b) {
  return static_cast<PointerOptions>(llvm::to_underlying(a) |
                                     llvm::to_underlying(b));
}
/// Bitwise AND combining \c PointerOptions flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Intersection of flag bits in \p a and \p b.
inline PointerOptions operator&(PointerOptions a, PointerOptions b) {
  return static_cast<PointerOptions>(llvm::to_underlying(a) &
                                     llvm::to_underlying(b));
}
/// Bitwise complement of \c PointerOptions flag bits.
/// \param a Operand to complement.
/// \returns Bitwise complement of \p a.
inline PointerOptions operator~(PointerOptions a) {
  return static_cast<PointerOptions>(~llvm::to_underlying(a));
}
/// In-place bitwise OR of \c PointerOptions flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline PointerOptions &operator|=(PointerOptions &a, PointerOptions b) {
  a = a | b;
  return a;
}
/// In-place bitwise AND of \c PointerOptions flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline PointerOptions &operator&=(PointerOptions &a, PointerOptions b) {
  a = a & b;
  return a;
}

/// Equivalent to CV_pmtype_e.
enum class PointerToMemberRepresentation : uint16_t {
  Unknown = 0x00, ///< Representation not specified (pre-VC8).
  SingleInheritanceData = 0x01, ///< Member-data pointer, single inheritance.
  MultipleInheritanceData = 0x02, ///< Member-data pointer, multiple inheritance.
  VirtualInheritanceData = 0x03, ///< Member-data pointer, virtual inheritance.
  GeneralData = 0x04, ///< Member-data pointer, most general layout.
  SingleInheritanceFunction = 0x05,   ///< Member-function pointer, single-inheritance layout.
  MultipleInheritanceFunction = 0x06, ///< Member-function pointer, multiple inheritance.
  VirtualInheritanceFunction = 0x07,  ///< Member-function pointer, virtual inheritance.
  GeneralFunction = 0x08              ///< Member-function pointer, most general layout.
};

/// Kind of virtual function table slot (CV_VTS_desc_e).
enum class VFTableSlotKind : uint8_t {
  Near16 = 0x00, ///< 16-bit near vtable slot.
  Far16 = 0x01,  ///< 16-bit far vtable slot.
  This = 0x02,   ///< Slot adjusts \c this before the call.
  Outer = 0x03,  ///< Slot for an outer (enclosing) class.
  Meta = 0x04,   ///< Metadata slot.
  Near = 0x05, ///< 32-bit near virtual function table slot.
  Far = 0x06     ///< Far virtual function table slot.
};

/// Windows Runtime class kind recorded in LF_STRUCTURE / related records.
enum class WindowsRTClassKind : uint8_t {
  None = 0x00,       ///< Not a Windows Runtime class.
  RefClass = 0x01, ///< C++/WinRT reference class (\c ^).
  ValueClass = 0x02, ///< Windows Runtime value class.
  Interface = 0x03   ///< Windows Runtime interface.
};

/// Corresponds to CV_LVARFLAGS bitfield.
enum class LocalSymFlags : uint16_t {
  None = 0,                      ///< No local-symbol flags set.
  IsParameter = 1 << 0,          ///< Symbol is a function parameter.
  IsAddressTaken = 1 << 1,       ///< Address of the symbol is taken.
  IsCompilerGenerated = 1 << 2,  ///< Symbol was generated by the compiler.
  IsAggregate = 1 << 3,          ///< Symbol is an aggregate (struct/class/etc.).
  IsAggregated = 1 << 4,         ///< Symbol is a member of an aggregate.
  IsAliased = 1 << 5,            ///< Symbol is aliased by another symbol.
  IsAlias = 1 << 6,              ///< Symbol is an alias of another symbol.
  IsReturnValue = 1 << 7,        ///< Symbol represents a return value.
  IsOptimizedOut = 1 << 8,       ///< Symbol was optimized out.
  IsEnregisteredGlobal = 1 << 9, ///< Global variable enregistered in a register.
  IsEnregisteredStatic = 1 << 10, ///< Static variable enregistered in a register.
};
/// Bitwise OR combining \c LocalSymFlags flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Combined flag bits of \p a and \p b.
inline LocalSymFlags operator|(LocalSymFlags a, LocalSymFlags b) {
  return static_cast<LocalSymFlags>(llvm::to_underlying(a) |
                                    llvm::to_underlying(b));
}
/// Bitwise AND combining \c LocalSymFlags flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Intersection of flag bits in \p a and \p b.
inline LocalSymFlags operator&(LocalSymFlags a, LocalSymFlags b) {
  return static_cast<LocalSymFlags>(llvm::to_underlying(a) &
                                    llvm::to_underlying(b));
}
/// Bitwise complement of \c LocalSymFlags flag bits.
/// \param a Operand to complement.
/// \returns Bitwise complement of \p a.
inline LocalSymFlags operator~(LocalSymFlags a) {
  return static_cast<LocalSymFlags>(~llvm::to_underlying(a));
}
/// In-place bitwise OR of \c LocalSymFlags flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline LocalSymFlags &operator|=(LocalSymFlags &a, LocalSymFlags b) {
  a = a | b;
  return a;
}
/// In-place bitwise AND of \c LocalSymFlags flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline LocalSymFlags &operator&=(LocalSymFlags &a, LocalSymFlags b) {
  a = a & b;
  return a;
}

/// Corresponds to the CV_PUBSYMFLAGS bitfield.
enum class PublicSymFlags : uint32_t {
  None = 0,         ///< No public-symbol flags set.
  Code = 1 << 0,    ///< Public symbol refers to code.
  Function = 1 << 1, ///< Public symbol is a function.
  Managed = 1 << 2, ///< Public symbol is managed code.
  MSIL = 1 << 3,    ///< Public symbol is MSIL (.NET CIL).
};
/// Bitwise OR combining \c PublicSymFlags flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Combined flag bits of \p a and \p b.
inline PublicSymFlags operator|(PublicSymFlags a, PublicSymFlags b) {
  return static_cast<PublicSymFlags>(llvm::to_underlying(a) |
                                     llvm::to_underlying(b));
}
/// Bitwise AND combining \c PublicSymFlags flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Intersection of flag bits in \p a and \p b.
inline PublicSymFlags operator&(PublicSymFlags a, PublicSymFlags b) {
  return static_cast<PublicSymFlags>(llvm::to_underlying(a) &
                                     llvm::to_underlying(b));
}
/// Bitwise complement of \c PublicSymFlags flag bits.
/// \param a Operand to complement.
/// \returns Bitwise complement of \p a.
inline PublicSymFlags operator~(PublicSymFlags a) {
  return static_cast<PublicSymFlags>(~llvm::to_underlying(a));
}
/// In-place bitwise OR of \c PublicSymFlags flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline PublicSymFlags &operator|=(PublicSymFlags &a, PublicSymFlags b) {
  a = a | b;
  return a;
}
/// In-place bitwise AND of \c PublicSymFlags flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline PublicSymFlags &operator&=(PublicSymFlags &a, PublicSymFlags b) {
  a = a & b;
  return a;
}

/// Corresponds to the CV_PROCFLAGS bitfield.
enum class ProcSymFlags : uint8_t {
  None = 0,                      ///< No procedure flags set.
  HasFP = 1 << 0,                ///< Procedure has a frame pointer.
  HasIRET = 1 << 1,              ///< Procedure returns with \c iret.
  HasFRET = 1 << 2,              ///< Procedure returns with a far return.
  IsNoReturn = 1 << 3,           ///< Procedure never returns.
  IsUnreachable = 1 << 4,        ///< Procedure label is unreachable.
  HasCustomCallingConv = 1 << 5, ///< Procedure uses a custom calling convention.
  IsNoInline = 1 << 6,           ///< Procedure must not be inlined.
  HasOptimizedDebugInfo = 1 << 7, ///< Procedure has optimized debug info.
};
/// Bitwise OR combining \c ProcSymFlags flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Combined flag bits of \p a and \p b.
inline ProcSymFlags operator|(ProcSymFlags a, ProcSymFlags b) {
  return static_cast<ProcSymFlags>(llvm::to_underlying(a) |
                                   llvm::to_underlying(b));
}
/// Bitwise AND combining \c ProcSymFlags flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Intersection of flag bits in \p a and \p b.
inline ProcSymFlags operator&(ProcSymFlags a, ProcSymFlags b) {
  return static_cast<ProcSymFlags>(llvm::to_underlying(a) &
                                   llvm::to_underlying(b));
}
/// Bitwise complement of \c ProcSymFlags flag bits.
/// \param a Operand to complement.
/// \returns Bitwise complement of \p a.
inline ProcSymFlags operator~(ProcSymFlags a) {
  return static_cast<ProcSymFlags>(~llvm::to_underlying(a));
}
/// In-place bitwise OR of \c ProcSymFlags flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline ProcSymFlags &operator|=(ProcSymFlags &a, ProcSymFlags b) {
  a = a | b;
  return a;
}
/// In-place bitwise AND of \c ProcSymFlags flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline ProcSymFlags &operator&=(ProcSymFlags &a, ProcSymFlags b) {
  a = a & b;
  return a;
}

/// Corresponds to COMPILESYM2::Flags bitfield.
enum class CompileSym2Flags : uint32_t {
  None = 0,                  ///< No compile-symbol flags set.
  SourceLanguageMask = 0xFF, ///< Low byte encodes the \c SourceLanguage.
  EC = 1 << 8,               ///< Compiled with edit-and-continue support.
  NoDbgInfo = 1 << 9, ///< Module was compiled without CodeView debug info.
  LTCG = 1 << 10,            ///< Compiled with link-time code generation.
  NoDataAlign = 1 << 11,     ///< Compiled without data alignment.
  ManagedPresent = 1 << 12,  ///< Managed code is present in the module.
  SecurityChecks = 1 << 13, ///< Compiled with buffer security checks (/GS).
  HotPatch = 1 << 14,        ///< Compiled for hotpatching.
  CVTCIL = 1 << 15,          ///< Converted from Common Intermediate Language.
  MSILModule = 1 << 16, ///< Module is compiled as MSIL (.NET CIL).
};
/// Bitwise OR combining \c CompileSym2Flags flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Combined flag bits of \p a and \p b.
inline CompileSym2Flags operator|(CompileSym2Flags a, CompileSym2Flags b) {
  return static_cast<CompileSym2Flags>(llvm::to_underlying(a) |
                                       llvm::to_underlying(b));
}
/// Bitwise AND combining \c CompileSym2Flags flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Intersection of flag bits in \p a and \p b.
inline CompileSym2Flags operator&(CompileSym2Flags a, CompileSym2Flags b) {
  return static_cast<CompileSym2Flags>(llvm::to_underlying(a) &
                                       llvm::to_underlying(b));
}
/// Bitwise complement of \c CompileSym2Flags flag bits.
/// \param a Operand to complement.
/// \returns Bitwise complement of \p a.
inline CompileSym2Flags operator~(CompileSym2Flags a) {
  return static_cast<CompileSym2Flags>(~llvm::to_underlying(a));
}
/// In-place bitwise OR of \c CompileSym2Flags flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline CompileSym2Flags &operator|=(CompileSym2Flags &a, CompileSym2Flags b) {
  a = a | b;
  return a;
}
/// In-place bitwise AND of \c CompileSym2Flags flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline CompileSym2Flags &operator&=(CompileSym2Flags &a, CompileSym2Flags b) {
  a = a & b;
  return a;
}

/// Corresponds to COMPILESYM3::Flags bitfield.
enum class CompileSym3Flags : uint32_t {
  None = 0,                      ///< No compile-symbol flags set.
  SourceLanguageMask = 0xFF,     ///< Low byte encodes the \c SourceLanguage.
  EC = 1 << 8,                   ///< Compiled with edit-and-continue support.
  NoDbgInfo = 1 << 9, ///< Module was compiled without CodeView debug info.
  LTCG = 1 << 10,                ///< Compiled with link-time code generation.
  NoDataAlign = 1 << 11,         ///< Compiled without data alignment.
  ManagedPresent = 1 << 12,      ///< Managed code is present in the module.
  SecurityChecks = 1 << 13,      ///< Compiled with buffer security checks (/GS).
  HotPatch = 1 << 14,            ///< Compiled for hotpatching.
  CVTCIL = 1 << 15,              ///< Converted from Common Intermediate Language.
  MSILModule = 1 << 16,          ///< Module is compiled as MSIL (.NET CIL).
  Sdl = 1 << 17,                 ///< Compiled with additional SDL security checks.
  PGO = 1 << 18, ///< Module compiled with profile-guided optimization.
  Exp = 1 << 19,                 ///< .exp export-file module.
};
/// Bitwise OR combining \c CompileSym3Flags flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Combined flag bits of \p a and \p b.
inline CompileSym3Flags operator|(CompileSym3Flags a, CompileSym3Flags b) {
  return static_cast<CompileSym3Flags>(llvm::to_underlying(a) |
                                       llvm::to_underlying(b));
}
/// Bitwise AND combining \c CompileSym3Flags flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Intersection of flag bits in \p a and \p b.
inline CompileSym3Flags operator&(CompileSym3Flags a, CompileSym3Flags b) {
  return static_cast<CompileSym3Flags>(llvm::to_underlying(a) &
                                       llvm::to_underlying(b));
}
/// Bitwise complement of \c CompileSym3Flags flag bits.
/// \param a Operand to complement.
/// \returns Bitwise complement of \p a.
inline CompileSym3Flags operator~(CompileSym3Flags a) {
  return static_cast<CompileSym3Flags>(~llvm::to_underlying(a));
}
/// In-place bitwise OR of \c CompileSym3Flags flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline CompileSym3Flags &operator|=(CompileSym3Flags &a, CompileSym3Flags b) {
  a = a | b;
  return a;
}
/// In-place bitwise AND of \c CompileSym3Flags flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline CompileSym3Flags &operator&=(CompileSym3Flags &a, CompileSym3Flags b) {
  a = a & b;
  return a;
}

/// Flags for an S_EXPORT symbol.
enum class ExportFlags : uint16_t {
  None = 0,                    ///< No export flags set.
  IsConstant = 1 << 0, ///< Exported symbol is constant (read-only) data.
  IsData = 1 << 1,             ///< Exported symbol is data (not code).
  IsPrivate = 1 << 2,          ///< Export is private to the module.
  HasNoName = 1 << 3,          ///< Export has no name (ordinal-only).
  HasExplicitOrdinal = 1 << 4, ///< Export has an explicitly assigned ordinal.
  IsForwarder = 1 << 5         ///< Export forwards to another module.
};
/// Bitwise OR combining \c ExportFlags flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Combined flag bits of \p a and \p b.
inline ExportFlags operator|(ExportFlags a, ExportFlags b) {
  return static_cast<ExportFlags>(llvm::to_underlying(a) |
                                  llvm::to_underlying(b));
}
/// Bitwise AND combining \c ExportFlags flag bits.
/// \param a Left-hand operand.
/// \param b Right-hand operand.
/// \returns Intersection of flag bits in \p a and \p b.
inline ExportFlags operator&(ExportFlags a, ExportFlags b) {
  return static_cast<ExportFlags>(llvm::to_underlying(a) &
                                  llvm::to_underlying(b));
}
/// Bitwise complement of \c ExportFlags flag bits.
/// \param a Operand to complement.
/// \returns Bitwise complement of \p a.
inline ExportFlags operator~(ExportFlags a) {
  return static_cast<ExportFlags>(~llvm::to_underlying(a));
}
/// In-place bitwise OR of \c ExportFlags flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline ExportFlags &operator|=(ExportFlags &a, ExportFlags b) {
  a = a | b;
  return a;
}
/// In-place bitwise AND of \c ExportFlags flag bits.
/// \param a Left-hand operand updated in place.
/// \param b Right-hand operand.
/// \returns Reference to the updated \p a.
inline ExportFlags &operator&=(ExportFlags &a, ExportFlags b) {
  a = a & b;
  return a;
}

/// Binary annotation opcodes used in S_INLINESITE records.
enum class BinaryAnnotationsOpCode : uint32_t {
  Invalid, ///< Sentinel for an unrecognized or malformed annotation opcode.
  CodeOffset, ///< Absolute code offset from the function start.
  ChangeCodeOffsetBase, ///< Set the base used by subsequent relative code offsets.
  ChangeCodeOffset, ///< Advance the code offset by a delta.
  ChangeCodeLength, ///< Set the length of the current code range.
  ChangeFile, ///< Switch to a different source file.
  ChangeLineOffset, ///< Advance the source line by a signed delta.
  ChangeLineEndDelta, ///< Set the end-of-statement line delta.
  ChangeRangeKind, ///< Change whether the range is a statement or expression.
  ChangeColumnStart, ///< Set the starting column of the current range.
  ChangeColumnEndDelta, ///< Advance the ending column by a delta.
  ChangeCodeOffsetAndLineOffset, ///< Combined code-offset and line-offset advance.
  ChangeCodeLengthAndCodeOffset, ///< Combined code-length and code-offset update.
  ChangeColumnEnd, ///< Set the ending column of the current range.
};

/// Kind of security cookie used in an S_FRAMECOOKIE record (CV_cookietype_e).
enum class FrameCookieKind : uint8_t {
  Copy,            ///< Cookie is a plain copy of the security cookie.
  XorStackPointer, ///< Cookie XOR'd with the stack pointer.
  XorFramePointer, ///< Cookie XOR'd with the frame pointer.
  XorR13,          ///< Cookie XOR'd with register R13.
};

/// CodeView hardware register ID (CV_HREG_e).
enum class RegisterId : uint16_t {
#define CV_REGISTERS_ALL
#define CV_REGISTER(name, value) name = value,
#include "CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_ALL
};

/// Pair of CPU type and register ID for CodeView register name lookup.
///
/// Register IDs are shared across architectures, so \c Cpu disambiguates them.
struct CPURegister {
  /// Deleted; a CPU/register pair requires both fields.
  CPURegister() = delete;
  /// Construct a CPU/register pair.
  /// \param Cpu CPU architecture that interprets \p Reg.
  /// \param Reg CodeView register ID for \p Cpu.
  CPURegister(CPUType Cpu, codeview::RegisterId Reg) {
    this->Cpu = Cpu;
    this->Reg = Reg;
  }
  CPUType Cpu; ///< CPU architecture that interprets \c Reg.
  RegisterId Reg; ///< CodeView register ID (interpreted with \c Cpu).
};

/// Two-bit value indicating which register is the designated frame pointer
/// register. Appears in the S_FRAMEPROC record flags.
enum class EncodedFramePtrReg : uint8_t {
  None = 0,     ///< No designated frame-pointer register.
  StackPtr = 1, ///< Stack pointer (e.g. ESP/RSP).
  FramePtr = 2, ///< Frame pointer (e.g. EBP/RBP).
  BasePtr = 3,  ///< Base pointer (e.g. EBX/RBX on some ABIs).
};

/// Decode a two-bit \c EncodedFramePtrReg from S_FRAMEPROC into a \c RegisterId
/// for \p CPU (x86/x64 only; other CPUs map to \c RegisterId::NONE).
/// \param EncodedReg Two-bit encoded frame-pointer register selector.
/// \param CPU CPU type used to map the encoded value to a register ID.
/// \returns The corresponding \c RegisterId, or \c RegisterId::NONE for unsupported CPUs.
LLVM_ABI RegisterId decodeFramePtrReg(EncodedFramePtrReg EncodedReg,
                                      CPUType CPU);

/// Encode a frame-pointer \c RegisterId as a two-bit \c EncodedFramePtrReg.
/// \param Reg Frame-pointer register to encode.
/// \param CPU CPU type that interprets \p Reg.
/// \returns The two-bit encoded frame-pointer register value.
LLVM_ABI EncodedFramePtrReg encodeFramePtrReg(RegisterId Reg, CPUType CPU);

/// Thunk kind ordinal (THUNK_ORDINAL).
enum class ThunkOrdinal : uint8_t {
  Standard,         ///< Ordinary thunk.
  ThisAdjustor,     ///< Adjusts the \c this pointer before the call.
  Vcall,            ///< Virtual call thunk.
  Pcode,            ///< P-code thunk.
  UnknownLoad,      ///< Load of an unknown target.
  TrampIncremental, ///< Incremental linking trampoline thunk.
  BranchIsland      ///< Branch-island thunk.
};

/// Trampoline kind recorded in S_TRAMPOLINE symbols.
enum class TrampolineType : uint16_t {
  TrampIncremental, ///< Incremental linking trampoline.
  BranchIsland      ///< Branch-island trampoline.
};

/// These values correspond to the CV_SourceChksum_t enumeration.
enum class FileChecksumKind : uint8_t {
  None,   ///< No checksum is recorded for the file.
  MD5,    ///< MD5 checksum.
  SHA1,   ///< SHA-1 checksum.
  SHA256, ///< SHA-256 checksum.
};

/// Flags in the line-number contribution header (CV_DebugSLinesHeader_t::Flags).
enum LineFlags : uint16_t {
  LF_None = 0,          ///< No special line contribution attributes.
  LF_HaveColumns = 1,   ///< Column info follows each \c LineNumberEntry.
};

/// Data in the SUBSEC_FRAMEDATA subsection.
struct FrameData {
  support::ulittle32_t RvaStart; ///< RVA of the start of the described code range.
  support::ulittle32_t CodeSize; ///< Size in bytes of the described code range.
  support::ulittle32_t LocalSize; ///< Stack space in bytes for local variables.
  support::ulittle32_t ParamsSize; ///< Stack space in bytes for parameters.
  support::ulittle32_t MaxStackSize; ///< Maximum stack depth in bytes for this frame.
  support::ulittle32_t FrameFunc; ///< String-table offset of the frame procedure program.
  /// Size in bytes of the function prologue.
  support::ulittle16_t PrologSize;
  support::ulittle16_t SavedRegsSize; ///< Size in bytes of saved registers on the stack.
  /// Frame flags such as \c HasSEH, \c HasEH, and \c IsFunctionStart.
  support::ulittle32_t Flags;
  /// Bit flags stored in \c FrameData::Flags.
  enum : uint32_t {
    HasSEH = 1 << 0,          ///< Frame uses structured exception handling.
    HasEH = 1 << 1,           ///< Frame uses C++ exception handling.
    IsFunctionStart = 1 << 2, ///< Frame marks the start of a function.
  };
};

/// Maps a module-local ID to a PDB-wide global ID.
///
/// This structure allows cross-referencing between PDBs. For example, when a
/// PDB is being built during compilation it is not yet known what other modules
/// may end up in the PDB at link time. So certain types of IDs may clash
/// between the various compile time PDBs. For each affected module, a
/// subsection would be put into the PDB containing a mapping from its local IDs
/// to a single ID namespace for all items in the PDB file.
struct CrossModuleExport {
  support::ulittle32_t Local;  ///< Module-local ID before remapping.
  support::ulittle32_t Global; ///< PDB-wide global ID after remapping.
};

/// Header for a list of IDs imported from another module.
struct CrossModuleImport {
  support::ulittle32_t ModuleNameOffset; ///< Offset of the imported module name in the string table.
  support::ulittle32_t Count; ///< Number of imported IDs in the trailing array.
  // support::ulittle32_t ids[Count]; // id from referenced module
};

/// Kind of CodeView container that holds records.
enum class CodeViewContainer {
  ObjectFile, ///< Object-file CodeView records (byte-aligned).
  Pdb         ///< PDB CodeView records (4-byte aligned).
};

/// Return the byte alignment required for CodeView records in \p Container.
///
/// Object-file records are byte-aligned; PDB records are 4-byte aligned.
/// \param Container Object-file or PDB container whose record alignment is
///        requested.
/// \returns 1 for object-file containers, or 4 for PDB containers.
inline uint32_t alignOf(CodeViewContainer Container) {
  if (Container == CodeViewContainer::ObjectFile)
    return 1;
  return 4;
}

// Corresponds to CV_armswitchtype enum.
/// Describes how jump-table entries encode their target address.
///
/// Entries may store an absolute pointer, a signed or unsigned integer added to
/// a base address, or a shifted integer added to a base address.
enum class JumpTableEntrySize : uint16_t {
  Int8 = 0,             ///< Signed 8-bit offset added to a base address.
  UInt8 = 1,            ///< Unsigned 8-bit offset added to a base address.
  Int16 = 2,            ///< Signed 16-bit offset added to a base address.
  UInt16 = 3,           ///< Unsigned 16-bit offset added to a base address.
  Int32 = 4,            ///< Signed 32-bit offset added to a base address.
  UInt32 = 5,           ///< Unsigned 32-bit offset added to a base address.
  Pointer = 6,          ///< Absolute pointer-sized entry.
  UInt8ShiftLeft = 7,   ///< Unsigned 8-bit value shifted left, then added to a base.
  UInt16ShiftLeft = 8,  ///< Unsigned 16-bit value shifted left, then added to a base.
  Int8ShiftLeft = 9,    ///< Signed 8-bit value shifted left, then added to a base.
  Int16ShiftLeft = 10,  ///< Signed 16-bit value shifted left, then added to a base.
};
}
}

#endif
