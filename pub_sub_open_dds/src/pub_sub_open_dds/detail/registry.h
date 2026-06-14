// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pub_sub_open_dds/detail/data_adapter.h"

#include <typeinfo>
#include <typeindex>

namespace pub_sub_open_dds::detail {

void register_type_adapter(const TypeAdapter& adapter);

const TypeAdapter* find_type_adapter(std::type_index idx);

template <class T>
const TypeAdapter* find_type_adapter() {
  return find_type_adapter(std::type_index(typeid(T)));
}

} // namespace pub_sub_open_dds::detail