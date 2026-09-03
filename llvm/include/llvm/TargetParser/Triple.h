//===-- llvm/TargetParser/Triple.h - Target triple helper class--*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_TRIPLE_H
#define LLVM_TARGETPARSER_TRIPLE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/VersionTuple.h"

// Some system headers or GCC predefined macros conflict with identifiers in
// this file.  Undefine them here.
#undef NetBSD
#undef mips
#undef sparc

namespace llvm {
enum class ExceptionHandling;
class Twine;

/// Helper class for working with autoconf-style target configuration names.
///
/// For historical reasons, we also call these 'triples' (they used to contain
/// exactly three fields).
///
/// Configuration names are strings in the canonical form:
///   ARCHITECTURE-VENDOR-OPERATING_SYSTEM
/// or
///   ARCHITECTURE-VENDOR-OPERATING_SYSTEM-ENVIRONMENT
///
/// This class is used for clients which want to support arbitrary
/// configuration names, but also want to implement certain special
/// behavior for particular configurations. This class isolates the mapping
/// from the components of the configuration name to well known IDs.
///
/// At its core the Triple class is designed to be a wrapper for a triple
/// string; the constructor does not change or normalize the triple string.
/// Clients that need to handle the non-canonical triples that users often
/// specify should use the normalize method.
///
/// See autoconf/config.guess for a glimpse into what configuration names
/// look like in practice.
class Triple {
public:
  /// Architecture type of a target triple.
  enum ArchType {
    UnknownArch, ///< Unknown or unspecified architecture.

    arm,         ///< ARM (little endian): arm, armv.*, xscale
    armeb,       ///< ARM (big endian): armeb
    aarch64,     ///< AArch64 (little endian): aarch64
    aarch64_be,  ///< AArch64 (big endian): aarch64_be
    aarch64_32,  ///< AArch64 (little endian) ILP32: aarch64_32
    arc,         ///< ARC: Synopsys ARC
    avr,         ///< AVR: Atmel AVR microcontroller
    bpfel,       ///< eBPF or extended BPF or 64-bit BPF (little endian)
    bpfeb,       ///< eBPF or extended BPF or 64-bit BPF (big endian)
    csky,        ///< CSKY: csky
    dxil,        ///< DXIL 32-bit DirectX bytecode
    hexagon,     ///< Hexagon: hexagon
    loongarch32, ///< LoongArch (32-bit): loongarch32
    loongarch64, ///< LoongArch (64-bit): loongarch64
    m68k,        ///< M68k: Motorola 680x0 family
    mips,        ///< MIPS: mips, mipsallegrex, mipsr6
    mipsel,      ///< MIPSEL: mipsel, mipsallegrexe, mipsr6el
    mips64,      ///< MIPS64: mips64, mips64r6, mipsn32, mipsn32r6
    mips64el,    ///< MIPS64EL: mips64el, mips64r6el, mipsn32el, mipsn32r6el
    msp430,      ///< MSP430: msp430
    ppc,         ///< PPC: powerpc
    ppcle,       ///< PPCLE: powerpc (little endian)
    ppc64,       ///< PPC64: powerpc64, ppu
    ppc64le,     ///< PPC64LE: powerpc64le
    r600,        ///< R600: AMD GPUs HD2XXX - HD6XXX
    amdgpu,      ///< AMDGPU: AMD GCN+ GPUs
    riscv32,     ///< RISC-V (32-bit, little endian): riscv32
    riscv64,     ///< RISC-V (64-bit, little endian): riscv64
    riscv32be,   ///< RISC-V (32-bit, big endian): riscv32be
    riscv64be,   ///< RISC-V (64-bit, big endian): riscv64be
    sparc,       ///< Sparc: sparc
    sparcv9,     ///< Sparcv9: Sparcv9
    sparcel,     ///< Sparc little endian. NB: 'Sparcle' is a CPU variant
    systemz,     ///< SystemZ: s390x
    // OpenASIP (http://openasip.org) / big endian 32b targets: tce
    tce, ///< OpenASIP big-endian 32-bit: tce
    // OpenASIP (http://openasip.org) / little endian 32b targets: tcele
    tcele, ///< OpenASIP little-endian 32-bit: tcele
    // OpenASIP (http://openasip.org) / little endian 64b targets: tcele
    tcele64, ///< OpenASIP little-endian 64-bit: tcele64
    thumb,          ///< Thumb (little endian): thumb, thumbv.*
    thumbeb,        ///< Thumb (big endian): thumbeb
    x86,            ///< X86: i[3-9]86
    x86_64,         ///< X86-64: amd64, x86_64
    xcore,          ///< XCore: xcore
    xtensa,         ///< Tensilica: Xtensa
    nvptx,          ///< NVPTX: 32-bit
    nvptx64,        ///< NVPTX: 64-bit
    amdil,          ///< AMDIL
    amdil64,        ///< AMDIL with 64-bit pointers
    hsail,          ///< AMD HSAIL
    hsail64,        ///< AMD HSAIL with 64-bit pointers
    spir,           ///< SPIR: standard portable IR for OpenCL 32-bit version
    spir64,         ///< SPIR: standard portable IR for OpenCL 64-bit version
    spirv,          ///< SPIR-V with logical memory layout.
    spirv32,        ///< SPIR-V with 32-bit pointers
    spirv64,        ///< SPIR-V with 64-bit pointers
    kalimba,        ///< Kalimba: generic kalimba
    shave,          ///< SHAVE: Movidius vector VLIW processors
    lanai,          ///< Lanai: Lanai 32-bit
    wasm32,         ///< WebAssembly with 32-bit pointers
    wasm64,         ///< WebAssembly with 64-bit pointers
    renderscript32, ///< 32-bit RenderScript
    renderscript64, ///< 64-bit RenderScript
    ve,             ///< NEC SX-Aurora Vector Engine
    LastArchType = ve ///< Sentinel for the last architecture type.
  };
  /// Sub-architecture type of a target triple.
  enum SubArchType {
    NoSubArch, ///< No sub-architecture.

    ARMSubArch_v9_7a, ///< ARM architecture v9.7-A
    ARMSubArch_v9_6a, ///< ARM architecture v9.6-A
    ARMSubArch_v9_5a, ///< ARM architecture v9.5-A
    ARMSubArch_v9_4a, ///< ARM architecture v9.4-A
    ARMSubArch_v9_3a, ///< ARM architecture v9.3-A
    ARMSubArch_v9_2a, ///< ARM architecture v9.2-A
    ARMSubArch_v9_1a, ///< ARM architecture v9.1-A
    ARMSubArch_v9, ///< ARM architecture v9-A
    ARMSubArch_v8_9a, ///< ARM architecture v8.9-A
    ARMSubArch_v8_8a, ///< ARM architecture v8.8-A
    ARMSubArch_v8_7a, ///< ARM architecture v8.7-A
    ARMSubArch_v8_6a, ///< ARM architecture v8.6-A
    ARMSubArch_v8_5a, ///< ARM architecture v8.5-A
    ARMSubArch_v8_4a, ///< ARM architecture v8.4-A
    ARMSubArch_v8_3a, ///< ARM architecture v8.3-A
    ARMSubArch_v8_2a, ///< ARM architecture v8.2-A
    ARMSubArch_v8_1a, ///< ARM architecture v8.1-A
    ARMSubArch_v8, ///< ARM architecture v8-A
    ARMSubArch_v8r, ///< ARM architecture v8-R
    ARMSubArch_v8m_baseline, ///< ARM architecture v8-M Baseline
    ARMSubArch_v8m_mainline, ///< ARM architecture v8-M Mainline
    ARMSubArch_v8_1m_mainline, ///< ARM architecture v8.1-M mainline
    ARMSubArch_v7, ///< ARM architecture v7
    ARMSubArch_v7em, ///< ARM architecture v7E-M
    ARMSubArch_v7m, ///< ARM architecture v7-M
    ARMSubArch_v7s, ///< ARM architecture v7s
    ARMSubArch_v7k, ///< ARM architecture v7k
    ARMSubArch_v7ve, ///< ARM architecture v7VE
    ARMSubArch_v6, ///< ARM architecture v6
    ARMSubArch_v6m, ///< ARM architecture v6-M
    ARMSubArch_v6k, ///< ARM architecture v6k
    ARMSubArch_v6t2, ///< ARM architecture v6T2
    ARMSubArch_v5, ///< ARM architecture v5
    ARMSubArch_v5te, ///< ARM architecture v5TE
    ARMSubArch_v4t, ///< ARM architecture v4T

    AArch64SubArch_arm64e, ///< AArch64 arm64e sub-architecture
    AArch64SubArch_arm64ec, ///< AArch64 ARM64EC sub-architecture
    AArch64SubArch_lfi, ///< AArch64 LFI sub-architecture

    X86_64SubArch_lfi, ///< X86-64 LFI sub-architecture

    KalimbaSubArch_v3, ///< Kalimba architecture v3
    KalimbaSubArch_v4, ///< Kalimba architecture v4
    KalimbaSubArch_v5, ///< Kalimba architecture v5

    MipsSubArch_r6, ///< MIPS Release 6
    PPCSubArch_spe, ///< PowerPC SPE sub-architecture

    // SPIR-V sub-arch corresponds to its version.
    SPIRVSubArch_v10, ///< SPIR-V version 1.0
    SPIRVSubArch_v11, ///< SPIR-V version 1.1
    SPIRVSubArch_v12, ///< SPIR-V version 1.2
    SPIRVSubArch_v13, ///< SPIR-V version 1.3
    SPIRVSubArch_v14, ///< SPIR-V version 1.4
    SPIRVSubArch_v15, ///< SPIR-V version 1.5
    SPIRVSubArch_v16, ///< SPIR-V version 1.6
    // DXIL sub-arch corresponds to its version.
    DXILSubArch_v1_0, ///< DXIL version 1.0
    DXILSubArch_v1_1, ///< DXIL version 1.1
    DXILSubArch_v1_2, ///< DXIL version 1.2
    DXILSubArch_v1_3, ///< DXIL version 1.3
    DXILSubArch_v1_4, ///< DXIL version 1.4
    DXILSubArch_v1_5, ///< DXIL version 1.5
    DXILSubArch_v1_6, ///< DXIL version 1.6
    DXILSubArch_v1_7, ///< DXIL version 1.7
    DXILSubArch_v1_8, ///< DXIL version 1.8
    DXILSubArch_v1_9, ///< DXIL version 1.9
    LatestDXILSubArch = DXILSubArch_v1_9, ///< Alias for the latest DXIL sub-architecture
    // AMDGPU sub-arch
    AMDGPUSubArch6, ///< AMDGPU gfx sub-architecture gfx6
    AMDGPUSubArch600, ///< AMDGPU gfx sub-architecture gfx600
    AMDGPUSubArch601, ///< AMDGPU gfx sub-architecture gfx601
    AMDGPUSubArch602, ///< AMDGPU gfx sub-architecture gfx602

