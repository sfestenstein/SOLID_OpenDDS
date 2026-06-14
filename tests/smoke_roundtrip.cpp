// SPDX-License-Identifier: Apache-2.0
//
// End-to-end smoke test: register a publisher and a subscriber on the
// same topic, push N samples, assert they all arrive. Parameterised over
// runtime — the same code runs once with the OpenDDS runtime (real RTPS,
// the original smoke entry) and once with the in-memory runtime (no DDS,
// no env required, milliseconds). The two runs together validate the
// in-memory fake against the real implementation.
//
// Runtime selection: first positional arg is "opendds" (default) or
// "inmemory". Any subsequent OpenDDS args (e.g. `-DCPSConfigFile rtps.ini`)
// are forwarded to the OpenDDS runtime via ServiceConfig::runtime_args.
//
// No test framework: exits non-zero on failure so CTest catches it.

#include "pub_sub_open_dds_generated/PingPubSub.h"

#include "pub_sub_open_dds/runtime.h"
#include "pub_sub_open_dds/service.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {
constexpr int  N_SAMPLES = 5;
constexpr auto RECV_WAIT = std::chrono::seconds(10);
} // namespace

int main(int argc, char* argv[]) {
  using namespace pub_sub_open_dds;

  // First positional arg picks the runtime; remaining args are forwarded
  // to the runtime (the OpenDDS runtime needs `-DCPSConfigFile rtps.ini`).
  std::string runtime_kind = "opendds";
  std::vector<std::string> runtime_args;
  if (argc > 1) {
    std::string first = argv[1];
    if (first == "opendds" || first == "inmemory") {
      runtime_kind = std::move(first);
      for (int i = 2; i < argc; ++i) runtime_args.emplace_back(argv[i]);
    } else {
      for (int i = 1; i < argc; ++i) runtime_args.emplace_back(argv[i]);
    }
  }

  std::shared_ptr<IRuntime> runtime =
      (runtime_kind == "inmemory") ? make_in_memory_runtime()
                                    : make_opendds_runtime();

  try {
    std::atomic<int> received{0};

    Service svc(runtime);
    ServiceConfig cfg;
    cfg.domain_id    = 42;
    cfg.runtime_args = runtime_args;
    svc.pre_activate(cfg);

    svc.subscribe<Smoke::Ping>(
        "smoke_topic",
        [&received](const Smoke::Ping& m) {
          (void)m;
          received.fetch_add(1, std::memory_order_relaxed);
        });

    svc.post_activate();

    Smoke::Ping m;
    m.id(7);
    m.text("ping");
    for (int i = 0; i < N_SAMPLES; ++i) {
      m.counter(i);
      const auto rc = svc.publish("smoke_topic", m);
      if (rc != WriteResult::Ok) {
        std::cerr << "smoke[" << runtime_kind << "]: write failed at i=" << i
                  << " (WriteResult=" << static_cast<int>(rc) << ")\n";
        return 3;
      }
    }

    const auto deadline = std::chrono::steady_clock::now() + RECV_WAIT;
    while (received.load() < N_SAMPLES &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    svc.deactivate();

    const int got = received.load();
    if (got != N_SAMPLES) {
      std::cerr << "smoke[" << runtime_kind << "]: expected " << N_SAMPLES
                << " samples, got " << got << "\n";
      return 4;
    }
    std::cout << "smoke[" << runtime_kind << "]: ok (" << got << " samples)\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "smoke[" << runtime_kind << "]: exception: " << e.what() << "\n";
    return 1;
  }
}
