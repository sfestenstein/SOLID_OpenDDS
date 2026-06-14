// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pub_sub_open_dds/error.h"
#include "pub_sub_open_dds/fwd.h"
#include "pub_sub_open_dds/qos.h"

#include <functional>
#include <memory>
#include <string>
#include <typeindex>

namespace pub_sub_open_dds::detail {

struct TypeAdapter {
  virtual ~TypeAdapter() = default;

  virtual std::type_index type_index() const = 0;
  virtual const char* type_name() const = 0;
  virtual std::shared_ptr<void> clone(const void* sample) const = 0;

  virtual void register_with_opendds(void* /*participant*/) const {
    throw Error(std::string("type adapter for '") + type_name()
                + "' has no OpenDDS implementation linked");
  }

  virtual std::shared_ptr<TypedWriterBinding> make_opendds_writer(
      void* /*publisher*/, void* /*topic*/, const WriterQos& /*qos*/) const {
    throw Error(std::string("type adapter for '") + type_name()
                + "' has no OpenDDS writer implementation linked");
  }

  virtual std::shared_ptr<TypedReaderBinding> make_opendds_reader(
      void* /*subscriber*/, void* /*topic*/, const ReaderQos& /*qos*/,
      std::function<void(const void*)> /*on_sample*/) const {
    throw Error(std::string("type adapter for '") + type_name()
                + "' has no OpenDDS reader implementation linked");
  }
};

} // namespace pub_sub_open_dds::detail