    AMDGPUSubArch7, ///< AMDGPU gfx sub-architecture gfx7
    AMDGPUSubArch700, ///< AMDGPU gfx sub-architecture gfx700
    AMDGPUSubArch701, ///< AMDGPU gfx sub-architecture gfx701
    AMDGPUSubArch702, ///< AMDGPU gfx sub-architecture gfx702
    AMDGPUSubArch703, ///< AMDGPU gfx sub-architecture gfx703
    AMDGPUSubArch704, ///< AMDGPU gfx sub-architecture gfx704
    AMDGPUSubArch705, ///< AMDGPU gfx sub-architecture gfx705

    AMDGPUSubArch8, ///< AMDGPU gfx sub-architecture gfx8
    AMDGPUSubArch801, ///< AMDGPU gfx sub-architecture gfx801
    AMDGPUSubArch802, ///< AMDGPU gfx sub-architecture gfx802
    AMDGPUSubArch803, ///< AMDGPU gfx sub-architecture gfx803
    AMDGPUSubArch805, ///< AMDGPU gfx805
    // 810 is its own major arch.
    AMDGPUSubArch810, ///< AMDGPU gfx sub-architecture gfx810

    AMDGPUSubArch9, ///< AMDGPU gfx sub-architecture gfx9
    AMDGPUSubArch900, ///< AMDGPU gfx sub-architecture gfx900
    AMDGPUSubArch902, ///< AMDGPU gfx sub-architecture gfx902
    AMDGPUSubArch904, ///< AMDGPU gfx904
    AMDGPUSubArch906, ///< AMDGPU gfx sub-architecture gfx906
    AMDGPUSubArch909, ///< AMDGPU gfx sub-architecture gfx909
    AMDGPUSubArch90C, ///< AMDGPU gfx sub-architecture gfx90c

    // 908 and 90a are not covered by a generic target, and are their own major
    // subarches.
    AMDGPUSubArch908, ///< AMDGPU gfx sub-architecture gfx908
    AMDGPUSubArch90A, ///< AMDGPU gfx sub-architecture gfx90a

    AMDGPUSubArch9_4, ///< AMDGPU gfx9.4
    AMDGPUSubArch942, ///< AMDGPU gfx sub-architecture gfx942
    AMDGPUSubArch950, ///< AMDGPU gfx sub-architecture gfx950

    AMDGPUSubArch10_1, ///< AMDGPU gfx sub-architecture gfx10.1
    AMDGPUSubArch1010, ///< AMDGPU gfx1010
    AMDGPUSubArch1011, ///< AMDGPU gfx sub-architecture gfx1011
    AMDGPUSubArch1012, ///< AMDGPU gfx sub-architecture gfx1012
    AMDGPUSubArch1013, ///< AMDGPU gfx sub-architecture gfx1013

    AMDGPUSubArch10_3, ///< AMDGPU gfx sub-architecture gfx10.3
    AMDGPUSubArch1030, ///< AMDGPU gfx sub-architecture gfx1030
    AMDGPUSubArch1031, ///< AMDGPU gfx sub-architecture gfx1031
    AMDGPUSubArch1032, ///< AMDGPU gfx sub-architecture gfx1032
    AMDGPUSubArch1033, ///< AMDGPU gfx1033
    AMDGPUSubArch1034, ///< AMDGPU gfx sub-architecture gfx1034
    AMDGPUSubArch1035, ///< AMDGPU gfx sub-architecture gfx1035
    AMDGPUSubArch1036, ///< AMDGPU gfx sub-architecture gfx1036

    AMDGPUSubArch11, ///< AMDGPU gfx sub-architecture gfx11
    AMDGPUSubArch1100, ///< AMDGPU gfx1100
    AMDGPUSubArch1101, ///< AMDGPU gfx sub-architecture gfx1101
    AMDGPUSubArch1102, ///< AMDGPU gfx sub-architecture gfx1102
    AMDGPUSubArch1103, ///< AMDGPU gfx sub-architecture gfx1103
    AMDGPUSubArch1150, ///< AMDGPU gfx sub-architecture gfx1150
    AMDGPUSubArch1151, ///< AMDGPU gfx sub-architecture gfx1151
    AMDGPUSubArch1152, ///< AMDGPU gfx sub-architecture gfx1152
    AMDGPUSubArch1153, ///< AMDGPU gfx sub-architecture gfx1153
    AMDGPUSubArch1154, ///< AMDGPU gfx sub-architecture gfx1154

    AMDGPUSubArch11_7, ///< AMDGPU gfx sub-architecture gfx11.7
    AMDGPUSubArch1170, ///< AMDGPU gfx sub-architecture gfx1170
    AMDGPUSubArch1171, ///< AMDGPU gfx sub-architecture gfx1171
    AMDGPUSubArch1172, ///< AMDGPU gfx1172
    AMDGPUSubArch12, ///< AMDGPU gfx sub-architecture gfx12
    AMDGPUSubArch1200, ///< AMDGPU gfx sub-architecture gfx1200
    AMDGPUSubArch1201, ///< AMDGPU gfx sub-architecture gfx1201

    AMDGPUSubArch12_5, ///< AMDGPU gfx sub-architecture gfx12.5
    AMDGPUSubArch1250S, ///< AMDGPU gfx sub-architecture gfx1250s
    AMDGPUSubArch1250, ///< AMDGPU gfx sub-architecture gfx1250
    AMDGPUSubArch1251, ///< AMDGPU gfx sub-architecture gfx1251

    AMDGPUSubArch13, ///< AMDGPU gfx sub-architecture gfx13
    AMDGPUSubArch1310, ///< AMDGPU gfx sub-architecture gfx1310
    FirstAMDGPUSubArch = AMDGPUSubArch6, ///< Sentinel for the first AMDGPU sub-architecture
    LastAMDGPUSubArch = AMDGPUSubArch1310 ///< Sentinel for the last AMDGPU sub-architecture
  };
  /// Hardware/OS vendor component of a target triple.
  enum VendorType {
    UnknownVendor, ///< Unknown or unspecified vendor.

    Apple, ///< Apple.
    PC, ///< PC / generic vendor
    SCEI, ///< Sony Computer Entertainment Inc.
    Freescale, ///< Freescale Semiconductor.
    IBM, ///< IBM vendor
    ImaginationTechnologies, ///< Imagination Technologies.
    MipsTechnologies, ///< MIPS Technologies.
    NVIDIA, ///< NVIDIA Corporation.
    CSR, ///< Cambridge Silicon Radio.
    AMD, ///< Advanced Micro Devices.
    Mesa, ///< Mesa.
    SUSE, ///< SUSE Linux
    OpenEmbedded, ///< OpenEmbedded.
    Intel, ///< Intel Corporation.
    Meta, ///< Meta Platforms
    LastVendorType = Meta ///< Sentinel for the last vendor type.
  };
  /// Operating system type of a target triple.
  enum OSType {
    UnknownOS, ///< Unknown or unspecified OS

    Darwin, ///< Apple Darwin.
    DragonFly, ///< DragonFly BSD.
    FreeBSD, ///< FreeBSD.
    Fuchsia, ///< Google Fuchsia.
    IOS, ///< Apple iOS
    KFreeBSD, ///< Debian GNU/kFreeBSD.
    Linux, ///< Linux.
    Lv2, ///< PlayStation 3 Lv2 OS.
    MacOSX, ///< Apple macOS / OS X.
    Managarm, ///< Managarm.
    NetBSD, ///< NetBSD.
    OpenBSD, ///< OpenBSD.
    Solaris, ///< Oracle Solaris / SunOS
    UEFI, ///< Unified Extensible Firmware Interface.
    Win32, ///< Windows (Win32).
    ZOS, ///< IBM z/OS.
    Haiku, ///< Haiku OS.
    RTEMS, ///< Real-Time Executive for Multiprocessor Systems.
    AIX, ///< IBM AIX.
    CUDA,   ///< NVIDIA CUDA.
    NVCL, ///< NVIDIA OpenCL
    AMDHSA, ///< AMD HSA Runtime
    PS4, ///< Sony PlayStation 4
    PS5, ///< PlayStation 5.
    ELFIAMCU, ///< ELF IAMCU operating system.
    TvOS,      ///< Apple tvOS
    WatchOS,   ///< Apple watchOS
    BridgeOS,  ///< Apple bridgeOS
    DriverKit, ///< Apple DriverKit
    XROS,      ///< Apple XROS.
    Mesa3D, ///< Mesa 3D.
    AMDPAL,     ///< AMD PAL Runtime
    HermitCore, ///< HermitCore Unikernel/Multikernel
    Hurd,       ///< GNU/Hurd.
    WASI,       ///< Deprecated alias of WASI 0.1; in the future will be WASI 1.0.
    WASIp1, ///< WASI 0.1
    WASIp2,     ///< WASI 0.2
    WASIp3,     ///< WASI 0.3
    Emscripten, ///< Emscripten.
    ShaderModel, ///< DirectX ShaderModel
    LiteOS, ///< Huawei LiteOS.
    Serenity, ///< SerenityOS.
    Vulkan, ///< Vulkan SPIR-V
    CheriotRTOS, ///< CHERIOT RTOS.
    OpenCL, ///< OpenCL.
    ChipStar, ///< ChipStar.
    Firmware, ///< Firmware.
    QURT, ///< Qualcomm QuRT
    H2, ///< H2 operating system.
    LastOSType = H2 ///< Sentinel for the last OS type.
  };
  /// Environment / ABI component of a target triple.
  enum EnvironmentType {
    UnknownEnvironment, ///< Unknown or unspecified environment.

