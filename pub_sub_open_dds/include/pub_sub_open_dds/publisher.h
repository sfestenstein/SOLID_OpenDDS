// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pub_sub_open_dds/fwd.h"
#include <chrono>
#include <memory>

namespace pub_sub_open_dds {

// Typed publisher handle. Users only ever see std::shared_ptr<Publisher<T>>
// returned by Service::register_publisher; they never construct one
// directly.
//
// Before Service::activate() has run, the underlying binding is null and
// both write() and wait_for_subscribers() are well-defined no-ops
// (write returns WriteResult::PreconditionFailed; wait_for_subscribers
// returns false).
//
// No DDS::, OpenDDS::, or ACE_ types appear in this header — the binding
// hides the transport.
template <class T>
class Publisher {
public:
  Publisher(const Publisher&)            = delete;
  Publisher& operator=(const Publisher&) = delete;

  // Publish one sample. The Sample-to-void* cast is type-safe because the
  // binding was built for exactly T (Service::register_publisher<T> looks
  // up the TypeAdapter by typeid(T) and the binding statically casts back).
  WriteResult write(const T& sample) {
    if (!binding_) return WriteResult::PreconditionFailed;
    return binding_->write_erased(static_cast<const void*>(&sample));
  }

  // Block until at least `min_count` matched subscribers exist for this
  // writer, or until `timeout` elapses. Returns true on match, false on
  // timeout / no binding yet.
  bool wait_for_subscribers(std::chrono::milliseconds timeout,
                            int min_count = 1) {
    return binding_ ? binding_->wait_for_subscribers(timeout, min_count) : false;
  }

private:
  friend class Service;
  Publisher() = default;

  // Service::activate() builds the runtime binding and installs it here.
  void internal_bind(std::shared_ptr<detail::TypedWriterBinding> b) {
    binding_ = std::move(b);
  }

  std::shared_ptr<detail::TypedWriterBinding> binding_;
};

} // namespace pub_sub_open_dds
