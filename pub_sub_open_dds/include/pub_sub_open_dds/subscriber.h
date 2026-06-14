// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pub_sub_open_dds/fwd.h"
#include <functional>
#include <memory>

namespace pub_sub_open_dds {

// Typed subscriber handle. Users only ever see std::shared_ptr<Subscriber<T>>
// returned by Service::register_subscriber.
//
// The user supplies a Callback at registration time. Once the Service has
// activated, the binding dispatches each received sample by static-casting
// the void* it gets from the transport back to const T* and invoking the
// callback.
//
// No DDS::, OpenDDS::, or ACE_ types appear in this header — the binding
// hides the transport.
template <class T>
class Subscriber {
public:
  using Callback = std::function<void(const T&)>;

  Subscriber(const Subscriber&)            = delete;
  Subscriber& operator=(const Subscriber&) = delete;

  // Total number of valid samples this subscriber has dispatched to its
  // callback since registration. Useful for tests and back-pressure checks.
  long received_count() const noexcept {
    return binding_ ? binding_->received_count() : 0;
  }

private:
  friend class Service;
  Subscriber() = default;

  // Service stores the user callback and the binding; once activate() has
  // built the binding, install_thunk wires them together.
  void install_thunk() {
    if (!binding_ || !callback_) return;
    // Capture-by-value of the std::function keeps the callback alive even
    // if `*this` is dropped by the user (Service holds the keepalive too,
    // but this is the belt-and-braces for in-flight callbacks).
    auto cb = callback_;
    binding_->set_on_sample([cb](const void* p) {
      cb(*static_cast<const T*>(p));
    });
  }

  Callback                                    callback_;
  std::shared_ptr<detail::TypedReaderBinding> binding_;
};

} // namespace pub_sub_open_dds