    GNU, ///< GNU environment.
    GNUT64, ///< GNU environment with 64-bit time_t.
    GNUABIN32, ///< GNU ABIN32 environment.
    GNUABI64, ///< GNU ABI64 environment.
    GNUEABI, ///< GNU EABI environment
    GNUEABIT64, ///< GNU EABI with 64-bit time_t.
    GNUEABIHF, ///< GNU EABI hard-float environment.
    GNUEABIHFT64, ///< GNU EABI hard-float with 64-bit time_t.
    GNUF32, ///< GNU single-precision float environment.
    GNUF64, ///< GNU double-precision float environment.
    GNUSF, ///< GNU soft-float environment.
    GNUX32, ///< GNU X32 environment.
    GNUILP32, ///< GNU ILP32 environment.
    CODE16, ///< 16-bit code environment.
    EABI, ///< ARM EABI environment
    EABIHF, ///< EABI hard-float environment.
    Android, ///< Android environment.
    Musl, ///< musl libc environment.
    MuslABIN32, ///< musl ABIN32 environment.
    MuslABI64, ///< musl ABI64 environment.
    MuslEABI, ///< musl EABI environment.
    MuslEABIHF, ///< musl EABI hard-float environment.
    MuslF32, ///< musl single-precision float environment.
    MuslSF, ///< musl soft-float environment.
    MuslX32, ///< musl X32 environment.
    MuslWALI, ///< musl WALI (WebAssembly Linux Interface) environment.
    LLVM, ///< LLVM runtime environment.

    MSVC, ///< Microsoft Visual C++ environment.
    Itanium, ///< Itanium C++ ABI environment.
    Cygnus, ///< Cygnus (MinGW) environment.
    CoreCLR, ///< .NET CoreCLR environment.
    Simulator, ///< Simulator variants of other systems, e.g., Apple's iOS
    MacABI, ///< Mac Catalyst variant of Apple's iOS deployment target.

    // Shader Stages
    // The order of these values matters, and must be kept in sync with the
    // language options enum in Clang. The ordering is enforced in
    // static_asserts in Triple.cpp and in Clang.
    Pixel, ///< Pixel shader stage.
    Vertex, ///< Vertex shader stage.
    Geometry, ///< Geometry shader stage.
    Hull, ///< Hull shader stage.
    Domain, ///< Domain shader stage
    Compute, ///< Compute shader stage.
    Library, ///< Library shader stage.
    RayGeneration, ///< Ray-generation shader stage.
    Intersection, ///< Intersection shader stage.
    AnyHit, ///< Any-hit ray-tracing shader stage
    ClosestHit, ///< Closest-hit ray-tracing shader stage.
    Miss, ///< Miss shader stage.
    Callable, ///< Callable shader stage.
    Mesh, ///< Mesh shader stage.
    Amplification, ///< Amplification shader stage.
    RootSignature, ///< Root-signature environment.
    OpenHOS, ///< OpenHarmony OS environment.
    Mlibc, ///< mlibc C library environment.

    PAuthTest, ///< Pointer-authentication test environment.
    MTIA, ///< MTIA environment.
    LastEnvironmentType = MTIA ///< Sentinel for the last environment type.
  };
  /// Object file format component of a target triple.
  enum ObjectFormatType {
    UnknownObjectFormat, ///< Unknown or unspecified object format.

    COFF, ///< COFF object file format.
    DXContainer, ///< DirectX container format.
    ELF, ///< ELF object file format.
    GOFF, ///< GOFF object file format.
    MachO, ///< Mach-O object file format.
    SPIRV, ///< SPIR-V object file format.
    Wasm, ///< WebAssembly object file format.
    XCOFF, ///< XCOFF object file format.
  };

private:
  std::string Data;

  /// The parsed arch type.
  ArchType Arch{};

  /// The parsed subarchitecture type.
  SubArchType SubArch{};

  /// The parsed vendor type.
  VendorType Vendor{};

  /// The parsed OS type.
  OSType OS{};

  /// The parsed Environment type.
  EnvironmentType Environment{};

  /// The object format type.
  ObjectFormatType ObjectFormat{};

public:
  /// @name Constructors
  /// @{

  /// Default constructor is the same as an empty string and leaves all
  /// triple fields unknown.
  Triple() = default;

  /// Construct a Triple by taking ownership of \p Str.
  /// @param Str Triple string to own.
  LLVM_ABI explicit Triple(std::string &&Str);
  /// Construct a Triple from a string reference.
  /// @param Str Triple string to copy.
  explicit Triple(StringRef Str) : Triple(Str.str()) {}
  /// Construct a Triple from a C string.
  /// @param Str Null-terminated triple string to copy.
  explicit Triple(const char *Str) : Triple(std::string(Str)) {}
  /// Construct a Triple from a std::string.
  /// @param Str Triple string to copy.
  explicit Triple(const std::string &Str) : Triple(std::string(Str)) {}
  /// Construct a Triple from a Twine.
  /// @param Str Triple string to materialize and own.
  LLVM_ABI explicit Triple(const Twine &Str);

  /// Construct a Triple from separate architecture, vendor, and OS strings.
  /// @param ArchStr Architecture component string.
  /// @param VendorStr Vendor component string.
  /// @param OSStr Operating-system component string.
  LLVM_ABI Triple(const Twine &ArchStr, const Twine &VendorStr,
                  const Twine &OSStr);
  /// Construct a Triple from architecture, vendor, OS, and environment strings.
  /// @param ArchStr Architecture component string.
  /// @param VendorStr Vendor component string.
  /// @param OSStr Operating-system component string.
  /// @param EnvironmentStr Environment component string.
  LLVM_ABI Triple(const Twine &ArchStr, const Twine &VendorStr,
                  const Twine &OSStr, const Twine &EnvironmentStr);
  /// Construct a Triple from architecture, optional sub-architecture, vendor, and OS.
  /// @param A Architecture type.
  /// @param SA Optional sub-architecture type.
  /// @param V Optional vendor type.
  /// @param OS Optional operating-system type.
  LLVM_ABI Triple(ArchType A, SubArchType SA = NoSubArch,
                  VendorType V = UnknownVendor, OSType OS = UnknownOS);
  /// Construct a Triple from architecture, sub-architecture, vendor, OS, and environment.
  /// @param A Architecture type.
  /// @param SA Sub-architecture type.
  /// @param V Vendor type.
  /// @param OS Operating-system type.
  /// @param E Environment type.
  LLVM_ABI Triple(ArchType A, SubArchType SA, VendorType V, OSType OS,
                  EnvironmentType E);
  /// Construct a Triple from architecture, sub-architecture, vendor, OS,
  /// environment, and object format.
  /// @param A Architecture type.
  /// @param SA Sub-architecture type.
  /// @param V Vendor type.
  /// @param OS Operating-system type.
  /// @param E Environment type.
  /// @param OF Object format type.
  LLVM_ABI Triple(ArchType A, SubArchType SA, VendorType V, OSType OS,
                  EnvironmentType E, ObjectFormatType OF);

  /// True if this triple equals \p Other in all typed components.
  /// @param Other Triple to compare against.
  /// @return True if this triple equals \p Other.
  LLVM_ABI bool operator==(const Triple &Other) const;
  /// True if this triple is not equal to \p Other.
  /// @param Other Triple to compare against.
  /// @return True if this triple is not equal to \p Other.
  bool operator!=(const Triple &Other) const { return !(*this == Other); }

  /// Lexicographical compare of the normalized triple string against \p Other.
  /// @param Other Triple to compare against.
  /// @return True if this triple's string is lexicographically less than \p Other's.
  LLVM_ABI bool operator<(const Triple &Other) const;

  /// @}
  /// @name Normalization
  /// @{

  /// Canonical form
  enum class CanonicalForm {
    ANY = 0, ///< Keep as many components as present in the input.
    THREE_IDENT = 3, ///< ARCHITECTURE-VENDOR-OPERATING_SYSTEM
    FOUR_IDENT = 4,  ///< ARCHITECTURE-VENDOR-OPERATING_SYSTEM-ENVIRONMENT
    FIVE_IDENT = 5,  ///< ARCHITECTURE-VENDOR-OPERATING_SYSTEM-ENVIRONMENT-FORMAT
  };

  /// Normalize an arbitrary machine specification into a canonical triple string.
  ///
  /// Turns the input into the canonical triple form (or something sensible that
  /// the Triple class understands if nothing better can reasonably be done). In
  /// particular, it handles the common case in which otherwise valid components
  /// are in the wrong order.
  /// @param Str Machine specification string to normalize.
  /// @param Form Desired output canonical form.
  /// @return Normalized canonical triple string.
  LLVM_ABI static std::string
  normalize(StringRef Str, CanonicalForm Form = CanonicalForm::ANY);

  /// Return the normalized form of this triple's string.
  /// @param Form Desired output canonical form.
  /// @return Normalized form of this triple's string.
  std::string normalize(CanonicalForm Form = CanonicalForm::ANY) const {
    return normalize(Data, Form);
  }

  /// @}
  /// @name Typed Component Access
  /// @{

  /// Get the parsed architecture type of this triple.
  /// @return Parsed architecture type.
  ArchType getArch() const { return Arch; }

  /// get the parsed subarchitecture type for this triple.
  /// @return Parsed sub-architecture type.
  SubArchType getSubArch() const { return SubArch; }

  /// Get the parsed vendor type of this triple.
  /// @return Parsed vendor type.
  VendorType getVendor() const { return Vendor; }

  /// Get the parsed operating system type of this triple.
  /// @return Parsed operating-system type.
  OSType getOS() const { return OS; }

  /// Does this triple have the optional environment (fourth) component?
  /// @return True if the optional environment component is present.
  bool hasEnvironment() const { return getEnvironmentName() != ""; }

