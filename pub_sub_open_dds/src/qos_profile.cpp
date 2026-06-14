// SPDX-License-Identifier: Apache-2.0
#include "pub_sub_open_dds/qos.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace pub_sub_open_dds {

namespace qos {

const QosProfile& best_effort() {
  static const QosProfile p{"best_effort", /*reliable*/false, /*durable*/false,
                            /*keep_all*/false, /*depth*/1};
  return p;
}

const QosProfile& reliable() {
  static const QosProfile p{"reliable", true, false, false, 10};
  return p;
}

const QosProfile& reliable_transient() {
  static const QosProfile p{"reliable_transient", true, true, false, 10};
  return p;
}

const QosProfile& event_bus() {
  static const QosProfile p{"event_bus", true, false, /*keep_all*/true, 0};
  return p;
}

const QosProfile& latched() {
  static const QosProfile p{"latched", true, true, false, 1};
  return p;
}

const QosProfile& streaming() {
  static const QosProfile p{"streaming", false, false, false, 1};
  return p;
}

const QosProfile& persistent() {
  // Approximation of full PERSISTENT durability: TRANSIENT_LOCAL + KEEP_ALL
  // means every historical sample reaches a late-joining reader, but the
  // writer itself does not survive process restart. Future swap to true
  // PERSISTENT only requires updating this struct.
  static const QosProfile p{"persistent", true, true, /*keep_all*/true, 0};
  return p;
}

const QosProfile& heartbeat() {
  QosProfile p{"heartbeat", false, false, false, 1};
  // Caller is expected to write at least once every deadline_ms; readers
  // get on_requested_deadline_missed if the writer goes silent.
  p.deadline_ms = 3000;
  static const QosProfile pp = p;
  return pp;
}

const QosProfile& critical() {
  QosProfile p{"critical", true, false, /*keep_all*/true, 0};
  // MANUAL_BY_TOPIC means the writer has to explicitly assert liveliness
  // within the lease; otherwise the reader sees on_liveliness_changed and
  // the writer drops out of the matched set.
  p.liveliness_lease_ms = 5000;
  p.liveliness_manual   = true;
  // Cap unbounded queues so a slow subscriber can't OOM the writer.
  p.max_samples = 1000;
  static const QosProfile pp = p;
  return pp;
}

} // namespace qos

namespace {

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

} // namespace

const QosProfile* find_builtin_profile(const std::string& name) {
  static const QosProfile* const table[] = {
      &qos::best_effort(),
      &qos::reliable(),
      &qos::reliable_transient(),
      &qos::event_bus(),
      &qos::latched(),
      &qos::streaming(),
      &qos::persistent(),
      &qos::heartbeat(),
      &qos::critical(),
  };
  const std::string needle = to_lower(name);
  for (const QosProfile* p : table) {
    if (to_lower(p->name) == needle) return p;
  }
  return nullptr;
}

} // namespace pub_sub_open_dds
