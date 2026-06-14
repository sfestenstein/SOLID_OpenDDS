// SPDX-License-Identifier: Apache-2.0
//
// OpenDDS implementation of IRuntime. Plus the QoS / return-code
// translation helpers declared in pub_sub_open_dds/detail/opendds_bindings.h
// (kept here so QoS translation has a single home).
//
// Per-type work (TypeSupport registration, typed DataWriter/DataReader
// narrow, write thunk, listener) lives in the generated
// <TypeName>PubSub_adapter.cpp files. The runtime owns the participant,
// publisher, subscriber, and the topic dedup table, then hands the typed
// pointers (as void*) back to the adapter via TypeAdapter::make_opendds_*.

#include "pub_sub_open_dds/runtime.h"

#include "pub_sub_open_dds/detail/data_adapter.h"
#include "pub_sub_open_dds/detail/opendds_bindings.h"
#include "pub_sub_open_dds/detail/typed_binding.h"
#include "pub_sub_open_dds/qos.h"

#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/Service_Participant.h>
#include <dds/DCPS/WaitSet.h>
#include <dds/DdsDcpsDomainC.h>
#include <dds/DdsDcpsPublicationC.h>
#include <dds/DdsDcpsSubscriptionC.h>

#include <ace/Basic_Types.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pub_sub_open_dds {

namespace detail {

// ---- QoS translation: façade -> OpenDDS --------------------------------

namespace {

DDS::Duration_t ms_to_duration(int ms) {
  DDS::Duration_t d{};
  d.sec     = static_cast<CORBA::Long>(ms / 1000);
  d.nanosec = static_cast<CORBA::ULong>((ms % 1000) * 1000000);
  return d;
}

void apply_extras(const QosProfile& p,
                  DDS::DeadlineQosPolicy&       deadline,
                  DDS::LivelinessQosPolicy&     liveliness,
                  DDS::ResourceLimitsQosPolicy& res) {
  if (p.deadline_ms > 0) {
    deadline.period = ms_to_duration(p.deadline_ms);
  }
  if (p.liveliness_lease_ms > 0) {
    liveliness.kind = p.liveliness_manual
        ? DDS::MANUAL_BY_TOPIC_LIVELINESS_QOS
        : DDS::AUTOMATIC_LIVELINESS_QOS;
    liveliness.lease_duration = ms_to_duration(p.liveliness_lease_ms);
  }
  if (p.max_samples > 0)   res.max_samples   = p.max_samples;
  if (p.max_instances > 0) res.max_instances = p.max_instances;
}

} // namespace

void apply_writer_qos(const QosProfile& p, DDS::DataWriterQos& qos) {
  qos.reliability.kind = p.reliable
      ? DDS::RELIABLE_RELIABILITY_QOS
      : DDS::BEST_EFFORT_RELIABILITY_QOS;
  qos.durability.kind = p.durable
      ? DDS::TRANSIENT_LOCAL_DURABILITY_QOS
      : DDS::VOLATILE_DURABILITY_QOS;
  if (p.keep_all) {
    qos.history.kind  = DDS::KEEP_ALL_HISTORY_QOS;
  } else {
    qos.history.kind  = DDS::KEEP_LAST_HISTORY_QOS;
    qos.history.depth = p.history_depth;
  }
  apply_extras(p, qos.deadline, qos.liveliness, qos.resource_limits);
}

void apply_reader_qos(const QosProfile& p, DDS::DataReaderQos& qos) {
  qos.reliability.kind = p.reliable
      ? DDS::RELIABLE_RELIABILITY_QOS
      : DDS::BEST_EFFORT_RELIABILITY_QOS;
  qos.durability.kind = p.durable
      ? DDS::TRANSIENT_LOCAL_DURABILITY_QOS
      : DDS::VOLATILE_DURABILITY_QOS;
  if (p.keep_all) {
    qos.history.kind  = DDS::KEEP_ALL_HISTORY_QOS;
  } else {
    qos.history.kind  = DDS::KEEP_LAST_HISTORY_QOS;
    qos.history.depth = p.history_depth;
  }
  apply_extras(p, qos.deadline, qos.liveliness, qos.resource_limits);
}

WriteResult translate_return_code(DDS::ReturnCode_t rc) {
  switch (rc) {
    case DDS::RETCODE_OK:                     return WriteResult::Ok;
    case DDS::RETCODE_TIMEOUT:                return WriteResult::Timeout;
    case DDS::RETCODE_OUT_OF_RESOURCES:       return WriteResult::OutOfResources;
    case DDS::RETCODE_PRECONDITION_NOT_MET:   return WriteResult::PreconditionFailed;
    case DDS::RETCODE_NOT_ENABLED:            return WriteResult::PreconditionFailed;
    case DDS::RETCODE_ALREADY_DELETED:        return WriteResult::Disconnected;
    default:                                  return WriteResult::Unknown;
  }
}

// ---- OpenDddsWriterBinding::wait_for_subscribers ----------------------

bool OpenDddsWriterBinding::wait_for_subscribers(
    std::chrono::milliseconds timeout, int min_count) {
  if (!writer_) return false;
  DDS::StatusCondition_var sc = writer_->get_statuscondition();
  sc->set_enabled_statuses(DDS::PUBLICATION_MATCHED_STATUS);
  DDS::WaitSet_var ws = new DDS::WaitSet;
  ws->attach_condition(sc);

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  bool matched = false;
  while (true) {
    DDS::PublicationMatchedStatus status{};
    if (writer_->get_publication_matched_status(status) != DDS::RETCODE_OK) {
      break;
    }
    if (status.current_count >= min_count) { matched = true; break; }

    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) break;
    const auto remain_s  =
        std::chrono::duration_cast<std::chrono::seconds>(deadline - now);
    const auto remain_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now) -
        std::chrono::duration_cast<std::chrono::nanoseconds>(remain_s);
    DDS::Duration_t dds_timeout = {
        static_cast<CORBA::Long>(remain_s.count()),
        static_cast<CORBA::ULong>(remain_ns.count())
    };
    DDS::ConditionSeq active;
    if (ws->wait(active, dds_timeout) != DDS::RETCODE_OK) break;
  }
  ws->detach_condition(sc);
  return matched;
}

} // namespace detail

