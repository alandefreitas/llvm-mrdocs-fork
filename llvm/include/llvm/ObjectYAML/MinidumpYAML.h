//===- MinidumpYAML.h - Minidump YAMLIO implementation ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_MINIDUMPYAML_H
#define LLVM_OBJECTYAML_MINIDUMPYAML_H

#include "llvm/BinaryFormat/Minidump.h"
#include "llvm/Object/Minidump.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/YAMLTraits.h"

namespace llvm {
/// YAML representations of minidump object files.
namespace MinidumpYAML {

/// The base class for all minidump streams.
///
/// The "Type" of the stream corresponds to the Stream Type field in the
/// minidump file. The "Kind" field specifies how are we going to treat it. For
/// highly specialized streams (e.g. SystemInfo), there is a 1:1 mapping between
/// Types and Kinds, but in general one stream Kind can be used to represent
/// multiple stream Types (e.g. any unrecognised stream Type will be handled via
/// RawContentStream). The mapping from Types to Kinds is fixed and given by the
/// static getKind function.
struct LLVM_ABI Stream {
  /// How a minidump stream is represented in YAML.
  enum class StreamKind {
    /// Exception stream.
    Exception,
    /// Memory info list stream.
    MemoryInfoList,
    /// Memory list stream.
    MemoryList,
    /// 64-bit memory list stream.
    Memory64List,
    /// Module list stream.
    ModuleList,
    /// Raw binary content stream used as a fallback.
    RawContent,
    /// System info stream.
    SystemInfo,
    /// Textual content stream.
    TextContent,
    /// Thread list stream.
    ThreadList,
  };

  /// Construct a stream with the given Kind and Type.
  /// \param Kind YAML representation kind for this stream.
  /// \param Type Minidump stream type code.
  Stream(StreamKind Kind, minidump::StreamType Type) : Kind(Kind), Type(Type) {}
  /// Destroy a stream.
  virtual ~Stream(); // anchor

  /// YAML representation kind for this stream.
  const StreamKind Kind;
  /// Minidump stream type code from the stream directory.
  const minidump::StreamType Type;

  /// Get the stream Kind used for representing streams of a given Type.
  /// \param Type Minidump stream type to classify.
  /// \return The StreamKind used to represent \p Type.
  static StreamKind getKind(minidump::StreamType Type);

  /// Create an empty stream of the given Type.
  /// \param Type Minidump stream type to create.
  /// \return A newly allocated empty stream for \p Type.
  static std::unique_ptr<Stream> create(minidump::StreamType Type);

  /// Create a stream from the given stream directory entry.
  /// \param StreamDesc Stream directory entry describing the stream.
  /// \param File Minidump file providing stream contents.
  /// \return The parsed stream, or an error.
  static Expected<std::unique_ptr<Stream>>
  create(const minidump::Directory &StreamDesc,
         const object::MinidumpFile &File);
};

namespace detail {
/// A stream representing a list of abstract entries in a minidump stream. Its
/// instantiations can be used to represent the ModuleList stream and other
/// streams with a similar structure.
template <typename EntryT> struct ListStream : public Stream {
  /// Parsed entry type stored in this list stream.
  using entry_type = EntryT;

  /// Entries that make up this list stream.
  std::vector<entry_type> Entries;

  /// Construct a list stream from the given entries.
  /// \param Entries Parsed entries to store in the stream.
  explicit ListStream(std::vector<entry_type> Entries = {})
      : Stream(EntryT::Kind, EntryT::Type), Entries(std::move(Entries)) {}

  /// Return true if \p S is a list stream of this entry type.
  /// \param S Stream to test.
  /// \return True when \p S has this list stream's Kind.
  static bool classof(const Stream *S) { return S->Kind == EntryT::Kind; }
};

/// A structure containing all data belonging to a single minidump module.
struct ParsedModule {
  static constexpr Stream::StreamKind Kind = Stream::StreamKind::ModuleList;
  static constexpr minidump::StreamType Type = minidump::StreamType::ModuleList;

  minidump::Module Entry;
  std::string Name;
  yaml::BinaryRef CvRecord;
  yaml::BinaryRef MiscRecord;
};

/// A structure containing all data belonging to a single minidump thread.
struct ParsedThread {
  static constexpr Stream::StreamKind Kind = Stream::StreamKind::ThreadList;
  static constexpr minidump::StreamType Type = minidump::StreamType::ThreadList;

  minidump::Thread Entry;
  yaml::BinaryRef Stack;
  yaml::BinaryRef Context;
};

/// A structure containing all data describing a single memory region.
struct ParsedMemoryDescriptor {
  static constexpr Stream::StreamKind Kind = Stream::StreamKind::MemoryList;
  static constexpr minidump::StreamType Type = minidump::StreamType::MemoryList;

