#include "controllers/spoilerguard/SpoilerGuard.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "common/QLogging.hpp"
#include "controllers/spoilerguard/SpoilerGuardPrompt.hpp"
#include "messages/Message.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>

namespace {

using namespace chatterino;
using spoilerguard::Verdict;

const QUrl API_URL{QStringLiteral("https://api.anthropic.com/v1/messages")};

constexpr size_t MAX_BATCH = 25;
/// Lets a burst of lines share one request.
constexpr int DEBOUNCE_MS = 50;
/// Requests run side by side so a line never waits for the one before it to
/// come back, only for its own answer.
constexpr int MAX_OPEN_REQUESTS = 4;
/// A new API account may send 50 requests a minute. A short burst goes out at
/// once; a busy chat is then held to that rate and its batches grow instead.
constexpr double BURST_REQUESTS = 6;
constexpr int REQUEST_INTERVAL_MS = 1200;
constexpr int REQUEST_TIMEOUT_MS = 8000;
constexpr int MAX_ATTEMPTS = 3;
constexpr int RECENT_LINES = 8;
constexpr int WARNING_GAP_MS = 30000;

QString apiKey()
{
    return qEnvironmentVariable("CHATTERINO_ANTHROPIC_API_KEY");
}

QString platformName(const Channel &channel)
{
    switch (channel.getType())
    {
        case Channel::Type::Kick:
            return QStringLiteral("kick");
        case Channel::Type::YouTube:
            return QStringLiteral("youtube");
        default:
            return QStringLiteral("twitch");
    }
}

QString userName(const Message &message)
{
    return message.displayName.isEmpty() ? message.loginName
                                         : message.displayName;
}

/// What identifies the author across messages.
QString userKey(const Message &message)
{
    return (message.loginName.isEmpty() ? message.displayName
                                        : message.loginName)
        .toLower();
}

/// The name as the model should read it.
QString judgedName(const Message &message)
{
    auto name = spoilerguard::spellOut(userName(message));

    // A localized Twitch display name hides the login it stands for. Kick's
    // login repeats the display name and YouTube's is a channel id.
    if (!message.flags.hasAny({MessageFlag::Kick, MessageFlag::YouTube}) &&
        !message.displayName.isEmpty() &&
        message.loginName.compare(message.displayName, Qt::CaseInsensitive) !=
            0)
    {
        name += " / " + spoilerguard::spellOut(message.loginName);
    }
    return name;
}

/// The record the streamer reads after finishing the game, to undo a ban that
/// was a mistake. It is never shown in the client.
void logAction(const Channel &channel, const Message &message, Verdict verdict)
{
    static const char *const NAMES[] = {"shown", "unchecked", "hidden",
                                        "banned"};

    QFile file(getApp()->getPaths().miscDirectory +
               QStringLiteral("/spoiler-guard.log"));
    if (!file.open(QIODevice::Append | QIODevice::Text))
    {
        return;
    }

    auto text = message.messageText;
    text.replace('\n', ' ').replace('\t', ' ');
    const QStringList fields{
        QDateTime::currentDateTime().toString(Qt::ISODate),
        QString::fromLatin1(NAMES[static_cast<size_t>(verdict)]),
        platformName(channel),
        channel.getName(),
        message.loginName,
        message.displayName,
        text,
    };
    file.write(fields.join('\t').toUtf8() + '\n');
}

}  // namespace

