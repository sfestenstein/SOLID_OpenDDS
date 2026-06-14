// SPDX-License-Identifier: Apache-2.0
//
// ServiceBootstrapConfig parser tests. Focuses on plain-type config loading
// and input validation; does not require OpenDDS transport.

#include "pub_sub_open_dds/service_bootstrap_config.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace pso = pub_sub_open_dds;

namespace {

void fail(const std::string& m) {
  std::cerr << "service_bootstrap_config_test FAIL: " << m << "\n";
  std::exit(2);
}

#define EXPECT(cond) do { if (!(cond)) fail(std::string(__FILE__ ":") + std::to_string(__LINE__) + " expected " #cond); } while (0)

template <class F>
bool throws_error(F&& f) {
  try { f(); }
  catch (const std::runtime_error&) { return true; }
  catch (...) { return false; }
  return false;
}

} // namespace

int main() {
  // ---- happy path ------------------------------------------------------
  {
    const auto cfg = pso::ServiceBootstrapConfig::load_from_string(R"(
      domain_id = 43
      config_file = rtps.ini
      topic_config_file = sensor_topics.ini
      qos_xml_file = radar_qos.xml
      runtime_arg = -DCPSDebugLevel
      runtime_arg = 6
    )");

    EXPECT(cfg.domain_id == 43);
    EXPECT(cfg.config_file == "rtps.ini");
    EXPECT(cfg.topic_config_file == "sensor_topics.ini");
    EXPECT(cfg.qos_xml_file == "radar_qos.xml");
    EXPECT(cfg.runtime_args.size() == 2);
    EXPECT(cfg.runtime_args[0] == "-DCPSDebugLevel");
    EXPECT(cfg.runtime_args[1] == "6");
  }

  // ---- unknown key is rejected ----------------------------------------
  {
    EXPECT(throws_error([] {
      (void)pso::ServiceBootstrapConfig::load_from_string(
          "topic_config_file=t.ini\nnope=x\n");
    }));
  }

  // ---- missing required topic file is rejected ------------------------
  {
    EXPECT(throws_error([] {
      (void)pso::ServiceBootstrapConfig::load_from_string("domain_id=42\n");
    }));
  }

  // ---- malformed numeric value is rejected ----------------------------
  {
    EXPECT(throws_error([] {
      (void)pso::ServiceBootstrapConfig::load_from_string(
          "topic_config_file=t.ini\ndomain_id=forty-two\n");
    }));
  }

  std::cout << "service_bootstrap_config_test PASS\n";
  return 0;
}