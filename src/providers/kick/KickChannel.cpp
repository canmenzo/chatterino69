#include "providers/kick/KickChannel.hpp"

#include "Application.hpp"
#include "common/Aliases.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/kick/KickWebSocket.hpp"
#include "providers/seventv/SeventvAPI.hpp"
#include "providers/seventv/SeventvBadges.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/seventv/SeventvPaints.hpp"
#include "singletons/Settings.hpp"

#include <QRegularExpression>

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

bool KickChannel::isLive() const
{
    return this->isLive_;
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
        // TODO: Add proper native Kick badge rendering
        Q_UNUSED(badge);
    }

    // Load 7TV cosmetics (paints, badges) for this user if we haven't already
    if (kickMessage.sender.id > 0)
    {
        this->loadUserSevenTVCosmetics(kickMessage.sender.id,
                                       kickMessage.sender.username);
    }

    // Append 7TV badge for this user (if they have one assigned)
    QString kickUserIdStr = QString::number(kickMessage.sender.id);
    if (auto badge = getApp()->getSeventvBadges()->getBadge(UserId{kickUserIdStr}))
    {
        builder.emplace<BadgeElement>(*badge, MessageElementFlag::BadgeSevenTV);
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

void KickChannel::loadUserSevenTVCosmetics(int kickUserId,
                                           const QString &userName)
{
    // Check if we've already loaded cosmetics for this user
    {
        std::shared_lock lock(this->loadedUsersMutex_);
        if (this->usersWithLoadedCosmetics_.contains(kickUserId))
        {
            return;  // Already loaded
        }
    }

    // Mark as loading (even before the request completes to avoid duplicates)
    {
        std::unique_lock lock(this->loadedUsersMutex_);
        this->usersWithLoadedCosmetics_.insert(kickUserId);
    }

    // 7TV Kick endpoint returns a "connection" object, not the full user.
    // The connection contains "emote_set_id" which is the 7TV user ID.
    // We need a two-step lookup:
    // 1. GET /v3/users/KICK/{kick_id} -> get emote_set_id (7TV user ID)
    // 2. GET /v3/users/{7tv_user_id} -> get full profile with paint
    getApp()->getSeventvAPI()->getUserByKickID(
        QString::number(kickUserId),
        [userName, kickUserId](const QJsonObject &connectionJson) {
            // The Kick endpoint returns a connection object with emote_set_id
            QString seventvUserId = connectionJson["emote_set_id"].toString();

            if (seventvUserId.isEmpty())
            {
                // Try alternative field names
                seventvUserId = connectionJson["user_id"].toString();
            }

            if (seventvUserId.isEmpty())
            {
                qCDebug(chatterinoKick)
                    << "No 7TV user ID found for Kick user" << kickUserId;
                return;
            }

            qCDebug(chatterinoKick) << "Found 7TV user ID" << seventvUserId
                                    << "for Kick user" << userName;

            // Now fetch the full user profile to get cosmetics (including paint and badge)
            getApp()->getSeventvAPI()->getUserByID(
                seventvUserId,
                [userName, kickUserId](const QJsonObject &userJson) {
                    // Full user profile includes style.paint and style.badge
                    auto *paints = getApp()->getSeventvPaints();
                    if (paints)
                    {
                        // Pass Kick user ID for badge assignment
                        paints->loadUserCosmetics(userJson, userName,
                                                  QString::number(kickUserId));
                    }
                },
                [userName](const NetworkResult &) {
                    qCDebug(chatterinoKick)
                        << "Failed to load 7TV user profile for" << userName;
                });
        },
        [kickUserId](const NetworkResult &result) {
            // Not an error if 7TV doesn't have this user
            if (result.status() != 404)
            {
                qCDebug(chatterinoKick)
                    << "Failed to load 7TV connection for Kick user"
                    << kickUserId << ":" << result.formatError();
            }
        });
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

            // Update live status
            bool wasLive = this->isLive_;
            this->isLive_ = info.isLive;
            this->streamTitle_ = info.streamTitle;
            this->viewerCount_ = info.viewerCount;

            if (wasLive != this->isLive_)
            {
                this->liveStatusChanged.invoke();
            }

            qCDebug(chatterinoKick)
                << "Channel" << this->channelSlug_ << "resolved:"
                << "chatroomId=" << this->chatroomId_
                << "broadcasterUserId=" << this->broadcasterUserId_
                << "isLive=" << this->isLive_;

            if (this->webSocket_ && this->webSocket_->isConnected())
            {
                this->webSocket_->subscribe(this->chatroomId_);
                this->setConnectionState(KickConnectionState::Connected);

                QString statusMsg = QString("Connected to Kick channel: %1")
                                        .arg(this->channelSlug_);
                if (this->isLive_)
                {
                    statusMsg += QString(" [LIVE - %1 viewers]")
                                     .arg(this->viewerCount_);
                }
                this->addSystemMessage(statusMsg);

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

std::optional<EmotePtr> KickChannel::findThirdPartyEmote(
    const QString &word) const
{
    // Check 7TV channel emotes first
    if (this->seventvEmotes_)
    {
        auto it = this->seventvEmotes_->find(EmoteName{word});
        if (it != this->seventvEmotes_->end())
        {
            return it->second;
        }
    }

    // Check global 7TV emotes
    auto *globalSeventv = getApp()->getSeventvEmotes();
    if (globalSeventv)
    {
        auto emote = globalSeventv->globalEmote(EmoteName{word});
        if (emote)
        {
            return emote;
        }
    }

    // Check global BTTV emotes
    auto *globalBttv = getApp()->getBttvEmotes();
    if (globalBttv)
    {
        auto emote = globalBttv->emote(EmoteName{word});
        if (emote)
        {
            return emote;
        }
    }

    // Check global FFZ emotes
    auto *globalFfz = getApp()->getFfzEmotes();
    if (globalFfz)
    {
        auto emote = globalFfz->emote(EmoteName{word});
        if (emote)
        {
            return emote;
        }
    }

    return std::nullopt;
}

void KickChannel::addTextOrEmote(MessageBuilder &builder,
                                 const QString &text) const
{
    // Split text into words and check each for emotes
    QStringList words = text.split(' ', Qt::SkipEmptyParts);

    for (int i = 0; i < words.size(); i++)
    {
        const QString &word = words[i];

        // Check if this word is a third-party emote
        auto emote = this->findThirdPartyEmote(word);
        if (emote)
        {
            builder.emplace<EmoteElement>(*emote, MessageElementFlag::Emote);
        }
        else
        {
            // Just regular text
            builder.emplace<TextElement>(word, MessageElementFlag::Text,
                                         MessageColor::Text);
        }
    }
}

void KickChannel::parseMessageContent(MessageBuilder &builder,
                                      const QString &content,
                                      const std::vector<KickEmote> &emotes)
{
    // Regex to match [emote:ID:NAME] pattern (fallback if API positions don't work)
    // Format: [emote:37218:Clap] where 37218 is the emote ID and Clap is the name
    static const QRegularExpression emoteRegex(
        R"(\[emote:(\d+):([^\]]+)\])");

    // First, try to use emotes from API if they have valid positions
    bool hasValidApiEmotes = false;
    for (const auto &emote : emotes)
    {
        if (!emote.positions.empty() && !emote.emoteId.isEmpty())
        {
            hasValidApiEmotes = true;
            break;
        }
    }

    // If API provided valid emote positions, use them
    if (hasValidApiEmotes)
    {
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
                if (pos.start >= 0 && pos.end > pos.start &&
                    pos.end <= content.length())
                {
                    EmoteRange range;
                    range.start = pos.start;
                    range.end = pos.end;
                    range.emoteId = emote.emoteId;
                    range.emoteName = emote.emoteName;

                    // If no name from API, try to extract from content
                    if (range.emoteName.isEmpty())
                    {
                        QString emoteText =
                            content.mid(pos.start, pos.end - pos.start);
                        // Check for [emote:ID:NAME] format
                        if (emoteText.startsWith("[emote:") &&
                            emoteText.endsWith("]"))
                        {
                            QStringList parts =
                                emoteText.mid(7, emoteText.length() - 8)
                                    .split(':');
                            if (parts.size() >= 2)
                            {
                                range.emoteName = parts.last();
                            }
                        }
                        else
                        {
                            // Use the text itself as the name
                            range.emoteName = emoteText.trimmed();
                        }
                    }

                    ranges.push_back(range);
                }
            }
        }

        // Sort by start position
        std::sort(ranges.begin(), ranges.end(),
                  [](const EmoteRange &a, const EmoteRange &b) {
                      return a.start < b.start;
                  });

        if (!ranges.empty())
        {
            int currentPos = 0;
            for (const auto &range : ranges)
            {
                // Add text before this emote
                if (range.start > currentPos)
                {
                    QString textBefore =
                        content.mid(currentPos, range.start - currentPos)
                            .trimmed();
                    if (!textBefore.isEmpty())
                    {
                        this->addTextOrEmote(builder, textBefore);
                    }
                }

                // Add Kick emote
                QString emoteUrl =
                    QString("https://files.kick.com/emotes/%1/fullsize")
                        .arg(range.emoteId);

                EmoteId emoteId{range.emoteId};
                EmoteName emoteName{range.emoteName.isEmpty() ? range.emoteId
                                                              : range.emoteName};

                // Kick fullsize emotes are 70x70
                // Scale to match Twitch's base size (28x28) so emoteScale setting works uniformly
                // 28/70 ≈ 0.4, so scale factor of 0.4 renders at ~28px base
                constexpr QSize KICK_EMOTE_SIZE(70, 70);
                auto emote = std::make_shared<Emote>(Emote{
                    emoteName,
                    ImageSet{
                        Image::fromUrl({emoteUrl}, 0.4, KICK_EMOTE_SIZE),
                    },
                    Tooltip{emoteName.string + "<br>Kick Emote"},
                    Url{emoteUrl},
                    false,
                    emoteId,
                });

                builder.emplace<EmoteElement>(emote, MessageElementFlag::Emote);
                currentPos = range.end;
            }

            // Add remaining text
            if (currentPos < content.length())
            {
                QString remainingText = content.mid(currentPos).trimmed();
                if (!remainingText.isEmpty())
                {
                    this->addTextOrEmote(builder, remainingText);
                }
            }
            return;  // Successfully processed with API emotes
        }
    }

    // Fallback: Scan content with regex for [emote:ID:NAME] pattern

    // Find all native Kick emotes in the content
    struct EmoteMatch {
        int start;
        int end;
        QString emoteId;
        QString emoteName;
    };
    std::vector<EmoteMatch> matches;

    QRegularExpressionMatchIterator it = emoteRegex.globalMatch(content);
    while (it.hasNext())
    {
        QRegularExpressionMatch match = it.next();
        EmoteMatch em;
        em.start = match.capturedStart();
        em.end = match.capturedEnd();
        em.emoteId = match.captured(1);   // The numeric ID
        em.emoteName = match.captured(2); // The emote name
        matches.push_back(em);
    }

    // Build message elements
    int currentPos = 0;
    for (const auto &em : matches)
    {
        // Add text before this emote (check for third-party emotes)
        if (em.start > currentPos)
        {
            QString textBefore = content.mid(currentPos, em.start - currentPos);
            // Trim but preserve spacing intent
            QString trimmed = textBefore.trimmed();
            if (!trimmed.isEmpty())
            {
                this->addTextOrEmote(builder, trimmed);
            }
        }

        // Add native Kick emote element
        // Kick emote CDN URL: https://files.kick.com/emotes/{emote_id}/fullsize
        QString emoteUrl = QString("https://files.kick.com/emotes/%1/fullsize")
                               .arg(em.emoteId);

        EmoteId emoteId{em.emoteId};
        EmoteName emoteName{em.emoteName};

        // Kick fullsize emotes are 70x70
        // Scale to match Twitch's base size (28x28) so emoteScale setting works uniformly
        constexpr QSize KICK_EMOTE_SIZE(70, 70);
        auto emote = std::make_shared<Emote>(Emote{
            emoteName,
            ImageSet{
                Image::fromUrl({emoteUrl}, 0.4, KICK_EMOTE_SIZE),
            },
            Tooltip{emoteName.string + "<br>Kick Emote"},
            Url{emoteUrl},
            false,  // Not zero-width
            emoteId,
        });

        builder.emplace<EmoteElement>(emote, MessageElementFlag::Emote);

        currentPos = em.end;
    }

    // Handle remaining content after last emote (or whole content if no emotes)
    if (currentPos < content.length())
    {
        QString remainingText = content.mid(currentPos).trimmed();
        if (!remainingText.isEmpty())
        {
            this->addTextOrEmote(builder, remainingText);
        }
    }
    else if (matches.empty() && !content.isEmpty())
    {
        // No native emotes found, process whole content for third-party emotes
        this->addTextOrEmote(builder, content);
    }
}

}  // namespace chatterino

