// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdexcept>
#include <string>

namespace pub_sub_open_dds {

// Thrown by lifecycle / configuration / registration failures. Hot paths
// (Publisher<T>::write, Subscriber<T>::received_count) never throw; they
// return a WriteResult or a plain value.
class Error : public std::runtime_error {
public:
  explicit Error(const std::string& what) : std::runtime_error(what) {}
};

} // namespace pub_sub_open_dds
