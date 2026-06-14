// SPDX-License-Identifier: Apache-2.0
#include "registry.h"

#include <mutex>
#include <unordered_map>

namespace pub_sub_open_dds::detail {

namespace {

// Process-wide registry. Built lazily on first access so adapter static
// initialisers can register against it even if they run before any user
// code touches it.
struct Registry {
  std::mutex                                          mtx;
  std::unordered_map<std::type_index, const TypeAdapter*> by_index;
};

Registry& registry() {
  static Registry r;
  return r;
}

} // namespace

void register_type_adapter(const TypeAdapter& adapter) {
  auto& r = registry();
  std::lock_guard<std::mutex> lk(r.mtx);
  // First registration wins; subsequent registrations of the same type
  // (e.g. an adapter linked into both a static lib and the executable)
  // are silently ignored so behaviour stays deterministic.
  r.by_index.emplace(adapter.type_index(), &adapter);
}

const TypeAdapter* find_type_adapter(std::type_index idx) {
  auto& r = registry();
  std::lock_guard<std::mutex> lk(r.mtx);
  auto it = r.by_index.find(idx);
  return it == r.by_index.end() ? nullptr : it->second;
}

} // namespace pub_sub_open_dds::detail
