// SPDX-License-Identifier: Apache-2.0
//
// InMemoryRuntime — process-local topic bus that implements IRuntime
// without any DDS / OpenDDS / ACE plumbing. Tests use this to exercise
// Service / Publisher<T> / Subscriber<T> without standing up RTPS.
//
// Fidelity (per the implementation plan, option B):
//
//   * Topic dedup by name (incompatible type on the same topic throws).
//   * Multiple writers and readers per topic.
//   * Delivery is synchronous on write(): every currently-matched reader
//     is invoked before write() returns. This is enough to drive the test
//     suite; production fidelity is OpenDDS's job.
//   * BEST_EFFORT vs RELIABLE: identical delivery in this fake (there is
//     no transport to lose samples in). The qos.reliable bit is recorded
//     so future tests can assert on it.
//   * Durability + history:
//       - durable + KEEP_LAST(N): newest N samples replayed on match.
//       - durable + KEEP_ALL    : full history replayed on match.
//       - !durable              : nothing replayed (samples discarded
//                                  after fan-out).
//   * Liveliness / deadline / max_samples / max_instances: ignored
//     (recorded but not enforced).
//
// Not modeled (deferred): partition, ownership, latency_budget, true
// PERSISTENT durability, async delivery, threading.

#include "pub_sub_open_dds/runtime.h"

#include "pub_sub_open_dds/detail/data_adapter.h"
#include "pub_sub_open_dds/detail/typed_binding.h"
#include "pub_sub_open_dds/error.h"
#include "pub_sub_open_dds/qos.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pub_sub_open_dds {

namespace {

class InMemoryRuntime;
class InMemoryReaderBinding;
class InMemoryWriterBinding;

// One sample held in a bus's durability replay queue. The void* is an
// owning T-typed copy made via TypeAdapter::clone().
struct StoredSample {
  std::shared_ptr<void> data;
};

// Per-topic state. Lives in a shared_ptr so writer / reader bindings can
// keep it alive past Runtime::shutdown if they wanted to (they don't, in
// practice).
struct Bus {
  std::string                                       type_name;
  std::vector<std::weak_ptr<InMemoryReaderBinding>> readers;
  std::deque<StoredSample>                          history;
  bool                                              history_keep_all = false;
  int                                               history_depth    = 0;
};

// ---- writer binding ---------------------------------------------------

class InMemoryWriterBinding final : public detail::TypedWriterBinding {
public:
  InMemoryWriterBinding(InMemoryRuntime& rt,
                        std::shared_ptr<Bus> bus,
                        const detail::TypeAdapter& adapter,
                        QosProfile qos)
      : rt_(rt),
        bus_(std::move(bus)),
        adapter_(adapter),
        qos_(std::move(qos)) {}

  WriteResult write_erased(const void* sample) override;
  bool        wait_for_subscribers(std::chrono::milliseconds /*timeout*/,
                                    int min_count) override;

private:
  InMemoryRuntime&            rt_;
  std::shared_ptr<Bus>        bus_;
  const detail::TypeAdapter&  adapter_;
  QosProfile                  qos_;
};

// ---- reader binding ---------------------------------------------------
//
// Carries an optional pending-replay deque populated at create time when
// the bus has durable history. The first set_on_sample() drains it
// through the newly installed thunk so the user code never misses the
// replayed samples (Subscriber::install_thunk runs immediately after the
// binding is returned, so this happens in the same lifecycle step).
class InMemoryReaderBinding final : public detail::TypedReaderBinding {
public:
  InMemoryReaderBinding(InMemoryRuntime& rt,
                        std::shared_ptr<Bus> bus,
                        QosProfile qos)
      : rt_(rt), bus_(std::move(bus)), qos_(std::move(qos)) {}

  ~InMemoryReaderBinding() override;

  void set_on_sample(std::function<void(const void*)> thunk) override;

  // Called by InMemoryRuntime::create_reader to stash durable history
  // for the upcoming set_on_sample call to drain.
  void prime_replay(std::deque<StoredSample> samples) {
    std::lock_guard<std::mutex> lk(mtx_);
    pending_replay_ = std::move(samples);
  }

  // Called by the bus's fan-out loop for each live sample.
  void deliver(const void* sample) {
    std::function<void(const void*)> cb;
    {
      std::lock_guard<std::mutex> lk(mtx_);
      cb = on_sample_;
    }
    counter_.fetch_add(1, std::memory_order_relaxed);
    if (cb) cb(sample);
  }

  const QosProfile& qos() const { return qos_; }

private:
  InMemoryRuntime&                     rt_;
  std::shared_ptr<Bus>                 bus_;
  QosProfile                           qos_;
  std::mutex                           mtx_;
  std::function<void(const void*)>     on_sample_;
  std::deque<StoredSample>             pending_replay_;
};

// ---- runtime ----------------------------------------------------------

class InMemoryRuntime final : public IRuntime {
public:
  void init(const ServiceConfig& /*cfg*/) override {
    require(State::Constructed, "init");
    state_ = State::Initialised;
  }

  void activate() override {
    require(State::Initialised, "activate");
    state_ = State::Activated;
  }

