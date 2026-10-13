#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "smoothoperator/state_manager.hpp"

using namespace smoothoperator::core;
using ::testing::Return;
using ::testing::_;

class MockStreamProvider : public StreamProvider {
public:
    MOCK_METHOD(Result<std::string>, execute, (const std::string&), (override));
    MOCK_METHOD(Result<nlohmann::json>, get_metadata, (), (override));
};

class MockEventBus : public EventBus {
public:
    MOCK_METHOD(void, publish, (const std::string&, const nlohmann::json&), (override));
    MOCK_METHOD(void, subscribe, (const std::string&, (std::function<void(const std::string&, const nlohmann::json&)>)), (override));
};

class StateManagerTest : public ::testing::Test {
protected:
    std::shared_ptr<MockStreamProvider> mock_ls = std::make_shared<MockStreamProvider>();
    std::shared_ptr<MockEventBus> mock_bus = std::make_shared<MockEventBus>();
    std::unique_ptr<StateManager> sm;
    const std::string test_routing_key = "test.state";

    void SetUp() override {
        sm = std::make_unique<StateManager>(mock_ls, mock_bus, test_routing_key, 
                                          smoothoperator::config::CommandsConfig{},
                                          smoothoperator::config::IntentsConfig{});
    }
};

TEST_F(StateManagerTest, HandlesSkipCommand) {
    smoothoperator::config::IntentsConfig intents;
    EXPECT_CALL(*mock_ls, execute("source.skip")).WillOnce(Return(std::string("OK")));
    
    nlohmann::json payload = {};
    sm->handle_dj_command(intents.skip, payload);
}

TEST_F(StateManagerTest, EnrichesTemporalDataOnMetadataChange) {
    nlohmann::json meta;
    meta["title"] = "New Song";
    meta["artist"] = "Artist";
    meta["duration"] = 100.0;

    EXPECT_CALL(*mock_ls, get_metadata()).WillOnce(Return(meta));
    EXPECT_CALL(*mock_ls, execute("source.status")).WillOnce(Return(std::string("playing")));
    EXPECT_CALL(*mock_bus, publish(test_routing_key, _)).Times(2);

    sm->poll();
    
    auto state = sm->get_state_json();
    EXPECT_EQ(state["track"]["title"], "New Song");
    EXPECT_FALSE(state["track"]["start_time"].get<std::string>().empty());
}

TEST_F(StateManagerTest, HandlesStatusRequest) {
    smoothoperator::config::IntentsConfig intents;
    EXPECT_CALL(*mock_bus, publish(test_routing_key, _)).Times(1);
    
    sm->handle_dj_command(intents.status_request, {});
}

TEST(StatusResponseTest, OnOrPlayingCountsAsPlaying) {
    using smoothoperator::core::is_playing_response;
    EXPECT_TRUE(is_playing_response("on"));
    EXPECT_TRUE(is_playing_response("on\r\nEND\r\n"));
    EXPECT_TRUE(is_playing_response("  on \n"));
    EXPECT_TRUE(is_playing_response("source is playing"));
    EXPECT_FALSE(is_playing_response("off\r\nEND\r\n"));
    EXPECT_FALSE(is_playing_response("online"));
    EXPECT_FALSE(is_playing_response(""));
}
