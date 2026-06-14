// SPDX-License-Identifier: Apache-2.0
#include "pub_sub_open_dds/service.h"

#include "pub_sub_open_dds/runtime.h"

#include <sstream>

namespace pub_sub_open_dds {

Service::Service() : runtime_(make_opendds_runtime()) {}

Service::Service(std::shared_ptr<IRuntime> runtime)
    : runtime_(std::move(runtime)) {
  if (!runtime_) {
    throw Error("Service: runtime must be non-null");
  }
}

Service::~Service() {
  // Never let runtime resources outlive the Service. Swallow errors —
  // throwing from a destructor would be worse than a noisy shutdown.
  try {
    deactivate();
  } catch (...) {
    // intentional: dtor must not throw
  }
}

void Service::require_state(LifecycleState s, const char* op) {
  if (state_ != s) {
    std::ostringstream oss;
    oss << "pub_sub_open_dds::Service: cannot call '" << op
        << "' from state " << static_cast<int>(state_)
        << " (expected " << static_cast<int>(s) << ")";
    throw Error(oss.str());
  }
}

void Service::pre_activate(const ServiceConfig& cfg) {
  require_state(LifecycleState::Created, "pre_activate");
  config_ = cfg;
  runtime_->init(config_);
  state_ = LifecycleState::PreActivated;
}

void Service::activate() {
  require_state(LifecycleState::PreActivated, "activate");
  runtime_->activate();
  // Flush registrations in the order the user submitted them. Each thunk
  // calls runtime_->create_writer / create_reader and binds the typed
  // handle.
  for (auto& reg : pending_) reg.thunk();
  pending_.clear();
  state_ = LifecycleState::Activated;
}

void Service::post_activate() {
  require_state(LifecycleState::Activated, "post_activate");
  // No built-in work; this is the documented user hook for "service is
  // fully up" tasks (initial publishes, registering with other subsystems,
  // etc.).
  state_ = LifecycleState::PostActivated;
}

void Service::deactivate() {
  if (state_ == LifecycleState::Created ||
      state_ == LifecycleState::Deactivated) {
    state_ = LifecycleState::Deactivated;
    return;
  }

  // Shut the runtime down FIRST so any in-flight reader callbacks have
  // drained before we release the typed handles those callbacks closure
  // over (see HANDOFF §6 — the listener may hold a raw pointer back into
  // a Subscriber<T>'s binding).
  runtime_->shutdown();
  handle_keepalive_.clear();
  pending_.clear();
  registered_topics_.clear();

  state_ = LifecycleState::Deactivated;
}

} // namespace pub_sub_open_dds
