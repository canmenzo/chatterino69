#include "controllers/spoilerguard/SpoilerGuard.hpp"

#include "common/Channel.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/Logging.hpp"
#include "singletons/Settings.hpp"
#include "Test.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace chatterino;
using namespace chatterino::spoilerguard;

namespace {

class MockApplication : public mock::BaseApplication
{
public:
    ILogging *getChatLogger() override
    {
        return &this->logging;
    }

    mock::EmptyLogging logging;
};

/// A chat that records what the guard asks it to remove instead of calling a
/// platform.
class RecordingChannel : public Channel
{
public:
    using Channel::Channel;

    void removeSpoiler(const MessagePtr &message, bool banAuthor) override
    {
        this->removed.emplace_back(message->messageText, banAuthor);
    }

    std::vector<std::pair<QString, bool>> removed;
};

MessagePtr chatLine(const QString &user, const QString &text)
{
    MessageBuilder builder;
    builder.emplace<TextElement>(text, MessageElementFlag::Text);
    builder.message().loginName = user.toLower();
    builder.message().displayName = user;
    builder.message().messageText = text;
    return builder.release();
}

/// What viewers wrote, without the lines the client adds itself.
std::vector<MessagePtr> chatLines(const Channel &channel)
{
    std::vector<MessagePtr> lines;
    for (const auto &message : channel.getMessageSnapshot())
    {
        if (!message->flags.has(MessageFlag::System))
        {
            lines.push_back(message);
        }
    }
    return lines;
}

size_t systemLinesWith(const Channel &channel, const QString &text)
{
    size_t count = 0;
    for (const auto &message : channel.getMessageSnapshot())
    {
        if (message->flags.has(MessageFlag::System) &&
            message->messageText.contains(text))
        {
            count++;
        }
    }
    return count;
}

QByteArray reply(const QString &stopReason, const QString &text)
{
    return QJsonDocument{
        QJsonObject{
            {"stop_reason", stopReason},
            {"content",
             QJsonArray{
                 // the thinking block that comes ahead of the answer
                 QJsonObject{{"type", "thinking"}, {"thinking", ""}},
                 QJsonObject{{"type", "text"}, {"text", text}},
             }},
        }}
        .toJson();
}

}  // namespace

TEST(SpoilerGuard, readsOneVerdictPerLine)
{
    auto verdicts =
        parseResponse(reply("end_turn", R"({"ban":[1],"hide":[2]})"), 4);

    ASSERT_TRUE(verdicts.has_value());
    EXPECT_EQ(*verdicts, (std::vector{Verdict::Clean, Verdict::Ban,
                                      Verdict::Hide, Verdict::Clean}));
}

TEST(SpoilerGuard, banWinsWhenALineIsInBothLists)
{
    auto verdicts =
        parseResponse(reply("end_turn", R"({"ban":[0],"hide":[0]})"), 1);

    ASSERT_TRUE(verdicts.has_value());
    EXPECT_EQ(verdicts->front(), Verdict::Ban);
}

TEST(SpoilerGuard, ignoresIndexesOutsideTheBatch)
{
    auto verdicts =
        parseResponse(reply("end_turn", R"({"ban":[7,-1],"hide":[2]})"), 2);

    ASSERT_TRUE(verdicts.has_value());
    EXPECT_EQ(*verdicts, (std::vector{Verdict::Clean, Verdict::Clean}));
}

TEST(SpoilerGuard, rejectsAnAnswerItCannotUse)
{
    // nothing here may be read as "all clean": that would show every line
    EXPECT_FALSE(parseResponse(reply("refusal", ""), 2).has_value());
    EXPECT_FALSE(parseResponse(reply("max_tokens", R"({"ban":[],"hide")"), 2)
                     .has_value());
    EXPECT_FALSE(parseResponse(reply("end_turn", "all fine"), 2).has_value());
    EXPECT_FALSE(
        parseResponse(reply("end_turn", R"({"ban":[]})"), 2).has_value());
    EXPECT_FALSE(parseResponse("<html>bad gateway</html>", 2).has_value());
}

TEST(SpoilerGuard, splitsARunTogetherName)
{
    EXPECT_EQ(spellOut("JasonDiesAtTheEnd"), "Jason Dies At The End");
    EXPECT_EQ(spellOut("lucia_ihanet_ediyor"), "lucia ihanet ediyor");
    EXPECT_EQ(spellOut("kaan34"), "kaan34");
    EXPECT_EQ(spellOut("RIPmyWallet"), "RIPmy Wallet");
}

TEST(SpoilerGuard, leavesChatAloneWhenSwitchedOff)
{
    MockApplication app;
    auto channel =
        std::make_shared<RecordingChannel>("@test", Channel::Type::YouTube);

    channel->addMessage(chatLine("kaan34", "selam"), MessageContext::Original);

    EXPECT_EQ(channel->getMessageSnapshot().size(), 1u);
}