namespace chatterino::spoilerguard {

QString spellOut(const QString &name)
{
    QString words;
    for (qsizetype i = 0; i < name.size(); i++)
    {
        const auto c = name[i];
        if (c == '_')
        {
            words += ' ';
            continue;
        }
        if (i > 0 && c.isUpper() && name[i - 1].isLower())
        {
            words += ' ';
        }
        words += c;
    }
    return words;
}

QJsonObject buildRequest(const std::vector<Line> &lines,
                         const QStringList &recent, const QString &game,
                         const QString &model)
{
    QJsonArray messages;
    for (size_t i = 0; i < lines.size(); i++)
    {
        messages.append(QJsonObject{
            {"i", static_cast<int>(i)},
            {lines[i].isName ? "name" : "text", lines[i].text.left(600)},
        });
    }

    const QJsonObject payload{
        {"recent", QJsonArray::fromStringList(recent)},
        {"messages", messages},
    };

    const QJsonObject indexes{
        {"type", "array"},
        {"items", QJsonObject{{"type", "integer"}}},
    };
    const QJsonObject schema{
        {"type", "object"},
        {"properties", QJsonObject{{"ban", indexes}, {"hide", indexes}}},
        {"required", QJsonArray{"ban", "hide"}},
        {"additionalProperties", false},
    };

    return {
        {"model", model},
        // Thinking stays on (the default). Every line waits on this answer, but
        // with thinking off the model got about one entry in forty wrong on
        // tools/spoiler-guard/test_prompt.py, against one in two hundred with
        // it on, for 1.7s a batch instead of 0.9s.
        {"max_tokens", 4096},
        {"output_config",
         QJsonObject{
             {"effort", "low"},
             {"format",
              QJsonObject{{"type", "json_schema"}, {"schema", schema}}},
         }},
        {"system", QJsonArray{QJsonObject{
                       {"type", "text"},
                       {"text", SYSTEM_PROMPT.trimmed().arg(game)},
                       {"cache_control", QJsonObject{{"type", "ephemeral"}}},
                   }}},
        {"messages",
         QJsonArray{QJsonObject{
             {"role", "user"},
             {"content", QString::fromUtf8(QJsonDocument{payload}.toJson(
                             QJsonDocument::Compact))},
         }}},
    };
}

std::optional<std::vector<Verdict>> parseResponse(const QByteArray &body,
                                                  size_t lineCount)
{
    const auto root = QJsonDocument::fromJson(body).object();
    if (root["stop_reason"].toString() != u"end_turn")
    {
        return std::nullopt;
    }

    for (const auto &block : root["content"].toArray())
    {
        const auto obj = block.toObject();
        if (obj["type"].toString() != u"text")
        {
            continue;
        }

        const auto answer =
            QJsonDocument::fromJson(obj["text"].toString().toUtf8());
        if (!answer.isObject() || !answer["ban"].isArray() ||
            !answer["hide"].isArray())
        {
            return std::nullopt;
        }

        std::vector<Verdict> verdicts(lineCount, Verdict::Clean);
        auto mark = [&](const QJsonValue &list, Verdict verdict) {
            for (const auto &index : list.toArray())
            {
                const auto i = index.toInt(-1);
                if (i >= 0 && static_cast<size_t>(i) < lineCount)
                {
                    verdicts[static_cast<size_t>(i)] = verdict;
                }
            }
        };
        mark(answer["hide"], Verdict::Hide);
        mark(answer["ban"], Verdict::Ban);
        return verdicts;
    }

    return std::nullopt;
}

}  // namespace chatterino::spoilerguard

