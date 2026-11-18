#include "channels/MergedChannel.hpp"

#include "Test.hpp"

using namespace chatterino;

namespace {

ChannelPtr plainChannel(const QString &name, Channel::Type type)
{
    return std::make_shared<Channel>(name, type);
}

}  // namespace

TEST(MergedChannel, carriesThreePlatformsAtOnce)
{
    std::vector<ChannelPtr> sources{
        plainChannel("menzocs", Channel::Type::Twitch),
        plainChannel("menzocs", Channel::Type::Kick),
        plainChannel("@menzocs", Channel::Type::YouTube),
    };
    MergedChannel merged("three", sources);

    EXPECT_EQ(merged.getSourceChannels().size(), 3u);
    EXPECT_TRUE(merged.hasPlatform(Channel::Type::Twitch));
    EXPECT_TRUE(merged.hasPlatform(Channel::Type::Kick));
    EXPECT_TRUE(merged.hasPlatform(Channel::Type::YouTube));
    EXPECT_EQ(merged.getDisplayName(), "T:menzocs + K:menzocs + Y:@menzocs");
}

TEST(MergedChannel, titleNamesEveryPlatform)
{
    // the name the merge was saved under says nothing about its platforms
    MergedChannel same("menzocs + menzocs",
                       {
                           plainChannel("menzocs", Channel::Type::Twitch),
                           plainChannel("menzocs", Channel::Type::Kick),
                       });
    EXPECT_EQ(same.getLocalizedName(), "menzocs (Twitch + Kick)");

    same.addSourceChannel(plainChannel("@MenzoCS", Channel::Type::YouTube));
    EXPECT_EQ(same.getLocalizedName(), "menzocs (Twitch + Kick + YouTube)");

    MergedChannel mixed("xqc + other",
                        {
                            plainChannel("xqc", Channel::Type::Twitch),
                            plainChannel("trainwreckstv", Channel::Type::Kick),
                        });
    EXPECT_EQ(mixed.getLocalizedName(), "Twitch:xqc + Kick:trainwreckstv");
}

TEST(MergedChannel, acceptsAThirdPlatformAfterTheFact)
{
    // This is the path the "Merge with..." dialog takes when the split is
    // already a Twitch+Kick merge: it must extend the merge, not replace it.
    std::vector<ChannelPtr> sources{
        plainChannel("menzocs", Channel::Type::Twitch),
        plainChannel("menzocs", Channel::Type::Kick),
    };
    MergedChannel merged("two", sources);
    ASSERT_FALSE(merged.hasPlatform(Channel::Type::YouTube));

    merged.addSourceChannel(plainChannel("@menzocs", Channel::Type::YouTube));

    EXPECT_EQ(merged.getSourceChannels().size(), 3u);
    EXPECT_TRUE(merged.hasPlatform(Channel::Type::YouTube));
    // The display name is recomputed on read, so it picks the new source up.
    EXPECT_EQ(merged.getDisplayName(), "T:menzocs + K:menzocs + Y:@menzocs");
}

TEST(MergedChannel, doesNotAddTheSameChannelTwice)
{
    auto twitch = plainChannel("menzocs", Channel::Type::Twitch);
    MergedChannel merged("one", {twitch});

    merged.addSourceChannel(twitch);

    EXPECT_EQ(merged.getSourceChannels().size(), 1u);
}

TEST(MergedChannel, routesSendsPerPlatform)
{
    std::vector<ChannelPtr> sources{
        plainChannel("menzocs", Channel::Type::Twitch),
        plainChannel("menzocs", Channel::Type::Kick),
        plainChannel("@menzocs", Channel::Type::YouTube),
    };
    MergedChannel merged("three", sources);

    merged.setPlatformSelection(PlatformSelection::YouTubeOnly);
    EXPECT_EQ(merged.getPlatformSelection(), PlatformSelection::YouTubeOnly);

    ASSERT_NE(merged.sourceForPlatform(Channel::Type::YouTube), nullptr);
    EXPECT_EQ(merged.sourceForPlatform(Channel::Type::YouTube)->getName(),
              "@menzocs");
    EXPECT_EQ(merged.sourceForPlatform(Channel::Type::Kick)->getName(),
              "menzocs");
}
