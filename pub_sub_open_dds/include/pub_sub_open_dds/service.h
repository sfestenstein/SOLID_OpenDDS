// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pub_sub_open_dds/fwd.h"
#include "pub_sub_open_dds/service_bootstrap_config.h"
#include "pub_sub_open_dds/service_config.h"
#include "pub_sub_open_dds/topic_config.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pub_sub_open_dds {

// Facade over a domain participant's lifecycle. Depends on `IRuntime`,
// which keeps transport-specific setup behind a private seam. Production
// code uses the default OpenDDS runtime; repository tests can inject a
// small runtime test double.
//
//   Service svc;                                       // default: OpenDDS runtime
//   ServiceConfig cfg{.domain_id = 42, .runtime_args = {...}};
//   auto topics = TopicConfig::load_from_file("topics.ini");
//   svc.pre_activate(cfg, std::move(topics));
//   svc.subscribe<MyType>("MyTopic", [](const MyType& m){ ... });
//   svc.post_activate();
//   MyType sample;
//   svc.publish("MyTopic", sample);
//   svc.deactivate();   // also runs from the destructor
//
// Not copyable, not movable. One Service owns exactly one participant.
class Service {
public:
  // Default constructor uses the production OpenDDS runtime. Tests pass
  // a hand-rolled IRuntime test double to substitute transport behavior.
  Service();
  explicit Service(std::shared_ptr<IRuntime> runtime);
  ~Service();

  Service(const Service&)            = delete;
  Service& operator=(const Service&) = delete;
  Service(Service&&)                 = delete;
  Service& operator=(Service&&)      = delete;

  // ---- lifecycle --------------------------------------------------------
  void pre_activate(const ServiceBootstrapConfig& cfg);
  void pre_activate_from_file(const std::string& config_path);
  void pre_activate(const ServiceConfig& cfg);
  void pre_activate(const ServiceConfig& cfg, TopicConfig topic_config);
  void post_activate();
  void deactivate();

  LifecycleState state() const noexcept { return state_; }

  // ---- service-centric API ----------------------------------------------
  // Topic policy comes from TopicConfig passed to pre_activate. subscribe()
  // may be called any time after pre_activate; before post_activate it is
  // staged and bound when post_activate performs the hidden activation step.
  // publish() requires the service to be fully post-activated.

  template <class T>
  void subscribe(const std::string&               topic_name,
                 std::function<void(const T&)>    callback) {
    if (state_ == LifecycleState::Created ||
        state_ == LifecycleState::Deactivated) {
      throw std::runtime_error(
          "pub_sub_open_dds::Service: subscribe requires pre_activate");
    }

    subscribe_erased(topic_name,
                     std::type_index(typeid(T)),
                     type_name_for<T>(),
                     [callback](const void* sample) {
                       callback(*static_cast<const T*>(sample));
                     });
  }

  template <class T>
  WriteResult publish(const std::string& topic_name, const T& sample) {
    if (state_ != LifecycleState::PostActivated) {
      throw std::runtime_error(
          "pub_sub_open_dds::Service: publish requires post_activate");
    }

    return publish_erased(topic_name,
                          std::type_index(typeid(T)),
                          type_name_for<T>(),
                          static_cast<const void*>(&sample));
  }

private:
  void activate();

  // Compile-time-resolved diagnostic name for the user type. Falls back
  // to typeid(T).name() (mangled but informative). Used only in error
  // messages.
  template <class T>
  static const char* type_name_for() { return typeid(T).name(); }

  // ---- non-template impl details ----------------------------------------
  void require_state(LifecycleState s, const char* op);
  void require_declared_topic(const std::string& topic_name,
                              const char*        op) const;
  void remember_topic_type(const std::string& topic_name,
                           std::type_index     idx,
                           const char*         type_name_for_diag);
  const detail::TypeAdapter& require_type_adapter(std::type_index idx,
                                                  const char* type_name_for_diag) const;
  void subscribe_erased(const std::string&                  topic_name,
                        std::type_index                      idx,
                        const char*                          type_name_for_diag,
                        std::function<void(const void*)>     on_sample);
  WriteResult publish_erased(const std::string& topic_name,
                             std::type_index     idx,
                             const char*         type_name_for_diag,
                             const void*         sample);
  WriterQos writer_qos_for_topic(const std::string& topic_name) const;
  ReaderQos reader_qos_for_topic(const std::string& topic_name) const;

  // Stores staged reader registrations so post_activate() can realize them
  // in submission order once the runtime has transitioned to Active.
  struct PendingRegistration {
    std::function<void()> thunk;
  };

  std::shared_ptr<IRuntime>                runtime_;
  LifecycleState                           state_ = LifecycleState::Created;
  ServiceConfig                            config_{};
  std::unique_ptr<TopicConfig>             topic_config_;

  std::vector<PendingRegistration>         pending_;

  // Reader bindings own the installed callback thunk, so the Service keeps
  // them alive until deactivate() drains the transport.
  std::vector<std::shared_ptr<detail::TypedReaderBinding> > subscriber_bindings_;

  // Lazily created writer bindings keyed by topic.
  std::unordered_map<std::string, std::shared_ptr<detail::TypedWriterBinding> >
      publisher_cache_;

  // First observed user type for a topic. Used to reject accidental
  // topic/type mismatches across publish/subscribe calls.
  std::unordered_map<std::string, std::type_index> topic_types_;
};

} // namespace pub_sub_open_dds