  minidump::MemoryDescriptor Entry;
  yaml::BinaryRef Content;
};

struct ParsedMemory64Descriptor {
  static constexpr Stream::StreamKind Kind = Stream::StreamKind::Memory64List;
  static constexpr minidump::StreamType Type =
      minidump::StreamType::Memory64List;

  minidump::MemoryDescriptor_64 Entry;
  yaml::BinaryRef Content;
};
} // namespace detail

/// Module list minidump stream.
using ModuleListStream = detail::ListStream<detail::ParsedModule>;
/// Thread list minidump stream.
using ThreadListStream = detail::ListStream<detail::ParsedThread>;
/// Memory list minidump stream.
using MemoryListStream = detail::ListStream<detail::ParsedMemoryDescriptor>;

/// 64-bit memory list minidump stream.
struct Memory64ListStream
    : public detail::ListStream<detail::ParsedMemory64Descriptor> {
  /// Header for the 64-bit memory list stream.
  minidump::Memory64ListHeader Header;

  /// Construct a 64-bit memory list stream from the given entries.
  /// \param Entries Parsed 64-bit memory descriptors to store.
  explicit Memory64ListStream(
      std::vector<detail::ParsedMemory64Descriptor> Entries = {})
      : ListStream(Entries) {}
};

/// ExceptionStream minidump stream.
struct ExceptionStream : public Stream {
  /// Native minidump exception stream record.
  minidump::ExceptionStream MDExceptionStream;
  /// Thread context bytes associated with the exception.
  yaml::BinaryRef ThreadContext;

  /// Construct an empty exception stream.
  ExceptionStream()
      : Stream(StreamKind::Exception, minidump::StreamType::Exception),
        MDExceptionStream({}) {}

  /// Construct an exception stream from native minidump data.
  /// \param MDExceptionStream Native exception stream record.
  /// \param ThreadContext Thread context bytes for the exception.
  explicit ExceptionStream(const minidump::ExceptionStream &MDExceptionStream,
                           ArrayRef<uint8_t> ThreadContext)
      : Stream(StreamKind::Exception, minidump::StreamType::Exception),
        MDExceptionStream(MDExceptionStream), ThreadContext(ThreadContext) {}

  /// Return true if \p S is an exception stream.
  /// \param S Stream to test.
  /// \return True when \p S has Kind Exception.
  static bool classof(const Stream *S) {
    return S->Kind == StreamKind::Exception;
  }
};

/// A structure containing the list of MemoryInfo entries comprising a
/// MemoryInfoList stream.
struct MemoryInfoListStream : public Stream {
  /// Memory info records in this stream.
  std::vector<minidump::MemoryInfo> Infos;

  /// Construct an empty memory info list stream.
  MemoryInfoListStream()
      : Stream(StreamKind::MemoryInfoList,
               minidump::StreamType::MemoryInfoList) {}

  /// Construct a memory info list stream from a minidump iterator range.
  /// \param Range Range of memory info records to copy.
  explicit MemoryInfoListStream(
      iterator_range<object::MinidumpFile::MemoryInfoIterator> Range)
      : Stream(StreamKind::MemoryInfoList,
               minidump::StreamType::MemoryInfoList),
        Infos(Range.begin(), Range.end()) {}

  /// Return true if \p S is a memory info list stream.
  /// \param S Stream to test.
  /// \return True when \p S has Kind MemoryInfoList.
  static bool classof(const Stream *S) {
    return S->Kind == StreamKind::MemoryInfoList;
  }
};

/// A minidump stream represented as a sequence of hex bytes. This is used as a
/// fallback when no other stream kind is suitable.
struct RawContentStream : public Stream {
  /// Raw stream payload bytes.
  yaml::BinaryRef Content;
  /// Declared size of the stream contents.
  yaml::Hex32 Size;

  /// Construct a raw content stream of the given type.
  /// \param Type Minidump stream type code.
  /// \param Content Optional raw payload bytes.
  RawContentStream(minidump::StreamType Type, ArrayRef<uint8_t> Content = {})
      : Stream(StreamKind::RawContent, Type), Content(Content),
        Size(Content.size()) {}

  /// Return true if \p S is a raw content stream.
  /// \param S Stream to test.
  /// \return True when \p S has Kind RawContent.
  static bool classof(const Stream *S) {
    return S->Kind == StreamKind::RawContent;
  }
};

/// SystemInfo minidump stream.
struct SystemInfoStream : public Stream {
  /// Native system info record.
  minidump::SystemInfo Info;
  /// CSD version string from the system info stream.
  std::string CSDVersion;

