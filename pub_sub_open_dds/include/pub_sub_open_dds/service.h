// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pub_sub_open_dds/fwd.h"
#include "pub_sub_open_dds/error.h"
#include "pub_sub_open_dds/publisher.h"
#include "pub_sub_open_dds/qos.h"
#include "pub_sub_open_dds/runtime.h"
#include "pub_sub_open_dds/service_config.h"
#include "pub_sub_open_dds/subscriber.h"

#include <memory>
#include <unordered_set>
#include <vector>

namespace pub_sub_open_dds {

#include <typeinfo>
#include <typeindex>
// Facade over a domain participant's lifecycle. Depends on `IRuntime`,
// which lets the same Service instance be exercised against either the
// real OpenDDS transport or the in-memory test fake.
//
//   Service svc;                                       // default: OpenDDS runtime
//   ServiceConfig cfg{.domain_id = 42, .runtime_args = {...}};
//   svc.pre_activate(cfg);
//   auto pub = svc.register_publisher<MyType>("MyTopic");                   // default reliable QoS
//   auto sub = svc.register_subscriber<MyType>("MyTopic",
//                                              [](const MyType& m){ ... });
//   auto pub2 = svc.register_publisher<MyType>("Other",
//                                              make_writer_qos(qos::best_effort()));
//   svc.activate();
//   svc.post_activate();
//   // ... use pub / sub ...
//   svc.deactivate();   // also runs from the destructor
//
// Not copyable, not movable. One Service owns exactly one participant.
class Service {
public:
  // Default constructor uses the production OpenDDS runtime. Tests pass
  // make_in_memory_runtime() (or a hand-rolled IRuntime) to substitute a
  // fake transport.
  Service();
  explicit Service(std::shared_ptr<IRuntime> runtime);
  ~Service();

  Service(const Service&)            = delete;
  Service& operator=(const Service&) = delete;
  Service(Service&&)                 = delete;
  Service& operator=(Service&&)      = delete;

  // ---- lifecycle --------------------------------------------------------
  void pre_activate(const ServiceConfig& cfg);
  void activate();
  void post_activate();
  void deactivate();

  LifecycleState state() const noexcept { return state_; }

  // ---- registration (only valid between pre_activate and activate) ------
  //
  // QoS defaults preserve the previous iteration's "reliable" behaviour;
  // pass `make_writer_qos(...)` / `make_reader_qos(...)` (or one of the
  // resolutions from TopicConfig) to pick a profile.

  template <class T>
  std::shared_ptr<Publisher<T>> register_publisher(
      const std::string& topic_name,
      WriterQos          qos = make_writer_qos(qos::reliable())) {
    auto handle = std::shared_ptr<Publisher<T>>(new Publisher<T>);
    const detail::TypeAdapter* adapter =
        detail::find_type_adapter(std::type_index(typeid(T)));
    register_publisher_impl(topic_name, std::move(qos), adapter,
                            handle, type_name_for<T>());
    return handle;
  }

  template <class T>
  std::shared_ptr<Subscriber<T>> register_subscriber(
      const std::string&               topic_name,
      typename Subscriber<T>::Callback callback,
      ReaderQos                        qos = make_reader_qos(qos::reliable())) {
    auto handle = std::shared_ptr<Subscriber<T>>(new Subscriber<T>);
    handle->callback_ = std::move(callback);
    const detail::TypeAdapter* adapter =
        detail::find_type_adapter(std::type_index(typeid(T)));
    register_subscriber_impl(topic_name, std::move(qos), adapter,
                             handle, type_name_for<T>());
    return handle;
  }

private:
  // Per-type binding installer used by the template register_publisher.
  // Defined in service.cpp; only needs the templated info passed through
  // function arguments.
  template <class T>
  void register_publisher_impl(
      const std::string& topic_name, WriterQos qos,
      const detail::TypeAdapter* adapter,
      std::shared_ptr<Publisher<T>> handle,
      const char* type_name_for_diag);

  template <class T>
  void register_subscriber_impl(
      const std::string& topic_name, ReaderQos qos,
      const detail::TypeAdapter* adapter,
      std::shared_ptr<Subscriber<T>> handle,
      const char* type_name_for_diag);

  // Compile-time-resolved diagnostic name for the user type. Falls back
  // to typeid(T).name() (mangled but informative). Used only in error
  // messages.
  template <class T>
  static const char* type_name_for() { return typeid(T).name(); }

  // ---- non-template impl details ----------------------------------------
  void require_state(LifecycleState s, const char* op);

  // Stores a thunk and a keepalive handle pair so activate() can flush
  // them in registration order and so the handles stay alive for the
  // Service's whole lifetime (the listener installed by create_reader holds
  // a raw pointer back into the handle's binding).
  struct PendingRegistration {
    std::function<void()> thunk;
  };

  std::shared_ptr<IRuntime>                runtime_;
  LifecycleState                           state_ = LifecycleState::Created;
  ServiceConfig                            config_{};

  std::vector<PendingRegistration>         pending_;

  // Keeps every registered Publisher<T> / Subscriber<T> alive for the
  // Service's lifetime. The runtime's reader binding may hold a callback
  // that references the Subscriber<T>; if the user drops their shared_ptr,
  // this vector keeps the handle alive until deactivate() drains the
  // transport.
  std::vector<std::shared_ptr<void>>       handle_keepalive_;

  std::unordered_set<std::string>          registered_topics_;
};

// ===== template implementations =========================================

template <class T>
void Service::register_publisher_impl(
    const std::string& topic_name, WriterQos qos,
    const detail::TypeAdapter* adapter,
    std::shared_ptr<Publisher<T>> handle,
    const char* type_name_for_diag) {
  require_state(LifecycleState::PreActivated, "register_publisher");
  if (!adapter) {
    throw Error(std::string("no TypeAdapter registered for '")
                + type_name_for_diag
                + "' — did you forget pub_sub_open_dds_generate_bindings("
                  "... TYPES <T>)?");
  }
  if (registered_topics_.insert(topic_name + "::pub").second == false) {
    throw Error("publisher already registered for topic '" + topic_name + "'");
  }
  handle_keepalive_.push_back(handle);
  pending_.push_back(PendingRegistration{
    [this, topic_name, qos, adapter, handle]() {
      auto binding = runtime_->create_writer(topic_name, *adapter, qos);
      handle->internal_bind(std::move(binding));
    }
  });
}

template <class T>
void Service::register_subscriber_impl(
    const std::string& topic_name, ReaderQos qos,
    const detail::TypeAdapter* adapter,
    std::shared_ptr<Subscriber<T>> handle,
    const char* type_name_for_diag) {
  require_state(LifecycleState::PreActivated, "register_subscriber");
  if (!adapter) {
    throw Error(std::string("no TypeAdapter registered for '")
                + type_name_for_diag
                + "' — did you forget pub_sub_open_dds_generate_bindings("
                  "... TYPES <T>)?");
  }
  if (registered_topics_.insert(topic_name + "::sub").second == false) {
    throw Error("subscriber already registered for topic '" + topic_name + "'");
  }
  handle_keepalive_.push_back(handle);
  pending_.push_back(PendingRegistration{
    [this, topic_name, qos, adapter, handle]() {
      auto binding = runtime_->create_reader(topic_name, *adapter, qos);
      handle->binding_ = std::move(binding);
      handle->install_thunk();
    }
  });
}

} // namespace pub_sub_open_dds
