// SPDX-License-Identifier: Apache-2.0
//
// Lifecycle state-machine test. Drives Service against a tiny no-op
// IRuntime so the lifecycle assertions don't depend on OpenDDS or on the
// in-memory bus.
//
// Framework-less: each `expect_*` helper exits non-zero on failure so
// CTest catches it; success prints a single PASS line.

#include "pub_sub_open_dds/runtime.h"
#include "pub_sub_open_dds/service.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace pso = pub_sub_open_dds;

namespace {

// No-op runtime that records lifecycle calls so we can assert ordering.
class RecordingRuntime final : public pso::IRuntime {
public:
  int init_calls = 0, activate_calls = 0, shutdown_calls = 0;

  void init(const pso::ServiceConfig&) override { ++init_calls; }
  void activate() override                       { ++activate_calls; }
  void shutdown() override                       { ++shutdown_calls; }

  std::shared_ptr<pso::detail::TypedWriterBinding> create_writer(
      const std::string&, const pso::detail::TypeAdapter&,
      const pso::WriterQos&) override { return nullptr; }

  std::shared_ptr<pso::detail::TypedReaderBinding> create_reader(
      const std::string&, const pso::detail::TypeAdapter&,
      const pso::ReaderQos&) override { return nullptr; }
};

void fail(const std::string& msg) {
  std::cerr << "lifecycle_test FAIL: " << msg << "\n";
  std::exit(2);
}

#define EXPECT(cond) do { if (!(cond)) fail(std::string(__FILE__ ":") + std::to_string(__LINE__) + " expected " #cond); } while (0)

template <class F>
bool throws_error(F&& f) {
  try { f(); }
  catch (const pso::Error&) { return true; }
  catch (...) { return false; }
  return false;
}

} // namespace

int main() {
  // ---- happy path lifecycle ---------------------------------------------
  {
    auto rt = std::make_shared<RecordingRuntime>();
    pso::Service svc(rt);
    EXPECT(svc.state() == pso::LifecycleState::Created);

    svc.pre_activate({});
    EXPECT(svc.state() == pso::LifecycleState::PreActivated);
    EXPECT(rt->init_calls == 1);

    svc.activate();
    EXPECT(svc.state() == pso::LifecycleState::Activated);
    EXPECT(rt->activate_calls == 1);

    svc.post_activate();
    EXPECT(svc.state() == pso::LifecycleState::PostActivated);

    svc.deactivate();
    EXPECT(svc.state() == pso::LifecycleState::Deactivated);
    EXPECT(rt->shutdown_calls == 1);
  }

  // ---- out-of-order operations throw ------------------------------------
  {
    auto rt = std::make_shared<RecordingRuntime>();
    pso::Service svc(rt);
    EXPECT(throws_error([&]{ svc.activate(); }));
    EXPECT(throws_error([&]{ svc.post_activate(); }));
    svc.pre_activate({});
    EXPECT(throws_error([&]{ svc.pre_activate({}); }));
    EXPECT(throws_error([&]{ svc.post_activate(); }));
    svc.activate();
    EXPECT(throws_error([&]{ svc.activate(); }));
    EXPECT(throws_error([&]{ svc.pre_activate({}); }));
  }

  // ---- destructor calls deactivate even if user forgot ------------------
  {
    auto rt = std::make_shared<RecordingRuntime>();
    {
      pso::Service svc(rt);
      svc.pre_activate({});
      svc.activate();
      // no explicit deactivate — destructor handles it
    }
    EXPECT(rt->shutdown_calls == 1);
  }

  // ---- deactivate from Created is a no-op (and transitions terminally) --
  {
    auto rt = std::make_shared<RecordingRuntime>();
    pso::Service svc(rt);
    svc.deactivate();
    EXPECT(svc.state() == pso::LifecycleState::Deactivated);
    EXPECT(rt->init_calls == 0);
    EXPECT(rt->shutdown_calls == 0);
  }

  // ---- registering for an unknown user type throws a clear Error --------
  {
    struct UnregisteredType {};
    auto rt = std::make_shared<RecordingRuntime>();
    pso::Service svc(rt);
    svc.pre_activate({});
    try {
      svc.register_publisher<UnregisteredType>("nope");
      fail("register_publisher of unregistered type did not throw");
    } catch (const pso::Error& e) {
      const std::string msg = e.what();
      EXPECT(msg.find("no TypeAdapter") != std::string::npos);
      EXPECT(msg.find("pub_sub_open_dds_generate_bindings") != std::string::npos);
    }
  }

  // ---- null runtime is rejected ----------------------------------------
  {
    EXPECT(throws_error([]{ pso::Service svc(nullptr); }));
  }

  std::cout << "lifecycle_test PASS\n";
  return 0;
}
