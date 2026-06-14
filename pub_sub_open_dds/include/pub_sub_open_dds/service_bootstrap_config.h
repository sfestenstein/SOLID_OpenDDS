// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

namespace pub_sub_open_dds {

// High-level service bootstrap input meant for production app wiring.
//
// A single file can describe the domain/runtime args plus where to load
// topic/QoS policy from, allowing callers to avoid hand-assembling
// ServiceConfig + TopicConfig in application code.
struct ServiceBootstrapConfig {
  int                      domain_id = 42;
  std::vector<std::string> runtime_args;
  std::string              config_file;

  // Required: path to the TopicConfig INI mapping (<topic> = <profile>).
  std::string              topic_config_file;

  // Optional: DDS-XML QoS profile file used by `xml:<profile>` bindings.
  std::string              qos_xml_file;

  // Parse from a file path. Throws std::runtime_error on parse/validation
  // failures.
  static ServiceBootstrapConfig load_from_file(const std::string& path);

  // Parse from in-memory text using the same syntax as load_from_file.
  static ServiceBootstrapConfig load_from_string(
      const std::string& contents,
      const std::string& source_label = "<string>");
};

} // namespace pub_sub_open_dds