//===- raw_ostream_proxy.h - Proxies for raw output streams -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RAW_OSTREAM_PROXY_H
#define LLVM_SUPPORT_RAW_OSTREAM_PROXY_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// Adaptor to create a stream class that proxies another \a raw_ostream.
///
/// Use \a raw_ostream_proxy_adaptor<> directly to implement an abstract
/// derived class of \a raw_ostream as a proxy. Otherwise use \a
/// raw_ostream_proxy.
///
/// Most operations are forwarded to the proxied stream.
///
/// If the proxied stream is buffered, the buffer is dropped and moved to this
/// stream. This allows \a flush() to work correctly, flushing immediately from
/// the proxy through to the final stream, and avoids any wasteful
/// double-buffering.
///
/// \a enable_colors() changes both the proxied stream and the proxy itself.
/// \a is_displayed() and \a has_colors() are forwarded to the proxy. \a
/// changeColor(), resetColor(), and \a reverseColor() are not forwarded, since
/// they need to call \a flush() and the buffer lives in the proxy.
template <class RawOstreamT = raw_ostream>
class raw_ostream_proxy_adaptor : public RawOstreamT {
  void write_impl(const char *Ptr, size_t Size) override {
    getProxiedOS().write(Ptr, Size);
  }
  uint64_t current_pos() const override { return getProxiedOS().tell(); }
  size_t preferred_buffer_size() const override {
    return getPreferredBufferSize();
  }

public:
  /// Forward a reserve request to the proxied stream.
  ///
  /// \param ExtraSize Additional bytes to reserve beyond the current tell().
  void reserveExtraSpace(uint64_t ExtraSize) override {
    getProxiedOS().reserveExtraSpace(ExtraSize);
  }
  /// Return whether the proxied stream is connected to a display.
  ///
  /// @return True if the proxied stream is connected to a display.
  bool is_displayed() const override { return getProxiedOS().is_displayed(); }
  /// Return whether the proxied stream supports colors.
  ///
  /// @return True if the proxied stream supports colors.
  bool has_colors() const override { return getProxiedOS().has_colors(); }
  /// Enable or disable colors on both this proxy and the proxied stream.
  ///
  /// \param enable If true, enable colors; otherwise disable them.
  void enable_colors(bool enable) override {
    RawOstreamT::enable_colors(enable);
    getProxiedOS().enable_colors(enable);
  }
  /// Return true if this adaptor still has a proxied stream.
  ///
  /// @return True if a proxied stream is still set.
  bool hasProxiedOS() const { return OS; }
  /// Return a reference to the proxied stream.
  ///
  /// Asserts that a proxied stream is still set.
  ///
  /// @return The proxied \a raw_ostream.
  raw_ostream &getProxiedOS() const {
    assert(OS && "raw_ostream_proxy_adaptor use after reset");
    return *OS;
  }
  /// Return the preferred buffer size cached at construction.
  ///
  /// @return The preferred buffer size in bytes.
  size_t getPreferredBufferSize() const { return PreferredBufferSize; }

  /// Destroy the adaptor and stop proxying the underlying stream.
  ~raw_ostream_proxy_adaptor() override { resetProxiedOS(); }

protected:
  /// Construct an adaptor that proxies \p OS and forwards base constructor
  /// arguments.
  ///
  /// Takes ownership of \p OS's buffer preference and sets \p OS unbuffered so
  /// that flush goes through this proxy.
  ///
  /// \param OS Stream to proxy.
  /// \param Args Arguments forwarded to the \p RawOstreamT base constructor.
  template <class... ArgsT>
  explicit raw_ostream_proxy_adaptor(raw_ostream &OS, ArgsT &&...Args)
      : RawOstreamT(std::forward<ArgsT>(Args)...), OS(&OS),
        PreferredBufferSize(OS.GetBufferSize()) {
    // Drop OS's buffer to make this->flush() forward. This proxy will add a
    // buffer in its place.
    OS.SetUnbuffered();
  }

  /// Stop proxying the stream. Flush and set up a crash for future writes.
  ///
  /// For example, this can simplify logic when a subclass might have a longer
  /// lifetime than the stream it proxies.
  void resetProxiedOS() {
    this->SetUnbuffered();
    OS = nullptr;
  }

private:
  raw_ostream *OS;

  /// Caches the value of OS->GetBufferSize() at construction time.
  size_t PreferredBufferSize;
};

/// Adaptor for creating a stream that proxies a \a raw_pwrite_stream.
template <class RawPwriteStreamT = raw_pwrite_stream>
class raw_pwrite_stream_proxy_adaptor
    : public raw_ostream_proxy_adaptor<RawPwriteStreamT> {
  using RawOstreamAdaptorT = raw_ostream_proxy_adaptor<RawPwriteStreamT>;

  void pwrite_impl(const char *Ptr, size_t Size, uint64_t Offset) override {
    this->flush();
    getProxiedOS().pwrite(Ptr, Size, Offset);
  }

protected:
  /// Default-construct an adaptor with no proxied stream yet.
  raw_pwrite_stream_proxy_adaptor() = default;
  /// Construct an adaptor that proxies \p OS and forwards base constructor
  /// arguments.
  ///
  /// \param OS Pwrite stream to proxy.
  /// \param Args Arguments forwarded to the base adaptor constructor.
  template <class... ArgsT>
  explicit raw_pwrite_stream_proxy_adaptor(raw_pwrite_stream &OS,
                                           ArgsT &&...Args)
      : RawOstreamAdaptorT(OS, std::forward<ArgsT>(Args)...) {}

  /// Return a reference to the proxied pwrite stream.
  ///
  /// @return The proxied \a raw_pwrite_stream.
  raw_pwrite_stream &getProxiedOS() const {
    return static_cast<raw_pwrite_stream &>(RawOstreamAdaptorT::getProxiedOS());
  }
};

/// Non-owning proxy for a \a raw_ostream. Enables passing a stream into an
/// API that takes ownership.
class LLVM_ABI raw_ostream_proxy : public raw_ostream_proxy_adaptor<> {
  void anchor() override;

public:
  /// Construct a non-owning proxy around \p OS.
  ///
  /// \param OS Stream to proxy.
  raw_ostream_proxy(raw_ostream &OS) : raw_ostream_proxy_adaptor<>(OS) {}
};

/// Non-owning proxy for a \a raw_pwrite_stream. Enables passing a stream
/// into an API that takes ownership.
class LLVM_ABI raw_pwrite_stream_proxy
    : public raw_pwrite_stream_proxy_adaptor<> {
  void anchor() override;

public:
  /// Construct a non-owning proxy around \p OS.
  ///
  /// \param OS Pwrite stream to proxy.
  raw_pwrite_stream_proxy(raw_pwrite_stream &OS)
      : raw_pwrite_stream_proxy_adaptor<>(OS) {}
};

} // end namespace llvm

#endif // LLVM_SUPPORT_RAW_OSTREAM_PROXY_H