  /// Get the parsed environment type of this triple.
  /// @return Parsed environment type.
  EnvironmentType getEnvironment() const { return Environment; }

  /// Parse the version number from the OS name component of the
  /// triple, if present.
  ///
  /// For example, "fooos1.2.3" would return (1, 2, 3).
  /// @return Parsed environment version tuple, or empty if absent.
  LLVM_ABI VersionTuple getEnvironmentVersion() const;

  /// Get the object format for this triple.
  /// @return Object format type for this triple.
  ObjectFormatType getObjectFormat() const { return ObjectFormat; }

  /// Parse the version number from the OS name component of the triple, if
  /// present.
  ///
  /// For example, "fooos1.2.3" would return (1, 2, 3).
  /// @return Parsed OS version tuple, or empty if absent.
  LLVM_ABI VersionTuple getOSVersion() const;

  /// Return just the major version number, this is specialized because it is a
  /// common query.
  /// @return Major component of the OS version.
  unsigned getOSMajorVersion() const { return getOSVersion().getMajor(); }

  /// Parse the OS version and translate generic Darwin versions to macOS versions.
  ///
  /// Parses the version number as with getOSVersion and then translates generic
  /// "darwin" versions to the corresponding OS X versions. This may also be
  /// called with IOS triples but the OS X version number is just set to a
  /// constant 10.4.0 in that case. Returns true if successful.
  /// @param Version Receives the translated macOS version on success.
  /// @return True if a macOS version was successfully parsed into \p Version.
  LLVM_ABI bool getMacOSXVersion(VersionTuple &Version) const;

  /// Parse the version number as with getOSVersion.  This should only be called
  /// with IOS or generic triples.
  /// @return Parsed iOS (or generic Darwin) version tuple.
  LLVM_ABI VersionTuple getiOSVersion() const;

  /// Parse the version number as with getOSVersion.  This should only be called
  /// with WatchOS or generic triples.
  /// @return Parsed watchOS (or generic) version tuple.
  LLVM_ABI VersionTuple getWatchOSVersion() const;

  /// Parse the version number as with getOSVersion.
  /// @return Parsed DriverKit version tuple.
  LLVM_ABI VersionTuple getDriverKitVersion() const;

  /// Parse the Vulkan version number from the OSVersion and SPIR-V version
  /// (SubArch).  This should only be called with Vulkan SPIR-V triples.
  /// @return Parsed Vulkan version tuple.
  LLVM_ABI VersionTuple getVulkanVersion() const;

  /// Parse the DXIL version number from the OSVersion and DXIL version
  /// (SubArch).  This should only be called with DXIL triples.
  /// @return Parsed DXIL version tuple.
  LLVM_ABI VersionTuple getDXILVersion() const;

  /// @}
  /// @name Direct Component Access
  /// @{

  /// Return the full triple string.
  /// @return Full triple string.
  const std::string &str() const { return Data; }

  /// Get the full triple string.
  /// @return Full triple string.
  const std::string &getTriple() const { return Data; }

  /// Whether the triple is empty / default constructed.
  /// @return True if the triple string is empty.
  bool empty() const { return Data.empty(); }

  /// Get the architecture (first) component of the triple.
  /// @return Architecture (first) component of the triple.
  LLVM_ABI StringRef getArchName() const;

  /// Get the vendor (second) component of the triple.
  /// @return Vendor (second) component of the triple.
  LLVM_ABI StringRef getVendorName() const;

  /// Get the operating system (third) component of the triple.
  /// @return Operating-system (third) component of the triple.
  LLVM_ABI StringRef getOSName() const;

  /// Get the optional environment (fourth) component of the triple, or "" if
  /// empty.
  /// @return Environment component, or an empty string if absent.
  LLVM_ABI StringRef getEnvironmentName() const;

  /// Get the operating system and optional environment components as a single
  /// string (separated by a '-' if the environment component is present).
  /// @return Combined OS and optional environment components.
  LLVM_ABI StringRef getOSAndEnvironmentName() const;

  /// Get the version component of the environment component as a single
  /// string (the version after the environment).
  ///
  /// For example, "fooos1.2.3" would return "1.2.3".
  /// @return Version substring from the environment component.
  LLVM_ABI StringRef getEnvironmentVersionString() const;

  /// @}
  /// @name Convenience Predicates
  /// @{

  /// Returns the pointer width of this architecture.
  /// @param Arch Architecture whose pointer width is requested.
  /// @return Pointer width in bits for \p Arch.
  LLVM_ABI static unsigned getArchPointerBitWidth(llvm::Triple::ArchType Arch);

  /// Returns the pointer width of this architecture.
  /// @return Pointer width in bits for this triple's architecture.
  unsigned getArchPointerBitWidth() const {
    return getArchPointerBitWidth(getArch());
  }

  /// Returns the trampoline size in bytes for this configuration.
  /// @return Trampoline size in bytes for this configuration.
  LLVM_ABI unsigned getTrampolineSize() const;

  /// Test whether the architecture is 64-bit
  ///
  /// Note that this tests for 64-bit pointer width, and nothing else. Note
  /// that we intentionally expose only three predicates, 64-bit, 32-bit, and
  /// 16-bit. The inner details of pointer width for particular architectures
  /// is not summed up in the triple, and so only a coarse grained predicate
  /// system is provided.
  /// @return True if the architecture has 64-bit pointer width.
  LLVM_ABI bool isArch64Bit() const;

  /// Test whether the architecture is 32-bit
  ///
  /// Note that this tests for 32-bit pointer width, and nothing else.
  /// @return True if the architecture has 32-bit pointer width.
  LLVM_ABI bool isArch32Bit() const;

  /// Test whether the architecture is 16-bit
  ///
  /// Note that this tests for 16-bit pointer width, and nothing else.
  /// @return True if the architecture has 16-bit pointer width.
  LLVM_ABI bool isArch16Bit() const;

  /// True if this triple's OS version is less than the given version.
  /// @param Major Major version component to compare against.
  /// @param Minor Minor version component to compare against.
  /// @param Micro Micro version component to compare against.
  /// @return True if this triple's OS version is less than the given version.
  bool isOSVersionLT(unsigned Major, unsigned Minor = 0,
                     unsigned Micro = 0) const {
    if (Minor == 0) {
      return getOSVersion() < VersionTuple(Major);
    }
    if (Micro == 0) {
      return getOSVersion() < VersionTuple(Major, Minor);
    }
    return getOSVersion() < VersionTuple(Major, Minor, Micro);
  }

  /// True if the OS version is greater than or equal to the given version.
  /// @param Major Major version component to compare against.
  /// @param Minor Minor version component to compare against.
  /// @param Micro Micro version component to compare against.
  /// @return True if this triple's OS version is greater than or equal to the given version.
  bool isOSVersionGE(unsigned Major, unsigned Minor = 0,
                     unsigned Micro = 0) const {
    return !isOSVersionLT(Major, Minor, Micro);
  }

  /// True if this triple's OS version is less than \p Other's.
  /// @param Other Triple whose OS version is compared against.
  /// @return True if this triple's OS version is less than \p Other's.
  bool isOSVersionLT(const Triple &Other) const {
    return getOSVersion() < Other.getOSVersion();
  }

  /// True if this triple's OS version is greater than or equal to \p Other's.
  /// @param Other Triple whose OS version is compared against.
  /// @return True if this triple's OS version is greater than or equal to \p Other's.
  bool isOSVersionGE(const Triple &Other) const {
    return getOSVersion() >= Other.getOSVersion();
  }

  /// True if this Mac OS X triple's version is less than the given version.
  ///
  /// Handles skewed version numbering schemes used by the "darwin" triples.
  /// @param Major Major version component to compare against.
  /// @param Minor Minor version component to compare against.
  /// @param Micro Micro version component to compare against.
  /// @return True if this Mac OS X triple's version is less than the given version.
  LLVM_ABI bool isMacOSXVersionLT(unsigned Major, unsigned Minor = 0,
                                  unsigned Micro = 0) const;

  /// True if this Mac OS X triple's version is greater than or equal to the given version.
  /// @param Major Major version component to compare against.
  /// @param Minor Minor version component to compare against.
  /// @param Micro Micro version component to compare against.
  /// @return True if this Mac OS X triple's version is greater than or equal to the given version.
  bool isMacOSXVersionGE(unsigned Major, unsigned Minor = 0,
                         unsigned Micro = 0) const {
    return !isMacOSXVersionLT(Major, Minor, Micro);
  }

  /// Is this a Mac OS X triple. For legacy reasons, we support both "darwin"
  /// and "osx" as OS X triples.
  /// @return True if this is a Mac OS X (darwin/osx) triple.
  bool isMacOSX() const {
    return getOS() == Triple::Darwin || getOS() == Triple::MacOSX;
  }

  /// True if this is an iOS (or tvOS) triple.
  ///
  /// Note: This identifies tvOS as a variant of iOS. If that ever changes,
  /// i.e., if the two operating systems diverge or their version numbers get
  /// out of sync, that will need to be changed. watchOS has completely
  /// different version numbers so it is not included.
  /// @return True if this is an iOS or tvOS triple.
  bool isiOS() const { return getOS() == Triple::IOS || isTvOS(); }

  /// Is this an Apple tvOS triple.
  /// @return True if this is an Apple tvOS triple.
  bool isTvOS() const { return getOS() == Triple::TvOS; }

  /// Is this an Apple watchOS triple.
  /// @return True if this is an Apple watchOS triple.
  bool isWatchOS() const { return getOS() == Triple::WatchOS; }

  /// True if this uses the Apple watchOS ABI (ARM v7k sub-architecture).
  /// @return True if this uses the Apple watchOS ABI.
  bool isWatchABI() const { return getSubArch() == Triple::ARMSubArch_v7k; }

  /// Is this an Apple XROS triple.
  /// @return True if this is an Apple XROS triple.
  bool isXROS() const { return getOS() == Triple::XROS; }

  /// Is this an Apple BridgeOS triple.
  /// @return True if this is an Apple BridgeOS triple.
  bool isBridgeOS() const { return getOS() == Triple::BridgeOS; }

