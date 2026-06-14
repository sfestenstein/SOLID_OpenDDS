// SPDX-License-Identifier: Apache-2.0
#include "pub_sub_open_dds/service.h"

#include "runtime.h"
#include "pub_sub_open_dds/service_bootstrap_config.h"
#include "pub_sub_open_dds/topic_config.h"

#include <sstream>
#include <stdexcept>

namespace pub_sub_open_dds {

Service::Service() : runtime_(make_opendds_runtime()) {}

Service::Service(std::shared_ptr<IRuntime> runtime)
    : runtime_(std::move(runtime)) {
  if (!runtime_) {
    throw std::runtime_error("Service: runtime must be non-null");
  }
}

Service::~Service() {
  // Never let runtime resources outlive the Service. Swallow errors —
  // throwing from a destructor would be worse than a noisy shutdown.
  try {
    deactivate();
  } catch (...) {
    // intentional: dtor must not throw
  }
}

void Service::require_state(LifecycleState s, const char* op) {
  if (state_ != s) {
    std::ostringstream oss;
    oss << "pub_sub_open_dds::Service: cannot call '" << op
        << "' from state " << static_cast<int>(state_)
        << " (expected " << static_cast<int>(s) << ")";
    throw std::runtime_error(oss.str());
  }
}

void Service::pre_activate(const ServiceConfig& cfg) {
  require_state(LifecycleState::Created, "pre_activate");
  config_ = cfg;
  runtime_->init(config_);
  state_ = LifecycleState::PreActivated;
}

void Service::pre_activate(const ServiceBootstrapConfig& cfg) {
  ServiceConfig runtime_cfg;
  runtime_cfg.domain_id    = cfg.domain_id;
  runtime_cfg.runtime_args = cfg.runtime_args;
  runtime_cfg.config_file  = cfg.config_file;

  TopicConfig topic_cfg = TopicConfig::load_from_file(cfg.topic_config_file);
  if (!cfg.qos_xml_file.empty()) {
    topic_cfg.use_xml_qos_file(cfg.qos_xml_file);
  }

  pre_activate(runtime_cfg, std::move(topic_cfg));
}

void Service::pre_activate_from_file(const std::string& config_path) {
  pre_activate(ServiceBootstrapConfig::load_from_file(config_path));
}

void Service::pre_activate(const ServiceConfig& cfg, TopicConfig topic_config) {
  require_state(LifecycleState::Created, "pre_activate");
  config_ = cfg;
  topic_config_.reset(new TopicConfig(std::move(topic_config)));
  runtime_->init(config_);
  state_ = LifecycleState::PreActivated;
}

void Service::activate() {
  require_state(LifecycleState::PreActivated, "activate");
  runtime_->activate();
  // Flush registrations in the order the user submitted them. Each thunk
  // calls runtime_->create_writer / create_reader and binds the typed
  // handle.
  for (auto& reg : pending_) reg.thunk();
  pending_.clear();
  state_ = LifecycleState::Activated;
}

void Service::post_activate() {
  if (state_ == LifecycleState::PreActivated) {
    activate();
  }
  require_state(LifecycleState::Activated, "post_activate");
  // No built-in work; this is the documented user hook for "service is
  // fully up" tasks (initial publishes, registering with other subsystems,
  // etc.).
  state_ = LifecycleState::PostActivated;
}

void Service::require_declared_topic(const std::string& topic_name,
                                     const char*        op) const {
  if (!topic_config_) {
    return;
  }
  if (!topic_config_->has_binding(topic_name)) {
    throw std::runtime_error(std::string("pub_sub_open_dds::Service: cannot ")
                             + op + " undeclared topic '" + topic_name + "'");
  }
}

void Service::remember_topic_type(const std::string& topic_name,
                                  std::type_index     idx,
                                  const char*         type_name_for_diag) {
  std::unordered_map<std::string, std::type_index>::const_iterator it =
      topic_types_.find(topic_name);
  if (it == topic_types_.end()) {
    topic_types_.insert(std::make_pair(topic_name, idx));
    return;
  }
  if (it->second != idx) {
    throw std::runtime_error(std::string("pub_sub_open_dds::Service: topic '")
                             + topic_name + "' already declared with a different type; "
                             "cannot use type '" + type_name_for_diag + "'");
  }
}

const detail::TypeAdapter& Service::require_type_adapter(
    std::type_index idx,
    const char*     type_name_for_diag) const {
  const detail::TypeAdapter* adapter = detail::find_type_adapter(idx);
  if (!adapter) {
    throw std::runtime_error(std::string("no TypeAdapter registered for '")
                             + type_name_for_diag
                             + "' — did you forget pub_sub_open_dds_generate_bindings(" 
                               "... TYPES <T>)?");
  }
  return *adapter;
}

void Service::subscribe_erased(const std::string&              topic_name,
                               std::type_index                  idx,
                               const char*                      type_name_for_diag,
                               std::function<void(const void*)> on_sample) {
  require_declared_topic(topic_name, "subscribe");
  remember_topic_type(topic_name, idx, type_name_for_diag);

  const detail::TypeAdapter* adapter = &require_type_adapter(idx, type_name_for_diag);
  const ReaderQos qos = reader_qos_for_topic(topic_name);
  if (state_ == LifecycleState::PreActivated) {
    pending_.push_back(PendingRegistration{
      [this, topic_name, qos, adapter, on_sample]() {
        std::shared_ptr<detail::TypedReaderBinding> binding =
            runtime_->create_reader(topic_name, *adapter, qos);
        binding->set_on_sample(on_sample);
        subscriber_bindings_.push_back(binding);
      }
    });
    return;
  }

  std::shared_ptr<detail::TypedReaderBinding> binding =
      runtime_->create_reader(topic_name, *adapter, qos);
  binding->set_on_sample(on_sample);
  subscriber_bindings_.push_back(binding);
}

WriteResult Service::publish_erased(const std::string& topic_name,
                                    std::type_index     idx,
                                    const char*         type_name_for_diag,
                                    const void*         sample) {
  require_declared_topic(topic_name, "publish");
  remember_topic_type(topic_name, idx, type_name_for_diag);

  std::unordered_map<std::string, std::shared_ptr<detail::TypedWriterBinding> >::const_iterator it =
      publisher_cache_.find(topic_name);
  if (it == publisher_cache_.end()) {
    const detail::TypeAdapter& adapter = require_type_adapter(idx, type_name_for_diag);
    std::shared_ptr<detail::TypedWriterBinding> binding = runtime_->create_writer(
        topic_name, adapter, writer_qos_for_topic(topic_name));
    it = publisher_cache_.insert(std::make_pair(topic_name, binding)).first;
  }

  return it->second->write_erased(sample);
}

WriterQos Service::writer_qos_for_topic(const std::string& topic_name) const {
  if (!topic_config_) {
    return make_writer_qos(qos::reliable());
  }
  return topic_config_->writer_qos_for(topic_name, qos::reliable());
}

ReaderQos Service::reader_qos_for_topic(const std::string& topic_name) const {
  if (!topic_config_) {
    return make_reader_qos(qos::reliable());
  }
  return topic_config_->reader_qos_for(topic_name, qos::reliable());
}

void Service::deactivate() {
  if (state_ == LifecycleState::Created ||
      state_ == LifecycleState::Deactivated) {
    state_ = LifecycleState::Deactivated;
    return;
  }

  // Shut the runtime down FIRST so any in-flight reader callbacks have
  // drained before we release the reader bindings that own those callback
  // thunks.
  runtime_->shutdown();
  subscriber_bindings_.clear();
  pending_.clear();
  publisher_cache_.clear();
  topic_types_.clear();
  topic_config_.reset();

  state_ = LifecycleState::Deactivated;
}

} // namespace pub_sub_open_dds
