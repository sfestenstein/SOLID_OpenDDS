# Test-local IDL library. Lives next to Smoke.idl so opendds_idl's
# auto-added -I${CMAKE_CURRENT_SOURCE_DIR} resolves any sibling #includes
# without producing duplicate-include hazards.
add_library(smoke_idl)
OPENDDS_TARGET_SOURCES(smoke_idl
  PRIVATE
    Ping.idl
  OPENDDS_IDL_OPTIONS -Lc++11
)
target_link_libraries(smoke_idl PUBLIC OpenDDS::Dcps)
