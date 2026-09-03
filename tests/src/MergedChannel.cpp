/**
 * T078: Unit tests for MergedChannel multi-platform send logic
 */

#include "channels/MergedChannel.hpp"

#include "common/Channel.hpp"
#include "messages/Message.hpp"

#include <gtest/gtest.h>
#include <QString>

#include <memory>
#include <vector>

using namespace chatterino;

namespace {

// Mock channel for testing
class MockChannel : public Channel
{
public:
    MockChannel(const QString &name, Channel::Type type)
        : Channel(name, type)
    {
    }

    void sendMessage(const QString &message) override
    {
        lastSentMessage = message;
        sendCount++;
    }

    bool isMod() const override
    {
        return isMod_;
    }

    bool canSendMessage() const override
    {
        return canSend_;
    }

    void setCanSend(bool value)
    {
        canSend_ = value;
    }

    void setIsMod(bool value)
    {
        isMod_ = value;
    }

    QString lastSentMessage;
    int sendCount{0};

private:
    bool canSend_{true};
    bool isMod_{false};
};

}  // namespace

class MergedChannelTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        twitchChannel =
            std::make_shared<MockChannel>("testuser", Channel::Type::Twitch);
        kickChannel =
            std::make_shared<MockChannel>("testuser", Channel::Type::Kick);

        std::vector<ChannelPtr> sources = {twitchChannel, kickChannel};
        mergedChannel = std::make_shared<MergedChannel>("testuser", sources);
    }

    std::shared_ptr<MockChannel> twitchChannel;
    std::shared_ptr<MockChannel> kickChannel;
    std::shared_ptr<MergedChannel> mergedChannel;
};

TEST_F(MergedChannelTest, DisplayNameShowsBothPlatforms)
{
    QString displayName = mergedChannel->getDisplayName();

    EXPECT_TRUE(displayName.contains("T:"));
    EXPECT_TRUE(displayName.contains("K:"));
    EXPECT_TRUE(displayName.contains("testuser"));
}

TEST_F(MergedChannelTest, DefaultPlatformSelectionIsBoth)
{
    EXPECT_EQ(mergedChannel->getPlatformSelection(), PlatformSelection::Both);
}

TEST_F(MergedChannelTest, SendMessageToBothPlatforms)
{
    mergedChannel->setPlatformSelection(PlatformSelection::Both);
    mergedChannel->sendMessage("Hello world!");

    EXPECT_EQ(twitchChannel->sendCount, 1);
    EXPECT_EQ(kickChannel->sendCount, 1);
    EXPECT_EQ(twitchChannel->lastSentMessage, "Hello world!");
    EXPECT_EQ(kickChannel->lastSentMessage, "Hello world!");
}

TEST_F(MergedChannelTest, SendMessageToTwitchOnly)
{
    mergedChannel->setPlatformSelection(PlatformSelection::TwitchOnly);
    mergedChannel->sendMessage("Twitch message");

    EXPECT_EQ(twitchChannel->sendCount, 1);
    EXPECT_EQ(kickChannel->sendCount, 0);
    EXPECT_EQ(twitchChannel->lastSentMessage, "Twitch message");
}

TEST_F(MergedChannelTest, SendMessageToKickOnly)
{
    mergedChannel->setPlatformSelection(PlatformSelection::KickOnly);
    mergedChannel->sendMessage("Kick message");

    EXPECT_EQ(twitchChannel->sendCount, 0);
    EXPECT_EQ(kickChannel->sendCount, 1);
    EXPECT_EQ(kickChannel->lastSentMessage, "Kick message");
}

TEST_F(MergedChannelTest, PlatformSelectionChangedSignal)
{
    PlatformSelection receivedSelection = PlatformSelection::Both;
    int signalCount = 0;

    mergedChannel->platformSelectionChanged.connect(
        [&receivedSelection, &signalCount](PlatformSelection selection) {
            receivedSelection = selection;
            signalCount++;
        });

    mergedChannel->setPlatformSelection(PlatformSelection::TwitchOnly);

    EXPECT_EQ(signalCount, 1);
    EXPECT_EQ(receivedSelection, PlatformSelection::TwitchOnly);
}