  void shutdown() override {
    if (state_ == State::Constructed || state_ == State::Shutdown) {
      state_ = State::Shutdown;
      return;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    buses_.clear();
    state_ = State::Shutdown;
  }

  std::shared_ptr<detail::TypedWriterBinding> create_writer(
      const std::string& topic, const detail::TypeAdapter& adapter,
      const WriterQos& qos) override {
    require(State::Activated, "create_writer");
    auto bus = get_or_create_bus(topic, adapter.type_name());
    {
      std::lock_guard<std::mutex> lk(mtx_);
      if (qos.profile().durable) {
        if (qos.profile().keep_all) {
          bus->history_keep_all = true;
        } else if (!bus->history_keep_all) {
          bus->history_depth =
              std::max(bus->history_depth, qos.profile().history_depth);
        }
      }
    }
    return std::make_shared<InMemoryWriterBinding>(*this, std::move(bus),
                                                    adapter, qos.profile());
  }

  std::shared_ptr<detail::TypedReaderBinding> create_reader(
      const std::string& topic, const detail::TypeAdapter& adapter,
      const ReaderQos& qos) override {
    require(State::Activated, "create_reader");
    auto bus = get_or_create_bus(topic, adapter.type_name());
    auto b = std::make_shared<InMemoryReaderBinding>(*this, bus, qos.profile());
    std::deque<StoredSample> replay;
    {
      std::lock_guard<std::mutex> lk(mtx_);
      bus->readers.push_back(b);
      if (qos.profile().durable) replay = bus->history;  // copy
    }
    if (!replay.empty()) b->prime_replay(std::move(replay));
    return b;
  }

  // Called by InMemoryWriterBinding::write_erased.
  void publish(Bus& bus, const detail::TypeAdapter& adapter,
               const void* sample, bool writer_durable) {
    std::vector<std::shared_ptr<InMemoryReaderBinding>> live;
    {
      std::lock_guard<std::mutex> lk(mtx_);
      // Compact dead readers.
      bus.readers.erase(std::remove_if(bus.readers.begin(), bus.readers.end(),
                                       [](const std::weak_ptr<InMemoryReaderBinding>& w) {
                                         return w.expired();
                                       }),
                        bus.readers.end());
      live.reserve(bus.readers.size());
      for (auto& w : bus.readers) {
        if (auto sp = w.lock()) live.push_back(std::move(sp));
      }
      // Store in history for late joiners if this writer is durable AND
      // the bus has at least one durable writer's history shape.
      if (writer_durable && (bus.history_keep_all || bus.history_depth > 0)) {
        bus.history.push_back(StoredSample{adapter.clone(sample)});
        if (!bus.history_keep_all) {
          while (static_cast<int>(bus.history.size()) > bus.history_depth) {
            bus.history.pop_front();
          }
        }
      }
    }
    // Deliver outside the lock so a user callback can publish back into
    // the runtime without deadlocking on its own reentry.
    for (auto& r : live) r->deliver(sample);
  }

  long reader_count(Bus& bus) {
    std::lock_guard<std::mutex> lk(mtx_);
    long n = 0;
    for (auto& w : bus.readers) if (!w.expired()) ++n;
    return n;
  }

  void unregister_reader(Bus& bus, InMemoryReaderBinding* r) {
    std::lock_guard<std::mutex> lk(mtx_);
    bus.readers.erase(std::remove_if(bus.readers.begin(), bus.readers.end(),
                                     [r](const std::weak_ptr<InMemoryReaderBinding>& w) {
                                       auto sp = w.lock();
                                       return !sp || sp.get() == r;
                                     }),
                      bus.readers.end());
  }

private:
  enum class State { Constructed, Initialised, Activated, Shutdown };

  void require(State expected, const char* op) {
    if (state_ != expected) {
      throw Error(std::string("InMemoryRuntime: cannot ") + op
                  + " from state " + std::to_string(static_cast<int>(state_)));
    }
  }

  std::shared_ptr<Bus> get_or_create_bus(const std::string& topic,
                                          const std::string& type_name) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = buses_.find(topic);
    if (it != buses_.end()) {
      if (it->second->type_name != type_name) {
        throw Error("InMemoryRuntime: topic '" + topic
                    + "' already registered with type '"
                    + it->second->type_name
                    + "', cannot reuse for type '" + type_name + "'");
      }
      return it->second;
    }
    auto bus = std::make_shared<Bus>();
    bus->type_name = type_name;
    buses_.emplace(topic, bus);
    return bus;
  }

  State                                                 state_ = State::Constructed;
  std::mutex                                            mtx_;
  std::unordered_map<std::string, std::shared_ptr<Bus>> buses_;
};

// ---- writer / reader members that need the runtime defined -----------

WriteResult InMemoryWriterBinding::write_erased(const void* sample) {
  rt_.publish(*bus_, adapter_, sample, qos_.durable);
  return WriteResult::Ok;
}

bool InMemoryWriterBinding::wait_for_subscribers(
    std::chrono::milliseconds /*timeout*/, int min_count) {
  return rt_.reader_count(*bus_) >= min_count;
}

InMemoryReaderBinding::~InMemoryReaderBinding() {
  rt_.unregister_reader(*bus_, this);
}

void InMemoryReaderBinding::set_on_sample(std::function<void(const void*)> thunk) {
  std::deque<StoredSample> replay;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    on_sample_ = std::move(thunk);
    replay.swap(pending_replay_);
  }
  // Drain durability replay through the newly installed thunk. Bumps the
  // receive counter for each replayed sample.
  for (auto& s : replay) {
    counter_.fetch_add(1, std::memory_order_relaxed);
    if (on_sample_) on_sample_(s.data.get());
  }
}

} // namespace

std::shared_ptr<IRuntime> make_in_memory_runtime() {
  return std::make_shared<InMemoryRuntime>();
}

} // namespace pub_sub_open_dds