  /// Is this an Apple DriverKit triple.
  /// @return True if this is an Apple DriverKit triple.
  bool isDriverKit() const { return getOS() == Triple::DriverKit; }

  /// True if this is a z/OS triple.
  /// @return True if this is a z/OS triple.
  bool isOSzOS() const { return getOS() == Triple::ZOS; }

  /// Is this an Apple MachO triple.
  /// @return True if this is an Apple MachO triple.
  bool isAppleMachO() const {
    return (getVendor() == Triple::Apple) && isOSBinFormatMachO();
  }

  /// Is this an Apple firmware triple.
  /// @return True if this is an Apple firmware triple.
  bool isAppleFirmware() const {
    return (getVendor() == Triple::Apple) && isOSFirmware();
  }

  /// Is this a "Darwin" OS (macOS, iOS, tvOS, watchOS, DriverKit, XROS, or
  /// bridgeOS).
  /// @return True if this is a Darwin-family OS triple.
  bool isOSDarwin() const {
    return isMacOSX() || isiOS() || isWatchOS() || isDriverKit() || isXROS() ||
           isBridgeOS() || isAppleFirmware();
    // Apple firmware isn't necessarily a Darwin based OS, but for most intents
    // and purposes it can be treated like a Darwin OS in the compiler.
  }

  /// True if the environment is a simulator variant (e.g. Apple Simulator).
  /// @return True if the environment is a simulator variant.
  bool isSimulatorEnvironment() const {
    return getEnvironment() == Triple::Simulator;
  }

  /// True if this is the Mac Catalyst (MacABI) environment.
  /// @return True if this is the Mac Catalyst environment.
  bool isMacCatalystEnvironment() const {
    return getEnvironment() == Triple::MacABI;
  }

  /// Returns true for targets that run on a macOS machine.
  /// @return True if the target runs on a macOS machine.
  bool isTargetMachineMac() const {
    return isMacOSX() || (isOSDarwin() && (isSimulatorEnvironment() ||
                                           isMacCatalystEnvironment()));
  }

  /// Tests whether the OS is NetBSD.
  /// @return True if the OS is NetBSD.
  bool isOSNetBSD() const { return getOS() == Triple::NetBSD; }

  /// Tests whether the OS is OpenBSD.
  /// @return True if the OS is OpenBSD.
  bool isOSOpenBSD() const { return getOS() == Triple::OpenBSD; }

  /// Tests whether the OS is FreeBSD.
  /// @return True if the OS is FreeBSD.
  bool isOSFreeBSD() const { return getOS() == Triple::FreeBSD; }

  /// Tests whether the OS is Fuchsia.
  /// @return True if the OS is Fuchsia.
  bool isOSFuchsia() const { return getOS() == Triple::Fuchsia; }

  /// Tests whether the OS is DragonFly BSD.
  /// @return True if the OS is DragonFly BSD.
  bool isOSDragonFly() const { return getOS() == Triple::DragonFly; }

  /// Tests whether the OS is Solaris.
  /// @return True if the OS is Solaris.
  bool isOSSolaris() const { return getOS() == Triple::Solaris; }

  /// True if this is the Intel MCU (ELFIAMCU) OS.
  /// @return True if this is the Intel MCU (ELFIAMCU) OS.
  bool isOSIAMCU() const { return getOS() == Triple::ELFIAMCU; }

  /// Tests whether the OS is unknown/unspecified.
  /// @return True if the OS is unknown.
  bool isOSUnknown() const { return getOS() == Triple::UnknownOS; }

  /// Tests whether the environment is a GNU environment.
  /// @return True if the environment is a GNU variant.
  bool isGNUEnvironment() const {
    EnvironmentType Env = getEnvironment();
    return Env == Triple::GNU || Env == Triple::GNUT64 ||
           Env == Triple::GNUABIN32 || Env == Triple::GNUABI64 ||
           Env == Triple::GNUEABI || Env == Triple::GNUEABIT64 ||
           Env == Triple::GNUEABIHF || Env == Triple::GNUEABIHFT64 ||
           Env == Triple::GNUF32 || Env == Triple::GNUF64 ||
           Env == Triple::GNUSF || Env == Triple::GNUX32;
  }

  /// Tests whether the OS is Haiku.
  /// @return True if the OS is Haiku.
  bool isOSHaiku() const { return getOS() == Triple::Haiku; }

  /// Tests whether the OS is UEFI.
  /// @return True if the OS is UEFI.
  bool isUEFI() const { return getOS() == Triple::UEFI; }

  /// Tests whether the OS is Windows.
  /// @return True if the OS is Windows.
  bool isOSWindows() const { return getOS() == Triple::Win32; }

  /// Tests whether the OS is Windows or UEFI.
  ///
  /// These targets generally share Windows low-level platform ABI conventions,
  /// but this does not imply support for a hosted Windows environment or its
  /// runtime libraries. Use object format or environment predicates when those
  /// properties matter.
  /// @return True if the OS is Windows or UEFI.
  bool isOSWindowsOrUEFI() const { return isOSWindows() || isUEFI(); }

  /// Checks if the environment is MSVC.
  /// @return True if the environment is explicitly MSVC on Windows.
  bool isKnownWindowsMSVCEnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::MSVC;
  }

  /// Checks if the environment could be MSVC.
  /// @return True if the environment could be MSVC on Windows.
  bool isWindowsMSVCEnvironment() const {
    return isKnownWindowsMSVCEnvironment() ||
           (isOSWindows() && getEnvironment() == Triple::UnknownEnvironment);
  }

  /// True if this is a Windows target using the Arm64EC ABI.
  /// @return True if this is a Windows Arm64EC target.
  bool isWindowsArm64EC() const {
    return getArch() == Triple::aarch64 &&
           getSubArch() == Triple::AArch64SubArch_arm64ec;
  }

