# Tests for pub_sub_open_dds:
#   * smoke_roundtrip_opendds   — full RTPS round-trip on domain 42.
#   * smoke_roundtrip_inmemory  — same test driver, in-memory runtime.
#   * lifecycle_test            — Service state machine, no runtime.
#   * qos_profile_test          — built-in QoS profile field values.
#   * topic_config_test         — INI parser via load_from_string.
#   * in_memory_roundtrip_test  — end-to-end via in-memory runtime.

# ---- IDL ------------------------------------------------------------------
# Test-local IDL with one trivial `Smoke::Ping` type. Lives in its own
# subdirectory next to the .idl file (HANDOFF §5 gotcha #1).
add_subdirectory(idl)

# ---- smoke_roundtrip ------------------------------------------------------
# Same test source compiled into one executable that takes the runtime as
# its first positional argument. Both CTest entries point at this binary.
add_executable(smoke_roundtrip smoke_roundtrip.cpp)
target_include_directories(smoke_roundtrip PRIVATE ${CMAKE_SOURCE_DIR}/pub_sub_open_dds/src)
pub_sub_open_dds_generate_bindings(
  TARGET     smoke_roundtrip
  IDL_TARGET smoke_idl
  TYPES      Smoke::Ping
)

# Copy the RTPS config next to the test binary so the OpenDDS run can find it.
configure_file(
  ${CMAKE_SOURCE_DIR}/config/rtps.ini
  ${CMAKE_CURRENT_BINARY_DIR}/rtps.ini
  COPYONLY
)
add_test(NAME smoke_roundtrip_opendds
         COMMAND smoke_roundtrip opendds
                 -DCPSConfigFile ${CMAKE_CURRENT_BINARY_DIR}/rtps.ini
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(smoke_roundtrip_opendds PROPERTIES TIMEOUT 60)

add_test(NAME smoke_roundtrip_inmemory
         COMMAND smoke_roundtrip inmemory)
set_tests_properties(smoke_roundtrip_inmemory PROPERTIES TIMEOUT 10)

# ---- in_memory_roundtrip_test --------------------------------------------
# Same generated bindings as smoke_roundtrip, but exercises additional
# scenarios (HANDOFF §6 keepalive regression, durability, pre-activate
# guards).
add_executable(in_memory_roundtrip_test unit/in_memory_roundtrip_test.cpp)
target_include_directories(in_memory_roundtrip_test PRIVATE ${CMAKE_SOURCE_DIR}/pub_sub_open_dds/src)
pub_sub_open_dds_generate_bindings(
  TARGET     in_memory_roundtrip_test
  IDL_TARGET smoke_idl
  TYPES      Smoke::Ping
)
add_test(NAME in_memory_roundtrip_test COMMAND in_memory_roundtrip_test)
set_tests_properties(in_memory_roundtrip_test PROPERTIES TIMEOUT 10)

# ---- pure unit tests (no IDL needed) -------------------------------------
add_executable(lifecycle_test    unit/lifecycle_test.cpp)
target_include_directories(lifecycle_test PRIVATE ${CMAKE_SOURCE_DIR}/pub_sub_open_dds/src)
target_link_libraries(lifecycle_test    PRIVATE pub_sub_open_dds)
add_test(NAME lifecycle_test    COMMAND lifecycle_test)
set_tests_properties(lifecycle_test PROPERTIES TIMEOUT 10)

add_executable(qos_profile_test  unit/qos_profile_test.cpp)
target_link_libraries(qos_profile_test  PRIVATE pub_sub_open_dds)
add_test(NAME qos_profile_test  COMMAND qos_profile_test)
set_tests_properties(qos_profile_test PROPERTIES TIMEOUT 10)

add_executable(topic_config_test unit/topic_config_test.cpp)
target_link_libraries(topic_config_test PRIVATE pub_sub_open_dds)
add_test(NAME topic_config_test COMMAND topic_config_test)
set_tests_properties(topic_config_test PROPERTIES TIMEOUT 10)

# ---- xml_qos_test --------------------------------------------------------
# Regression test for the bug where TopicConfig::{writer,reader}_qos_for
# returned uninitialised QoS fields when the binding used `xml:<profile>`,
# breaking reader/writer matching for any XML-bound topic. Uses the real
# OpenDDS XML loader, so it needs the OpenDDS env (CTest passes rtps.ini).
configure_file(
  ${CMAKE_SOURCE_DIR}/RadarSystem/config/radar_qos.xml
  ${CMAKE_CURRENT_BINARY_DIR}/radar_qos.xml
  COPYONLY
)
add_executable(xml_qos_test xml_qos_test.cpp)
target_include_directories(xml_qos_test PRIVATE ${CMAKE_SOURCE_DIR}/pub_sub_open_dds/src)
target_link_libraries(xml_qos_test PRIVATE pub_sub_open_dds)
add_test(NAME xml_qos_test
         COMMAND xml_qos_test
                 -DCPSConfigFile ${CMAKE_CURRENT_BINARY_DIR}/rtps.ini
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(xml_qos_test PROPERTIES TIMEOUT 30)
