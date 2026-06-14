// SPDX-License-Identifier: Apache-2.0
//
// Lifecycle state-machine test. Drives Service against a MockRuntime so
// assertions target runtime call count/order without depending on OpenDDS.

#include "runtime.h"
#include "pub_sub_open_dds/service.h"
#include "pub_sub_open_dds/service_bootstrap_config.h"
#include "pub_sub_open_dds/topic_config.h"

#include <cstdio>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace pso = pub_sub_open_dds;

using ::testing::_;
using ::testing::InSequence;

// ---------------------------------------------------------------------------
// Mock runtime
// ---------------------------------------------------------------------------

class MockRuntime : public pso::IRuntime {
public:
  // GMock tracks the three lifecycle transition calls — that's what these
  // tests care about. create_writer/create_reader are never invoked by
  // lifecycle tests, so plain stubs avoid pulling TypeAdapter/QoS types
  // into GTest's printer-instantiation path (which GTest 1.8 does eagerly).
  MOCK_METHOD1(init,     void(const pso::ServiceConfig&));
  MOCK_METHOD0(activate, void());
  MOCK_METHOD0(shutdown, void());

  std::shared_ptr<pso::detail::TypedWriterBinding> create_writer(
      const std::string&, const pso::detail::TypeAdapter&,
      const pso::WriterQos&) override { return nullptr; }

  std::shared_ptr<pso::detail::TypedReaderBinding> create_reader(
      const std::string&, const pso::detail::TypeAdapter&,
      const pso::ReaderQos&) override { return nullptr; }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(LifecycleTest, HappyPathCallsRuntimeInOrder) {
  auto rt = std::make_shared<MockRuntime>();
  {
    InSequence seq;
    EXPECT_CALL(*rt, init(_)).Times(1);
    EXPECT_CALL(*rt, activate()).Times(1);
    EXPECT_CALL(*rt, shutdown()).Times(1);
  }

  pso::Service svc(rt);
  EXPECT_EQ(svc.state(), pso::LifecycleState::Created);

  svc.pre_activate(pso::ServiceConfig{});
  EXPECT_EQ(svc.state(), pso::LifecycleState::PreActivated);

  svc.post_activate();
  EXPECT_EQ(svc.state(), pso::LifecycleState::PostActivated);

  svc.deactivate();
  EXPECT_EQ(svc.state(), pso::LifecycleState::Deactivated);
}

TEST(LifecycleTest, OutOfOrderOperationsThrow) {
  auto rt = std::make_shared<MockRuntime>();
  EXPECT_CALL(*rt, init(_)).Times(1);
  EXPECT_CALL(*rt, activate()).Times(1);
  EXPECT_CALL(*rt, shutdown()).Times(1);

  pso::Service svc(rt);
  EXPECT_THROW(svc.post_activate(), std::runtime_error);

  svc.pre_activate(pso::ServiceConfig{});
  EXPECT_THROW(svc.pre_activate(pso::ServiceConfig{}), std::runtime_error);

  svc.post_activate();
  EXPECT_THROW(svc.post_activate(), std::runtime_error);
  EXPECT_THROW(svc.pre_activate(pso::ServiceConfig{}), std::runtime_error);
}

TEST(LifecycleTest, DestructorCallsShutdown) {
  auto rt = std::make_shared<MockRuntime>();
  EXPECT_CALL(*rt, init(_)).Times(1);
  EXPECT_CALL(*rt, activate()).Times(1);
  EXPECT_CALL(*rt, shutdown()).Times(1);
  {
    pso::Service svc(rt);
    svc.pre_activate(pso::ServiceConfig{});
    svc.post_activate();
    // no explicit deactivate — destructor must call it
  }
}

TEST(LifecycleTest, DeactivateFromCreatedIsNoOp) {
  auto rt = std::make_shared<MockRuntime>();
  EXPECT_CALL(*rt, init(_)).Times(0);
  EXPECT_CALL(*rt, shutdown()).Times(0);

  pso::Service svc(rt);
  svc.deactivate();
  EXPECT_EQ(svc.state(), pso::LifecycleState::Deactivated);
}

TEST(LifecycleTest, SubscribeUnregisteredTypeGivesClearError) {
  struct UnregisteredType {};

  auto rt = std::make_shared<MockRuntime>();
  EXPECT_CALL(*rt, init(_)).Times(1);
  EXPECT_CALL(*rt, shutdown()).Times(1);

  pso::Service svc(rt);
  auto topic_cfg = pso::TopicConfig::load_from_string("nope = reliable\n");
  svc.pre_activate(pso::ServiceConfig{}, std::move(topic_cfg));

  try {
    svc.subscribe<UnregisteredType>("nope", [](const UnregisteredType&) {});
    FAIL() << "Expected std::runtime_error for unregistered type";
  } catch (const std::runtime_error& e) {
    const std::string msg = e.what();
    EXPECT_NE(msg.find("no TypeAdapter"), std::string::npos);
    EXPECT_NE(msg.find("pub_sub_open_dds_generate_bindings"), std::string::npos);
  }
}

TEST(LifecycleTest, NullRuntimeIsRejected) {
  EXPECT_THROW(pso::Service svc(nullptr), std::runtime_error);
}

TEST(LifecycleTest, PreActivateFromBootstrapConfigLoadsTopicPolicy) {
  const std::string token = std::to_string(::testing::UnitTest::GetInstance()->random_seed());
  const std::string topic_path = std::string("/tmp/pso_topic_") + token + ".ini";
  const std::string cfg_path   = std::string("/tmp/pso_bootstrap_") + token + ".ini";

  {
    std::ofstream out(topic_path.c_str());
    ASSERT_TRUE(static_cast<bool>(out));
    out << "topicA = reliable\n";
  }
  {
    std::ofstream out(cfg_path.c_str());
    ASSERT_TRUE(static_cast<bool>(out));
    out << "domain_id = 77\n";
    out << "config_file = rtps.ini\n";
    out << "topic_config_file = " << topic_path << "\n";
    out << "runtime_arg = -DCPSDebugLevel\n";
    out << "runtime_arg = 6\n";
  }

  auto rt = std::make_shared<MockRuntime>();
  EXPECT_CALL(*rt, init(_))
      .WillOnce([](const pso::ServiceConfig& cfg) {
        EXPECT_EQ(cfg.domain_id, 77);
        EXPECT_EQ(cfg.config_file, "rtps.ini");
        ASSERT_EQ(cfg.runtime_args.size(), 2u);
        EXPECT_EQ(cfg.runtime_args[0], "-DCPSDebugLevel");
        EXPECT_EQ(cfg.runtime_args[1], "6");
      });
  EXPECT_CALL(*rt, shutdown()).Times(1);

  pso::Service svc(rt);
  svc.pre_activate_from_file(cfg_path);
  EXPECT_EQ(svc.state(), pso::LifecycleState::PreActivated);

  svc.deactivate();

  (void)std::remove(topic_path.c_str());
  (void)std::remove(cfg_path.c_str());
}