  /// True if this is Windows targeting the CoreCLR environment.
  /// @return True if this is Windows with the CoreCLR environment.
  bool isWindowsCoreCLREnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::CoreCLR;
  }

  /// True if this is Windows targeting the Itanium C++ ABI environment.
  /// @return True if this is Windows with the Itanium environment.
  bool isWindowsItaniumEnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::Itanium;
  }

  /// Tests whether the environment is Cygwin on Windows.
  /// @return True if this is Windows with the Cygwin environment.
  bool isWindowsCygwinEnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::Cygnus;
  }

  /// True if this is Windows targeting the GNU (MinGW) environment.
  /// @return True if this is Windows with the GNU (MinGW) environment.
  bool isWindowsGNUEnvironment() const {
    return isOSWindows() && getEnvironment() == Triple::GNU;
  }

  /// Tests for either Cygwin or MinGW OS
  /// @return True if this is Cygwin or MinGW.
  bool isOSCygMing() const {
    return isWindowsCygwinEnvironment() || isWindowsGNUEnvironment();
  }

  /// Is this a "Windows" OS targeting a "MSVCRT.dll" environment.
  /// @return True if this Windows target uses an MSVCRT-compatible environment.
  bool isOSMSVCRT() const {
    return isWindowsMSVCEnvironment() || isWindowsGNUEnvironment() ||
           isWindowsItaniumEnvironment();
  }

  /// Tests whether the OS is Linux.
  /// @return True if the OS is Linux.
  bool isOSLinux() const { return getOS() == Triple::Linux; }

  /// Tests whether the OS is kFreeBSD.
  /// @return True if the OS is kFreeBSD.
  bool isOSKFreeBSD() const { return getOS() == Triple::KFreeBSD; }

  /// Tests whether the OS is Hurd.
  /// @return True if the OS is Hurd.
  bool isOSHurd() const { return getOS() == Triple::Hurd; }

  /// Tests whether the OS is WASI.
  /// @return True if the OS is WASI.
  bool isOSWASI() const {
    return getOS() == Triple::WASI || getOS() == Triple::WASIp1 ||
           getOS() == Triple::WASIp2 || getOS() == Triple::WASIp3;
  }

  /// Tests whether the OS is Emscripten.
  /// @return True if the OS is Emscripten.
  bool isOSEmscripten() const { return getOS() == Triple::Emscripten; }

  /// Tests whether the OS uses glibc.
  /// @return True if the OS uses glibc.
  bool isOSGlibc() const {
    return (getOS() == Triple::Linux || getOS() == Triple::KFreeBSD ||
            getOS() == Triple::Hurd) &&
           !isAndroid() && !isMusl() && getEnvironment() != Triple::PAuthTest;
  }

  /// Tests whether the OS is AIX.
  /// @return True if the OS is AIX.
  bool isOSAIX() const { return getOS() == Triple::AIX; }

  /// True if this is a Serenity OS triple.
  /// @return True if this is a Serenity OS triple.
  bool isOSSerenity() const { return getOS() == Triple::Serenity; }

  /// Tests whether the OS is QURT.
  /// @return True if the OS is QURT.
  bool isOSQurt() const { return getOS() == Triple::QURT; }

  /// Tests whether the OS is H2.
  /// @return True if the OS is H2.
  bool isOSH2() const { return getOS() == Triple::H2; }

  /// Tests whether the OS uses the ELF binary format.
  /// @return True if the object format is ELF.
  bool isOSBinFormatELF() const { return getObjectFormat() == Triple::ELF; }

  /// Tests whether the OS uses the COFF binary format.
  /// @return True if the object format is COFF.
  bool isOSBinFormatCOFF() const { return getObjectFormat() == Triple::COFF; }

  /// Tests whether the OS uses the GOFF binary format.
  /// @return True if the object format is GOFF.
  bool isOSBinFormatGOFF() const { return getObjectFormat() == Triple::GOFF; }

  /// Tests whether the environment is MachO.
  /// @return True if the object format is MachO.
  bool isOSBinFormatMachO() const { return getObjectFormat() == Triple::MachO; }

  /// Tests whether the OS uses the Wasm binary format.
  /// @return True if the object format is Wasm.
  bool isOSBinFormatWasm() const { return getObjectFormat() == Triple::Wasm; }

  /// Tests whether the OS uses the XCOFF binary format.
  /// @return True if the object format is XCOFF.
  bool isOSBinFormatXCOFF() const { return getObjectFormat() == Triple::XCOFF; }

  /// Tests whether the OS uses the DXContainer binary format.
  /// @return True if the object format is DXContainer.
  bool isOSBinFormatDXContainer() const {
    return getObjectFormat() == Triple::DXContainer;
  }

  /// Tests whether the target uses WALI Wasm
  /// @return True if the target uses WALI Wasm.
  bool isWALI() const {
    return getArch() == Triple::wasm32 && isOSLinux() &&
           getEnvironment() == Triple::MuslWALI;
  }

  /// Tests whether the target is the PS4 platform.
  /// @return True if the target is the PS4 platform.
  bool isPS4() const {
    return getArch() == Triple::x86_64 && getVendor() == Triple::SCEI &&
           getOS() == Triple::PS4;
  }

  /// Tests whether the target is the PS5 platform.
  /// @return True if the target is the PS5 platform.
  bool isPS5() const {
    return getArch() == Triple::x86_64 && getVendor() == Triple::SCEI &&
           getOS() == Triple::PS5;
  }

  /// Tests whether the target is the PS4 or PS5 platform.
  /// @return True if the target is PS4 or PS5.
  bool isPS() const { return isPS4() || isPS5(); }

  /// Tests whether the target is Android
  /// @return True if the target is Android.
  bool isAndroid() const { return getEnvironment() == Triple::Android; }

  /// True if this Android target's API level is less than \p Major.
  /// @param Major Android API level to compare against.
  /// @return True if the Android API level is less than \p Major.
  bool isAndroidVersionLT(unsigned Major) const {
    assert(isAndroid() && "Not an Android triple!");

    VersionTuple Version = getEnvironmentVersion();

    // 64-bit targets did not exist before API level 21 (Lollipop).
    if (isArch64Bit() && Version.getMajor() < 21)
      return VersionTuple(21) < VersionTuple(Major);

    return Version < VersionTuple(Major);
  }

  /// Tests whether the environment is musl-libc
  /// @return True if the environment is musl-libc.
  bool isMusl() const {
    return getEnvironment() == Triple::Musl ||
           getEnvironment() == Triple::MuslABIN32 ||
           getEnvironment() == Triple::MuslABI64 ||
           getEnvironment() == Triple::MuslEABI ||
           getEnvironment() == Triple::MuslEABIHF ||
           getEnvironment() == Triple::MuslF32 ||
           getEnvironment() == Triple::MuslSF ||
           getEnvironment() == Triple::MuslX32 ||
           getEnvironment() == Triple::MuslWALI ||
           getEnvironment() == Triple::OpenHOS || isOSLiteOS();
  }

  /// Tests whether the target is OHOS
  /// LiteOS default enviroment is also OHOS, but omited on triple.
  /// @return True if the target is in the OHOS family.
  bool isOHOSFamily() const { return isOpenHOS() || isOSLiteOS(); }

  /// True if the environment is OpenHarmony OS (OpenHOS).
  /// @return True if the environment is OpenHarmony OS.
  bool isOpenHOS() const { return getEnvironment() == Triple::OpenHOS; }

  /// Tests whether the OS is LiteOS.
  /// @return True if the OS is LiteOS.
  bool isOSLiteOS() const { return getOS() == Triple::LiteOS; }

  /// Tests whether the target is DXIL.
  /// @return True if the architecture is DXIL.
  bool isDXIL() const { return getArch() == Triple::dxil; }

  /// True if this is a DirectX ShaderModel OS triple.
  /// @return True if this is a DirectX ShaderModel OS triple.
  bool isShaderModelOS() const { return getOS() == Triple::ShaderModel; }

  /// True if this is a Vulkan OS triple.
  /// @return True if this is a Vulkan OS triple.
  bool isVulkanOS() const { return getOS() == Triple::Vulkan; }

  /// Tests whether the OS is Managarm.
  /// @return True if the OS is Managarm.
  bool isOSManagarm() const { return getOS() == Triple::Managarm; }

  /// True if this is a bare-metal firmware OS triple.
  /// @return True if this is a bare-metal firmware OS triple.
  bool isOSFirmware() const { return getOS() == Triple::Firmware; }

  /// True if the environment names a DirectX/Vulkan shader stage.
  /// @return True if the environment names a shader stage.
  bool isShaderStageEnvironment() const {
    EnvironmentType Env = getEnvironment();
    return Env == Triple::Pixel || Env == Triple::Vertex ||
           Env == Triple::Geometry || Env == Triple::Hull ||
           Env == Triple::Domain || Env == Triple::Compute ||
           Env == Triple::Library || Env == Triple::RayGeneration ||
           Env == Triple::Intersection || Env == Triple::AnyHit ||
           Env == Triple::ClosestHit || Env == Triple::Miss ||
           Env == Triple::Callable || Env == Triple::Mesh ||
           Env == Triple::Amplification || Env == Triple::RootSignature;
  }

  /// Tests whether the target is SPIR (32- or 64-bit).
  /// @return True if the architecture is SPIR.
  bool isSPIR() const {
    return getArch() == Triple::spir || getArch() == Triple::spir64;
  }

  /// Tests whether the target is SPIR-V (32/64-bit/Logical).
  /// @return True if the architecture is SPIR-V.
  bool isSPIRV() const {
    return getArch() == Triple::spirv32 || getArch() == Triple::spirv64 ||
           getArch() == Triple::spirv;
  }

  /// Tests whether the target is SPIR-V or SPIR.
  /// @return True if the architecture is SPIR or SPIR-V.
  bool isSPIROrSPIRV() const { return isSPIR() || isSPIRV(); }

  /// Tests whether the target is SPIR-V Logical
  /// @return True if the architecture is SPIR-V Logical.
  bool isSPIRVLogical() const { return getArch() == Triple::spirv; }

  /// Tests whether the target is NVPTX (32- or 64-bit).
  /// @return True if the architecture is NVPTX.
  bool isNVPTX() const {
    return getArch() == Triple::nvptx || getArch() == Triple::nvptx64;
  }

  /// Tests whether the target is AMDGCN
  /// True if the architecture is AMDGCN (amdgpu).
  /// @return True if the architecture is AMDGCN.
  bool isAMDGCN() const { return getArch() == Triple::amdgpu; }

  /// True if the architecture is AMDGPU (AMDGCN or R600).
  /// @return True if the architecture is AMDGPU.
  bool isAMDGPU() const { return isAMDGCN() || getArch() == Triple::r600; }

  /// Tests whether the target is Thumb (little and big endian).
  /// @return True if the architecture is Thumb.
  bool isThumb() const {
    return getArch() == Triple::thumb || getArch() == Triple::thumbeb;
  }

  /// Tests whether the target is ARM (little and big endian).
  /// @return True if the architecture is ARM.
  bool isARM() const {
    return getArch() == Triple::arm || getArch() == Triple::armeb;
  }

  /// Tests whether the target is LFI.
  /// @return True if the target is LFI.
  bool isLFI() const {
    return (getArch() == Triple::aarch64 &&
            getSubArch() == Triple::AArch64SubArch_lfi) ||
           (getArch() == Triple::x86_64 &&
            getSubArch() == Triple::X86_64SubArch_lfi);
  }

  /// Tests whether the target supports the EHABI exception
  /// handling standard.
  /// @return True if the target supports the EHABI exception-handling standard.
  bool isTargetEHABICompatible() const {
    return (isARM() || isThumb()) &&
           (getEnvironment() == Triple::EABI ||
            getEnvironment() == Triple::GNUEABI ||
            getEnvironment() == Triple::GNUEABIT64 ||
            getEnvironment() == Triple::MuslEABI ||
            getEnvironment() == Triple::EABIHF ||
            getEnvironment() == Triple::GNUEABIHF ||
            getEnvironment() == Triple::GNUEABIHFT64 ||
            getEnvironment() == Triple::OpenHOS ||
            getEnvironment() == Triple::MuslEABIHF || isOSFuchsia() ||
            isAndroid()) &&
           isOSBinFormatELF() && !isOSNetBSD();
  }

  /// True for bare-metal ARM EABI/EABIHF (not Darwin or Windows).
  ///
  /// ARM EABI is the bare-metal EABI described in ARM ABI documents and
  /// can be accessed via -target arm-none-eabi. This is NOT GNUEABI.
  /// FIXME: Add a flag for bare-metal for that target and set Triple::EABI
  /// even for GNUEABI, so we can make a distinction here and still conform to
  /// the EABI on GNU (and Android) mode. This requires change in Clang, too.
  /// FIXME: The Darwin exception is temporary, while we move users to
  /// "*-*-*-macho" triples as quickly as possible.
  /// @return True if this is bare-metal ARM EABI/EABIHF.
  bool isTargetAEABI() const {
    return (getEnvironment() == Triple::EABI ||
            getEnvironment() == Triple::EABIHF) &&
           !isOSDarwin() && !isOSWindows();
  }

  /// True if the environment is a GNU EABI variant (non-Darwin/Windows).
  /// @return True if the environment is a GNU EABI variant.
  bool isTargetGNUAEABI() const {
    return (getEnvironment() == Triple::GNUEABI ||
            getEnvironment() == Triple::GNUEABIT64 ||
            getEnvironment() == Triple::GNUEABIHF ||
            getEnvironment() == Triple::GNUEABIHFT64) &&
           !isOSDarwin() && !isOSWindows();
  }

  /// True if the environment is a musl EABI or OpenHOS variant (non-Darwin/Windows).
  /// @return True if the environment is a musl EABI or OpenHOS variant.
  bool isTargetMuslAEABI() const {
    return (getEnvironment() == Triple::MuslEABI ||
            getEnvironment() == Triple::MuslEABIHF ||
            getEnvironment() == Triple::OpenHOS) &&
           !isOSDarwin() && !isOSWindows();
  }

  /// Tests whether the target is T32.
  /// @return True if the target is ARM T32.
  bool isArmT32() const {
    switch (getSubArch()) {
    case Triple::ARMSubArch_v8m_baseline:
    case Triple::ARMSubArch_v7s:
    case Triple::ARMSubArch_v7k:
    case Triple::ARMSubArch_v7ve:
    case Triple::ARMSubArch_v6:
    case Triple::ARMSubArch_v6m:
    case Triple::ARMSubArch_v6k:
    case Triple::ARMSubArch_v6t2:
    case Triple::ARMSubArch_v5:
    case Triple::ARMSubArch_v5te:
    case Triple::ARMSubArch_v4t:
      return false;
    default:
      return true;
    }
  }

  /// Tests whether the target is an M-class.
  /// @return True if the target is an ARM M-class sub-architecture.
  bool isArmMClass() const {
    switch (getSubArch()) {
    case Triple::ARMSubArch_v6m:
    case Triple::ARMSubArch_v7m:
    case Triple::ARMSubArch_v7em:
    case Triple::ARMSubArch_v8m_mainline:
    case Triple::ARMSubArch_v8m_baseline:
    case Triple::ARMSubArch_v8_1m_mainline:
      return true;
    default:
      return false;
    }
  }

  /// Tests whether the target is AArch64 (little and big endian).
  /// @return True if the architecture is AArch64.
  bool isAArch64() const {
    return getArch() == Triple::aarch64 || getArch() == Triple::aarch64_be ||
           getArch() == Triple::aarch64_32;
  }

  /// Tests whether the target is AArch64 and pointers are the size specified by
  /// \p PointerWidth.
  /// @param PointerWidth Expected pointer width in bits (32 or 64).
  /// @return True if this is AArch64 with the given pointer width.
  bool isAArch64(int PointerWidth) const {
    assert(PointerWidth == 64 || PointerWidth == 32);
    if (!isAArch64())
      return false;
    return getArch() == Triple::aarch64_32 ||
                   getEnvironment() == Triple::GNUILP32
               ? PointerWidth == 32
               : PointerWidth == 64;
  }

  /// Whether the triple is for the AVR architecture.
  /// @return True if the architecture is AVR.
  bool isAVR() const { return getArch() == Triple::avr; }

  /// Tests whether the target is 32-bit LoongArch.
  /// @return True if the architecture is 32-bit LoongArch.
  bool isLoongArch32() const { return getArch() == Triple::loongarch32; }

  /// Tests whether the target is 64-bit LoongArch.
  /// @return True if the architecture is 64-bit LoongArch.
  bool isLoongArch64() const { return getArch() == Triple::loongarch64; }

  /// Tests whether the target is LoongArch (32- and 64-bit).
  /// @return True if the architecture is LoongArch.
  bool isLoongArch() const { return isLoongArch32() || isLoongArch64(); }

  /// Tests whether the target is MIPS 32-bit (little and big endian).
  /// @return True if the architecture is 32-bit MIPS.
  bool isMIPS32() const {
    return getArch() == Triple::mips || getArch() == Triple::mipsel;
  }

  /// Tests whether the target is MIPS 64-bit (little and big endian).
  /// @return True if the architecture is 64-bit MIPS.
  bool isMIPS64() const {
    return getArch() == Triple::mips64 || getArch() == Triple::mips64el;
  }

  /// Tests whether the target is MIPS (little and big endian, 32- or 64-bit).
  /// @return True if the architecture is MIPS.
  bool isMIPS() const { return isMIPS32() || isMIPS64(); }

  /// Tests whether the target is PowerPC (32- or 64-bit LE or BE).
  /// @return True if the architecture is PowerPC.
  bool isPPC() const {
    return getArch() == Triple::ppc || getArch() == Triple::ppc64 ||
           getArch() == Triple::ppcle || getArch() == Triple::ppc64le;
  }

  /// Tests whether the target is 32-bit PowerPC (little and big endian).
  /// @return True if the architecture is 32-bit PowerPC.
  bool isPPC32() const {
    return getArch() == Triple::ppc || getArch() == Triple::ppcle;
  }

  /// Tests whether the target is 64-bit PowerPC (little and big endian).
  /// @return True if the architecture is 64-bit PowerPC.
  bool isPPC64() const {
    return getArch() == Triple::ppc64 || getArch() == Triple::ppc64le;
  }

  /// Tests whether the target 64-bit PowerPC big endian ABI is ELFv2.
  /// @return True if the 64-bit PowerPC big-endian ABI is ELFv2.
  bool isPPC64ELFv2ABI() const {
    return (getArch() == Triple::ppc64 &&
            ((getOS() == Triple::FreeBSD &&
              (getOSMajorVersion() >= 13 || getOSVersion().empty())) ||
             getOS() == Triple::OpenBSD || isMusl()));
  }

  /// Tests whether the target 32-bit PowerPC uses Secure PLT.
  /// @return True if 32-bit PowerPC uses Secure PLT.
  bool isPPC32SecurePlt() const {
    return ((getArch() == Triple::ppc || getArch() == Triple::ppcle) &&
            ((getOS() == Triple::FreeBSD &&
              (getOSMajorVersion() >= 13 || getOSVersion().empty())) ||
             getOS() == Triple::NetBSD || getOS() == Triple::OpenBSD ||
             isMusl()));
  }

  /// Tests whether the target is 32-bit RISC-V.
  /// @return True if the architecture is 32-bit RISC-V.
  bool isRISCV32() const {
    return getArch() == Triple::riscv32 || getArch() == Triple::riscv32be;
  }

  /// Tests whether the target is 64-bit RISC-V.
  /// @return True if the architecture is 64-bit RISC-V.
  bool isRISCV64() const {
    return getArch() == Triple::riscv64 || getArch() == Triple::riscv64be;
  }

  /// Tests whether the target is RISC-V (32- and 64-bit).
  /// @return True if the architecture is RISC-V.
  bool isRISCV() const { return isRISCV32() || isRISCV64(); }

  /// Tests whether the target is 32-bit SPARC (little and big endian).
  /// @return True if the architecture is 32-bit SPARC.
  bool isSPARC32() const {
    return getArch() == Triple::sparc || getArch() == Triple::sparcel;
  }

  /// Tests whether the target is 64-bit SPARC (big endian).
  /// @return True if the architecture is 64-bit SPARC.
  bool isSPARC64() const { return getArch() == Triple::sparcv9; }

  /// Tests whether the target is SPARC.
  /// @return True if the architecture is SPARC.
  bool isSPARC() const { return isSPARC32() || isSPARC64(); }

  /// Tests whether the target is SystemZ.
  /// @return True if the architecture is SystemZ.
  bool isSystemZ() const { return getArch() == Triple::systemz; }

  /// Tests whether the target is x86 (32- or 64-bit).
  /// @return True if the architecture is x86.
  bool isX86() const {
    return getArch() == Triple::x86 || getArch() == Triple::x86_64;
  }

  /// Tests whether the target is x86 (32-bit).
  /// @return True if the architecture is 32-bit x86.
  bool isX86_32() const { return getArch() == Triple::x86; }

  /// Tests whether the target is x86 (64-bit).
  /// @return True if the architecture is 64-bit x86.
  bool isX86_64() const { return getArch() == Triple::x86_64; }

  /// Tests whether the target is VE
  /// @return True if the architecture is VE.
  bool isVE() const { return getArch() == Triple::ve; }

  /// Tests whether the target is wasm (32- and 64-bit).
  /// @return True if the architecture is Wasm.
  bool isWasm() const {
    return getArch() == Triple::wasm32 || getArch() == Triple::wasm64;
  }

  /// Tests whether the target is CSKY.
  /// @return True if the architecture is CSKY.
  bool isCSKY() const { return getArch() == Triple::csky; }

  /// Tests whether the target is the Apple "arm64e" AArch64 subarch.
  /// @return True if the target is Apple arm64e.
  bool isArm64e() const {
    return getArch() == Triple::aarch64 &&
           getSubArch() == Triple::AArch64SubArch_arm64e;
  }

  /// True if the environment is an N32 ABI (GNU or musl).
  /// @return True if the environment is an N32 ABI.
  bool isABIN32() const {
    EnvironmentType Env = getEnvironment();
    return Env == Triple::GNUABIN32 || Env == Triple::MuslABIN32;
  }

  /// Tests whether the target is X32.
  /// @return True if the target is X32.
  bool isX32() const {
    EnvironmentType Env = getEnvironment();
    return Env == Triple::GNUX32 || Env == Triple::MuslX32;
  }

  /// Tests whether the target is eBPF.
  /// @return True if the architecture is eBPF.
  bool isBPF() const {
    return getArch() == Triple::bpfel || getArch() == Triple::bpfeb;
  }

  /// Tests whether MSVC linker or UEFI targets.
  /// Used to default to -mincremental-linker-compatible if we are
  /// targeting the MSVC linker or *-uefi triples.
  /// @return True if the default is incremental-linker-compatible.
  bool isDefaultIncrementalLinkerCompatibleByDefault() const {
    return isWindowsMSVCEnvironment() || isUEFI();
  }

  /// Tests if the target forces 64-bit time_t on a 32-bit architecture.
  /// @return True if the target forces 64-bit time_t on 32-bit.
  bool isTime64ABI() const {
    EnvironmentType Env = getEnvironment();
    return Env == Triple::GNUT64 || Env == Triple::GNUEABIT64 ||
           Env == Triple::GNUEABIHFT64;
  }

  /// Returns the default floating-point ABI for this target triple, i.e. the
  /// ABI the code generator will resolve FloatABI::Default to
  /// @return Default floating-point ABI for this triple.
  LLVM_ABI FloatABI::ABIType getDefaultFloatABI() const;

  /// Tests if the target's default floating-point ABI is hard float.
  /// @return True if the default floating-point ABI is hard float.
  bool isHardFloatABI() const { return getDefaultFloatABI() == FloatABI::Hard; }

  /// Returns the default floating-point format for the "long double" type. A
  /// particular module may override this default.
  /// @return Default long double floating-point format.
  LLVM_ABI LongDoubleFormat getDefaultLongDoubleFormat() const;

  /// Tests whether the target supports comdat
  /// @return True if the target supports COMDAT.
  bool supportsCOMDAT() const {
    return !(isOSBinFormatMachO() || isOSBinFormatXCOFF() ||
             isOSBinFormatDXContainer());
  }

  /// Tests whether the target uses emulated TLS as default.
  ///
  /// Note: Android API level 29 (10) introduced ELF TLS.
  /// @return True if the target uses emulated TLS by default.
  bool hasDefaultEmulatedTLS() const {
    return (isAndroid() && isAndroidVersionLT(29)) || isOSOpenBSD() ||
           isWindowsCygwinEnvironment() || isOHOSFamily();
  }

  /// True if the target uses TLSDESC by default.
  /// @return True if the target uses TLSDESC by default.
  bool hasDefaultTLSDESC() const {
    return isAArch64() || (isAndroid() && isRISCV64()) || isOSFuchsia();
  }

  /// Tests whether the target uses -data-sections as default.
  /// @return True if the target uses -data-sections by default.
  bool hasDefaultDataSections() const {
    return isOSBinFormatXCOFF() || isWasm();
  }

  /// Returns the default wchar_t size (in bytes) for this target triple.
  /// @return Default wchar_t size in bytes.
  LLVM_ABI unsigned getDefaultWCharSize() const;

  /// Tests if the environment supports dllimport/export annotations.
  /// @return True if the environment supports dllimport/export annotations.
  bool hasDLLImportExport() const { return isOSWindows() || isPS(); }

  /// @}
  /// @name Mutators
  /// @{

  /// Set the architecture (first) component of the triple to a known type.
  /// @param Kind Architecture type to set.
  /// @param SubArch Optional sub-architecture type to set.
  LLVM_ABI void setArch(ArchType Kind, SubArchType SubArch = NoSubArch);

  /// Set the vendor (second) component of the triple to a known type.
  /// @param Kind Vendor type to set.
  LLVM_ABI void setVendor(VendorType Kind);

  /// Set the operating system (third) component of the triple to a known type.
  /// @param Kind Operating-system type to set.
  LLVM_ABI void setOS(OSType Kind);

  /// Set the environment (fourth) component of the triple to a known type.
  /// @param Kind Environment type to set.
  LLVM_ABI void setEnvironment(EnvironmentType Kind);

  /// Set the object file format.
  /// @param Kind Object format type to set.
  LLVM_ABI void setObjectFormat(ObjectFormatType Kind);

  /// Set all components to the new triple \p Str.
  /// @param Str New full triple string.
  LLVM_ABI void setTriple(const Twine &Str);

  /// Set the architecture (first) component of the triple by name.
  /// @param Str Architecture name string.
  LLVM_ABI void setArchName(StringRef Str);

  /// Set the vendor (second) component of the triple by name.
  /// @param Str Vendor name string.
  LLVM_ABI void setVendorName(StringRef Str);

  /// Set the operating system (third) component of the triple by name.
  /// @param Str Operating-system name string.
  LLVM_ABI void setOSName(StringRef Str);

  /// Set the optional environment (fourth) component of the triple by name.
  /// @param Str Environment name string.
  LLVM_ABI void setEnvironmentName(StringRef Str);

  /// Set the operating system and optional environment components with a single
  /// string.
  /// @param Str Combined OS and optional environment string.
  LLVM_ABI void setOSAndEnvironmentName(StringRef Str);

  /// @}
  /// @name Helpers to build variants of a particular triple.
  /// @{

  /// Form a triple with a 32-bit variant of the current architecture.
  ///
  /// This can be used to move across "families" of architectures where useful.
  ///
  /// \returns A new triple with a 32-bit architecture or an unknown
  ///          architecture if no such variant can be found.
  LLVM_ABI llvm::Triple get32BitArchVariant() const;

  /// Form a triple with a 64-bit variant of the current architecture.
  ///
  /// This can be used to move across "families" of architectures where useful.
  ///
  /// \returns A new triple with a 64-bit architecture or an unknown
  ///          architecture if no such variant can be found.
  LLVM_ABI llvm::Triple get64BitArchVariant() const;

  /// Form a triple with a big endian variant of the current architecture.
  ///
  /// This can be used to move across "families" of architectures where useful.
  ///
  /// \returns A new triple with a big endian architecture or an unknown
  ///          architecture if no such variant can be found.
  LLVM_ABI llvm::Triple getBigEndianArchVariant() const;

  /// Form a triple with a little endian variant of the current architecture.
  ///
  /// This can be used to move across "families" of architectures where useful.
  ///
  /// \returns A new triple with a little endian architecture or an unknown
  ///          architecture if no such variant can be found.
  LLVM_ABI llvm::Triple getLittleEndianArchVariant() const;

  /// Tests whether the target triple is little endian.
  ///
  /// \returns true if the triple is little endian, false otherwise.
  LLVM_ABI bool isLittleEndian() const;

  /// Test whether target triples are compatible.
  /// @param Other Triple to test compatibility against.
  /// @return True if this triple is compatible with \p Other.
  LLVM_ABI bool isCompatibleWith(const Triple &Other) const;

  /// Test whether the target triple is for a GPU.
  /// @return True if the target triple is for a GPU.
  bool isGPU() const { return isSPIROrSPIRV() || isNVPTX() || isAMDGPU(); }

  /// Merge target triples.
  /// @param Other Triple whose components may be merged into this one.
  /// @return Merged triple string combining components from this and \p Other.
  LLVM_ABI std::string merge(const Triple &Other) const;

  /// Return the minimum supported OS version for this triple, if any.
  ///
  /// Some platforms have different minimum supported OS versions that varies by
  /// the architecture specified in the triple. Returns an invalid version tuple
  /// if this triple does not have one.
  /// @return Minimum supported OS version, or an invalid tuple if none.
  LLVM_ABI VersionTuple getMinimumSupportedOSVersion() const;

  /// @}
  /// @name Static helpers for IDs.
  /// @{

  /// Get the canonical name for the \p Kind architecture.
  /// @param Kind Architecture type to name.
  /// @return Canonical name for the architecture type.
  LLVM_ABI static StringRef getArchTypeName(ArchType Kind);

  /// Get the architecture name based on \p Kind and \p SubArch.
  /// @param Kind Architecture type to name.
  /// @param SubArch Optional sub-architecture that may refine the name.
  /// @return Architecture name for \p Kind and optional \p SubArch.
  LLVM_ABI static StringRef getArchName(ArchType Kind,
                                        SubArchType SubArch = NoSubArch);

  /// Get the "prefix" canonical name for the \p Kind architecture. This is the
  /// prefix used by the architecture specific builtins, and is suitable for
  /// passing to \see Intrinsic::getIntrinsicForClangBuiltin().
  ///
  /// \return - The architecture prefix, or 0 if none is defined.
  /// @param Kind Architecture type whose prefix is requested.
  LLVM_ABI static StringRef getArchTypePrefix(ArchType Kind);

  /// Get the canonical name for the \p Kind vendor.
  /// @param Kind Vendor type to name.
  /// @return Canonical name for the vendor type.
  LLVM_ABI static StringRef getVendorTypeName(VendorType Kind);

  /// Get the canonical name for the \p Kind operating system.
  /// @param Kind Operating-system type to name.
  /// @return Canonical name for the operating-system type.
  LLVM_ABI static StringRef getOSTypeName(OSType Kind);

  /// Get the canonical name for the \p Kind environment.
  /// @param Kind Environment type to name.
  /// @return Canonical name for the environment type.
  LLVM_ABI static StringRef getEnvironmentTypeName(EnvironmentType Kind);

  /// Get the name for the \p Object format.
  /// @param ObjectFormat Object format type to name.
  /// @return Name for the object format type.
  LLVM_ABI static StringRef
  getObjectFormatTypeName(ObjectFormatType ObjectFormat);

  /// @}
  /// @name Static helpers for converting alternate architecture names.
  /// @{

  /// The canonical type for the given LLVM architecture name (e.g., "x86").
  /// @param Str LLVM architecture name to look up.
  /// @return Architecture type for the LLVM architecture name, or UnknownArch.
  LLVM_ABI static ArchType getArchTypeForLLVMName(StringRef Str);

  /// Parse anything recognized as an architecture for the first field of the
  /// triple.
  /// @param Str Architecture field string to parse.
  /// @return Parsed architecture type, or UnknownArch if unrecognized.
  LLVM_ABI static ArchType parseArch(StringRef Str);

  /// Parse a sub-architecture from the architecture field of a triple.
  ///
  /// Parses the subarchitecture encoded in the first (architecture) field of
  /// the triple (e.g. "amdgpu9.00" -> AMDGPUSubArch900). Returns NoSubArch if
  /// the string does not encode a recognized subarchitecture.
  /// @param Str Architecture field string that may encode a sub-architecture.
  /// @return Parsed sub-architecture type, or NoSubArch if unrecognized.
  LLVM_ABI static SubArchType parseSubArch(StringRef Str);

  /// @}

  /// Returns a canonicalized OS version number for the specified OS.
  /// @param OSKind Operating system whose version conventions apply.
  /// @param Version Version tuple to canonicalize.
  /// @param IsInValidRange Whether \p Version is already in a valid range.
  /// @return Canonicalized OS version for \p OSKind.
  LLVM_ABI static VersionTuple
  getCanonicalVersionForOS(OSType OSKind, const VersionTuple &Version,
                           bool IsInValidRange);

  /// Returns whether an OS version is invalid and would not map to an Apple OS.
  /// @param OSKind Operating system whose version conventions apply.
  /// @param Version Version tuple to validate.
  /// @return True if \p Version is valid for \p OSKind on Apple platforms.
  LLVM_ABI static bool isValidVersionForOS(OSType OSKind,
                                           const VersionTuple &Version);

  /// Returns the default exception-handling model for this triple.
  /// @return Default exception-handling model for this triple.
  LLVM_ABI ExceptionHandling getDefaultExceptionHandling() const;

  /// Compute the LLVM IR data layout string based on the triple. Some targets
  /// customize the layout based on the ABIName string.
  /// @param ABIName Optional ABI name that may customize the data layout.
  /// @return LLVM IR data-layout string for this triple.
  LLVM_ABI std::string computeDataLayout(StringRef ABIName = "") const;
};

} // namespace llvm

#endif
