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
    return msg;
}

bool KickMessage::isChatMessage() const
{
    return this->type == "message";
}

}  // namespace chatterino