namespace {

// ---- OpenDddsRuntime --------------------------------------------------

class OpenDddsRuntime final : public IRuntime {
public:
  void init(const ServiceConfig& cfg) override {
    config_ = cfg;
    rebuild_argv(cfg);

    factory_ = TheParticipantFactoryWithArgs(argc_, argv_.data());
    if (!factory_) {
      throw std::runtime_error("OpenDddsRuntime: TheParticipantFactoryWithArgs returned null");
    }
    participant_ = factory_->create_participant(
        static_cast<DDS::DomainId_t>(cfg.domain_id),
        PARTICIPANT_QOS_DEFAULT,
        nullptr,
        OpenDDS::DCPS::DEFAULT_STATUS_MASK);
    if (!participant_) {
      throw std::runtime_error("OpenDddsRuntime: create_participant failed");
    }
  }

  void activate() override {
    if (!participant_) {
      throw std::runtime_error("OpenDddsRuntime: activate before init");
    }
    publisher_ = participant_->create_publisher(
        PUBLISHER_QOS_DEFAULT, nullptr, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
    if (!publisher_) throw std::runtime_error("OpenDddsRuntime: create_publisher failed");

    subscriber_ = participant_->create_subscriber(
        SUBSCRIBER_QOS_DEFAULT, nullptr, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
    if (!subscriber_) throw std::runtime_error("OpenDddsRuntime: create_subscriber failed");
  }

  void shutdown() override {
    if (participant_) {
      // Drains in-flight listener callbacks before returning; safe to
      // release the typed-binding keepalive immediately after.
      participant_->delete_contained_entities();
      if (factory_) {
        factory_->delete_participant(participant_);
      }
    }
    participant_ = nullptr;
    publisher_   = nullptr;
    subscriber_  = nullptr;
    topics_.clear();
    topic_types_.clear();
    registered_types_.clear();

    if (factory_) {
      TheServiceParticipant->shutdown();
      factory_ = nullptr;
    }
  }

  std::shared_ptr<detail::TypedWriterBinding> create_writer(
      const std::string& topic, const detail::TypeAdapter& adapter,
      const WriterQos& qos) override {
    if (!publisher_) throw std::runtime_error("OpenDddsRuntime: create_writer before activate");
    register_type_once(adapter);
    DDS::Topic_var topic_var = get_or_create_topic(topic, adapter.type_name());
    auto b = adapter.make_opendds_writer(
        static_cast<void*>(publisher_.in()),
        static_cast<void*>(topic_var.in()),
        qos);
    if (!b) {
      throw std::runtime_error("OpenDddsRuntime: TypeAdapter for '"
                               + std::string(adapter.type_name())
                               + "' returned null writer binding");
    }
    return b;
  }

  std::shared_ptr<detail::TypedReaderBinding> create_reader(
      const std::string& topic, const detail::TypeAdapter& adapter,
      const ReaderQos& qos) override {
    if (!subscriber_) throw std::runtime_error("OpenDddsRuntime: create_reader before activate");
    register_type_once(adapter);
    DDS::Topic_var topic_var = get_or_create_topic(topic, adapter.type_name());
    auto b = adapter.make_opendds_reader(
        static_cast<void*>(subscriber_.in()),
        static_cast<void*>(topic_var.in()),
        qos,
      /*on_sample=*/{});  // Service installs the callback thunk after we return
    if (!b) {
      throw std::runtime_error("OpenDddsRuntime: TypeAdapter for '"
                               + std::string(adapter.type_name())
                               + "' returned null reader binding");
    }
    return b;
  }

private:
  void rebuild_argv(const ServiceConfig& cfg) {
    arg_storage_.clear();
    argv_.clear();
    arg_storage_.push_back("pub_sub_open_dds");
    if (!cfg.config_file.empty()) {
      arg_storage_.push_back("-DCPSConfigFile");
      arg_storage_.push_back(cfg.config_file);
    }
    for (const auto& a : cfg.runtime_args) arg_storage_.push_back(a);
    argv_.reserve(arg_storage_.size() + 1);
    for (auto& s : arg_storage_) argv_.push_back(const_cast<char*>(s.c_str()));
    argv_.push_back(nullptr);
    argc_ = static_cast<int>(arg_storage_.size());
  }

  void register_type_once(const detail::TypeAdapter& adapter) {
    const std::string key = adapter.type_name();
    if (!registered_types_.insert(key).second) return;
    adapter.register_with_opendds(static_cast<void*>(participant_.in()));
  }

  DDS::Topic_var get_or_create_topic(const std::string& topic_name,
                                     const std::string& type_name) {
    auto it = topics_.find(topic_name);
    if (it != topics_.end()) {
      const std::string& existing = topic_types_[topic_name];
      if (existing != type_name) {
        throw std::runtime_error("OpenDddsRuntime: topic '" + topic_name
                                 + "' already registered with type '" + existing
                                 + "', cannot reuse for type '" + type_name + "'");
      }
      return it->second;
    }
    DDS::Topic_var topic = participant_->create_topic(
        topic_name.c_str(),
        type_name.c_str(),
        TOPIC_QOS_DEFAULT,
        nullptr,
        OpenDDS::DCPS::DEFAULT_STATUS_MASK);
    if (!topic) {
      throw std::runtime_error("OpenDddsRuntime: create_topic failed for '" + topic_name + "'");
    }
    topics_[topic_name]      = topic;
    topic_types_[topic_name] = type_name;
    return topic;
  }

  ServiceConfig                                    config_{};
  std::vector<std::string>                         arg_storage_;
  std::vector<char*>                               argv_;
  int                                              argc_ = 0;

  DDS::DomainParticipantFactory_var                factory_;
  DDS::DomainParticipant_var                       participant_;
  DDS::Publisher_var                               publisher_;
  DDS::Subscriber_var                              subscriber_;

  std::unordered_map<std::string, DDS::Topic_var>  topics_;
  std::unordered_map<std::string, std::string>     topic_types_;
  std::unordered_set<std::string>                  registered_types_;
};

} // namespace

std::shared_ptr<IRuntime> make_opendds_runtime() {
  return std::make_shared<OpenDddsRuntime>();
}

} // namespace pub_sub_open_dds
