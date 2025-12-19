#include "providers/kick/KickApi.hpp"

#include "common/QLogging.hpp"
#include "providers/kick/KickAccount.hpp"

#include <pajlada/signals/scoped-connection.hpp>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace chatterino {

KickApi::KickApi(QObject *parent)
    : QObject(parent)
    , networkManager_(std::make_unique<QNetworkAccessManager>(this))
{
}

KickApi::~KickApi() = default;

void KickApi::setAccount(std::shared_ptr<KickAccount> account)
{
    this->account_ = std::move(account);
}

std::shared_ptr<KickAccount> KickApi::getAccount() const
{
    return this->account_;
}

void KickApi::resolveChannelInfo(
    const QString &channelSlug,
    std::function<void(ChannelInfo info)> callback)
{
    // Use the channel API to get broadcaster info
    // Note: This uses unofficial endpoint as official API doesn't expose this
    QString url = QString("%1/channels/%2")
                      .arg(QString::fromLatin1(KICK_CHANNEL_API), channelSlug);

    qCDebug(chatterinoKick) << "Resolving channel info for:" << channelSlug
                            << "URL:" << url;

    QNetworkRequest request{QUrl{url}};
    request.setHeader(QNetworkRequest::UserAgentHeader, "Chatterino7");
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = this->networkManager_->get(request);

    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, callback, channelSlug] {
        reply->deleteLater();

        ChannelInfo info;
        info.slug = channelSlug;

        if (reply->error() != QNetworkReply::NoError)
        {
            qCWarning(chatterinoKick)
                << "Failed to resolve channel info for" << channelSlug
                << ":" << reply->errorString();
            callback(info);
            return;
        }

        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);

        if (!doc.isObject())
        {
            qCWarning(chatterinoKick) << "Invalid channel response";
            callback(info);
            return;
        }

        QJsonObject obj = doc.object();

        // Extract broadcaster user_id from response
        // The structure includes: { "user_id": 12345, "chatroom": { "id": 67890 } }
        if (obj.contains("user_id"))
        {
            info.broadcasterUserId = obj["user_id"].toInt();
        }
        else if (obj.contains("user") && obj["user"].isObject())
        {
            // Alternative structure: { "user": { "id": 12345 } }
            info.broadcasterUserId = obj["user"].toObject()["id"].toInt();
        }

        // Extract chatroom ID
        if (obj.contains("chatroom") && obj["chatroom"].isObject())
        {
            QJsonObject chatroom = obj["chatroom"].toObject();
            info.chatroomId = chatroom["id"].toInt();
        }
        else if (obj.contains("id"))
        {
            // Sometimes the channel ID is used as chatroom ID
            info.chatroomId = obj["id"].toInt();
        }

        // Extract display name
        if (obj.contains("user") && obj["user"].isObject())
        {
            info.displayName = obj["user"].toObject()["username"].toString();
        }

        // Extract livestream info (check if channel is live)
        if (obj.contains("livestream") && !obj["livestream"].isNull())
        {
            QJsonObject livestream = obj["livestream"].toObject();
            info.isLive = livestream["is_live"].toBool();
            info.streamTitle = livestream["session_title"].toString();
            info.viewerCount = livestream["viewer_count"].toInt();
        }

        if (info.broadcasterUserId > 0 && info.chatroomId > 0)
        {
            info.success = true;
            qCDebug(chatterinoKick)
                << "Resolved channel" << channelSlug
                << "- broadcaster ID:" << info.broadcasterUserId
                << "- isLive:" << info.isLive
                << "- chatroom ID:" << info.chatroomId;
        }
        else
        {
            qCWarning(chatterinoKick)
                << "Incomplete channel info for" << channelSlug
                << "- broadcaster ID:" << info.broadcasterUserId
                << "- chatroom ID:" << info.chatroomId;
        }

        callback(info);
    });
}

void KickApi::resolveBroadcasterId(
    const QString &channelSlug,
    std::function<void(int broadcasterUserId, bool success)> callback)
{
    // Delegate to resolveChannelInfo for backward compatibility
    this->resolveChannelInfo(channelSlug, [callback](ChannelInfo info) {
        callback(info.broadcasterUserId, info.success);
    });
}