namespace chatterino {

SpoilerGuard::SpoilerGuard(QObject *parent)
    : QObject(parent)
    , network_(new QNetworkAccessManager(this))
    , allowance_(BURST_REQUESTS)
{
    this->allowanceClock_.start();
    // The first line should not also wait for a TLS handshake.
    this->network_->connectToHostEncrypted(API_URL.host());
    this->sendTimer_.setSingleShot(true);
    QObject::connect(&this->sendTimer_, &QTimer::timeout, this, [this] {
        this->sendBatch();
    });
}

SpoilerGuard &SpoilerGuard::instance()
{
    static auto *guard = new SpoilerGuard(QCoreApplication::instance());
    return *guard;
}

bool SpoilerGuard::shouldScreen(const Channel &channel, const Message &message)
{
    if (!getSettings()->spoilerGuardEnabled)
    {
        return false;
    }

    // A whisper from a stranger is shown inline in every open chat, so it is
    // screened wherever it lands.
    if (!message.flags.has(MessageFlag::Whisper))
    {
        switch (channel.getType())
        {
            case Channel::Type::Twitch:
            case Channel::Type::Kick:
                // Only the streamer's own chat: screening someone else's would
                // put every line of a busy channel through the model for
                // nothing.
                if (!channel.isBroadcaster())
                {
                    return false;
                }
                break;
            case Channel::Type::YouTube:
                // A YouTube split only knows it is the streamer's own once
                // they are signed in, and an unscreened chat is the worse
                // mistake.
                break;
            default:
                return false;
        }
    }

    if (message.messageText.isEmpty() ||
        (message.loginName.isEmpty() && message.displayName.isEmpty()) ||
        message.flags.hasAny({MessageFlag::System, MessageFlag::Timeout,
                              MessageFlag::Debug,
                              MessageFlag::ModerationAction}))
    {
        return false;
    }

    // the streamer's own lines
    return message.loginName.compare(channel.getName(), Qt::CaseInsensitive) !=
           0;
}

bool SpoilerGuard::removedUser(const QString &login)
{
    return getSettings()->spoilerGuardEnabled &&
           instance().removedUsers_.contains(login.toLower());
}

void SpoilerGuard::screen(std::shared_ptr<Channel> channel, MessagePtr message,
                          std::optional<MessageFlags> overridingFlags)
{
    this->held_.push_back({
        .channel = channel,
        .message = std::move(message),
        .overridingFlags = overridingFlags,
        .verdict = std::nullopt,
    });
    this->scheduleSend();
}

SpoilerGuard::Held &SpoilerGuard::line(quint64 seq)
{
    return this->held_[static_cast<size_t>(seq - this->frontSeq_)];
}

int SpoilerGuard::msUntilAllowed()
{
    this->allowance_ =
        std::min(BURST_REQUESTS,
                 this->allowance_ +
                     static_cast<double>(this->allowanceClock_.restart()) /
                         REQUEST_INTERVAL_MS);
    if (this->allowance_ >= 1)
    {
        return 0;
    }
    return static_cast<int>((1 - this->allowance_) * REQUEST_INTERVAL_MS) + 1;
}

void SpoilerGuard::scheduleSend()
{
    const auto unsent =
        this->frontSeq_ + this->held_.size() - this->nextUnsent_;
    if (unsent == 0 || this->sendTimer_.isActive() ||
        this->openRequests_ >= MAX_OPEN_REQUESTS)
    {
        return;
    }

    this->sendTimer_.start(std::max(DEBOUNCE_MS, this->msUntilAllowed()));
}

void SpoilerGuard::sendBatch()
{
    const auto end = this->frontSeq_ + this->held_.size();
    if (this->nextUnsent_ == end || this->openRequests_ >= MAX_OPEN_REQUESTS)
    {
        return;
    }
    if (this->msUntilAllowed() > 0)
    {
        this->scheduleSend();
        return;
    }
    this->allowance_ -= 1;

    Batch batch{
        .firstSeq = this->nextUnsent_,
        .count = static_cast<size_t>(
            std::min<quint64>(end - this->nextUnsent_, MAX_BATCH)),
    };
    this->nextUnsent_ += batch.count;

    std::vector<spoilerguard::Line> lines;
    lines.reserve(batch.count * 2);
    for (size_t i = 0; i < batch.count; i++)
    {
        lines.push_back({this->line(batch.firstSeq + i).message->messageText});
    }
    for (size_t i = 0; i < batch.count; i++)
    {
        const auto &message = *this->line(batch.firstSeq + i).message;
        const auto key = userKey(message);
        if (!this->clearedNames_.contains(key) &&
            !this->spoilerNames_.contains(key) &&
            !this->pendingNames_.contains(key))
        {
            this->pendingNames_.insert(key);
            batch.names.append(key);
            lines.push_back({judgedName(message), true});
        }
    }

    batch.body =
        QJsonDocument{spoilerguard::buildRequest(
                          lines, this->recent_, getSettings()->spoilerGuardGame,
                          getSettings()->spoilerGuardModel)}
            .toJson(QJsonDocument::Compact);

    this->openRequests_++;
    this->post(std::move(batch));

    // more than one batch may be waiting
    this->scheduleSend();
}

void SpoilerGuard::post(Batch batch)
{
    const auto key = apiKey();
    if (key.isEmpty())
    {
        if (auto channel = this->line(batch.firstSeq).channel.lock())
        {
            this->warn(*channel,
                       QStringLiteral(
                           "Spoiler guard is on but "
                           "CHATTERINO_ANTHROPIC_API_KEY is not set, so chat "
                           "stays hidden. Set it and restart, or switch the "
                           "guard off in Settings > Platforms."));
        }
        for (const auto &name : batch.names)
        {
            this->pendingNames_.remove(name);
        }
        this->finish(batch, std::vector(batch.count, Verdict::Unchecked));
        return;
    }

    QNetworkRequest request{API_URL};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("x-api-key", key.toUtf8());
    request.setRawHeader("anthropic-version", "2026-10-21");
    request.setTransferTimeout(REQUEST_TIMEOUT_MS);

    QElapsedTimer took;
    took.start();
    auto *reply = this->network_->post(request, batch.body);
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, took, batch = std::move(batch)]() mutable {
                         qCDebug(chatterinoApp)
                             << "Spoiler guard:" << batch.count
                             << "line(s) answered in" << took.elapsed() << "ms";
                         this->handleReply(reply, std::move(batch));
                     });
}

