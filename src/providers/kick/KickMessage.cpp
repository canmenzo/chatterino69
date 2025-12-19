#include "providers/kick/KickMessage.hpp"

#include <QJsonArray>

namespace chatterino {

KickBadge KickBadge::fromJson(const QJsonObject &json)
{
    KickBadge badge;
    badge.type = json["type"].toString();
    badge.text = json["text"].toString();
    badge.count = json["count"].toInt(0);
    return badge;
}

KickIdentity KickIdentity::fromJson(const QJsonObject &json)
{
    KickIdentity identity;
    identity.color = json["color"].toString();

    const auto badgesArray = json["badges"].toArray();
    identity.badges.reserve(badgesArray.size());
    for (const auto &badgeVal : badgesArray)
    {
        identity.badges.push_back(KickBadge::fromJson(badgeVal.toObject()));
    }

    return identity;
}

KickSender KickSender::fromJson(const QJsonObject &json)
{
    KickSender sender;
    sender.id = json["id"].toInt();
    sender.username = json["username"].toString();
    sender.slug = json["slug"].toString();
    sender.identity = KickIdentity::fromJson(json["identity"].toObject());
    return sender;
}

KickEmote KickEmote::fromJson(const QJsonObject &json)
{
    KickEmote emote;
    emote.emoteId = json["emote_id"].toString();

    // Parse positions array: [{ "s": start, "e": end }, ...]
    const auto positionsArray = json["positions"].toArray();
    emote.positions.reserve(positionsArray.size());
    for (const auto &posVal : positionsArray)
    {
        QJsonObject posObj = posVal.toObject();
        KickEmotePosition pos;
        pos.start = posObj["s"].toInt();
        pos.end = posObj["e"].toInt();
        emote.positions.push_back(pos);
    }

    return emote;
}

KickMessage KickMessage::fromJson(const QJsonObject &json)
{
    KickMessage msg;
    msg.id = json["id"].toString();
    msg.chatroomId = json["chatroom_id"].toInt();
    msg.content = json["content"].toString();
    msg.type = json["type"].toString();
    msg.createdAt = QDateTime::fromString(json["created_at"].toString(),
                                          Qt::ISODateWithMs);
    msg.sender = KickSender::fromJson(json["sender"].toObject());

    // Parse emotes array
    const auto emotesArray = json["emotes"].toArray();
    msg.emotes.reserve(emotesArray.size());
    for (const auto &emoteVal : emotesArray)
    {
        msg.emotes.push_back(KickEmote::fromJson(emoteVal.toObject()));
    }

    return msg;
}

bool KickMessage::isChatMessage() const
{
    return this->type == "message";
}

}  // namespace chatterino

