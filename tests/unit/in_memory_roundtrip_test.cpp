// SPDX-License-Identifier: Apache-2.0
//
// In-memory runtime end-to-end test. Goes through the service-centric
// publish/subscribe path but never touches OpenDDS. Validates that the
// generated adapter's clone() path + the bus's fan-out + the durability
// replay actually work.

#include "pub_sub_open_dds_generated/PingPubSub.h"

#include "pub_sub_open_dds/qos.h"
#include "pub_sub_open_dds/runtime.h"
#include "pub_sub_open_dds/service.h"
#include "pub_sub_open_dds/topic_config.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>

namespace pso = pub_sub_open_dds;

namespace {

void fail(const std::string& m) {
  std::cerr << "in_memory_roundtrip_test FAIL: " << m << "\n";
  std::exit(2);
}

#define EXPECT(cond) do { if (!(cond)) fail(std::string(__FILE__ ":") + std::to_string(__LINE__) + " expected " #cond); } while (0)

} // namespace

int main() {
  // ---- basic fan-out ---------------------------------------------------
  {
    pso::Service svc(pso::make_in_memory_runtime());
    auto topic_cfg = pso::TopicConfig::load_from_string(
        "topic1 = reliable\n");
    svc.pre_activate({}, std::move(topic_cfg));

    std::atomic<int> seen{0};
    int last_counter = -1;
    svc.subscribe<Smoke::Ping>(
        "topic1",
        [&](const Smoke::Ping& m) {
          last_counter = m.counter();
          seen.fetch_add(1);
        });

    svc.post_activate();

    Smoke::Ping m;
    m.id(1);
    m.text("hi");
    for (int i = 0; i < 5; ++i) {
      m.counter(i);
      EXPECT(svc.publish("topic1", m) == pso::WriteResult::Ok);
    }
    EXPECT(seen.load() == 5);
    EXPECT(last_counter == 4);
  }

  // ---- durability replay (durable writer + durable reader) -------------
  {
    pso::Service svc(pso::make_in_memory_runtime());
    auto topic_cfg = pso::TopicConfig::load_from_string(
        "durable_topic = reliable_transient\n");
    svc.pre_activate({}, std::move(topic_cfg));

    // Write first, then late-join the subscriber.
    std::atomic<int> seen{0};

    svc.post_activate();

    Smoke::Ping m;
    m.id(7);
    for (int i = 0; i < 3; ++i) {
      m.counter(i);
      EXPECT(svc.publish("durable_topic", m) == pso::WriteResult::Ok);
    }
    EXPECT(seen.load() == 0);  // no subscriber yet

    svc.subscribe<Smoke::Ping>(
        "durable_topic",
        [&](const Smoke::Ping&) { seen.fetch_add(1); });
    EXPECT(seen.load() == 3);
  }

  // ---- HANDOFF §6 regression: drop the subscriber shared_ptr -----------
  // The Service must keep registered handles alive for its lifetime
  // regardless of whether the user holds onto the returned shared_ptr.
  {
    pso::Service svc(pso::make_in_memory_runtime());
    auto topic_cfg = pso::TopicConfig::load_from_string(
        "keepalive_topic = reliable\n");
    svc.pre_activate({}, std::move(topic_cfg));

    std::atomic<int> seen{0};
    svc.subscribe<Smoke::Ping>(
        "keepalive_topic",
        [&](const Smoke::Ping&) { seen.fetch_add(1); });

    svc.post_activate();

    Smoke::Ping m;
    m.id(1);
    for (int i = 0; i < 3; ++i) {
      m.counter(i);
      EXPECT(svc.publish("keepalive_topic", m) == pso::WriteResult::Ok);
    }
    EXPECT(seen.load() == 3);
  }

  // ---- publish before post_activate is rejected -----------------------
  {
    pso::Service svc(pso::make_in_memory_runtime());
    auto topic_cfg = pso::TopicConfig::load_from_string(
        "topic_pre = reliable\n");
    svc.pre_activate({}, std::move(topic_cfg));
    Smoke::Ping m;
    m.id(0);
    bool threw = false;
    try {
      (void)svc.publish("topic_pre", m);
    } catch (const std::runtime_error&) {
      threw = true;
    }
    EXPECT(threw);
  }

  std::cout << "in_memory_roundtrip_test PASS\n";
  return 0;
}
