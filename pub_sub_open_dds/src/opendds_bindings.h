// SPDX-License-Identifier: Apache-2.0
#pragma once

// PRIVATE detail header. Not part of the user-facing API. Included only by:
//   * pub_sub_open_dds/src/opendds_runtime.cpp
//   * the generated <TypeName>PubSub_adapter.cpp files produced by the
//     pub_sub_open_dds_generate_bindings() CMake helper.

#include "typed_binding.h"
#include "pub_sub_open_dds/fwd.h"
#include "pub_sub_open_dds/qos.h"

#include <dds/DCPS/LocalObject.h>
#include <dds/DCPS/WaitSet.h>
#include <dds/DdsDcpsPublicationC.h>
#include <dds/DdsDcpsSubscriptionC.h>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

namespace pub_sub_open_dds::detail {

void        apply_writer_qos(const QosProfile& p, DDS::DataWriterQos& qos);
void        apply_reader_qos(const QosProfile& p, DDS::DataReaderQos& qos);
WriteResult translate_return_code(DDS::ReturnCode_t rc);

class OpenDddsWriterBinding final : public TypedWriterBinding {
public:
  OpenDddsWriterBinding(DDS::DataWriter_var generic_writer,
                        std::shared_ptr<void> typed_writer_keepalive,
                        std::function<WriteResult(const void*)> write_fn)
      : writer_(std::move(generic_writer)),
        typed_keepalive_(std::move(typed_writer_keepalive)),
        write_fn_(std::move(write_fn)) {}

  WriteResult write_erased(const void* sample) override {
    return write_fn_ ? write_fn_(sample) : WriteResult::PreconditionFailed;
  }

  bool wait_for_subscribers(std::chrono::milliseconds timeout,
                            int min_count) override;

private:
  DDS::DataWriter_var                     writer_;
  std::shared_ptr<void>                   typed_keepalive_;
  std::function<WriteResult(const void*)> write_fn_;
};

class OpenDddsReaderBinding final : public TypedReaderBinding {
public:
  OpenDddsReaderBinding() = default;

  void install(DDS::DataReader_var generic_reader,
               DDS::DataReaderListener_var listener,
               std::shared_ptr<void> typed_reader_keepalive) {
    reader_          = std::move(generic_reader);
    listener_        = std::move(listener);
    typed_keepalive_ = std::move(typed_reader_keepalive);
  }

  void set_on_sample(std::function<void(const void*)> thunk) override {
    std::lock_guard<std::mutex> lk(mtx_);
    on_sample_ = std::move(thunk);
  }

  void deliver(const void* sample) {
    std::function<void(const void*)> cb;
    {
      std::lock_guard<std::mutex> lk(mtx_);
      cb = on_sample_;
    }
    counter_.fetch_add(1, std::memory_order_relaxed);
    if (cb) cb(sample);
  }

private:
  DDS::DataReader_var              reader_;
  DDS::DataReaderListener_var      listener_;
  std::shared_ptr<void>            typed_keepalive_;
  std::mutex                       mtx_;
  std::function<void(const void*)> on_sample_;
};

template <class T, class TypedReader>
class OpenDddsListener
    : public virtual OpenDDS::DCPS::LocalObject<DDS::DataReaderListener> {
public:
  explicit OpenDddsListener(std::weak_ptr<OpenDddsReaderBinding> binding)
      : binding_(std::move(binding)) {}

  void on_data_available(DDS::DataReader_ptr reader) override {
    typename TypedReader::_var_type typed = TypedReader::_narrow(reader);
    if (!typed) return;
    T sample;
    DDS::SampleInfo info;
    while (typed->take_next_sample(sample, info) == DDS::RETCODE_OK) {
      if (info.valid_data) {
        if (auto b = binding_.lock()) {
          b->deliver(static_cast<const void*>(&sample));
        }
      }
    }
  }

  void on_requested_deadline_missed (DDS::DataReader_ptr, const DDS::RequestedDeadlineMissedStatus&) override {}
  void on_requested_incompatible_qos(DDS::DataReader_ptr, const DDS::RequestedIncompatibleQosStatus&) override {}
  void on_sample_rejected           (DDS::DataReader_ptr, const DDS::SampleRejectedStatus&) override {}
  void on_liveliness_changed        (DDS::DataReader_ptr, const DDS::LivelinessChangedStatus&) override {}
  void on_subscription_matched      (DDS::DataReader_ptr, const DDS::SubscriptionMatchedStatus&) override {}
  void on_sample_lost               (DDS::DataReader_ptr, const DDS::SampleLostStatus&) override {}

private:
  std::weak_ptr<OpenDddsReaderBinding> binding_;
};

} // namespace pub_sub_open_dds::detail