TEST(SpoilerGuard, screensOwnChatsAndEveryWhisper)
{
    MockApplication app;
    getSettings()->spoilerGuardEnabled = true;

    const auto line = chatLine("kaan34", "selam");
    MessageBuilder whisperBuilder;
    whisperBuilder.message().loginName = "stranger";
    whisperBuilder.message().messageText = "Jason dies";
    whisperBuilder.message().flags.set(MessageFlag::Whisper);
    const auto whisper = whisperBuilder.release();

    // someone else's Twitch chat: the plain Channel is not ours
    Channel other("xqc", Channel::Type::Twitch);
    EXPECT_FALSE(SpoilerGuard::shouldScreen(other, *line));
    EXPECT_TRUE(SpoilerGuard::shouldScreen(other, *whisper));

    Channel whispers("/whispers", Channel::Type::TwitchWhispers);
    EXPECT_TRUE(SpoilerGuard::shouldScreen(whispers, *whisper));

    Channel youtube("@test", Channel::Type::YouTube);
    EXPECT_TRUE(SpoilerGuard::shouldScreen(youtube, *line));
    EXPECT_FALSE(
        SpoilerGuard::shouldScreen(youtube, *makeSystemMessage("joined")));

    Channel merged("merged", Channel::Type::Merged);
    EXPECT_FALSE(SpoilerGuard::shouldScreen(merged, *line));

    // A channel named in the settings is guarded with no login behind it: a
    // Kick login that had quietly expired once left the whole chat unscreened.
    Channel kick("MenzoCS", Channel::Type::Kick);
    EXPECT_FALSE(SpoilerGuard::shouldScreen(kick, *line));
    getSettings()->spoilerGuardChannels = "someoneelse, menzocs";
    EXPECT_TRUE(SpoilerGuard::shouldScreen(kick, *line));
    EXPECT_FALSE(SpoilerGuard::shouldScreen(other, *line));
    getSettings()->spoilerGuardChannels = "";

    getSettings()->spoilerGuardEnabled = false;
    EXPECT_FALSE(SpoilerGuard::shouldScreen(youtube, *line));
}

