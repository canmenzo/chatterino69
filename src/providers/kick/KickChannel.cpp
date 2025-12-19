#include "providers/kick/KickChannel.hpp"

#include "Application.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "messages/Emote.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/kick/KickWebSocket.hpp"
#include "providers/seventv/SeventvAPI.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "singletons/Settings.hpp"

#include <QTimer>

#include <algorithm>

namespace chatterino {

KickChannel::KickChannel(const QString &channelSlug)
    : Channel(channelSlug, Channel::Type::Kick)
    , channelSlug_(channelSlug)
    , connectionState_(KickConnectionState::Disconnected)
{
}

KickChannel::~KickChannel()
{
    this->disconnect();
}

void KickChannel::sendMessage(const QString &message)
{
    if (!this->canSendMessage())
    {
        this->addSystemMessage("Cannot send message: not authenticated or "
                               "channel not connected");
        return;
    }

    if (!this->api_)
    {
        this->addSystemMessage("Cannot send message: API not initialized");
        return;
    }

    if (this->broadcasterUserId_ == 0)
    {
        this->addSystemMessage("Cannot send message: channel not resolved");
        return;
    }

    // Send via KickApi using broadcaster_user_id (per Context7 docs)
    this->api_->sendMessage(
        this->broadcasterUserId_, message,
        [this, message](KickApiResult result) {
            if (result.success)
            {
                // Message sent successfully
                bool sent = true;
                this->sendMessageSignal.invoke(this->channelSlug_, message,
                                               sent);
            }
            else
            {
                // Show error
                this->addSystemMessage(
                    QString("Failed to send message: %1").arg(result.errorMessage));

                // Check for rate limit
                if (result.rateLimit.has_value() &&
                    result.rateLimit->remaining <= 0)
                {
                    int secondsUntilReset =
                        QDateTime::currentDateTime().secsTo(
                            result.rateLimit->resetAt);
                    this->addSystemMessage(
                        QString("Rate limited. Try again in %1 seconds.")
                            .arg(secondsUntilReset));
                }
            }
        });
}

bool KickChannel::isMod() const
{
    // TODO: Implement mod status checking
    return false;
}

bool KickChannel::canSendMessage() const
{
    return this->connectionState_ == KickConnectionState::Connected &&
           this->isAuthenticated_ && this->api_ != nullptr;
}

void KickChannel::setApi(std::shared_ptr<KickApi> api)
{
    this->api_ = std::move(api);
}

void KickChannel::setAccount(std::shared_ptr<KickAccount> account)
{
    this->account_ = std::move(account);
    this->isAuthenticated_ = (this->account_ != nullptr &&
                              this->account_->isAuthenticated());
}

void KickChannel::connect()
{
    if (this->connectionState_ == KickConnectionState::Connected ||
        this->connectionState_ == KickConnectionState::Connecting)
    {
        return;
    }

    if (!getSettings()->enableKickIntegration)
    {
        this->addSystemMessage(
            "Kick integration is disabled. Enable it in Settings → General → "
            "Kick Integration");
        return;
    }

    this->setConnectionState(KickConnectionState::Connecting);

    // Initialize WebSocket if needed
    if (!this->webSocket_)
    {
        this->webSocket_ = std::make_unique<KickWebSocket>();

        // Connect signals
        std::ignore = this->webSocket_->messageReceived.connect(
            [this](const KickMessage &msg) {
                this->onMessageReceived(msg);
            });

        std::ignore = this->webSocket_->connectionStateChanged.connect(
            [this](bool connected) {
                if (connected)
                {
                    // Connection established, now resolve channel and subscribe
                    this->resolveAndSubscribe();
                }
                else
                {
                    this->setConnectionState(KickConnectionState::Disconnected);
                    this->addSystemMessage("Disconnected from Kick chat");
                }
            });

        std::ignore = this->webSocket_->errorOccurred.connect(
            [this](const QString &error) {
                this->addSystemMessage(QString("Kick error: %1").arg(error));
                this->handleConnectionError();
            });
    }

    // Connect to WebSocket
    if (!this->webSocket_->connect())
    {
        this->setConnectionState(KickConnectionState::Failed);
        this->addSystemMessage("Failed to initiate Kick WebSocket connection");
    }
}

void KickChannel::disconnect()
{
    if (this->webSocket_)
    {
        if (this->chatroomId_ != 0)
        {
            this->webSocket_->unsubscribe(this->chatroomId_);
        }
        this->webSocket_->disconnect();
    }
    this->setConnectionState(KickConnectionState::Disconnected);
}

void KickChannel::reconnect()
{
    this->disconnect();

    // Reset reconnection attempts on manual reconnect
    this->reconnectAttempts_ = 0;

    QTimer::singleShot(1000, [this] {
        this->connect();
    });
}

KickConnectionState KickChannel::getConnectionState() const
{
    return this->connectionState_;
}

QString KickChannel::getChannelSlug() const
{
    return this->channelSlug_;
}

int KickChannel::getChatroomId() const
{
    return this->chatroomId_;
}

int KickChannel::getBroadcasterUserId() const
{
    return this->broadcasterUserId_;
}

void KickChannel::setAuthenticated(bool authenticated)
{
    this->isAuthenticated_ = authenticated;
}

bool KickChannel::isAuthenticated() const
{
    return this->isAuthenticated_;
}

void KickChannel::fetchRecentMessages()
{
    // T081: Kick history API is not publicly available
    // This is documented in research.md as a limitation
    // Display notice to user instead of silently failing
    this->addSystemMessage(
        "📡 Kick: Showing live messages only. Chat history is not available "
        "for Kick channels.");
}

void KickChannel::refreshSevenTVChannelEmotes()
{
    if (this->broadcasterUserId_ == 0)
    {
        qCDebug(chatterinoKick) << "Cannot load 7TV emotes: broadcaster ID not resolved";
        return;
    }

    auto *seventv = getApp()->getSeventvAPI();
    if (!seventv)
    {
        qCWarning(chatterinoKick) << "7TV API not available";
        return;
    }

    qCDebug(chatterinoKick) << "Loading 7TV emotes for Kick channel"
                           << this->channelSlug_ << "user ID:" << this->broadcasterUserId_;

    // Use Kick-specific 7TV endpoint: https://7tv.io/v3/users/KICK/{user_id}
    seventv->getUserByKickID(
        QString::number(this->broadcasterUserId_),
        [this](const QJsonObject &json) {
            const auto emoteSet = json["emote_set"].toObject();
            const auto parsedEmotes = emoteSet["emotes"].toArray();

            auto emoteMap = seventv::detail::parseEmotes(
                parsedEmotes, SeventvEmoteSetKind::Channel);

            if (!emoteMap.empty())
            {
                this->seventvEmotes_ = std::make_shared<const EmoteMap>(emoteMap);
                qCDebug(chatterinoKick)
                    << "Loaded" << emoteMap.size() << "7TV emotes for Kick channel"
                    << this->channelSlug_;
            }
            else
            {
                qCDebug(chatterinoKick)
                    << "No 7TV emotes found for Kick channel" << this->channelSlug_;
            }
        },
        [this](const auto &result) {
            qCDebug(chatterinoKick)
                << "Failed to load 7TV emotes for Kick channel" << this->channelSlug_
                << ":" << result.formatError();
        });
}

std::shared_ptr<const EmoteMap> KickChannel::getSeventvEmotes() const
{
    return this->seventvEmotes_;
}

void KickChannel::onMessageReceived(const KickMessage &kickMessage)
{
    MessageBuilder builder;

    // Set timestamp from Kick message
    builder.message().serverReceivedTime = kickMessage.createdAt;

    // Add Kick platform indicator (for merged channel support)
    builder.message().flags.set(MessageFlag::Kick);

    // Build username element with color
    builder.emplace<TimestampElement>(kickMessage.createdAt.time());

    // Add badges (if any)
    for (const auto &badge : kickMessage.sender.identity.badges)
    {
        // TODO: Add proper badge rendering
        Q_UNUSED(badge);
    }

    // Add username with color
    QString usernameText = kickMessage.sender.username;
    QColor userColor;
    if (!kickMessage.sender.identity.color.isEmpty())
    {
        userColor = QColor(kickMessage.sender.identity.color);
    }
    else
    {
        // Default color for users without a set color
        userColor = QColor(155, 89, 182);  // Purple default
    }

    builder
        .emplace<TextElement>(usernameText + ":",
                              MessageElementFlag::Username,
                              MessageColor(userColor), FontStyle::ChatMediumBold)
        ->setLink({Link::UserInfo, kickMessage.sender.username});

    // Add message content with emote parsing
    // Kick emotes are in format: [emote:ID:NAME]
    // Example: "Hello [emote:4148074:HYPERCLAP] world"
    this->parseMessageContent(builder, kickMessage.content, kickMessage.emotes);

    // Store Kick-specific metadata
    builder.message().loginName = kickMessage.sender.slug;
    builder.message().displayName = kickMessage.sender.username;

    auto message = builder.release();
    this->addMessage(message, MessageContext::Original);
}

void KickChannel::setConnectionState(KickConnectionState state)
{
    if (this->connectionState_ != state)
    {
        this->connectionState_ = state;
        this->connectionStateChanged.invoke(state);
    }
}

void KickChannel::resolveAndSubscribe()
{
    // Emit connecting message
    this->addSystemMessage(
        QString("Connecting to Kick channel: %1...").arg(this->channelSlug_));

    // Create API instance if needed (for channel resolution)
    if (!this->api_)
    {
        this->api_ = std::make_shared<KickApi>();
    }

    // Use KickApi to resolve channel slug to chatroom ID
    this->api_->resolveChannelInfo(
        this->channelSlug_,
        [this](KickApi::ChannelInfo info) {
            if (!info.success)
            {
                this->setConnectionState(KickConnectionState::Failed);
                this->addSystemMessage(
                    QString("Failed to resolve Kick channel: %1. The channel "
                            "may not exist or is unavailable.")
                        .arg(this->channelSlug_));
                return;
            }

            this->chatroomId_ = info.chatroomId;
            this->broadcasterUserId_ = info.broadcasterUserId;

            qCDebug(chatterinoKick)
                << "Channel" << this->channelSlug_ << "resolved:"
                << "chatroomId=" << this->chatroomId_
                << "broadcasterUserId=" << this->broadcasterUserId_;

            if (this->webSocket_ && this->webSocket_->isConnected())
            {
                this->webSocket_->subscribe(this->chatroomId_);
                this->setConnectionState(KickConnectionState::Connected);
                this->addSystemMessage(
                    QString("Connected to Kick channel: %1")
                        .arg(this->channelSlug_));

                // Load 7TV emotes for this Kick channel
                this->refreshSevenTVChannelEmotes();
            }
            else
            {
                this->setConnectionState(KickConnectionState::Failed);
                this->addSystemMessage(
                    "WebSocket disconnected during channel resolution");
            }
        });
}

void KickChannel::handleConnectionError()
{
    if (this->connectionState_ == KickConnectionState::Reconnecting)
    {
        // Already trying to reconnect
        return;
    }

    this->setConnectionState(KickConnectionState::Reconnecting);
    this->scheduleReconnect();
}

void KickChannel::scheduleReconnect()
{
    if (this->reconnectAttempts_ >= MAX_RECONNECT_ATTEMPTS)
    {
        this->setConnectionState(KickConnectionState::Failed);
        this->addSystemMessage(
            "Failed to reconnect to Kick after multiple attempts. "
            "Use the reconnect option to try again.");
        return;
    }

    // Exponential backoff: 1s, 2s, 4s, 8s, 16s, 30s (capped)
    int delayMs = std::min(
        1000 * (1 << this->reconnectAttempts_),
        30000  // Max 30 seconds
    );

    this->reconnectAttempts_++;

    this->addSystemMessage(
        QString("Reconnecting to Kick in %1 seconds... (attempt %2/%3)")
            .arg(delayMs / 1000)
            .arg(this->reconnectAttempts_)
            .arg(MAX_RECONNECT_ATTEMPTS));

    QTimer::singleShot(delayMs, [this] {
        if (this->connectionState_ == KickConnectionState::Reconnecting)
        {
            this->disconnect();
            this->connect();
        }
    });
}

void KickChannel::addSystemMessage(const QString &text)
{
    auto msg = makeSystemMessage(text);
    this->addMessage(msg, MessageContext::Original);
}

void KickChannel::parseMessageContent(MessageBuilder &builder,
                                      const QString &content,
                                      const std::vector<KickEmote> &emotes)
{
    // Build a flat list of emote ranges sorted by position
    struct EmoteRange {
        int start;
        int end;
        QString emoteId;
        QString emoteName;
    };
    std::vector<EmoteRange> ranges;

    for (const auto &emote : emotes)
    {
        for (const auto &pos : emote.positions)
        {
            EmoteRange range;
            range.start = pos.start;
            range.end = pos.end;
            range.emoteId = emote.emoteId;

            // Extract emote name from content (format: [emote:ID:NAME])
            if (pos.start >= 0 && pos.end <= content.length())
            {
                QString emoteText = content.mid(pos.start, pos.end - pos.start);
                // Parse [emote:ID:NAME] format
                if (emoteText.startsWith("[emote:") && emoteText.endsWith("]"))
                {
                    QStringList parts = emoteText.mid(7, emoteText.length() - 8).split(':');
                    if (parts.size() >= 2)
                    {
                        range.emoteName = parts.last();  // NAME is the last part
                    }
                }
            }

            ranges.push_back(range);
        }
    }

    // Sort by start position
    std::sort(ranges.begin(), ranges.end(),
              [](const EmoteRange &a, const EmoteRange &b) {
                  return a.start < b.start;
              });

    // Build message elements
    int currentPos = 0;
    for (const auto &range : ranges)
    {
        // Add text before this emote
        if (range.start > currentPos)
        {
            QString textBefore = content.mid(currentPos, range.start - currentPos).trimmed();
            if (!textBefore.isEmpty())
            {
                builder.emplace<TextElement>(textBefore, MessageElementFlag::Text,
                                             MessageColor::Text);
            }
        }

        // Add emote element
        // Kick emote CDN URL: https://files.kick.com/emotes/{emote_id}/fullsize
        QString emoteUrl = QString("https://files.kick.com/emotes/%1/fullsize")
                               .arg(range.emoteId);

        // Create emote with the URL
        EmoteId emoteId{range.emoteId};
        EmoteName emoteName{range.emoteName.isEmpty() ? range.emoteId : range.emoteName};

        auto emote = std::make_shared<Emote>(Emote{
            emoteName,
            ImageSet{Url{emoteUrl}},
            Tooltip{emoteName.string},
            Url{emoteUrl},
            false,  // Not zero-width
            emoteId,
        });

        builder.emplace<EmoteElement>(emote, MessageElementFlag::Emote);

        currentPos = range.end;
    }

    // Add remaining text after last emote
    if (currentPos < content.length())
    {
        QString remainingText = content.mid(currentPos).trimmed();
        if (!remainingText.isEmpty())
        {
            builder.emplace<TextElement>(remainingText, MessageElementFlag::Text,
                                         MessageColor::Text);
        }
    }

    // If no emotes, just add the whole content as text
    if (ranges.empty() && !content.isEmpty())
    {
        builder.emplace<TextElement>(content, MessageElementFlag::Text,
                                     MessageColor::Text);
    }
}

}  // namespace chatterino

