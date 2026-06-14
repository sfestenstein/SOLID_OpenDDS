// SPDX-License-Identifier: Apache-2.0
//
// WorkstationApp — operator console for the notional radar.
//
// Subscribes:
//   - ComponentStatus  (sensor heartbeats + replies)
//   - RadarTrack       (live tracks)
//   - CommandStatus    (sensor reply to our Commands)
//   - SystemAlarm      (latched alarms)
//   - RawIQSample      (high-rate stream; logged 1-in-N for legibility)
//   - OperatorChat     (echoes from other workstations — and this one)
//
// Publishes:
//   - Command                 (rotates through start/stop/reset/etc every few seconds)
//   - ComponentStatusRequest  (every few command cycles)
//   - TrackingCue             ("look here" hint every few command cycles)
//   - OperatorAuditLog        (one entry per outbound Command)
//   - OperatorChat            (heartbeat chat message every ~9 s)
//
// As with SensorApp, no dds/* or ace/* header appears anywhere in this
// file. Ctrl-C exits cleanly.

#include "pub_sub_open_dds_generated/ComponentStatusPubSub.h"
#include "pub_sub_open_dds_generated/RadarTrackPubSub.h"
#include "pub_sub_open_dds_generated/CommandPubSub.h"
#include "pub_sub_open_dds_generated/CommandStatusPubSub.h"
#include "pub_sub_open_dds_generated/ComponentStatusRequestPubSub.h"
#include "pub_sub_open_dds_generated/SystemAlarmPubSub.h"
#include "pub_sub_open_dds_generated/OperatorAuditLogPubSub.h"
#include "pub_sub_open_dds_generated/RawIQSamplePubSub.h"
#include "pub_sub_open_dds_generated/TrackingCuePubSub.h"
#include "pub_sub_open_dds_generated/OperatorChatPubSub.h"

#include "pub_sub_open_dds/qos.h"
#include "pub_sub_open_dds/service.h"
#include "pub_sub_open_dds/topic_config.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

constexpr int     DOMAIN_ID         = 43;
const std::string TARGET_SENSOR     = "sensor_alpha";
const std::string OPERATOR_ID       = "ws_operator_1";
const char* const TOPIC_CONFIG_PATH = "workstation_topics.ini";
const char* const QOS_XML_PATH      = "radar_qos.xml";

constexpr int IQ_LOG_EVERY = 40;

std::atomic<bool> g_running{true};
std::mutex        g_cout_mtx;

void handle_sigint(int) { g_running.store(false); }

uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

const char* to_string(RadarSystem::ComponentState s) {
  switch (s) {
    case RadarSystem::ComponentState::COMPONENT_OFFLINE:      return "OFFLINE";
    case RadarSystem::ComponentState::COMPONENT_INITIALIZING: return "INIT";
    case RadarSystem::ComponentState::COMPONENT_ONLINE:       return "ONLINE";
    case RadarSystem::ComponentState::COMPONENT_DEGRADED:     return "DEGRADED";
    case RadarSystem::ComponentState::COMPONENT_FAULT:        return "FAULT";
  }
  return "?";
}

const char* to_string(RadarSystem::CommandResult r) {
  switch (r) {
    case RadarSystem::CommandResult::CMD_ACCEPTED:  return "ACCEPTED";
    case RadarSystem::CommandResult::CMD_REJECTED:  return "REJECTED";
    case RadarSystem::CommandResult::CMD_COMPLETED: return "COMPLETED";
    case RadarSystem::CommandResult::CMD_FAILED:    return "FAILED";
  }
  return "?";
}

const char* to_string(RadarSystem::AlarmSeverity s) {
  switch (s) {
    case RadarSystem::AlarmSeverity::ALARM_INFO:     return "INFO";
    case RadarSystem::AlarmSeverity::ALARM_WARNING:  return "WARNING";
    case RadarSystem::AlarmSeverity::ALARM_ERROR:    return "ERROR";
    case RadarSystem::AlarmSeverity::ALARM_CRITICAL: return "CRITICAL";
  }
  return "?";
}

const char* to_string(RadarSystem::CommandType t) {
  switch (t) {
    case RadarSystem::CommandType::CMD_START_SCAN:    return "START_SCAN";
    case RadarSystem::CommandType::CMD_STOP_SCAN:     return "STOP_SCAN";
    case RadarSystem::CommandType::CMD_SHUTDOWN:      return "SHUTDOWN";
    case RadarSystem::CommandType::CMD_RESET:         return "RESET";
    case RadarSystem::CommandType::CMD_SET_SCAN_RATE: return "SET_SCAN_RATE";
  }
  return "?";
}

} // namespace