/// The whole path against the real model: hold, ask, show or remove. Runs only
/// when an API key is around, so it never runs in CI.
TEST(SpoilerGuard, holdsLinesUntilTheModelHasReadThem)
{
    if (qEnvironmentVariableIsEmpty("CHATTERINO_ANTHROPIC_API_KEY"))
    {
        GTEST_SKIP() << "CHATTERINO_ANTHROPIC_API_KEY is not set";
    }

    MockApplication app;
    getSettings()->spoilerGuardEnabled = true;
    auto channel =
        std::make_shared<RecordingChannel>("@live", Channel::Type::YouTube);

    channel->addMessage(chatLine("kaan34", "selam hocam hayirli yayinlar"),
                        MessageContext::Original);
    channel->addMessage(chatLine("leaker", "Jason sonunda ölüyor, final "
                                           "görevinde Lucia ihanet ediyor"),
                        MessageContext::Original);
    channel->addMessage(chatLine("JasonDiesAtTheEnd", "selam"),
                        MessageContext::Original);
    channel->addMessage(chatLine("mert_x", "grafikler efsane lan"),
                        MessageContext::Original);
    channel->addSystemMessage("joined");

    // nothing a viewer wrote is shown yet, system lines pass straight through
    ASSERT_TRUE(chatLines(*channel).empty());
    EXPECT_EQ(systemLinesWith(*channel, "joined"), 1u);
    EXPECT_EQ(systemLinesWith(*channel, "is screening this chat"), 1u)
        << "the chat is told once that the guard is at work";

    QElapsedTimer waited;
    waited.start();
    while (chatLines(*channel).size() + channel->removed.size() < 4 &&
           waited.elapsed() < 30000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    getSettings()->spoilerGuardEnabled = false;

    const auto shown = chatLines(*channel);
    ASSERT_EQ(shown.size(), 2u) << "took " << waited.elapsed() << "ms";
    EXPECT_EQ(shown[0]->messageText, "selam hocam hayirli yayinlar");
    EXPECT_EQ(shown[1]->messageText, "grafikler efsane lan");

    ASSERT_EQ(channel->removed.size(), 2u);
    EXPECT_TRUE(channel->removed[0].first.startsWith("Jason sonunda"));
    EXPECT_TRUE(channel->removed[0].second) << "a spoiler bans its author";
    EXPECT_EQ(channel->removed[1].first, "selam");
    EXPECT_TRUE(channel->removed[1].second) << "so does a spoiler name";
    EXPECT_TRUE(SpoilerGuard::removedUser("leaker") == false)
        << "the guard is off again, so nothing is filtered";
}

/// Lines that arrive a moment apart go out in separate requests that run side
/// by side. They must still be shown in the order they were written, and each
/// one about as fast as a single request takes.
TEST(SpoilerGuard, showsLinesInTheOrderTheyArrived)
{
    if (qEnvironmentVariableIsEmpty("CHATTERINO_ANTHROPIC_API_KEY"))
    {
        GTEST_SKIP() << "CHATTERINO_ANTHROPIC_API_KEY is not set";
    }

    MockApplication app;
    getSettings()->spoilerGuardEnabled = true;
    auto channel =
        std::make_shared<RecordingChannel>("@order", Channel::Type::YouTube);

    QElapsedTimer clock;
    clock.start();
    std::vector<qint64> writtenAt;
    std::vector<qint64> shownAt;
    std::ignore = channel->messageAppended.connect([&](auto &message, auto) {
        if (!message->flags.has(MessageFlag::System))
        {
            shownAt.push_back(clock.elapsed());
        }
    });

    const QStringList texts{
        "selam hocam",
        "grafikler efsane lan",
        "kaç fps alıyorsun",
        "abi şuradan sağa dön",
        "bu görev zor",
        "yayın kaça kadar",
        "KEKW",
        "radyoda çalan şarkı ne",
    };
    for (const auto &text : texts)
    {
        writtenAt.push_back(clock.elapsed());
        channel->addMessage(chatLine("kaan34", text), MessageContext::Original);

        // the next viewer types a third of a second later
        const auto next = clock.elapsed() + 330;
        while (clock.elapsed() < next)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
    }
    while (shownAt.size() < static_cast<size_t>(texts.size()) &&
           clock.elapsed() < 30000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    getSettings()->spoilerGuardEnabled = false;

    const auto shown = chatLines(*channel);
    ASSERT_EQ(shown.size(), static_cast<size_t>(texts.size()));
    qint64 slowest = 0;
    for (qsizetype i = 0; i < texts.size(); i++)
    {
        EXPECT_EQ(shown[static_cast<size_t>(i)]->messageText, texts[i]);
        const auto delay =
            shownAt[static_cast<size_t>(i)] - writtenAt[static_cast<size_t>(i)];
        slowest = std::max(slowest, delay);
        std::cout << "  held for " << delay << "ms: " << texts[i].toStdString()
                  << '\n';
    }
    EXPECT_LT(slowest, 4000);
}

/// A spoiler that got through cannot be unread, so a line nobody could check
/// stays hidden and nobody is banned for it.
TEST(SpoilerGuard, keepsLinesHiddenWhenTheModelCannotBeReached)
{
    const auto realKey = qgetenv("CHATTERINO_ANTHROPIC_API_KEY");
    if (realKey.isEmpty())
    {
        GTEST_SKIP() << "needs the network, runs only next to the live test";
    }

    MockApplication app;
    getSettings()->spoilerGuardEnabled = true;
    qputenv("CHATTERINO_ANTHROPIC_API_KEY", "sk-ant-not-a-real-key");
    auto channel =
        std::make_shared<RecordingChannel>("@down", Channel::Type::YouTube);

    channel->addMessage(chatLine("kaan34", "selam hocam"),
                        MessageContext::Original);

    QElapsedTimer waited;
    waited.start();
    while (systemLinesWith(*channel, "could not check") == 0 &&
           waited.elapsed() < 30000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    getSettings()->spoilerGuardEnabled = false;
    qputenv("CHATTERINO_ANTHROPIC_API_KEY", realKey);

    // the streamer is told, the line is not shown, nobody is banned
    EXPECT_EQ(systemLinesWith(*channel, "could not check"), 1u);
    EXPECT_TRUE(chatLines(*channel).empty());
    EXPECT_TRUE(channel->removed.empty());
}

TEST(SpoilerGuard, sendsLinesNamesAndContext)
{
    const auto request =
        buildRequest({{"selam"}, {"Jason Dies At The End", true}}, {"NOOO"},
                     "Test Game", "claude-haiku-5-5");

    EXPECT_EQ(request["model"].toString(), "claude-haiku-5-5");
    EXPECT_TRUE(request["system"][0]["text"].toString().contains("Test Game"));
    EXPECT_FALSE(request["system"][0]["text"].toString().contains("%1"));

    const auto payload = QJsonDocument::fromJson(
        request["messages"][0]["content"].toString().toUtf8());
    EXPECT_EQ(payload["recent"][0].toString(), "NOOO");
    EXPECT_EQ(payload["messages"][0]["text"].toString(), "selam");
    EXPECT_EQ(payload["messages"][1]["i"].toInt(), 1);
    EXPECT_EQ(payload["messages"][1]["name"].toString(),
              "Jason Dies At The End");
}
