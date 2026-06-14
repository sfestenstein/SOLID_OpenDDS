# Tests for pub_sub_open_dds:
#   * smoke_roundtrip_opendds   — full RTPS round-trip on domain 42.
#   * lifecycle_test            — Service state machine; uses GTest + GMock.
#   * service_bootstrap_config_test — bootstrap config parser validation.
#   * qos_profile_test          — built-in QoS profile field values.
#   * topic_config_test         — INI parser via load_from_string.

# ---- Google Test / Mock ---------------------------------------------------
# Use the googletest source tree bundled with OpenDDS rather than a separate
# install. INSTALL_GTEST=OFF keeps its install() rules from polluting ours.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(INSTALL_GMOCK OFF CACHE BOOL "" FORCE)
add_subdirectory(
  /opt/OpenDDS/tests/googletest
  ${CMAKE_CURRENT_BINARY_DIR}/googletest
  EXCLUDE_FROM_ALL
)
# GTest 1.8.2 has a -Wmaybe-uninitialized in its own source that triggers
# -Werror; suppress all warnings on the bundled targets to keep our build clean.
foreach(_gt_target gtest gtest_main gmock gmock_main)
  if(TARGET ${_gt_target})
    target_compile_options(${_gt_target} PRIVATE -w)
  endif()
endforeach()

# ---- IDL ------------------------------------------------------------------
# Test-local IDL with one trivial `Smoke::Ping` type. Lives in its own
# subdirectory next to the .idl file so OpenDDS-generated includes stay local
# and do not pick up duplicate include roots.
add_subdirectory(idl)

# ---- smoke_roundtrip ------------------------------------------------------
# OpenDDS round-trip smoke test.
add_executable(smoke_roundtrip smoke_roundtrip.cpp)
target_link_libraries(smoke_roundtrip PRIVATE pub_sub_open_dds_test_support)
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
         COMMAND smoke_roundtrip
                 -DCPSConfigFile ${CMAKE_CURRENT_BINARY_DIR}/rtps.ini
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(smoke_roundtrip_opendds PROPERTIES TIMEOUT 60)

# ---- pure unit tests (no IDL needed) -------------------------------------
add_executable(lifecycle_test unit/lifecycle_test.cpp)
target_link_libraries(lifecycle_test PRIVATE
  pub_sub_open_dds_test_support
  gmock
  gtest
  gtest_main
)
add_test(NAME lifecycle_test COMMAND lifecycle_test)
set_tests_properties(lifecycle_test PROPERTIES TIMEOUT 10)

add_executable(service_bootstrap_config_test unit/service_bootstrap_config_test.cpp)
target_link_libraries(service_bootstrap_config_test PRIVATE pub_sub_open_dds)
add_test(NAME service_bootstrap_config_test COMMAND service_bootstrap_config_test)
set_tests_properties(service_bootstrap_config_test PROPERTIES TIMEOUT 10)

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
# breaking reader/writer matching for any XML-bound topic.
if(TARGET OpenDDS::QOS_XML_XSC_Handler)
  configure_file(
    ${CMAKE_SOURCE_DIR}/RadarSystem/config/radar_qos.xml
    ${CMAKE_CURRENT_BINARY_DIR}/radar_qos.xml
    COPYONLY
  )
  add_executable(xml_qos_test xml_qos_test.cpp)
  target_link_libraries(xml_qos_test PRIVATE pub_sub_open_dds_test_support)
  add_test(NAME xml_qos_test
           COMMAND xml_qos_test
                   -DCPSConfigFile ${CMAKE_CURRENT_BINARY_DIR}/rtps.ini
           WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(xml_qos_test PROPERTIES TIMEOUT 30)
else()
  message(STATUS "Skipping xml_qos_test because OpenDDS::QOS_XML_XSC_Handler is unavailable")
endif()
