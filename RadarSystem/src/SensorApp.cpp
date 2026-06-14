// SPDX-License-Identifier: Apache-2.0
//
// SensorApp — notional radar sensor built on the pub_sub_open_dds facade.
//
// Publishes:
//   - ComponentStatus  (heartbeat every second + on-demand replies)
//   - RadarTrack       (two rotating tracks every second)
//   - CommandStatus    (one per inbound Command)
//   - SystemAlarm      (rotating, every ~5 s; latched per alarm_id)
//   - RawIQSample      (high-rate, ~10 Hz per beam)
//
// Subscribes:
//   - Command                 (operator action from a workstation)
//   - ComponentStatusRequest  (poll from a workstation)
//   - TrackingCue             ("look here" hint from an operator)
//   - OperatorAuditLog        (observe the operator action stream)
//
// No #include of any dds/* or ace/* header anywhere in this file: the
// pub_sub_open_dds_generated/*PubSub.h wrappers expose only the IDL
// struct definitions plus the service facade. The generated
// <Type>PubSub_adapter.cpp files (compiled into this target by the
// pub_sub_open_dds_generate_bindings helper) are the only TUs that see
// OpenDDS-generated TypeSupportImpl headers.
//
// Ctrl-C exits cleanly.

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

#include "pub_sub_open_dds/service.h"
#include "pub_sub_open_dds/service_bootstrap_config.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

constexpr int      DOMAIN_ID         = 43;
const std::string  SENSOR_ID         = "sensor_alpha";
const char* const  SERVICE_CONFIG_PATH = "sensor_service.ini";

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

const char* to_string(RadarSystem::AlarmSeverity s) {
  switch (s) {
    case RadarSystem::AlarmSeverity::ALARM_INFO:     return "INFO";
    case RadarSystem::AlarmSeverity::ALARM_WARNING:  return "WARNING";
    case RadarSystem::AlarmSeverity::ALARM_ERROR:    return "ERROR";
    case RadarSystem::AlarmSeverity::ALARM_CRITICAL: return "CRITICAL";
  }
  return "?";
}

const char* to_string(RadarSystem::AuditAction a) {
  switch (a) {
    case RadarSystem::AuditAction::AUDIT_COMMAND_ISSUED:      return "COMMAND_ISSUED";
    case RadarSystem::AuditAction::AUDIT_ALARM_ACKNOWLEDGED:  return "ALARM_ACKED";
    case RadarSystem::AuditAction::AUDIT_CONFIG_CHANGE:       return "CONFIG_CHANGE";
    case RadarSystem::AuditAction::AUDIT_LOGIN:               return "LOGIN";
    case RadarSystem::AuditAction::AUDIT_LOGOUT:              return "LOGOUT";
  }
  return "?";
}

void publish_status(pub_sub_open_dds::Service& svc,
                    const std::string& component_id,
                    RadarSystem::ComponentState state,
                    const std::string& msg) {
  RadarSystem::ComponentStatus s;
  s.sensor_id(SENSOR_ID);
  s.component_id(component_id);
  s.state(state);
  s.message(msg);
  s.timestamp_ns(now_ns());
  svc.publish("ComponentStatus", s);
}

} // namespace

