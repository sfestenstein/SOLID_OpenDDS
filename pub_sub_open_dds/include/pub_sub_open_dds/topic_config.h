// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pub_sub_open_dds/qos.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

namespace pub_sub_open_dds {

// Per-app config that maps topic names to a QoS profile name. The INI
// value can be either:
//
//   * a built-in profile name (case-insensitive): `reliable`, `best_effort`,
//     `reliable_transient`, `event_bus`, `latched`, `streaming`,
//     `persistent`, `heartbeat`, `critical`.
//
//   * an XML profile reference of the form `xml:<profile_name>` (resolved
//     against the DDS-XML file loaded by `use_xml_qos_file`).
//
// File format: tiny INI subset (no sections; '#' and ';' comments; blank
// lines OK). Whitespace around keys, values, and '=' is trimmed. Topic
// names are case sensitive; built-in profile names are case insensitive.
//
// The XML loader is held PIMPL-style so this header drags in no OpenDDS
// or xerces includes — `Impl` is forward-declared and defined entirely
// inside topic_config.cpp.
class TopicConfig {
public:
  // Load topic bindings from a file. Throws std::runtime_error on I/O or
  // parse failure.
  static TopicConfig load_from_file(const std::string& path);

  // Load topic bindings from an in-memory string with the same INI syntax.
  // Useful for tests that don't want to drop a file on disk. Throws
  // std::runtime_error on parse failure; the second argument is used only
  // for error messages.
  static TopicConfig load_from_string(const std::string& contents,
                                      const std::string& source_label = "<string>");

  // Make the named DDS-XML QoS profile file available to `xml:<profile>`
  // bindings. Path may omit the `.xml` suffix. Optional — a TopicConfig
  // with no XML file still works for any binding that uses a built-in
  // profile name. Calling this more than once replaces the prior loader.
  // Throws std::runtime_error on parse failure.
  void use_xml_qos_file(const std::string& path);

  // Resolve `topic` to a writer/reader QoS. Falls back to
  // `make_writer_qos(default_profile)` (resp. reader) on any miss, with a
  // single-line warning on std::cerr.
  WriterQos writer_qos_for(const std::string& topic,
                           const QosProfile&  default_profile) const;
  ReaderQos reader_qos_for(const std::string& topic,
                           const QosProfile&  default_profile) const;

  // Raw profile-name lookup (the verbatim INI value); empty when unbound.
  std::string profile_name_for(const std::string& topic) const;

  bool        has_binding(const std::string& topic) const;
  std::size_t size()    const noexcept { return bindings_.size(); }

  TopicConfig();
  ~TopicConfig();
  TopicConfig(TopicConfig&&) noexcept;
  TopicConfig& operator=(TopicConfig&&) noexcept;
  TopicConfig(const TopicConfig&)            = delete;
  TopicConfig& operator=(const TopicConfig&) = delete;

  // PIMPL holding the XML loader. Defined in topic_config.cpp; declared
  // public so file-local helpers in the .cpp can take an Impl* parameter
  // without needing friend declarations. Users never touch Impl.
  struct Impl;

private:
  std::unordered_map<std::string, std::string> bindings_;
  std::unique_ptr<Impl>                        impl_;
};

} // namespace pub_sub_open_dds
