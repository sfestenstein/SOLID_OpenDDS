# RadarSystem demo: a notional radar sensor and its operator workstation,
# built on the pub_sub_open_dds facade. Adds its own IDL library (radar_idl)
# and two executables. Wired into the build from the root CMakeLists.

# ---- IDL ------------------------------------------------------------------
# `radar_idl` is defined in its own subdir next to the .idl files
# so the generated headers and include roots stay local to this IDL target.
add_subdirectory(idl)

# Convenience list of every @topic struct in the radar IDL. Both apps need
# all ten because each side either publishes or subscribes to each topic.
set(RADAR_TYPES
  RadarSystem::ComponentStatus
  RadarSystem::RadarTrack
  RadarSystem::Command
  RadarSystem::CommandStatus
  RadarSystem::ComponentStatusRequest
  RadarSystem::SystemAlarm
  RadarSystem::OperatorAuditLog
  RadarSystem::RawIQSample
  RadarSystem::TrackingCue
  RadarSystem::OperatorChat
)

# ---- SensorApp -----------------------------------------------------------
add_executable(SensorApp src/SensorApp.cpp)
pub_sub_open_dds_generate_bindings(
  TARGET     SensorApp
  IDL_TARGET radar_idl
  TYPES      ${RADAR_TYPES}
)

# ---- WorkstationApp ------------------------------------------------------
add_executable(WorkstationApp src/WorkstationApp.cpp)
pub_sub_open_dds_generate_bindings(
  TARGET     WorkstationApp
  IDL_TARGET radar_idl
  TYPES      ${RADAR_TYPES}
)

# ---- Runtime config ------------------------------------------------------
# Copy the shared rtps.ini and the per-app topic-QoS config files next to
# the radar binaries so they can be launched from build/RadarSystem/ with
# `-DCPSConfigFile rtps.ini`. The apps look for {sensor,workstation}_topics.ini
# in their cwd.
configure_file(
  ${CMAKE_SOURCE_DIR}/config/rtps.ini
  ${CMAKE_CURRENT_BINARY_DIR}/rtps.ini
  COPYONLY
)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/config/sensor_topics.ini
  ${CMAKE_CURRENT_BINARY_DIR}/sensor_topics.ini
  COPYONLY
)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/config/workstation_topics.ini
  ${CMAKE_CURRENT_BINARY_DIR}/workstation_topics.ini
  COPYONLY
)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/config/sensor_service.ini
  ${CMAKE_CURRENT_BINARY_DIR}/sensor_service.ini
  COPYONLY
)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/config/workstation_service.ini
  ${CMAKE_CURRENT_BINARY_DIR}/workstation_service.ini
  COPYONLY
)
# DDS-XML QoS profiles referenced by `xml:<profile>` entries in the topic
# config files. Read by TopicConfig::use_xml_qos_file at startup.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/config/radar_qos.xml
  ${CMAKE_CURRENT_BINARY_DIR}/radar_qos.xml
  COPYONLY
)