  /// Construct an empty system info stream.
  SystemInfoStream()
      : Stream(StreamKind::SystemInfo, minidump::StreamType::SystemInfo) {
    memset(&Info, 0, sizeof(Info));
  }

  /// Construct a system info stream from native minidump data.
  /// \param Info Native system info record.
  /// \param CSDVersion CSD version string.
  explicit SystemInfoStream(const minidump::SystemInfo &Info,
                            std::string CSDVersion)
      : Stream(StreamKind::SystemInfo, minidump::StreamType::SystemInfo),
        Info(Info), CSDVersion(std::move(CSDVersion)) {}

  /// Return true if \p S is a system info stream.
  /// \param S Stream to test.
  /// \return True when \p S has Kind SystemInfo.
  static bool classof(const Stream *S) {
    return S->Kind == StreamKind::SystemInfo;
  }
};

/// A StringRef printed using YAML block notation.
struct BlockStringRef {
  /// Construct a zero-initialized block string reference.
  BlockStringRef() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  BlockStringRef(const StringRef v) : value(v) {}
  /// Copy-construct from another \c BlockStringRef.
  /// \param v Value to copy.
  BlockStringRef(const BlockStringRef &v) = default;
  /// Copy-assign from another \c BlockStringRef.
  /// \param rhs Value to assign.
  /// \return Reference to this object.
  BlockStringRef &operator=(const BlockStringRef &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return Reference to this object.
  BlockStringRef &operator=(const StringRef &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return Const reference to the stored string.
  operator const StringRef &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True when the stored values are equal.
  bool operator==(const BlockStringRef &rhs) const {
    return value == rhs.value;
  }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True when the stored value equals \p rhs.
  bool operator==(const StringRef &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True when this value is ordered before \p rhs.
  bool operator<(const BlockStringRef &rhs) const { return value < rhs.value; }
  /// Stored string value.
  StringRef value;
  /// Underlying string reference base type.
  using BaseType = StringRef;
};

/// A minidump stream containing textual data (typically, the contents of a
/// /proc/<pid> file on linux).
struct TextContentStream : public Stream {
  /// Textual contents of the stream.
  BlockStringRef Text;

  /// Construct a text content stream of the given type.
  /// \param Type Minidump stream type code.
  /// \param Text Optional textual payload.
  TextContentStream(minidump::StreamType Type, StringRef Text = {})
      : Stream(StreamKind::TextContent, Type), Text(Text) {}

  /// Return true if \p S is a text content stream.
  /// \param S Stream to test.
  /// \return True when \p S has Kind TextContent.
  static bool classof(const Stream *S) {
    return S->Kind == StreamKind::TextContent;
  }
};

/// The top level structure representing a minidump object.
///
/// Consists of a minidump header, and zero or more streams. To construct an
/// Object from a minidump file, use the static create function. To serialize
/// to/from yaml, use the appropriate streaming operator on a yaml stream.
struct Object {
  /// Default-construct an empty minidump object.
  Object() = default;
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  Object(const Object &Other) = delete;
  /// Deleted copy assignment.
  /// \param Other Unused; copy assignment is deleted.
  Object &operator=(const Object &Other) = delete;
  /// Move-construct from another minidump object.
  /// \param Other Object to move from.
  Object(Object &&Other) = default;
  /// Move-assign from another minidump object.
  /// \param Other Object to move from.
  /// \return Reference to this object.
  Object &operator=(Object &&Other) = default;

  /// Construct a minidump object from a header and streams.
  /// \param Header Minidump file header.
  /// \param Streams Owned list of streams in the object.
  Object(const minidump::Header &Header,
         std::vector<std::unique_ptr<Stream>> Streams)
      : Header(Header), Streams(std::move(Streams)) {}

  /// The minidump header.
  minidump::Header Header;

  /// The list of streams in this minidump object.
  std::vector<std::unique_ptr<Stream>> Streams;

  /// Create a minidump YAML object from a parsed minidump file.
  /// \param File Parsed minidump file to convert.
  /// \return The YAML object, or an error.
  LLVM_ABI static Expected<Object> create(const object::MinidumpFile &File);
};

} // namespace MinidumpYAML

namespace yaml {
/// YAMLIO block scalar traits for \c MinidumpYAML::BlockStringRef.
template <> struct BlockScalarTraits<MinidumpYAML::BlockStringRef> {
  /// Write \p Text as a YAML block scalar to \p OS.
  /// \param Text Block string value to write.
  /// \param Ctx Optional YAML context pointer.
  /// \param OS Output stream.
  static void output(const MinidumpYAML::BlockStringRef &Text, void *Ctx,
                     raw_ostream &OS) {
    (void)Ctx;
    OS << Text;
  }

  /// Parse YAML block scalar \p Scalar into \p Text.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Optional YAML context pointer.
  /// \param Text Destination block string value.
  /// \return Empty string on success, or an error message.
  static StringRef input(StringRef Scalar, void *Ctx,
                         MinidumpYAML::BlockStringRef &Text) {
    (void)Ctx;
    Text = Scalar;
    return "";
  }
};

/// YAMLIO mapping traits for owned minidump streams.
template <> struct MappingTraits<std::unique_ptr<MinidumpYAML::Stream>> {
  /// Map an owned minidump stream to and from YAML.
  /// \param IO YAML input/output state.
  /// \param S Stream being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               std::unique_ptr<MinidumpYAML::Stream> &S);
  /// Validate that a mapped minidump stream is consistent.
  /// \param IO YAML input/output state.
  /// \param S Stream to validate.
  /// \return An error message, or an empty string on success.
  LLVM_ABI static std::string
  validate(IO &IO, std::unique_ptr<MinidumpYAML::Stream> &S);
};

/// YAMLIO context mapping traits for 32-bit memory descriptors.
template <> struct MappingContextTraits<minidump::MemoryDescriptor, BinaryRef> {
  /// Map a memory descriptor together with its content bytes.
  /// \param IO YAML input/output state.
  /// \param Memory Memory descriptor being mapped.
  /// \param Content Content bytes associated with \p Memory.
  LLVM_ABI static void mapping(IO &IO, minidump::MemoryDescriptor &Memory,
                               BinaryRef &Content);
};

/// YAMLIO context mapping traits for 64-bit memory descriptors.
template <>
struct MappingContextTraits<minidump::MemoryDescriptor_64, BinaryRef> {
  /// Map a 64-bit memory descriptor together with its content bytes.
  /// \param IO YAML input/output state.
  /// \param Memory 64-bit memory descriptor being mapped.
  /// \param Content Content bytes associated with \p Memory.
  LLVM_ABI static void mapping(IO &IO, minidump::MemoryDescriptor_64 &Memory,
                               BinaryRef &Content);
};

/// YAMLIO scalar bitset traits for \c minidump::MemoryProtection.
template <> struct ScalarBitSetTraits<minidump::MemoryProtection> {
  /// Map memory protection flags to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Options Memory protection flags being mapped.
  LLVM_ABI static void bitset(IO &IO, minidump::MemoryProtection &Options);
};

/// YAMLIO scalar bitset traits for \c minidump::MemoryState.
template <> struct ScalarBitSetTraits<minidump::MemoryState> {
  /// Map memory state flags to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Options Memory state flags being mapped.
  LLVM_ABI static void bitset(IO &IO, minidump::MemoryState &Options);
};

/// YAMLIO scalar bitset traits for \c minidump::MemoryType.
template <> struct ScalarBitSetTraits<minidump::MemoryType> {
  /// Map memory type flags to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Options Memory type flags being mapped.
  LLVM_ABI static void bitset(IO &IO, minidump::MemoryType &Options);
};

/// YAMLIO scalar enumeration traits for \c minidump::ProcessorArchitecture.
template <> struct ScalarEnumerationTraits<minidump::ProcessorArchitecture> {
  /// Map processor architecture enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Processor architecture being mapped.
  LLVM_ABI static void enumeration(IO &IO,
                                   minidump::ProcessorArchitecture &Value);
};

/// YAMLIO scalar enumeration traits for \c minidump::OSPlatform.
template <> struct ScalarEnumerationTraits<minidump::OSPlatform> {
  /// Map OS platform enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value OS platform being mapped.
  LLVM_ABI static void enumeration(IO &IO, minidump::OSPlatform &Value);
};

/// YAMLIO scalar enumeration traits for \c minidump::StreamType.
template <> struct ScalarEnumerationTraits<minidump::StreamType> {
  /// Map minidump stream type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Stream type being mapped.
  LLVM_ABI static void enumeration(IO &IO, minidump::StreamType &Value);
};

/// YAMLIO mapping traits for \c minidump::CPUInfo::ArmInfo.
template <> struct MappingTraits<minidump::CPUInfo::ArmInfo> {
  /// Map ARM CPU info fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj ARM CPU info being mapped.
  LLVM_ABI static void mapping(IO &IO, minidump::CPUInfo::ArmInfo &Obj);
};

/// YAMLIO mapping traits for \c minidump::CPUInfo::OtherInfo.
template <> struct MappingTraits<minidump::CPUInfo::OtherInfo> {
  /// Map other-architecture CPU info fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj Other CPU info being mapped.
  LLVM_ABI static void mapping(IO &IO, minidump::CPUInfo::OtherInfo &Obj);
};

/// YAMLIO mapping traits for \c minidump::CPUInfo::X86Info.
template <> struct MappingTraits<minidump::CPUInfo::X86Info> {
  /// Map x86 CPU info fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj X86 CPU info being mapped.
  LLVM_ABI static void mapping(IO &IO, minidump::CPUInfo::X86Info &Obj);
};

/// YAMLIO mapping traits for \c minidump::Exception.
template <> struct MappingTraits<minidump::Exception> {
  /// Map exception record fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj Exception record being mapped.
  LLVM_ABI static void mapping(IO &IO, minidump::Exception &Obj);
};

/// YAMLIO mapping traits for \c minidump::MemoryInfo.
template <> struct MappingTraits<minidump::MemoryInfo> {
  /// Map memory info fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj Memory info being mapped.
  LLVM_ABI static void mapping(IO &IO, minidump::MemoryInfo &Obj);
};

/// YAMLIO mapping traits for \c minidump::VSFixedFileInfo.
template <> struct MappingTraits<minidump::VSFixedFileInfo> {
  /// Map fixed file info fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj Fixed file info being mapped.
  LLVM_ABI static void mapping(IO &IO, minidump::VSFixedFileInfo &Obj);
};

/// YAMLIO mapping traits for memory list stream entries.
template <> struct MappingTraits<MinidumpYAML::MemoryListStream::entry_type> {
  /// Map a memory list entry to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj Memory list entry being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               MinidumpYAML::MemoryListStream::entry_type &Obj);
};

/// YAMLIO mapping traits for module list stream entries.
template <> struct MappingTraits<MinidumpYAML::ModuleListStream::entry_type> {
  /// Map a module list entry to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj Module list entry being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               MinidumpYAML::ModuleListStream::entry_type &Obj);
};

/// YAMLIO mapping traits for thread list stream entries.
template <> struct MappingTraits<MinidumpYAML::ThreadListStream::entry_type> {
  /// Map a thread list entry to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj Thread list entry being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               MinidumpYAML::ThreadListStream::entry_type &Obj);
};

/// YAMLIO mapping traits for 64-bit memory list stream entries.
template <>
struct MappingTraits<MinidumpYAML::Memory64ListStream::entry_type> {
  /// Map a 64-bit memory list entry to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj 64-bit memory list entry being mapped.
  LLVM_ABI static void
  mapping(IO &IO, MinidumpYAML::Memory64ListStream::entry_type &Obj);
};

/// Sequences of owned minidump streams use block formatting.
template <> struct SequenceElementTraits<std::unique_ptr<MinidumpYAML::Stream>> {
  /// Emit sequences of owned minidump streams in block style.
  static const bool flow = false;
};

/// Sequences of memory list entries use block formatting.
template <>
struct SequenceElementTraits<MinidumpYAML::MemoryListStream::entry_type> {
  /// Emit sequences of memory list entries in block style.
  static const bool flow = false;
};

/// Sequences of module list entries use block formatting.
template <>
struct SequenceElementTraits<MinidumpYAML::ModuleListStream::entry_type> {
  /// Emit sequences of module list entries in block style.
  static const bool flow = false;
};

/// Sequences of thread list entries use block formatting.
template <>
struct SequenceElementTraits<MinidumpYAML::ThreadListStream::entry_type> {
  /// Emit sequences of thread list entries in block style.
  static const bool flow = false;
};

/// Sequences of 64-bit memory list entries use block formatting.
template <>
struct SequenceElementTraits<MinidumpYAML::Memory64ListStream::entry_type> {
  /// Emit sequences of 64-bit memory list entries in block style.
  static const bool flow = false;
};

/// Sequences of memory info records use block formatting.
template <> struct SequenceElementTraits<minidump::MemoryInfo> {
  /// Emit sequences of memory info records in block style.
  static const bool flow = false;
};

/// YAMLIO mapping traits for \c MinidumpYAML::Object.
template <> struct MappingTraits<MinidumpYAML::Object> {
  /// Map minidump object fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj Minidump object being mapped.
  LLVM_ABI static void mapping(IO &IO, MinidumpYAML::Object &Obj);
};

} // namespace yaml

} // namespace llvm

#endif // LLVM_OBJECTYAML_MINIDUMPYAML_H
