// SPDX-License-Identifier: Apache-2.0
#include "pub_sub_open_dds/service_bootstrap_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace pub_sub_open_dds {

namespace {

std::string trim(std::string s) {
  const std::string::const_iterator first =
      std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); });
  s.erase(s.begin(), first);
  const std::string::const_reverse_iterator rfirst =
      std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); });
  s.erase(rfirst.base(), s.end());
  return s;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

int parse_int(const std::string& key,
              const std::string& raw,
              const std::string& source,
              int line_no) {
  std::istringstream iss(raw);
  int value = 0;
  char tail = 0;
  if (!(iss >> value) || (iss >> tail)) {
    throw std::runtime_error("ServiceBootstrapConfig: " + source + ":"
                             + std::to_string(line_no)
                             + ": invalid integer for '" + key + "': '"
                             + raw + "'");
  }
  return value;
}

ServiceBootstrapConfig parse_stream(std::istream& in,
                                    const std::string& source) {
  ServiceBootstrapConfig out;
  std::string line;
  int line_no = 0;

  while (std::getline(in, line)) {
    ++line_no;
    for (std::size_t i = 0; i < line.size(); ++i) {
      if (line[i] == '#' || line[i] == ';') {
        line.erase(i);
        break;
      }
    }

    std::string s = trim(line);
    if (s.empty()) continue;

    const std::size_t eq = s.find('=');
    if (eq == std::string::npos) {
      throw std::runtime_error("ServiceBootstrapConfig: " + source + ":"
                               + std::to_string(line_no)
                               + ": expected '<key> = <value>'");
    }

    const std::string key = lower(trim(s.substr(0, eq)));
    const std::string val = trim(s.substr(eq + 1));
    if (key.empty() || val.empty()) {
      throw std::runtime_error("ServiceBootstrapConfig: " + source + ":"
                               + std::to_string(line_no)
                               + ": empty key or value");
    }

    if (key == "domain_id") {
      out.domain_id = parse_int(key, val, source, line_no);
    } else if (key == "config_file") {
      out.config_file = val;
    } else if (key == "topic_config_file") {
      out.topic_config_file = val;
    } else if (key == "qos_xml_file") {
      out.qos_xml_file = val;
    } else if (key == "runtime_arg") {
      out.runtime_args.push_back(val);
    } else {
      throw std::runtime_error("ServiceBootstrapConfig: " + source + ":"
                               + std::to_string(line_no)
                               + ": unknown key '" + key + "'");
    }
  }

  if (out.topic_config_file.empty()) {
    throw std::runtime_error("ServiceBootstrapConfig: " + source
                             + ": missing required key 'topic_config_file'");
  }

  return out;
}

} // namespace

ServiceBootstrapConfig ServiceBootstrapConfig::load_from_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("ServiceBootstrapConfig: cannot open '" + path + "'");
  }
  return parse_stream(in, path);
}

ServiceBootstrapConfig ServiceBootstrapConfig::load_from_string(
    const std::string& contents,
    const std::string& source_label) {
  std::istringstream in(contents);
  return parse_stream(in, source_label);
}

} // namespace pub_sub_open_dds