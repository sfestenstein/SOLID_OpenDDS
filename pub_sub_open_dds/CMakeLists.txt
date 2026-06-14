# pub_sub_open_dds — OpenDDS facade library.
#
# Self-contained CMake module. Builds a static library whose PUBLIC include
# path exposes the headers under include/pub_sub_open_dds/. Consumers link
# against the `pub_sub_open_dds` target; transport libraries are propagated
# publicly so users don't have to think about which transport the facade uses.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_pub_sub_open_dds_public_headers
  include/pub_sub_open_dds/fwd.h
  include/pub_sub_open_dds/service_config.h
  include/pub_sub_open_dds/qos.h
  include/pub_sub_open_dds/qos_profile.h
  include/pub_sub_open_dds/service.h
  include/pub_sub_open_dds/topic_config.h
)

set(_pub_sub_open_dds_private_codegen_headers
  src/pub_sub_open_dds/detail/data_adapter.h
  src/pub_sub_open_dds/detail/opendds_bindings.h
  src/pub_sub_open_dds/detail/registry.h
  src/pub_sub_open_dds/detail/typed_binding.h
)

set(PUB_SUB_OPEN_DDS_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/pub_sub_open_dds")
set(PUB_SUB_OPEN_DDS_PACKAGE_VERSION "0.1.0")

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
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
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

# Internal-only target for repository tests that need the private runtime
# seam (`runtime.h`) or other source-only headers. Not installed.
add_library(pub_sub_open_dds_test_support INTERFACE)
target_include_directories(pub_sub_open_dds_test_support INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_link_libraries(pub_sub_open_dds_test_support INTERFACE pub_sub_open_dds)

# ---- codegen helper -------------------------------------------------------
# Make pub_sub_open_dds_generate_bindings() available to consumers in the
# same CMake invocation. The helper template files live next to this
# CMakeLists.txt under cmake/templates/.
set(PUB_SUB_OPEN_DDS_ADAPTER_SUPPORT_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    CACHE INTERNAL "pub_sub_open_dds private include root for generated adapters")
set(PUB_SUB_OPEN_DDS_TARGET_NAME
  "pub_sub_open_dds"
  CACHE INTERNAL "pub_sub_open_dds facade target name for codegen consumers")
set(PUB_SUB_OPEN_DDS_CODEGEN_TEMPLATES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates"
  CACHE INTERNAL "pub_sub_open_dds codegen template directory")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/pub_sub_open_dds_codegen.cmake")

# ---- install / export -----------------------------------------------------
install(TARGETS pub_sub_open_dds
  EXPORT pub_sub_open_ddsTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${_pub_sub_open_dds_public_headers}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pub_sub_open_dds
)

install(FILES ${_pub_sub_open_dds_private_codegen_headers}
  DESTINATION ${PUB_SUB_OPEN_DDS_INSTALL_CMAKEDIR}/private_include/pub_sub_open_dds/detail
)

install(FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/pub_sub_open_dds_codegen.cmake
  DESTINATION ${PUB_SUB_OPEN_DDS_INSTALL_CMAKEDIR}
)

install(FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates/PubSub.h.in
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates/PubSub_adapter.cpp.in
  DESTINATION ${PUB_SUB_OPEN_DDS_INSTALL_CMAKEDIR}/templates
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/pub_sub_open_ddsConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/pub_sub_open_ddsConfig.cmake
  INSTALL_DESTINATION ${PUB_SUB_OPEN_DDS_INSTALL_CMAKEDIR}
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/pub_sub_open_ddsConfigVersion.cmake
  VERSION ${PUB_SUB_OPEN_DDS_PACKAGE_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/pub_sub_open_ddsConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/pub_sub_open_ddsConfigVersion.cmake
  DESTINATION ${PUB_SUB_OPEN_DDS_INSTALL_CMAKEDIR}
)

install(EXPORT pub_sub_open_ddsTargets
  FILE pub_sub_open_ddsTargets.cmake
  NAMESPACE pub_sub_open_dds::
  DESTINATION ${PUB_SUB_OPEN_DDS_INSTALL_CMAKEDIR}
)
