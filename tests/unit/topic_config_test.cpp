// SPDX-License-Identifier: Apache-2.0
//
// TopicConfig parsing test. Drives the INI subset through
// load_from_string so the test doesn't need to drop files on disk; checks
// trim / comments / unknown-profile fallback / unbound-topic fallback.

#include "pub_sub_open_dds/error.h"
#include "pub_sub_open_dds/qos.h"
#include "pub_sub_open_dds/topic_config.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace pso = pub_sub_open_dds;

namespace {

void fail(const std::string& m) {
  std::cerr << "topic_config_test FAIL: " << m << "\n";
  std::exit(2);
}

#define EXPECT(cond) do { if (!(cond)) fail(std::string(__FILE__ ":") + std::to_string(__LINE__) + " expected " #cond); } while (0)

template <class F>
bool throws_error(F&& f) {
  try { f(); }
  catch (const pso::Error&) { return true; }
  catch (...) { return false; }
  return false;
}

} // namespace

int main() {
  // ---- happy path: comments, whitespace, mixed-case profile names ----
  {
    auto cfg = pso::TopicConfig::load_from_string(R"(
      # comment line
      ; also a comment
        TopicA   =   reliable    # trailing comment
      TopicB=best_effort
        TopicC = ReliAble
    )");
    EXPECT(cfg.size() == 3);
    EXPECT(cfg.has_binding("TopicA"));
    EXPECT(cfg.has_binding("TopicB"));
    EXPECT(cfg.has_binding("TopicC"));
    EXPECT(!cfg.has_binding("nope"));
    EXPECT(cfg.profile_name_for("TopicA") == "reliable");
    EXPECT(cfg.profile_name_for("TopicC") == "ReliAble");

    // Resolution: the built-in lookup is case-insensitive; TopicC's
    // mixed-case 'ReliAble' resolves to the reliable profile.
    auto wq = cfg.writer_qos_for("TopicC", pso::qos::best_effort());
    EXPECT(wq.profile().reliable);
  }

  // ---- unbound topic falls back to default ---------------------------
  {
    auto cfg = pso::TopicConfig::load_from_string("Bound = reliable\n");
    auto wq  = cfg.writer_qos_for("Unbound", pso::qos::best_effort());
    EXPECT(!wq.profile().reliable);
    EXPECT(wq.profile().name == "best_effort");
  }

  // ---- unknown profile falls back to default with a warning ----------
  // (We don't capture stderr here; just verify the fallback semantics.)
  {
    auto cfg = pso::TopicConfig::load_from_string("T = no_such_profile\n");
    auto rq  = cfg.reader_qos_for("T", pso::qos::reliable());
    EXPECT(rq.profile().name == "reliable");
  }

  // ---- xml: prefix without a loaded XML file falls back ---------------
  {
    auto cfg = pso::TopicConfig::load_from_string("T = xml:Foo\n");
    auto rq  = cfg.reader_qos_for("T", pso::qos::best_effort());
    EXPECT(rq.profile().name == "best_effort");
  }

  // ---- malformed lines throw ------------------------------------------
  {
    EXPECT(throws_error([]{
      pso::TopicConfig::load_from_string("no equals here\n");
    }));
    EXPECT(throws_error([]{
      pso::TopicConfig::load_from_string("= empty_key\n");
    }));
    EXPECT(throws_error([]{
      pso::TopicConfig::load_from_string("empty_value =\n");
    }));
  }

  // ---- empty document is OK (zero bindings) ---------------------------
  {
    auto cfg = pso::TopicConfig::load_from_string("   \n# nothing\n\n");
    EXPECT(cfg.size() == 0);
  }

  std::cout << "topic_config_test PASS\n";
  return 0;
}
