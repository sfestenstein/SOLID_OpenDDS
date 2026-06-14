// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pub_sub_open_dds/fwd.h"
#include "pub_sub_open_dds/service_config.h"

#include <memory>
#include <string>

namespace pub_sub_open_dds {

namespace detail { struct TypeAdapter; }

class IRuntime {
public:
  virtual ~IRuntime() = default;

  virtual void init(const ServiceConfig& cfg) = 0;
  virtual void activate() = 0;
  virtual void shutdown() = 0;

  virtual std::shared_ptr<detail::TypedWriterBinding> create_writer(
      const std::string& topic, const detail::TypeAdapter& adapter,
      const WriterQos& qos) = 0;

  virtual std::shared_ptr<detail::TypedReaderBinding> create_reader(
      const std::string& topic, const detail::TypeAdapter& adapter,
      const ReaderQos& qos) = 0;
};

std::shared_ptr<IRuntime> make_opendds_runtime();
std::shared_ptr<IRuntime> make_in_memory_runtime();

} // namespace pub_sub_open_dds