void KickApi::sendMessage(int broadcasterUserId, const QString &message,
                          std::function<void(KickApiResult result)> callback)
{
    if (!this->account_ || !this->account_->isAuthenticated())
    {
        KickApiResult result;
        result.success = false;
        result.errorMessage = "Not authenticated";
        callback(result);
        return;
    }

    // Check rate limit
    if (this->isRateLimited())
    {
        KickApiResult result;
        result.success = false;
        result.errorMessage = "Rate limited";
        result.rateLimit = this->rateLimitInfo_;

        int secondsUntilReset =
            QDateTime::currentDateTime().secsTo(this->rateLimitInfo_.resetAt);
        this->rateLimited.invoke(secondsUntilReset);

        callback(result);
        return;
    }

    // Check if token needs refresh
    if (this->account_->isTokenExpired())
    {
        this->refreshAndRetry([this, broadcasterUserId, message, callback] {
            this->sendMessage(broadcasterUserId, message, callback);
        });
        return;
    }

    // Official Kick API endpoint (from Context7: docs.kick.com/apis/chat)
    // POST https://api.kick.com/public/v1/chat
    // Body: { "content": string, "type": "user"|"bot", "broadcaster_user_id": int }
    QString url = QString("%1/chat").arg(QString::fromLatin1(KICK_API_BASE));

    QNetworkRequest request{QUrl{url}};
    request.setHeader(QNetworkRequest::UserAgentHeader, "Chatterino7");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader(
        "Authorization",
        QString("Bearer %1").arg(this->account_->getAccessToken()).toUtf8());

    // Request body per official API spec (docs.kick.com)
    // Required fields: content, type
    // broadcaster_user_id is required when type="user"
    QJsonObject body;
    body["content"] = message;                       // Message text (max 500 chars)
    body["type"] = QString("user");                  // Send as user (not bot)
    body["broadcaster_user_id"] = broadcasterUserId; // Required for type="user"
    QJsonDocument bodyDoc(body);

    QNetworkReply *reply =
        this->networkManager_->post(request, bodyDoc.toJson(QJsonDocument::Compact));

    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, callback, broadcasterUserId, message] {
        reply->deleteLater();

        // Update rate limit info from headers
        this->updateRateLimitFromReply(reply);

        KickApiResult result;
        result.rateLimit = this->rateLimitInfo_;

        int statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.httpStatus = statusCode;

        if (reply->error() != QNetworkReply::NoError)
        {
            result = this->handleErrorResponse(reply);

            // Handle specific error codes
            if (statusCode == 401)
            {
                // Token expired, try refresh
                this->refreshAndRetry([this, broadcasterUserId, message, callback] {
                    this->sendMessage(broadcasterUserId, message, callback);
                });
                return;
            }
            else if (statusCode == 429)
            {
                // Rate limited
                int secondsUntilReset = QDateTime::currentDateTime().secsTo(
                    this->rateLimitInfo_.resetAt);
                this->rateLimited.invoke(secondsUntilReset);
            }
            else if (statusCode == 403)
            {
                // Banned or no permission
                result.errorMessage = "You don't have permission to send "
                                      "messages in this channel";
            }

            callback(result);
            return;
        }

        result.success = true;
        qCDebug(chatterinoKick)
            << "Message sent successfully to broadcaster" << broadcasterUserId;
        callback(result);
    });
}

KickRateLimitInfo KickApi::getRateLimitInfo() const
{
    return this->rateLimitInfo_;
}

bool KickApi::isRateLimited() const
{
    return this->rateLimitInfo_.remaining <= 0 &&
           QDateTime::currentDateTime() < this->rateLimitInfo_.resetAt;
}

void KickApi::updateRateLimitFromReply(QNetworkReply *reply)
{
    // Parse X-RateLimit-* headers
    QByteArray limitHeader = reply->rawHeader("X-RateLimit-Limit");
    QByteArray remainingHeader = reply->rawHeader("X-RateLimit-Remaining");
    QByteArray resetHeader = reply->rawHeader("X-RateLimit-Reset");

    if (!limitHeader.isEmpty())
    {
        this->rateLimitInfo_.limit = limitHeader.toInt();
    }
    if (!remainingHeader.isEmpty())
    {
        this->rateLimitInfo_.remaining = remainingHeader.toInt();
    }
    if (!resetHeader.isEmpty())
    {
        // Reset header is typically a Unix timestamp
        qint64 resetTimestamp = resetHeader.toLongLong();
        this->rateLimitInfo_.resetAt =
            QDateTime::fromSecsSinceEpoch(resetTimestamp);
    }
}

KickApiResult KickApi::handleErrorResponse(QNetworkReply *reply)
{
    KickApiResult result;
    result.success = false;
    result.httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    QByteArray responseData = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (doc.isObject())
    {
        QJsonObject obj = doc.object();
        if (obj.contains("message"))
        {
            result.errorMessage = obj["message"].toString();
        }
        else if (obj.contains("error"))
        {
            result.errorMessage = obj["error"].toString();
        }
        else
        {
            result.errorMessage = reply->errorString();
        }
    }
    else
    {
        result.errorMessage = reply->errorString();
    }

    qCWarning(chatterinoKick)
        << "API error:" << result.httpStatus << result.errorMessage;

    return result;
}

void KickApi::refreshAndRetry(std::function<void()> retryFn)
{
    if (!this->account_)
    {
        return;
    }

    qCDebug(chatterinoKick) << "Token expired, attempting refresh...";

    // Connect to token refresh result
    // Use shared_ptr to ensure the connection outlives the callback scope
    auto connection = std::make_shared<pajlada::Signals::ScopedConnection>();
    *connection = this->account_->tokenRefreshed.connect([retryFn, connection](bool success) {
        if (success)
        {
            qCDebug(chatterinoKick) << "Token refreshed, retrying request...";
            retryFn();
        }
        else
        {
            qCWarning(chatterinoKick) << "Token refresh failed";
        }
        // Disconnect after handling by assigning an empty connection
        *connection = pajlada::Signals::ScopedConnection{};
    });

    this->account_->refreshAccessToken();
}

}  // namespace chatterino

