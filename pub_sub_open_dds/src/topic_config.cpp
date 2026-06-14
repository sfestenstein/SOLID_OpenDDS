// SPDX-License-Identifier: Apache-2.0
#include "pub_sub_open_dds/topic_config.h"

#include <dds/DCPS/Service_Participant.h>
#include <dds/DdsDcpsPublicationC.h>
#include <dds/DdsDcpsSubscriptionC.h>

#if PUB_SUB_OPEN_DDS_HAS_XML_QOS
#include <dds/DCPS/QOS_XML_Handler/QOS_XML_Loader.h>
#include <ace/SString.h>
#endif

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace pub_sub_open_dds {

// ---- PIMPL holding the XML loader + lookup state -----------------------
//
// Defined here so the header can stay OpenDDS-free. The XML loader is the
// only OpenDDS-specific piece in this file; the rest is INI parsing and
// QoS-name resolution against the built-in profile table.
struct TopicConfig::Impl {
#if PUB_SUB_OPEN_DDS_HAS_XML_QOS
  std::unique_ptr<OpenDDS::DCPS::QOS_XML_Loader> loader;
#endif
  std::string                                     base_name;  // for diagnostics

  // Resolve the named profile's writer QoS into a fully-populated
  // DDS::DataWriterQos. Returns an empty shared_ptr on miss.
  //
  // Critical implementation detail: `OpenDDS::DCPS::QOS_XML_Loader`
  // overwrites only the QoS fields that the XML mentions. Fields it does
  // NOT mention are left at whatever the caller's struct held. A
  // default-constructed `DDS::DataWriterQos` leaves several substructures
  // (deadline, liveliness, resource_limits, ...) value-initialised to
  // garbage from the C++ side because the CORBA-generated structs do not
  // run real DDS-default initialisation. Forwarding that to OpenDDS
  // produces wildly incompatible QoS (e.g. a 600-billion-millisecond
  // deadline) and silently breaks reader/writer matching.
  //
  // Pre-seeding with `TheServiceParticipant->initial_DataWriterQos()`
  // gives us the canonical DDS defaults (DURATION_INFINITE for deadline,
  // UNLIMITED resource limits, AUTOMATIC liveliness, etc.) so anything
  // the XML doesn't mention stays sensible.
#if PUB_SUB_OPEN_DDS_HAS_XML_QOS
  std::shared_ptr<DDS::DataWriterQos> writer_qos(const std::string& profile,
                                                  const std::string& topic) {
    if (!loader) return nullptr;
    auto qos = std::make_shared<DDS::DataWriterQos>(
        TheServiceParticipant->initial_DataWriterQos());
    const ACE_TString id = ACE_TString((base_name + "#" + profile).c_str());
    if (loader->get_datawriter_qos(*qos, id.c_str(), topic.c_str()) !=
        DDS::RETCODE_OK) {
      return nullptr;
    }
    return qos;
  }

  std::shared_ptr<DDS::DataReaderQos> reader_qos(const std::string& profile,
                                                  const std::string& topic) {
    if (!loader) return nullptr;
    auto qos = std::make_shared<DDS::DataReaderQos>(
        TheServiceParticipant->initial_DataReaderQos());
    const ACE_TString id = ACE_TString((base_name + "#" + profile).c_str());
    if (loader->get_datareader_qos(*qos, id.c_str(), topic.c_str()) !=
        DDS::RETCODE_OK) {
      return nullptr;
    }
    return qos;
  }

  // Cheap façade-side mirror of the headline QoS dimensions in the
  // resolved DDS struct. Carried inside WriterQos/ReaderQos for
  // diagnostics and for the InMemoryRuntime (which ignores the opaque
  // raw payload). Not used by the OpenDDS runtime — it consumes `raw()`
  // directly.
  static QosProfile profile_from(const DDS::DataWriterQos& qos,
                                  const std::string& name) {
    QosProfile p;
    p.name      = "xml:" + name;
    p.reliable  = (qos.reliability.kind == DDS::RELIABLE_RELIABILITY_QOS);
    p.durable   = (qos.durability.kind  == DDS::TRANSIENT_LOCAL_DURABILITY_QOS);
    p.keep_all  = (qos.history.kind     == DDS::KEEP_ALL_HISTORY_QOS);
    if (!p.keep_all) p.history_depth = qos.history.depth;
    return p;
  }

  static QosProfile profile_from(const DDS::DataReaderQos& qos,
                                  const std::string& name) {
    QosProfile p;
    p.name      = "xml:" + name;
    p.reliable  = (qos.reliability.kind == DDS::RELIABLE_RELIABILITY_QOS);
    p.durable   = (qos.durability.kind  == DDS::TRANSIENT_LOCAL_DURABILITY_QOS);
    p.keep_all  = (qos.history.kind     == DDS::KEEP_ALL_HISTORY_QOS);
    if (!p.keep_all) p.history_depth = qos.history.depth;
    return p;
  }
#endif
};

// ---- helpers -----------------------------------------------------------

namespace {

std::string trim(std::string s) {
  auto not_ws = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_ws));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_ws).base(), s.end());
  return s;
}

