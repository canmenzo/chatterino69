#include "providers/kick/KickEmotes.hpp"

#include "common/QLogging.hpp"
#include "messages/Emote.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace chatterino {

KickEmotes::KickEmotes()
    : globalEmotes_(std::make_shared<EmoteMap>())
{
}

EmotePtr KickEmotes::getEmote(const QString &emoteName) const
{
    // Check global emotes first
    if (this->globalEmotes_)
    {
        auto it = this->globalEmotes_->find(EmoteName{emoteName});
        if (it != this->globalEmotes_->end())
        {
            return it->second;
        }
    }
    return nullptr;
}

void KickEmotes::loadGlobalEmotes(std::function<void(bool success)> callback)
{
    // TODO: Kick doesn't have a documented global emotes API
    // This is a placeholder for when/if one becomes available
    // For now, rely on 7TV/BTTV/FFZ global emotes which work in Kick channels

    qCDebug(chatterinoKick) << "Kick global emotes: using 7TV/BTTV/FFZ "
                               "providers for emote support";

    if (callback)
    {
        callback(true);
    }
}

void KickEmotes::loadChannelEmotes(const QString &channelSlug,
                                   std::function<void(bool success)> callback)
{
    // TODO: Load channel-specific Kick emotes when API is available
    // For now, 7TV channel emotes work automatically through existing providers

    qCDebug(chatterinoKick)
        << "Kick channel emotes for" << channelSlug
        << ": using 7TV/BTTV/FFZ providers for emote support";

    // Create empty emote map for this channel
    this->channelEmotes_[channelSlug] = std::make_shared<EmoteMap>();

    if (callback)
    {
        callback(true);
    }
}

const EmoteMap &KickEmotes::getGlobalEmotes() const
{
    static EmoteMap empty;
    return this->globalEmotes_ ? *this->globalEmotes_ : empty;
}

std::shared_ptr<const EmoteMap> KickEmotes::getChannelEmotes(
    const QString &channelSlug) const
{
    auto it = this->channelEmotes_.find(channelSlug);
    if (it != this->channelEmotes_.end())
    {
        return it->second;
    }
    return nullptr;
}

void KickEmotes::clearCache()
{
    this->globalEmotes_ = std::make_shared<EmoteMap>();
    this->channelEmotes_.clear();
}

void KickEmotes::parseEmoteData(const QByteArray &data, EmoteMap &emoteMap)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        qCWarning(chatterinoKick)
            << "Failed to parse emote data:" << parseError.errorString();
        return;
    }

    if (!doc.isArray())
    {
        qCWarning(chatterinoKick) << "Emote data is not an array";
        return;
    }

    QJsonArray emoteArray = doc.array();
    for (const auto &emoteVal : emoteArray)
    {
        QJsonObject emoteObj = emoteVal.toObject();

        QString name = emoteObj["name"].toString();
        QString id = emoteObj["id"].toString();
        QString url = emoteObj["url"].toString();

        if (name.isEmpty() || url.isEmpty())
        {
            continue;
        }

        // Create emote
        EmoteId emoteId{id};
        EmoteName emoteName{name};

        auto emote = std::make_shared<Emote>(Emote{
            emoteName,
            ImageSet{Url{url}},
            Tooltip{name},
            Url{url},
            false,  // Not zero-width
            emoteId,
        });

        emoteMap[emoteName] = emote;
    }

    qCDebug(chatterinoKick) << "Loaded" << emoteMap.size() << "Kick emotes";
}

}  // namespace chatterino