void SpoilerGuard::handleReply(QNetworkReply *reply, Batch batch)
{
    reply->deleteLater();

    const auto status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto body = reply->readAll();

    QString failure;
    std::optional<std::vector<Verdict>> verdicts;
    if (reply->error() != QNetworkReply::NoError)
    {
        failure = QJsonDocument::fromJson(body)["error"]["message"].toString(
            reply->errorString());
        if (status != 0)
        {
            failure = QStringLiteral("HTTP %1: %2").arg(status).arg(failure);
        }

        if (++batch.attempts < MAX_ATTEMPTS)
        {
            qCWarning(chatterinoApp)
                << "Spoiler guard request failed, trying again:" << failure;
            // 429 says how long to back off for
            const auto retryAfter = reply->rawHeader("retry-after").toInt();
            QTimer::singleShot(std::clamp(retryAfter, 1, 10) * 1000, this,
                               [this, batch = std::move(batch)]() mutable {
                                   this->post(std::move(batch));
                               });
            return;
        }
    }
    else
    {
        verdicts = spoilerguard::parseResponse(
            body, batch.count + static_cast<size_t>(batch.names.size()));
        if (!verdicts)
        {
            // A refusal or a cut-off answer will not change on a second try.
            failure = QStringLiteral("the model gave no usable answer");
        }
    }

    if (!verdicts)
    {
        qCWarning(chatterinoApp) << "Spoiler guard gave up:" << failure;
        if (auto channel = this->line(batch.firstSeq).channel.lock())
        {
            this->warn(*channel,
                       QStringLiteral("Spoiler guard could not check %1 "
                                      "message(s) and kept them hidden: %2")
                           .arg(batch.count)
                           .arg(failure));
        }
        for (const auto &name : batch.names)
        {
            this->pendingNames_.remove(name);
        }
        this->finish(batch, std::vector(batch.count, Verdict::Unchecked));
        return;
    }

    // The names were judged after the lines. Only an outright spoiler counts
    // against a name: a "hide" there would cost someone their account over a
    // maybe.
    for (qsizetype i = 0; i < batch.names.size(); i++)
    {
        const auto &name = batch.names[i];
        this->pendingNames_.remove(name);
        if ((*verdicts)[batch.count + static_cast<size_t>(i)] == Verdict::Ban)
        {
            this->spoilerNames_.insert(name);
        }
        else
        {
            this->clearedNames_.insert(name);
        }
    }

    verdicts->resize(batch.count);
    this->finish(batch, *verdicts);
}

void SpoilerGuard::finish(const Batch &batch,
                          const std::vector<Verdict> &verdicts)
{
    for (size_t i = 0; i < batch.count; i++)
    {
        this->line(batch.firstSeq + i).verdict = verdicts[i];
    }

    this->openRequests_--;
    this->flush();
    this->scheduleSend();
}

void SpoilerGuard::flush()
{
    while (!this->held_.empty() && this->held_.front().verdict)
    {
        const auto held = std::move(this->held_.front());
        this->held_.pop_front();
        this->frontSeq_++;

        const auto channel = held.channel.lock();
        if (!channel)
        {
            continue;
        }
        const auto &message = *held.message;

        // A spoiler name may have been found by another request while this
        // line waited its turn.
        const auto verdict = this->spoilerNames_.contains(userKey(message)) &&
                                     *held.verdict != Verdict::Unchecked
                                 ? Verdict::Ban
                                 : *held.verdict;

        if (verdict != Verdict::Clean)
        {
            logAction(*channel, message, verdict);
            if (verdict != Verdict::Unchecked)
            {
                this->removedUsers_.insert(message.loginName.toLower());
                channel->removeSpoiler(held.message, verdict == Verdict::Ban);
            }
            continue;
        }

        this->recent_.append(message.messageText);
        if (this->recent_.size() > RECENT_LINES)
        {
            this->recent_.removeFirst();
        }

        // IrcMessageHandler left this to us, see its mentions branch. A
        // whisper reaches the mentions tab on its own.
        if (channel->isTwitchChannel() &&
            !message.flags.has(MessageFlag::Whisper) &&
            message.flags.hasAll(
                {MessageFlag::Highlighted, MessageFlag::ShowInMentions}))
        {
            getApp()->getTwitch()->getMentionsChannel()->addMessage(
                held.message, MessageContext::Original);
        }

        channel->addScreenedMessage(held.message, MessageContext::Original,
                                    held.overridingFlags);
    }
}

void SpoilerGuard::warn(Channel &channel, const QString &text)
{
    if (this->lastWarning_.isValid() &&
        this->lastWarning_.elapsed() < WARNING_GAP_MS)
    {
        return;
    }
    this->lastWarning_.start();
    channel.addSystemMessage(text);
}

}  // namespace chatterino
