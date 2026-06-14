// SPDX-License-Identifier: Apache-2.0
//
// Built-in QoS profile sanity check. Asserts each profile's QosProfile
// fields match what the documentation table in README claims. Pure value
// checks — no runtime seam or OpenDDS transport involved.

#include "pub_sub_open_dds/qos.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace pso = pub_sub_open_dds;

namespace {

void fail(const std::string& m) {
  std::cerr << "qos_profile_test FAIL: " << m << "\n";
  std::exit(2);
}

#define EXPECT(cond) do { if (!(cond)) fail(std::string(__FILE__ ":") + std::to_string(__LINE__) + " expected " #cond); } while (0)

} // namespace

int main() {
  // best_effort: BE / volatile / keep_last 1
  {
    const auto& p = pso::qos::best_effort();
    EXPECT(p.name == "best_effort");
    EXPECT(!p.reliable);
    EXPECT(!p.durable);
    EXPECT(!p.keep_all);
    EXPECT(p.history_depth == 1);
  }
  // reliable: REL / volatile / keep_last 10
  {
    const auto& p = pso::qos::reliable();
    EXPECT(p.reliable);
    EXPECT(!p.durable);
    EXPECT(!p.keep_all);
    EXPECT(p.history_depth == 10);
  }
  // reliable_transient: REL / transient_local / keep_last 10
  {
    const auto& p = pso::qos::reliable_transient();
    EXPECT(p.reliable);
    EXPECT(p.durable);
    EXPECT(p.history_depth == 10);
  }
  // event_bus: REL / volatile / keep_all
  {
    const auto& p = pso::qos::event_bus();
    EXPECT(p.reliable);
    EXPECT(!p.durable);
    EXPECT(p.keep_all);
  }
  // latched: REL / transient_local / keep_last 1
  {
    const auto& p = pso::qos::latched();
    EXPECT(p.reliable);
    EXPECT(p.durable);
    EXPECT(!p.keep_all);
    EXPECT(p.history_depth == 1);
  }
  // heartbeat: BE / volatile / keep_last 1 + deadline 3000 ms
  {
    const auto& p = pso::qos::heartbeat();
    EXPECT(!p.reliable);
    EXPECT(p.deadline_ms == 3000);
  }
  // critical: REL / volatile / keep_all + MANUAL_BY_TOPIC liveliness + bounded
  {
    const auto& p = pso::qos::critical();
    EXPECT(p.reliable);
    EXPECT(p.keep_all);
    EXPECT(p.liveliness_lease_ms == 5000);
    EXPECT(p.liveliness_manual);
    EXPECT(p.max_samples == 1000);
  }
  // case-insensitive lookup
  {
    EXPECT(pso::find_builtin_profile("RELIABLE")          != nullptr);
    EXPECT(pso::find_builtin_profile("BeSt_EfFort")       != nullptr);
    EXPECT(pso::find_builtin_profile("nonexistent")       == nullptr);
  }

  std::cout << "qos_profile_test PASS\n";
  return 0;
}
