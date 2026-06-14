cmake_minimum_required(VERSION 3.16)
project(SOLID_OpenDDS CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PUBSUB_OPEN_DDS_BUILD_TESTS "Build the smoke + unit tests, register with ctest" ON)

# OpenDDS ships a CMake config (OpenDDSConfig.cmake) that provides:
#   - imported targets:  OpenDDS::Dcps, OpenDDS::Rtps, OpenDDS::Rtps_Udp,
#                        OpenDDS::QOS_XML_XSC_Handler, ...
#   - helper function:   OPENDDS_TARGET_SOURCES(<target> [PUBLIC|PRIVATE] file1.idl ...)
# In the official Docker image OpenDDS lives in /opt/OpenDDS.
list(APPEND CMAKE_PREFIX_PATH "/opt/OpenDDS" "$ENV{DDS_ROOT}")
find_package(OpenDDS REQUIRED)

# ---- pub_sub_open_dds: the facade library ---------------------------------
# Defined in its own self-contained CMakeLists. Exposes the facade target
# plus the pub_sub_open_dds_generate_bindings() helper that emits a
# user-facing wrapper + private adapter `.cpp` per IDL type.
add_subdirectory(pub_sub_open_dds)

# ---- RadarSystem demo (notional sensor + workstation) ---------------------
# Self-contained subdirectory: own IDL library + two executables, all
# using the pub_sub_open_dds facade and the codegen helper. Runs on DDS
# domain 43.
add_subdirectory(RadarSystem)

# ---- Tests ----------------------------------------------------------------
if(PUBSUB_OPEN_DDS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
