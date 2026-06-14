# pub_sub_open_dds_codegen.cmake
#
# Provides pub_sub_open_dds_generate_bindings(): a CMake helper that emits
# the per-IDL-type "PubSub" wrapper + adapter pair for each named user
# type. The wrapper is a header users include from their application code;
# the adapter is a .cpp the helper compiles into the user's target. The
# adapter is the ONLY TU outside the facade that includes the OpenDDS-
# generated `*TypeSupportImpl.h`.
#
# Usage:
#
#   pub_sub_open_dds_generate_bindings(
#       TARGET     SensorApp                 # the consumer target
#       IDL_TARGET radar_idl                 # OPENDDS_TARGET_SOURCES target
#       TYPES                                # one entry per @topic struct
#         RadarSystem::ComponentStatus
#         RadarSystem::RadarTrack
#         ...
#       [HEADER_PREFIX RadarSystem]          # optional, see below
#   )
#
# For each `Namespace::TypeName` in TYPES, the helper emits:
#
#   <gen_dir>/pub_sub_open_dds_generated/<TypeName>PubSub.h
#       — user-facing wrapper. Includes <TypeName>C.h plus Service so
#         application code sees the IDL struct definition and the facade
#         entrypoint together. No OpenDDS API surface beyond what
#         <TypeName>C.h itself brings (which is unavoidable: the IDL
#         struct definition lives there).
#
#   <gen_dir>/pub_sub_open_dds_generated/<TypeName>PubSub_adapter.cpp
#       — includes <TypeName>TypeSupportImpl.h, specialises the adapter
#         and registers it with the global registry via a static initialiser.
#
# Both files are added to the TARGET; the generated include dir is added
# to the TARGET's PRIVATE include path (so other targets don't accidentally
# pick up the headers). The TARGET is made dependent on IDL_TARGET so
# generation orders correctly.

if(NOT DEFINED PUB_SUB_OPEN_DDS_CODEGEN_TEMPLATES)
  message(FATAL_ERROR
    "pub_sub_open_dds_codegen.cmake: PUB_SUB_OPEN_DDS_CODEGEN_TEMPLATES is not "
    "set. Set it (typically in the facade's CMakeLists.txt) before including "
    "this file.")
endif()
if(NOT DEFINED PUB_SUB_OPEN_DDS_ADAPTER_SUPPORT_INCLUDE_DIR)
  message(FATAL_ERROR
    "pub_sub_open_dds_codegen.cmake: PUB_SUB_OPEN_DDS_ADAPTER_SUPPORT_INCLUDE_DIR is not "
    "set. Set it to the private include root that contains the generated-adapter support headers."
  )
endif()
if(NOT DEFINED PUB_SUB_OPEN_DDS_TARGET_NAME)
  message(FATAL_ERROR
    "pub_sub_open_dds_codegen.cmake: PUB_SUB_OPEN_DDS_TARGET_NAME is not set. "
    "Set it to the facade library target that generated adapters should link against."
  )
endif()

function(pub_sub_open_dds_generate_bindings)
  set(_options)
  set(_one_value TARGET IDL_TARGET HEADER_PREFIX)
  set(_multi_value TYPES)
  cmake_parse_arguments(PSO "${_options}" "${_one_value}" "${_multi_value}" ${ARGN})

  if(NOT PSO_TARGET)
    message(FATAL_ERROR "pub_sub_open_dds_generate_bindings: TARGET is required")
  endif()
  if(NOT PSO_IDL_TARGET)
    message(FATAL_ERROR "pub_sub_open_dds_generate_bindings: IDL_TARGET is required")
  endif()
  if(NOT PSO_TYPES)
    message(FATAL_ERROR "pub_sub_open_dds_generate_bindings: TYPES is empty")
  endif()

  set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/${PSO_TARGET}_pubsub_gen")
  file(MAKE_DIRECTORY "${_gen_dir}/pub_sub_open_dds_generated")

  set(_generated_sources "")

  foreach(_fqn IN LISTS PSO_TYPES)
    # Split "Ns::Sub::TypeName" into:
    #   _ns          = "Ns::Sub"     (empty if no namespace)
    #   _type_name   = "TypeName"
    string(REGEX MATCH "^(.*)::([^:]+)$" _matched "${_fqn}")
    if(_matched)
      set(_ns "${CMAKE_MATCH_1}")
      set(_type_name "${CMAKE_MATCH_2}")
    else()
      set(_ns "")
      set(_type_name "${_fqn}")
    endif()

    # Header naming convention: generated TypeSupport headers from OpenDDS
    # are <TypeName>TypeSupportImpl.h and the IDL-mapping header is
    # <TypeName>C.h. We honour an explicit HEADER_PREFIX as an escape hatch
    # for IDLs whose generated headers don't match the convention.
    if(PSO_HEADER_PREFIX)
      set(_c_header              "${PSO_HEADER_PREFIX}/${_type_name}C.h")
      set(_typesupportimpl_hdr   "${PSO_HEADER_PREFIX}/${_type_name}TypeSupportImpl.h")
    else()
      set(_c_header              "${_type_name}C.h")
      set(_typesupportimpl_hdr   "${_type_name}TypeSupportImpl.h")
    endif()

    # configure_file substitutes @VAR@ tokens.
    set(PSO_GEN_FQN              "${_fqn}")
    set(PSO_GEN_NAMESPACE        "${_ns}")
    set(PSO_GEN_TYPE_NAME        "${_type_name}")
    set(PSO_GEN_C_HEADER         "${_c_header}")
    set(PSO_GEN_TYPESUPPORT_HDR  "${_typesupportimpl_hdr}")
    # An IDL-name suffix used to construct related class names. OpenDDS's
    # convention is <TypeName>TypeSupport, <TypeName>DataWriter, etc.
    set(PSO_GEN_TS_CLASS         "${_type_name}TypeSupport")
    set(PSO_GEN_TS_IMPL_CLASS    "${_type_name}TypeSupportImpl")
    set(PSO_GEN_DW_CLASS         "${_type_name}DataWriter")
    set(PSO_GEN_DR_CLASS         "${_type_name}DataReader")
    # A safe identifier (used for the static registrar variable).
    string(MAKE_C_IDENTIFIER "_pso_register_${_fqn}" PSO_GEN_REG_IDENT)

    set(_out_header  "${_gen_dir}/pub_sub_open_dds_generated/${_type_name}PubSub.h")
    set(_out_adapter "${_gen_dir}/pub_sub_open_dds_generated/${_type_name}PubSub_adapter.cpp")

    configure_file(
      "${PUB_SUB_OPEN_DDS_CODEGEN_TEMPLATES}/PubSub.h.in"
      "${_out_header}"
      @ONLY
    )
    configure_file(
      "${PUB_SUB_OPEN_DDS_CODEGEN_TEMPLATES}/PubSub_adapter.cpp.in"
      "${_out_adapter}"
      @ONLY
    )

    list(APPEND _generated_sources "${_out_adapter}")
  endforeach()

  target_sources(${PSO_TARGET} PRIVATE ${_generated_sources})
  target_include_directories(${PSO_TARGET} PRIVATE "${_gen_dir}")
  target_include_directories(${PSO_TARGET} PRIVATE "${PUB_SUB_OPEN_DDS_ADAPTER_SUPPORT_INCLUDE_DIR}")
  target_link_libraries(${PSO_TARGET} PRIVATE ${PUB_SUB_OPEN_DDS_TARGET_NAME} ${PSO_IDL_TARGET})
  add_dependencies(${PSO_TARGET} ${PSO_IDL_TARGET})
endfunction()
