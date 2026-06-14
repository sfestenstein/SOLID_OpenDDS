// SPDX-License-Identifier: Apache-2.0
//
// XML-QoS regression test. Locks in the fix for the bug where
// TopicConfig::{writer,reader}_qos_for(..., xml:<profile>) silently
// produced uninitialised garbage in QoS fields the XML did not mention
// (deadline, liveliness, resource_limits, ...), which made OpenDDS-side
// reader/writer matching fail and broke the radar demo.
//
// What we check, against the real OpenDDS::DCPS::QOS_XML_Loader:
//   * Profiles named in the XML resolve (no fallback warning).
//   * The opaque `raw()` payload exists and points at a fully-populated
//     DDS::DataWriterQos / DataReaderQos — specifically, fields the XML
//     does NOT mention contain the canonical DDS defaults from
//     TheServiceParticipant->initial_*Qos(), not garbage.
//   * The headline dimensions in the QosProfile mirror agree with the
//     raw payload (sanity check that the two views are consistent).
//
// Requires the OpenDDS environment (DDS_ROOT, runtime libs). CTest passes
// `-DCPSConfigFile rtps.ini` for transport init.

#include "pub_sub_open_dds/qos.h"
#include "pub_sub_open_dds/runtime.h"
#include "pub_sub_open_dds/service.h"
#include "pub_sub_open_dds/topic_config.h"

#include <dds/DCPS/Service_Participant.h>
#include <dds/DdsDcpsPublicationC.h>
#include <dds/DdsDcpsSubscriptionC.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace pso = pub_sub_open_dds;

namespace {

void fail(const std::string& m) {
  std::cerr << "xml_qos_test FAIL: " << m << "\n";
  std::exit(2);
}

#define EXPECT(cond) do { if (!(cond)) fail(std::string(__FILE__ ":") + std::to_string(__LINE__) + " expected " #cond); } while (0)

// XML loader needs to find /opt/OpenDDS/docs/schema/dds_qos.xsd via DDS_ROOT.
// CTest source-sets the env via setenv.sh before running, but if a developer
// runs the binary directly we surface a clear error.
void require_dds_env() {
  if (!std::getenv("DDS_ROOT")) {
    fail("DDS_ROOT not set; source /opt/OpenDDS/setenv.sh first");
  }
}

} // namespace

int main(int argc, char* argv[]) {
  require_dds_env();

  // Bring up a Service so TheServiceParticipant is initialised — the XML
  // loader reads canonical defaults from it.
  pso::Service svc;
  pso::ServiceConfig cfg;
  for (int i = 1; i < argc; ++i) cfg.runtime_args.emplace_back(argv[i]);
  svc.pre_activate(cfg);

  const std::filesystem::path xml_path =
      std::filesystem::current_path() / "radar_qos.xml";
  if (!std::filesystem::exists(xml_path)) {
    fail("radar_qos.xml not next to the test binary (cwd=" +
         std::filesystem::current_path().string() + ")");
  }

  auto tc = pso::TopicConfig::load_from_string(
      "ComponentStatus = xml:ComponentStatusDurable\n"
      "RadarTrack      = xml:RadarTrackStreaming\n");
  tc.use_xml_qos_file(xml_path.string());

  // ---- ComponentStatus: RELIABLE + TRANSIENT_LOCAL + KEEP_LAST 10 -----
  {
    auto wq = tc.writer_qos_for("ComponentStatus", pso::qos::best_effort());
    EXPECT(wq.profile().reliable);
    EXPECT(wq.profile().durable);
    EXPECT(!wq.profile().keep_all);
    EXPECT(wq.profile().history_depth == 10);

    const void* raw = wq.raw();
    EXPECT(raw != nullptr);
    const auto& dwqos = *static_cast<const DDS::DataWriterQos*>(raw);
    EXPECT(dwqos.reliability.kind == DDS::RELIABLE_RELIABILITY_QOS);
    EXPECT(dwqos.durability.kind  == DDS::TRANSIENT_LOCAL_DURABILITY_QOS);
    EXPECT(dwqos.history.kind     == DDS::KEEP_LAST_HISTORY_QOS);
    EXPECT(dwqos.history.depth    == 10);

    // Fields the XML doesn't mention must equal the canonical defaults,
    // not the garbage the bug used to produce (deadline ≈ 600 G ms).
    const auto& defaults = TheServiceParticipant->initial_DataWriterQos();
    EXPECT(dwqos.deadline.period.sec     == defaults.deadline.period.sec);
    EXPECT(dwqos.deadline.period.nanosec == defaults.deadline.period.nanosec);
    EXPECT(dwqos.liveliness.kind         == defaults.liveliness.kind);
    EXPECT(dwqos.liveliness.lease_duration.sec
           == defaults.liveliness.lease_duration.sec);
    EXPECT(dwqos.liveliness.lease_duration.nanosec
           == defaults.liveliness.lease_duration.nanosec);
    EXPECT(dwqos.resource_limits.max_samples
           == defaults.resource_limits.max_samples);
    EXPECT(dwqos.resource_limits.max_instances
           == defaults.resource_limits.max_instances);
  }

  // ---- RadarTrack: BEST_EFFORT + VOLATILE + KEEP_LAST 1 ---------------
  {
    auto rq = tc.reader_qos_for("RadarTrack", pso::qos::reliable());
    EXPECT(!rq.profile().reliable);
    EXPECT(!rq.profile().durable);
    EXPECT(rq.profile().history_depth == 1);

    const void* raw = rq.raw();
    EXPECT(raw != nullptr);
    const auto& drqos = *static_cast<const DDS::DataReaderQos*>(raw);
    EXPECT(drqos.reliability.kind == DDS::BEST_EFFORT_RELIABILITY_QOS);
    EXPECT(drqos.durability.kind  == DDS::VOLATILE_DURABILITY_QOS);
    EXPECT(drqos.history.kind     == DDS::KEEP_LAST_HISTORY_QOS);
    EXPECT(drqos.history.depth    == 1);

    const auto& defaults = TheServiceParticipant->initial_DataReaderQos();
    EXPECT(drqos.deadline.period.sec     == defaults.deadline.period.sec);
    EXPECT(drqos.deadline.period.nanosec == defaults.deadline.period.nanosec);
    EXPECT(drqos.liveliness.lease_duration.sec
           == defaults.liveliness.lease_duration.sec);
    EXPECT(drqos.resource_limits.max_samples
           == defaults.resource_limits.max_samples);
  }

  // ---- Built-in profile path must NOT attach raw payload --------------
  // (so the OpenDDS adapter still hits its participant-default path).
  {
    auto built = pso::TopicConfig::load_from_string("T = reliable\n");
    auto wq    = built.writer_qos_for("T", pso::qos::best_effort());
    EXPECT(wq.raw() == nullptr);
    EXPECT(wq.profile().reliable);
  }

  svc.deactivate();
  std::cout << "xml_qos_test PASS\n";
  return 0;
}
