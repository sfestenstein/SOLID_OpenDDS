// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pub_sub_open_dds/fwd.h"

#include <memory>
#include <string>

namespace pub_sub_open_dds {

// Façade-owned, OpenDDS-free description of writer/reader QoS. Carries the
// dimensions that matter in practice:
//
//   reliability  : RELIABLE     vs BEST_EFFORT
//   durability   : VOLATILE     vs TRANSIENT_LOCAL   (no PERSISTENT yet)
//   history kind : KEEP_LAST    vs KEEP_ALL
//   history depth: only used with KEEP_LAST
//
// Plus optional knobs (0 / false == "leave at runtime default"):
//
//   deadline_ms          : DEADLINE in milliseconds
//   liveliness_lease_ms  : LIVELINESS lease in milliseconds
//   liveliness_manual    : when true and lease > 0, MANUAL_BY_TOPIC;
//                          otherwise AUTOMATIC
//   max_samples          : RESOURCE_LIMITS max_samples (0 == unlimited)
//   max_instances        : RESOURCE_LIMITS max_instances (0 == unlimited)
//
// Anything else stays at the runtime default. New dimensions can land here
// without breaking existing config files because they get sensible defaults.
struct QosProfile {
  QosProfile() {}
  QosProfile(const std::string& n, bool rel, bool dur, bool ka, int depth)
      : name(n), reliable(rel), durable(dur), keep_all(ka), history_depth(depth) {}

  std::string name           = "default";
  bool        reliable       = true;
  bool        durable        = false;
  bool        keep_all       = false;
  int         history_depth  = 10;

  int         deadline_ms          = 0;
  int         liveliness_lease_ms  = 0;
  bool        liveliness_manual    = false;
  int         max_samples          = 0;
  int         max_instances        = 0;
};

// Opaque QoS value types passed to Service::register_*. They wrap a
// QosProfile today; the runtime adapter is the only code that translates
// them into transport-specific QoS (e.g. DDS::DataWriterQos). Users build
// these via make_writer_qos / make_reader_qos.
//
// Optional escape hatch: callers that have a fully-resolved
// transport-specific QoS struct (e.g. one produced by parsing a DDS-XML
// profile file) can attach it as an opaque "raw" payload. When present,
// the runtime adapter uses the raw payload verbatim and ignores the
// QosProfile. This is how TopicConfig::writer_qos_for / reader_qos_for
// hand off XML-resolved QoS to the OpenDDS runtime without losing fields
// the QosProfile dimensions don't model (deadline, liveliness, resource
// limits, partition, ...). Runtime test doubles typically ignore the raw
// payload and consume only QosProfile.
class WriterQos {
public:
  WriterQos() = default;
  explicit WriterQos(QosProfile p) : profile_(std::move(p)) {}

  const QosProfile&   profile() const noexcept { return profile_; }
  const std::string&  name()    const noexcept { return profile_.name; }

  // Raw transport-specific QoS payload. nullptr unless attached. The
  // pointer's actual type is known only to whichever side attached it
  // (OpenDDS runtime: `const DDS::DataWriterQos*`).
  const void* raw() const noexcept { return raw_.get(); }

  // Attach an opaque payload. The shared_ptr's deleter is responsible for
  // freeing the underlying object. Intended for the facade's internals
  // (TopicConfig); user code does not call this.
  void attach_raw(std::shared_ptr<void> raw) noexcept {
    raw_ = std::move(raw);
  }

private:
  QosProfile            profile_;
  std::shared_ptr<void> raw_;
};

class ReaderQos {
public:
  ReaderQos() = default;
  explicit ReaderQos(QosProfile p) : profile_(std::move(p)) {}

  const QosProfile&   profile() const noexcept { return profile_; }
  const std::string&  name()    const noexcept { return profile_.name; }

  const void* raw() const noexcept { return raw_.get(); }
  void attach_raw(std::shared_ptr<void> raw) noexcept {
    raw_ = std::move(raw);
  }

private:
  QosProfile            profile_;
  std::shared_ptr<void> raw_;
};

inline WriterQos make_writer_qos(const QosProfile& p) { return WriterQos(p); }
inline ReaderQos make_reader_qos(const QosProfile& p) { return ReaderQos(p); }

// ---- built-in named profiles --------------------------------------------
//
// Same set the previous iteration shipped. Topic-config INI files reference
// these by name.
namespace qos {

const QosProfile& best_effort();        // BE  / volatile        / keep_last 1
const QosProfile& reliable();           // REL / volatile        / keep_last 10
const QosProfile& reliable_transient(); // REL / transient_local / keep_last 10
const QosProfile& event_bus();          // REL / volatile        / keep_all
const QosProfile& latched();            // REL / transient_local / keep_last 1
const QosProfile& streaming();          // BE  / volatile        / keep_last 1
const QosProfile& persistent();         // REL / transient_local / keep_all
const QosProfile& heartbeat();          // BE  / volatile        / keep_last 1
                                        //   + deadline 3000 ms
const QosProfile& critical();           // REL / volatile        / keep_all
                                        //   + MANUAL_BY_TOPIC liveliness, bounded

} // namespace qos

// Case-insensitive built-in profile lookup. Returns nullptr on miss; the
// caller is expected to fall back to a default.
const QosProfile* find_builtin_profile(const std::string& name);

} // namespace pub_sub_open_dds
