#include "channels/MergedChannel.hpp"

#include "common/QLogging.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageFlag.hpp"

#include <algorithm>

namespace chatterino {

MergedChannel::MergedChannel(const QString &name,
                             std::vector<ChannelPtr> sourceChannels)
    : Channel(name, Channel::Type::Merged)
    , sourceChannels_(std::move(sourceChannels))
{
    // Subscribe to all source channels
    for (const auto &channel : this->sourceChannels_)
    {
        this->subscribeToChannel(channel);
    }
}

MergedChannel::~MergedChannel()
{
    this->signalHolder_.clear();
}

void MergedChannel::sendMessage(const QString &message)
{
    if (!this->canSendMessage())
    {
        // T077: Show error when sending to unauthenticated platform
        this->addSystemMessage(
            "Cannot send message. Please ensure you are logged in to the "
            "selected platform(s).");
        return;
    }

    // Track results for each platform
    struct SendResult {
        Channel::Type type;
        bool attempted{false};
        bool canSend{false};
        QString error;
    };
    std::vector<SendResult> results;

    // Send to selected platforms
    for (const auto &channel : this->sourceChannels_)
    {
        bool shouldSend = false;

        switch (this->platformSelection_)
        {
            case PlatformSelection::Both:
                shouldSend = true;
                break;
            case PlatformSelection::TwitchOnly:
                shouldSend = (channel->getType() == Channel::Type::Twitch);
                break;
            case PlatformSelection::KickOnly:
                shouldSend = (channel->getType() == Channel::Type::Kick);
                break;
        }

        if (shouldSend)
        {
            SendResult result;
            result.type = channel->getType();
            result.attempted = true;
            result.canSend = channel->canSendMessage();

            if (result.canSend)
            {
                // Attempt to send
                channel->sendMessage(message);
            }
            else
            {
                // T077: Show error for unauthenticated platform
                result.error = "Not authenticated";
            }

            results.push_back(result);
        }
    }

    // T072/T076: Display per-platform send status
    QStringList statusParts;
    for (const auto &result : results)
    {
        QString platform = getPlatformPrefix(result.type);
        if (!result.attempted)
        {
            continue;
        }

        if (result.canSend)
        {
            statusParts.append(QString("%1✓").arg(platform));
        }
        else
        {
            statusParts.append(QString("%1✗").arg(platform));

            // T076: Show per-platform send errors
            this->addSystemMessage(
                QString("%1: %2")
                    .arg(result.type == Channel::Type::Twitch ? "Twitch" : "Kick")
                    .arg(result.error));
        }
    }

    // Log results
    if (!statusParts.isEmpty())
    {
        qCDebug(chatterinoCommon)
            << "Message send status:" << statusParts.join(" ");
    }
}

bool MergedChannel::isMod() const
{
    // Return true if mod in any source channel
    for (const auto &channel : this->sourceChannels_)
    {
        if (channel->isMod())
        {
            return true;
        }
    }
    return false;
}

bool MergedChannel::canSendMessage() const
{
    // Can send if any source channel allows it based on platform selection
    for (const auto &channel : this->sourceChannels_)
    {
        bool matchesPlatform = false;

        switch (this->platformSelection_)
        {
            case PlatformSelection::Both:
                matchesPlatform = true;
                break;
            case PlatformSelection::TwitchOnly:
                matchesPlatform = (channel->getType() == Channel::Type::Twitch);
                break;
            case PlatformSelection::KickOnly:
                matchesPlatform = (channel->getType() == Channel::Type::Kick);
                break;
        }

        if (matchesPlatform && channel->canSendMessage())
        {
            return true;
        }
    }
    return false;
}

const QString &MergedChannel::getDisplayName() const
{
    this->updateCachedDisplayName();
    return this->cachedDisplayName_;
}

void MergedChannel::updateCachedDisplayName() const
{
    QStringList parts;
    for (const auto &channel : this->sourceChannels_)
    {
        QString prefix = getPlatformPrefix(channel->getType());
        parts.append(QString("%1:%2").arg(prefix, channel->getName()));
    }
    this->cachedDisplayName_ = parts.join(" + ");
}

const std::vector<ChannelPtr> &MergedChannel::getSourceChannels() const
{
    return this->sourceChannels_;
}

void MergedChannel::addSourceChannel(ChannelPtr channel)
{
    // Check if already added
    for (const auto &existing : this->sourceChannels_)
    {
        if (existing == channel)
        {
            return;
        }
    }

    this->sourceChannels_.push_back(channel);
    this->subscribeToChannel(channel);
    this->sourceChannelsChanged.invoke();
}

void MergedChannel::removeSourceChannel(const ChannelPtr &channel)
{
    auto it = std::find(this->sourceChannels_.begin(),
                        this->sourceChannels_.end(), channel);
    if (it != this->sourceChannels_.end())
    {
        this->sourceChannels_.erase(it);
        this->sourceChannelsChanged.invoke();
    }
}

std::vector<ChannelPtr> MergedChannel::unmerge()
{
    // Clear signal connections
    this->signalHolder_.clear();

    // Move source channels to return value
    std::vector<ChannelPtr> channels = std::move(this->sourceChannels_);
    this->sourceChannels_.clear();

    // Notify listeners
    this->sourceChannelsChanged.invoke();

    qCDebug(chatterinoCommon)
        << "Merged channel unmerged, returning" << channels.size() << "channels";

    return channels;
}

PlatformSelection MergedChannel::getPlatformSelection() const
{
    return this->platformSelection_;
}

void MergedChannel::setPlatformSelection(PlatformSelection selection)
{
    if (this->platformSelection_ != selection)
    {
        this->platformSelection_ = selection;
        this->platformSelectionChanged.invoke(selection);
    }
}

bool MergedChannel::hasPlatform(Channel::Type type) const
{
    for (const auto &channel : this->sourceChannels_)
    {
        if (channel->getType() == type)
        {
            return true;
        }
    }
    return false;
}

void MergedChannel::subscribeToChannel(const ChannelPtr &channel)
{
    Channel::Type sourceType = channel->getType();

    // Subscribe to message appended signal
    this->signalHolder_.managedConnect(
        channel->messageAppended,
        [this, sourceType](MessagePtr &message,
                           std::optional<MessageFlags> overridingFlags) {
            Q_UNUSED(overridingFlags);
            this->onSourceMessageReceived(message, sourceType);
        });
}

void MergedChannel::onSourceMessageReceived(MessagePtr message,
                                            Channel::Type sourceType)
{
    // Set platform flag for UI display (flags is mutable so we can modify const messages)
    // The renderer will use this flag to display platform indicators
    if (sourceType == Channel::Type::Twitch)
    {
        message->flags.set(MessageFlag::Twitch);
    }
    else if (sourceType == Channel::Type::Kick)
    {
        message->flags.set(MessageFlag::Kick);
    }

    // Add message chronologically based on serverReceivedTime
    // The Channel::addMessage handles chronological insertion
    this->addMessage(message, MessageContext::Original);
}

QString MergedChannel::getPlatformPrefix(Channel::Type type)
{
    switch (type)
    {
        case Channel::Type::Twitch:
            return "T";
        case Channel::Type::Kick:
            return "K";
        default:
            return "?";
    }
}

void MergedChannel::addSystemMessage(const QString &text)
{
    auto msg = makeSystemMessage(text);
    this->addMessage(msg, MessageContext::Original);
}

}  // namespace chatterino