int main(int argc, char* argv[]) {
  using namespace pub_sub_open_dds;
  std::signal(SIGINT, handle_sigint);

  try {
    auto topic_cfg = TopicConfig::load_from_file(TOPIC_CONFIG_PATH);
    topic_cfg.use_xml_qos_file(QOS_XML_PATH);

    const auto wq_for = [&topic_cfg](const std::string& topic) {
      auto q = topic_cfg.writer_qos_for(topic, qos::best_effort());
      std::cout << "[ws] writer topic '" << topic
                << "' -> QoS '" << q.name() << "'\n";
      return q;
    };
    const auto rq_for = [&topic_cfg](const std::string& topic) {
      auto q = topic_cfg.reader_qos_for(topic, qos::best_effort());
      std::cout << "[ws] reader topic '" << topic
                << "' -> QoS '" << q.name() << "'\n";
      return q;
    };

    Service svc;
    ServiceConfig cfg;
    cfg.domain_id = DOMAIN_ID;
    for (int i = 1; i < argc; ++i) cfg.runtime_args.emplace_back(argv[i]);
    svc.pre_activate(cfg);

    auto cmd_pub    = svc.register_publisher<RadarSystem::Command>(
        "Command", wq_for("Command"));
    auto req_pub    = svc.register_publisher<RadarSystem::ComponentStatusRequest>(
        "ComponentStatusRequest", wq_for("ComponentStatusRequest"));
    auto cue_pub    = svc.register_publisher<RadarSystem::TrackingCue>(
        "TrackingCue", wq_for("TrackingCue"));
    auto audit_pub  = svc.register_publisher<RadarSystem::OperatorAuditLog>(
        "OperatorAuditLog", wq_for("OperatorAuditLog"));
    auto chat_pub   = svc.register_publisher<RadarSystem::OperatorChat>(
        "OperatorChat", wq_for("OperatorChat"));

    svc.register_subscriber<RadarSystem::ComponentStatus>(
        "ComponentStatus",
        [](const RadarSystem::ComponentStatus& s) {
          std::lock_guard<std::mutex> lk(g_cout_mtx);
          std::cout << "[ws] status  sensor=" << s.sensor_id()
                    << " comp=" << s.component_id()
                    << " state=" << to_string(s.state())
                    << " msg='" << s.message() << "'\n";
        },
        rq_for("ComponentStatus"));

    svc.register_subscriber<RadarSystem::RadarTrack>(
        "RadarTrack",
        [](const RadarSystem::RadarTrack& t) {
          std::lock_guard<std::mutex> lk(g_cout_mtx);
          std::cout << "[ws] track   sensor=" << t.sensor_id()
                    << " id=" << t.track_id()
                    << " range=" << t.range_m() << "m"
                    << " bearing=" << t.bearing_deg() << "deg"
                    << " speed=" << t.speed_mps() << "m/s\n";
        },
        rq_for("RadarTrack"));

    svc.register_subscriber<RadarSystem::CommandStatus>(
        "CommandStatus",
        [](const RadarSystem::CommandStatus& cs) {
          std::lock_guard<std::mutex> lk(g_cout_mtx);
          std::cout << "[ws] cmdstat cmd=" << cs.command_id()
                    << " sensor=" << cs.sensor_id()
                    << " result=" << to_string(cs.result())
                    << " msg='" << cs.message() << "'\n";
        },
        rq_for("CommandStatus"));

    svc.register_subscriber<RadarSystem::SystemAlarm>(
        "SystemAlarm",
        [](const RadarSystem::SystemAlarm& a) {
          std::lock_guard<std::mutex> lk(g_cout_mtx);
          std::cout << "[ws] ALARM  sensor=" << a.sensor_id()
                    << " id=" << a.alarm_id()
                    << " severity=" << to_string(a.severity())
                    << " comp=" << a.source_component()
                    << " desc='" << a.description()
                    << "' ack='" << a.acknowledged_by() << "'\n";
        },
        rq_for("SystemAlarm"));

    // RawIQSample: track per-beam rate and only log every Nth sample so the
    // console stays readable; the streaming QoS will silently drop samples
    // under load — that's the point.
    static std::atomic<long> g_iq_seen{0};
    svc.register_subscriber<RadarSystem::RawIQSample>(
        "RawIQSample",
        [](const RadarSystem::RawIQSample& iq) {
          const long n = g_iq_seen.fetch_add(1, std::memory_order_relaxed) + 1;
          if (n % IQ_LOG_EVERY != 0) return;
          std::lock_guard<std::mutex> lk(g_cout_mtx);
          std::cout << "[ws] iq     sensor=" << iq.sensor_id()
                    << " beam=" << iq.beam_id()
                    << " seq=" << iq.seq_no()
                    << " freq=" << iq.center_freq_hz()
                    << "Hz (seen " << n << " total)\n";
        },
        rq_for("RawIQSample"));

    svc.register_subscriber<RadarSystem::OperatorChat>(
        "OperatorChat",
        [](const RadarSystem::OperatorChat& c) {
          std::lock_guard<std::mutex> lk(g_cout_mtx);
          std::cout << "[ws] chat   <" << c.operator_id()
                    << "#" << c.seq_no() << "> " << c.text() << "\n";
        },
        rq_for("OperatorChat"));

    svc.activate();
    svc.post_activate();

    {
      std::lock_guard<std::mutex> lk(g_cout_mtx);
      std::cout << "[ws] up on domain " << DOMAIN_ID
                << " target='" << TARGET_SENSOR << "' (Ctrl-C to stop)\n";
    }

    // Brief settle so the very first publish isn't a black hole if the sensor
    // is just starting up.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    const RadarSystem::CommandType rotation[] = {
        RadarSystem::CommandType::CMD_START_SCAN,
        RadarSystem::CommandType::CMD_SET_SCAN_RATE,
        RadarSystem::CommandType::CMD_STOP_SCAN,
        RadarSystem::CommandType::CMD_RESET,
    };
    constexpr size_t rot_n = sizeof(rotation) / sizeof(rotation[0]);

    uint32_t cmd_seq  = 1;
    uint32_t req_seq  = 1;
    uint32_t chat_seq = 1;
    int      tick     = 0;
    while (g_running.load()) {
      RadarSystem::Command c;
      c.target_sensor_id(TARGET_SENSOR);
      c.command_id(cmd_seq);
      c.type(rotation[(cmd_seq - 1) % rot_n]);
      c.parameters(c.type() == RadarSystem::CommandType::CMD_SET_SCAN_RATE
                       ? "rate=2hz" : "");
      c.timestamp_ns(now_ns());
      cmd_pub->write(c);
      {
        std::lock_guard<std::mutex> lk(g_cout_mtx);
        std::cout << "[ws] sent command #" << cmd_seq << "\n";
      }

      // Audit every outbound Command on the event_bus topic.
      RadarSystem::OperatorAuditLog audit;
      audit.operator_id(OPERATOR_ID);
      audit.action(RadarSystem::AuditAction::AUDIT_COMMAND_ISSUED);
      audit.detail(std::string("command #") + std::to_string(cmd_seq)
                   + " type=" + to_string(c.type()));
      audit.timestamp_ns(now_ns());
      audit_pub->write(audit);

      ++cmd_seq;

      // Every third command cycle, poll for full component status.
      if ((tick % 3) == 2) {
        RadarSystem::ComponentStatusRequest r;
        r.target_sensor_id(TARGET_SENSOR);
        r.request_id(req_seq);
        r.component_id_filter("");
        r.timestamp_ns(now_ns());
        req_pub->write(r);
        {
          std::lock_guard<std::mutex> lk(g_cout_mtx);
          std::cout << "[ws] sent component-status request #" << req_seq << "\n";
        }
        ++req_seq;
      }

      // Every other command cycle, drop a tracking cue.
      if ((tick % 2) == 0) {
        RadarSystem::TrackingCue cue;
        cue.target_sensor_id(TARGET_SENSOR);
        cue.issued_by(OPERATOR_ID);
        cue.bearing_deg(static_cast<double>((tick * 30) % 360));
        cue.elevation_deg(5.0);
        cue.range_m(10000.0);
        cue.ownership_strength(10);  // future: exclusive ownership
        cue.timestamp_ns(now_ns());
        cue_pub->write(cue);
        {
          std::lock_guard<std::mutex> lk(g_cout_mtx);
          std::cout << "[ws] sent tracking cue bearing=" << cue.bearing_deg() << "\n";
        }
      }

      // Periodic chat heartbeat (every third cycle).
      if ((tick % 3) == 0) {
        RadarSystem::OperatorChat msg;
        msg.operator_id(OPERATOR_ID);
        msg.seq_no(chat_seq);
        msg.text("[#" + std::to_string(chat_seq) + "] all stations: status nominal");
        msg.timestamp_ns(now_ns());
        chat_pub->write(msg);
        ++chat_seq;
      }
      ++tick;

      for (int i = 0; i < 30 && g_running.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }

    svc.deactivate();
    std::cout << "[ws] shutdown clean\n";
  } catch (const std::exception& e) {
    std::cerr << "[ws] " << e.what() << "\n";
    return 1;
  }
  return 0;
}
