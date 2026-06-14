# pub_sub_open_dds — OpenDDS facade library.
#
# Self-contained CMake module. Builds a static library whose PUBLIC include
# path exposes the headers under include/pub_sub_open_dds/. Consumers link
# against the `pub_sub_open_dds` target; transport libraries are propagated
# publicly so users don't have to think about which transport the facade uses.

set(_pub_sub_open_dds_public_headers
  include/pub_sub_open_dds/fwd.h
  include/pub_sub_open_dds/service_config.h
  include/pub_sub_open_dds/qos.h
  include/pub_sub_open_dds/service.h
  include/pub_sub_open_dds/publisher.h
  include/pub_sub_open_dds/subscriber.h
  include/pub_sub_open_dds/topic_config.h
)

add_library(pub_sub_open_dds STATIC
  src/service.cpp
  src/qos_profile.cpp
  src/topic_config.cpp
  src/registry.cpp
  src/runtime/opendds_runtime.cpp
  src/runtime/in_memory_runtime.cpp
  ${_pub_sub_open_dds_public_headers}
)

target_include_directories(pub_sub_open_dds
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(pub_sub_open_dds
  PUBLIC
    # OpenDDS core + RTPS transport are PUBLIC because the generated
    # <TypeName>PubSub_adapter.cpp files (compiled into consumer targets,
    # not into this library) call OpenDDS symbols via the private
    # detail/opendds_bindings.h. Consumers therefore need the OpenDDS
    # libraries on their link line — but they do NOT include any DDS
    # headers from their own code.
    OpenDDS::Dcps
    OpenDDS::Rtps
    OpenDDS::Rtps_Udp
)

if(TARGET OpenDDS::QOS_XML_XSC_Handler)
  target_link_libraries(pub_sub_open_dds PRIVATE OpenDDS::QOS_XML_XSC_Handler)
  target_compile_definitions(pub_sub_open_dds PRIVATE PUB_SUB_OPEN_DDS_HAS_XML_QOS=1)
else()
  message(WARNING "OpenDDS::QOS_XML_XSC_Handler is unavailable; XML QoS loading will be disabled")
  target_compile_definitions(pub_sub_open_dds PRIVATE PUB_SUB_OPEN_DDS_HAS_XML_QOS=0)
endif()

set_target_properties(pub_sub_open_dds PROPERTIES
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

# ---- codegen helper -------------------------------------------------------
# Make pub_sub_open_dds_generate_bindings() available to consumers in the
# same CMake invocation. The helper template files live next to this
# CMakeLists.txt under cmake/templates/.
set(PUB_SUB_OPEN_DDS_CODEGEN_TEMPLATES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates"
    CACHE INTERNAL "pub_sub_open_dds codegen template directory")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/pub_sub_open_dds_codegen.cmake")
