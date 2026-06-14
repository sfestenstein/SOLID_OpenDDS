# radar_idl — IDL-generated type-support library for the RadarSystem demo.
#
# This CMakeLists lives next to the .idl files so that, when CMake runs
# OPENDDS_TARGET_SOURCES, CMAKE_CURRENT_SOURCE_DIR == this directory. That
# matters because the OpenDDS helper automatically passes
# `-I${CMAKE_CURRENT_SOURCE_DIR}` to opendds_idl: with the IDL files sitting
# right here, sibling `#include "Common.idl"` directives resolve via the
# single auto-added include path, so opendds_idl emits exactly one
# `#include "CommonC.h"` in the generated headers. (When the .idl files
# lived under an `idl/` subdir of the parent CMakeLists, opendds_idl could
# reach Common.idl through both `-I${SRC}` and `-I${SRC}/idl` and emitted
# duplicate, broken includes.)
add_library(radar_idl)
# IDL files are PRIVATE rather than PUBLIC: with PUBLIC, OpenDDS's helper
# propagates the generated *.idl source files into every consumer target's
# source list (via INTERFACE_SOURCES). Those generated files are marked
# GENERATED only in *this* directory's scope, so a consumer defined in the
# parent CMakeLists (SensorApp / WorkstationApp) would error out with
# "Cannot find source file". Keeping the IDL list PRIVATE limits the source
# propagation to radar_idl itself; consumers still pick up the generated
# *.h files via the helper's target_include_directories(... PUBLIC ...).
OPENDDS_TARGET_SOURCES(radar_idl
  PRIVATE
    Common.idl
    ComponentStatus.idl
    RadarTrack.idl
    Command.idl
    CommandStatus.idl
    ComponentStatusRequest.idl
    SystemAlarm.idl
    OperatorAuditLog.idl
    RawIQSample.idl
    TrackingCue.idl
    OperatorChat.idl
  OPENDDS_IDL_OPTIONS -Lc++11
)
target_link_libraries(radar_idl PUBLIC OpenDDS::Dcps)