// Strip a trailing ".xml" (case-insensitive). OpenDDS's loader appends
// ".xml" internally, so we keep just the base name.
std::string strip_xml_extension(std::string s) {
  if (s.size() >= 4) {
    std::string tail = s.substr(s.size() - 4);
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (tail == ".xml") s.resize(s.size() - 4);
  }
  return s;
}

bool starts_with_xml_prefix(const std::string& s) {
  static const char* kPrefix = "xml:";
  if (s.size() <= 4) return false;
  return std::equal(s.begin(), s.begin() + 4, kPrefix,
                    [](unsigned char a, unsigned char b) {
                      return std::tolower(a) == std::tolower(b);
                    });
}

void parse_bindings(std::istream& in,
                    const std::string& source_label,
                    std::unordered_map<std::string, std::string>& out) {
  std::string line;
  int         lineno = 0;
  while (std::getline(in, line)) {
    ++lineno;
    for (std::size_t i = 0; i < line.size(); ++i) {
      if (line[i] == '#' || line[i] == ';') { line.erase(i); break; }
    }
    std::string s = trim(std::move(line));
    if (s.empty()) continue;
    const auto eq = s.find('=');
    if (eq == std::string::npos) {
      throw std::runtime_error("TopicConfig: " + source_label + ":"
                               + std::to_string(lineno)
                               + ": expected '<topic> = <profile>'");
    }
    std::string topic   = trim(s.substr(0, eq));
    std::string profile = trim(s.substr(eq + 1));
    if (topic.empty() || profile.empty()) {
      throw std::runtime_error("TopicConfig: " + source_label + ":"
                               + std::to_string(lineno)
                               + ": empty topic or profile name");
    }
    out[std::move(topic)] = std::move(profile);
  }
}

} // namespace

// ---- TopicConfig out-of-line members -----------------------------------

TopicConfig::TopicConfig()  = default;
TopicConfig::~TopicConfig() = default;
TopicConfig::TopicConfig(TopicConfig&&) noexcept            = default;
TopicConfig& TopicConfig::operator=(TopicConfig&&) noexcept = default;

TopicConfig TopicConfig::load_from_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("TopicConfig: cannot open '" + path + "'");
  }
  TopicConfig cfg;
  parse_bindings(in, path, cfg.bindings_);
  return cfg;
}

TopicConfig TopicConfig::load_from_string(const std::string& contents,
                                          const std::string& source_label) {
  TopicConfig cfg;
  std::istringstream in(contents);
  parse_bindings(in, source_label, cfg.bindings_);
  return cfg;
}

void TopicConfig::use_xml_qos_file(const std::string& path) {
#if PUB_SUB_OPEN_DDS_HAS_XML_QOS
  auto loader = std::unique_ptr<OpenDDS::DCPS::QOS_XML_Loader>(
      new OpenDDS::DCPS::QOS_XML_Loader());
  auto basename = strip_xml_extension(path);
  // QOS_XML_Loader::init only consumes the "<base>" part of "<base>#<profile>"
  // to derive the .xml filename to parse; the profile name is looked up
  // later by get_datawriter_qos / get_datareader_qos. Pass a syntactically
  // legal placeholder (the schema restricts profile names to [a-zA-Z0-9 ]+,
  // so no underscores).
  const ACE_TString seed = ACE_TString((basename + "#pubsubopenddsseed").c_str());
  const DDS::ReturnCode_t rc = loader->init(seed.c_str());
  if (rc != DDS::RETCODE_OK) {
    throw std::runtime_error("TopicConfig: failed to parse XML QoS file '" + path
                             + "' (OpenDDS rc=" + std::to_string(rc) + ")");
  }
  if (!impl_) impl_.reset(new Impl());
  impl_->loader    = std::move(loader);
  impl_->base_name = std::move(basename);
#else
  (void)path;
  throw std::runtime_error(
      "TopicConfig: XML QoS support is unavailable in this build "
      "(OpenDDS::QOS_XML_XSC_Handler not found)");
#endif
}

