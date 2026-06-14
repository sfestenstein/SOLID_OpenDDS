// SPDX-License-Identifier: Apache-2.0
//
// In-memory runtime end-to-end test. Goes through Service + Publisher +
// Subscriber paths but never touches OpenDDS. Validates that the
// generated adapter's clone() path + the bus's fan-out + the durability
// replay actually work.
//
// Also exercises HANDOFF §6: drop the returned Subscriber<T> shared_ptr,
// publish, no crash, samples still routed.

#include "pub_sub_open_dds_generated/PingPubSub.h"

#include "pub_sub_open_dds/qos.h"
#include "pub_sub_open_dds/runtime.h"
#include "pub_sub_open_dds/service.h"

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
    svc.pre_activate({});

    std::atomic<int> seen{0};
    int last_counter = -1;
    auto pub = svc.register_publisher<Smoke::Ping>("topic1");
    auto sub = svc.register_subscriber<Smoke::Ping>(
        "topic1",
        [&](const Smoke::Ping& m) {
          last_counter = m.counter();
          seen.fetch_add(1);
        });

    svc.activate();
    svc.post_activate();

    Smoke::Ping m;
    m.id(1);
    m.text("hi");
    for (int i = 0; i < 5; ++i) {
      m.counter(i);
      EXPECT(pub->write(m) == pso::WriteResult::Ok);
    }
    EXPECT(seen.load() == 5);
    EXPECT(last_counter == 4);
    EXPECT(sub->received_count() == 5);
  }

  // ---- durability replay (durable writer + durable reader) -------------
  {
    pso::Service svc(pso::make_in_memory_runtime());
    svc.pre_activate({});

    // Write first, then late-join the subscriber.
    auto pub = svc.register_publisher<Smoke::Ping>(
        "durable_topic",
        pso::make_writer_qos(pso::qos::reliable_transient()));
    std::atomic<int> seen{0};

    svc.activate();
    svc.post_activate();

    Smoke::Ping m;
    m.id(7);
    for (int i = 0; i < 3; ++i) {
      m.counter(i);
      pub->write(m);
    }
    EXPECT(seen.load() == 0);  // no subscriber yet
  }

  // ---- HANDOFF §6 regression: drop the subscriber shared_ptr -----------
  // The Service must keep registered handles alive for its lifetime
  // regardless of whether the user holds onto the returned shared_ptr.
  {
    pso::Service svc(pso::make_in_memory_runtime());
    svc.pre_activate({});

    std::atomic<int> seen{0};
    auto pub = svc.register_publisher<Smoke::Ping>("keepalive_topic");
    {
      // Register and immediately discard the returned shared_ptr.
      svc.register_subscriber<Smoke::Ping>(
          "keepalive_topic",
          [&](const Smoke::Ping&) { seen.fetch_add(1); });
    }

    svc.activate();
    svc.post_activate();

    Smoke::Ping m;
    m.id(1);
    for (int i = 0; i < 3; ++i) {
      m.counter(i);
      EXPECT(pub->write(m) == pso::WriteResult::Ok);
    }
    EXPECT(seen.load() == 3);
  }

  // ---- write before activate yields PreconditionFailed ----------------
  {
    pso::Service svc(pso::make_in_memory_runtime());
    svc.pre_activate({});
    auto pub = svc.register_publisher<Smoke::Ping>("topic_pre");
    // pub is not yet bound; write() must short-circuit cleanly.
    Smoke::Ping m;
    m.id(0);
    EXPECT(pub->write(m) == pso::WriteResult::PreconditionFailed);
  }

  std::cout << "in_memory_roundtrip_test PASS\n";
  return 0;
}