int main(int argc, char* argv[]) {
  using namespace pub_sub_open_dds;
  std::signal(SIGINT, handle_sigint);

  try {
    Service svc;
    ServiceBootstrapConfig cfg =
        ServiceBootstrapConfig::load_from_file(SERVICE_CONFIG_PATH);
    if (cfg.domain_id != DOMAIN_ID) {
      std::lock_guard<std::mutex> lk(g_cout_mtx);
      std::cout << "[sensor] warning: expected domain " << DOMAIN_ID
                << " but service config sets " << cfg.domain_id << "\n";
    }
    // Forward every CLI arg (typically `-DCPSConfigFile rtps.ini`) into the
    // OpenDDS runtime. The facade owns the conversion to ACE_TCHAR**.
    for (int i = 1; i < argc; ++i) cfg.runtime_args.emplace_back(argv[i]);
    svc.pre_activate(cfg);

    // Inbound Command -> log it and ack with a CommandStatus.
    svc.subscribe<RadarSystem::Command>(
        "Command",
      [&svc](const RadarSystem::Command& cmd) {
          if (!cmd.target_sensor_id().empty() && cmd.target_sensor_id() != SENSOR_ID) {
            return; // not addressed to us
          }
          {
            std::lock_guard<std::mutex> lk(g_cout_mtx);
            std::cout << "[sensor] command #" << cmd.command_id()
                      << " type=" << to_string(cmd.type())
                      << " params='" << cmd.parameters() << "'\n";
          }
          RadarSystem::CommandStatus cs;
          cs.sensor_id(SENSOR_ID);
          cs.command_id(cmd.command_id());
          cs.result(RadarSystem::CommandResult::CMD_ACCEPTED);
          cs.message("acknowledged");
          cs.timestamp_ns(now_ns());
          svc.publish("CommandStatus", cs);
        });

    // Inbound ComponentStatusRequest -> push current status for every component.
    svc.subscribe<RadarSystem::ComponentStatusRequest>(
        "ComponentStatusRequest",
      [&svc](const RadarSystem::ComponentStatusRequest& req) {
          if (!req.target_sensor_id().empty() && req.target_sensor_id() != SENSOR_ID) {
            return;
          }
          {
            std::lock_guard<std::mutex> lk(g_cout_mtx);
            std::cout << "[sensor] status request #" << req.request_id()
                      << " filter='" << req.component_id_filter() << "'\n";
          }
          publish_status(svc, "alpha.radar_array",
                         RadarSystem::ComponentState::COMPONENT_ONLINE,
                         "on-demand report");
          publish_status(svc, "alpha.signal_processor",
                         RadarSystem::ComponentState::COMPONENT_ONLINE,
                         "on-demand report");
        });

    // Inbound TrackingCue -> log it. A real sensor would slew its beam.
    svc.subscribe<RadarSystem::TrackingCue>(
        "TrackingCue",
        [](const RadarSystem::TrackingCue& cue) {
          if (!cue.target_sensor_id().empty() && cue.target_sensor_id() != SENSOR_ID) return;
          std::lock_guard<std::mutex> lk(g_cout_mtx);
          std::cout << "[sensor] cue from '" << cue.issued_by()
                    << "' bearing=" << cue.bearing_deg()
                    << " elev=" << cue.elevation_deg()
                    << " range=" << cue.range_m()
                    << " (ownership=" << cue.ownership_strength() << ")\n";
        });

    // Inbound OperatorAuditLog -> observe (one-line dump per entry).
    svc.subscribe<RadarSystem::OperatorAuditLog>(
        "OperatorAuditLog",
        [](const RadarSystem::OperatorAuditLog& a) {
          std::lock_guard<std::mutex> lk(g_cout_mtx);
          std::cout << "[sensor] audit op='" << a.operator_id()
                    << "' action=" << to_string(a.action())
                    << " detail='" << a.detail() << "'\n";
        });

    svc.post_activate();

    {
      std::lock_guard<std::mutex> lk(g_cout_mtx);
      std::cout << "[sensor] '" << SENSOR_ID << "' up on domain " << DOMAIN_ID
                << " (Ctrl-C to stop)\n";
    }

    // Tick interval is 100 ms (~10 Hz). RawIQ goes every tick to give
    // the streaming QoS something to drop; the heartbeat / status / track
    // loop runs once per second (every 10 ticks); alarms every 5 s.
    const RadarSystem::AlarmSeverity alarm_rotation[] = {
        RadarSystem::AlarmSeverity::ALARM_INFO,
        RadarSystem::AlarmSeverity::ALARM_WARNING,
        RadarSystem::AlarmSeverity::ALARM_ERROR,
        RadarSystem::AlarmSeverity::ALARM_CRITICAL,
    };
    constexpr std::size_t alarm_rot_n = sizeof(alarm_rotation) / sizeof(alarm_rotation[0]);
    uint32_t   alarm_seq = 1;
    uint64_t   iq_seq    = 0;
    uint32_t tick = 0;
    while (g_running.load()) {
      // RawIQSample: 4 beams per tick (~40 Hz across all beams).
      for (uint32_t beam = 0; beam < 4; ++beam) {
        RadarSystem::RawIQSample iq;
        iq.sensor_id(SENSOR_ID);
        iq.beam_id(beam);
        iq.seq_no(++iq_seq);
        iq.center_freq_hz(9.5e9 + beam * 5.0e6);
        // C++11 mapping: IQVector is a bounded std::vector<float>. Reserve
        // up front so the writes don't reallocate per element.
        auto& is = iq.i_samples();
        auto& qs = iq.q_samples();
        is.reserve(16);
        qs.reserve(16);
        for (int n = 0; n < 16; ++n) {
          is.push_back(static_cast<float>(0.1 * n + tick));
          qs.push_back(static_cast<float>(0.1 * n - tick));
        }
        iq.timestamp_ns(now_ns());
        svc.publish("RawIQSample", iq);
      }

      // Once-per-second housekeeping.
      if ((tick % 10) == 0) {
        const uint32_t slow_tick = tick / 10;
        publish_status(svc, "alpha.radar_array",
                       RadarSystem::ComponentState::COMPONENT_ONLINE, "heartbeat");
        publish_status(svc, "alpha.signal_processor",
                       RadarSystem::ComponentState::COMPONENT_ONLINE, "heartbeat");

        for (uint32_t k = 0; k < 2; ++k) {
          RadarSystem::RadarTrack t;
          t.sensor_id(SENSOR_ID);
          t.track_id(100u + k);
          t.range_m(5000.0 + (slow_tick * 50.0) + k * 200.0);
          t.bearing_deg(static_cast<double>((slow_tick * 5 + k * 90) % 360));
          t.elevation_deg(2.5 + k);
          t.speed_mps(150.0 + k * 25.0);
          t.timestamp_ns(now_ns());
          svc.publish("RadarTrack", t);
        }
      }

      // SystemAlarm every 50 ticks (~5 s). Same alarm_id rotates state so
      // the latched profile shows updates rather than ever-growing instances.
      if (tick > 0 && (tick % 50) == 0) {
        RadarSystem::SystemAlarm a;
        a.sensor_id(SENSOR_ID);
        a.alarm_id(1);                  // single recurring alarm slot
        a.severity(alarm_rotation[(alarm_seq - 1) % alarm_rot_n]);
        a.source_component("alpha.signal_processor");
        a.description("periodic synthetic alarm #" + std::to_string(alarm_seq));
        a.acknowledged_by("");          // unacknowledged
        a.timestamp_ns(now_ns());
        svc.publish("SystemAlarm", a);
        {
          std::lock_guard<std::mutex> lk(g_cout_mtx);
          std::cout << "[sensor] raised alarm #" << alarm_seq
                    << " severity=" << to_string(a.severity()) << "\n";
        }
        ++alarm_seq;
      }
      ++tick;

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    svc.deactivate();
    std::cout << "[sensor] shutdown clean\n";
  } catch (const std::exception& e) {
    std::cerr << "[sensor] " << e.what() << "\n";
    return 1;
  }
  return 0;
}