TEST_F(MergedChannelTest, CanSendMessageWhenAtLeastOnePlatformAvailable)
{
    // Both can send
    EXPECT_TRUE(mergedChannel->canSendMessage());

    // Twitch can't send, but Kick can (with Both selection)
    twitchChannel->setCanSend(false);
    EXPECT_TRUE(mergedChannel->canSendMessage());

    // Neither can send
    kickChannel->setCanSend(false);
    EXPECT_FALSE(mergedChannel->canSendMessage());
}

TEST_F(MergedChannelTest, CanSendMessageRespectsPlatformSelection)
{
    // Twitch only, but Twitch can't send
    twitchChannel->setCanSend(false);
    mergedChannel->setPlatformSelection(PlatformSelection::TwitchOnly);
    EXPECT_FALSE(mergedChannel->canSendMessage());

    // Kick only, and Kick can send
    mergedChannel->setPlatformSelection(PlatformSelection::KickOnly);
    EXPECT_TRUE(mergedChannel->canSendMessage());
}

TEST_F(MergedChannelTest, HasPlatformReturnsCorrectly)
{
    EXPECT_TRUE(mergedChannel->hasPlatform(Channel::Type::Twitch));
    EXPECT_TRUE(mergedChannel->hasPlatform(Channel::Type::Kick));
    EXPECT_FALSE(mergedChannel->hasPlatform(Channel::Type::None));
}

TEST_F(MergedChannelTest, SourceForPlatformPicksTheRightChannel)
{
    EXPECT_EQ(mergedChannel->sourceForPlatform(Channel::Type::Twitch),
              twitchChannel);
    EXPECT_EQ(mergedChannel->sourceForPlatform(Channel::Type::Kick),
              kickChannel);
    EXPECT_EQ(mergedChannel->sourceForPlatform(Channel::Type::None), nullptr);
}

TEST_F(MergedChannelTest, GetSourceChannelsReturnsAll)
{
    const auto &sources = mergedChannel->getSourceChannels();

    EXPECT_EQ(sources.size(), 2);
}

TEST_F(MergedChannelTest, IsModReturnsTrueIfAnySourceIsMod)
{
    EXPECT_FALSE(mergedChannel->isMod());

    twitchChannel->setIsMod(true);
    EXPECT_TRUE(mergedChannel->isMod());
}

TEST_F(MergedChannelTest, UnmergeReturnsSources)
{
    auto sources = mergedChannel->unmerge();

    EXPECT_EQ(sources.size(), 2);
    EXPECT_TRUE(mergedChannel->getSourceChannels().empty());
}

TEST_F(MergedChannelTest, AddSourceChannel)
{
    auto newChannel =
        std::make_shared<MockChannel>("other", Channel::Type::Twitch);
    mergedChannel->addSourceChannel(newChannel);

    EXPECT_EQ(mergedChannel->getSourceChannels().size(), 3);
}

TEST_F(MergedChannelTest, RemoveSourceChannel)
{
    mergedChannel->removeSourceChannel(kickChannel);

    const auto &sources = mergedChannel->getSourceChannels();
    EXPECT_EQ(sources.size(), 1);
    EXPECT_EQ(sources[0]->getType(), Channel::Type::Twitch);
}

TEST_F(MergedChannelTest, ChannelTypeIsMerged)
{
    EXPECT_EQ(mergedChannel->getType(), Channel::Type::Merged);
}

TEST_F(MergedChannelTest, SetPlatformSelectionDoesNotEmitIfUnchanged)
{
    int signalCount = 0;
    mergedChannel->platformSelectionChanged.connect(
        [&signalCount](PlatformSelection) {
            signalCount++;
        });

    // Set to current value - should not emit
    mergedChannel->setPlatformSelection(PlatformSelection::Both);
    EXPECT_EQ(signalCount, 0);

    // Set to different value - should emit
    mergedChannel->setPlatformSelection(PlatformSelection::TwitchOnly);
    EXPECT_EQ(signalCount, 1);
}