WriterQos TopicConfig::writer_qos_for(const std::string& topic,
                                      const QosProfile&  default_profile) const {
  auto it = bindings_.find(topic);
  if (it == bindings_.end()) return make_writer_qos(default_profile);

  const std::string& raw = it->second;
  if (starts_with_xml_prefix(raw)) {
    if (!impl_
#if PUB_SUB_OPEN_DDS_HAS_XML_QOS
        || !impl_->loader
#endif
    ) {
      std::cerr << "pub_sub_open_dds: topic '" << topic
                << "' bound to '" << raw
                << "' but no XML QoS file has been loaded; falling back to '"
                << default_profile.name << "'\n";
      return make_writer_qos(default_profile);
    }
  #if PUB_SUB_OPEN_DDS_HAS_XML_QOS
    const std::string profile_name = raw.substr(4);
    auto resolved = impl_->writer_qos(profile_name, topic);
    if (!resolved) {
      std::cerr << "pub_sub_open_dds: XML writer QoS lookup failed for '"
                << impl_->base_name << "#" << profile_name << "' topic '"
                << topic << "'; falling back to '" << default_profile.name
                << "'\n";
      return make_writer_qos(default_profile);
    }
    WriterQos wq(Impl::profile_from(*resolved, profile_name));
    // Hand the fully-resolved DDS QoS to the OpenDDS runtime opaquely.
    // shared_ptr<void> erases the DDS-specific type so qos.h stays clean.
    wq.attach_raw(std::static_pointer_cast<void>(resolved));
    return wq;
  #else
    return make_writer_qos(default_profile);
  #endif
  }

  const QosProfile* p = find_builtin_profile(raw);
  if (!p) {
    std::cerr << "pub_sub_open_dds: topic '" << topic
              << "' bound to unknown QoS profile '" << raw
              << "', falling back to '" << default_profile.name << "'\n";
    return make_writer_qos(default_profile);
  }
  return make_writer_qos(*p);
}

ReaderQos TopicConfig::reader_qos_for(const std::string& topic,
                                      const QosProfile&  default_profile) const {
  auto it = bindings_.find(topic);
  if (it == bindings_.end()) return make_reader_qos(default_profile);

  const std::string& raw = it->second;
  if (starts_with_xml_prefix(raw)) {
    if (!impl_
#if PUB_SUB_OPEN_DDS_HAS_XML_QOS
        || !impl_->loader
#endif
    ) {
      std::cerr << "pub_sub_open_dds: topic '" << topic
                << "' bound to '" << raw
                << "' but no XML QoS file has been loaded; falling back to '"
                << default_profile.name << "'\n";
      return make_reader_qos(default_profile);
    }
  #if PUB_SUB_OPEN_DDS_HAS_XML_QOS
    const std::string profile_name = raw.substr(4);
    auto resolved = impl_->reader_qos(profile_name, topic);
    if (!resolved) {
      std::cerr << "pub_sub_open_dds: XML reader QoS lookup failed for '"
                << impl_->base_name << "#" << profile_name << "' topic '"
                << topic << "'; falling back to '" << default_profile.name
                << "'\n";
      return make_reader_qos(default_profile);
    }
    ReaderQos rq(Impl::profile_from(*resolved, profile_name));
    rq.attach_raw(std::static_pointer_cast<void>(resolved));
    return rq;
  #else
    return make_reader_qos(default_profile);
  #endif
  }

  const QosProfile* p = find_builtin_profile(raw);
  if (!p) {
    std::cerr << "pub_sub_open_dds: topic '" << topic
              << "' bound to unknown QoS profile '" << raw
              << "', falling back to '" << default_profile.name << "'\n";
    return make_reader_qos(default_profile);
  }
  return make_reader_qos(*p);
}

std::string TopicConfig::profile_name_for(const std::string& topic) const {
  auto it = bindings_.find(topic);
  return it == bindings_.end() ? std::string{} : it->second;
}

bool TopicConfig::has_binding(const std::string& topic) const {
  return bindings_.find(topic) != bindings_.end();
}

} // namespace pub_sub_open_dds
