// SPDX-License-Identifier: Apache-2.0
#pragma once

// Forward declarations and small leaf enums shared across the public facade
// headers. Intentionally has no `#include <dds/...>` or `#include <ace/...>`
// — every type defined here is façade-only.

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <typeindex>

namespace pub_sub_open_dds {

// Outcome of a Publisher<T>::write call. Replaces the previously leaked
// DDS::ReturnCode_t. Values cover the common DDS write outcomes plus a
// catch-all for runtime-specific failures.
enum class WriteResult {
  Ok,                  // sample accepted by the runtime
  Timeout,             // write blocked past max_blocking_time
  NotMatched,          // no matched subscribers for this topic right now
  OutOfResources,      // RESOURCE_LIMITS rejected the sample
  PreconditionFailed,  // writer not bound yet (Service not activated)
  Disconnected,        // runtime / transport is no longer usable
  Unknown,             // anything not covered above
};

// Where a Service sits in its lifecycle. Out-of-order operations throw
// std::runtime_error.
enum class LifecycleState {
  Created,        // constructed, no runtime resources yet
  PreActivated,   // runtime initialised; registrations allowed
  Activated,      // writers/readers built; registrations no longer allowed
  PostActivated,  // user-visible "fully up" hook has run
  Deactivated,    // resources torn down; terminal state
};

class Service;
struct ServiceConfig;
struct QosProfile;
class  WriterQos;
class  ReaderQos;
class  TopicConfig;
class  IRuntime;

template <class T> class Publisher;
template <class T> class Subscriber;

namespace detail {
class TypedWriterBinding {
public:
  virtual ~TypedWriterBinding() = default;

  virtual WriteResult write_erased(const void* sample) = 0;

  virtual bool wait_for_subscribers(std::chrono::milliseconds /*timeout*/,
                                    int /*min_count*/ = 1) {
    return true;
  }
};

class TypedReaderBinding {
public:
  virtual ~TypedReaderBinding() = default;

  virtual void set_on_sample(std::function<void(const void*)> thunk) = 0;

  virtual long received_count() const noexcept {
    return counter_.load(std::memory_order_relaxed);
  }

protected:
  std::atomic<long> counter_{0};
};

struct TypeAdapter;
const TypeAdapter* find_type_adapter(std::type_index idx);
} // namespace detail

} // namespace pub_sub_open_dds
