#include "providers/kick/KickApi.hpp"

#include "common/QLogging.hpp"
#include "providers/kick/KickAccount.hpp"

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

void KickApi::resolveBroadcasterId(
    const QString &channelSlug,
    std::function<void(int broadcasterUserId, bool success)> callback)
{
    // Use the channel API to get broadcaster info
    // Note: This uses unofficial endpoint as official API doesn't expose this
    QString url = QString("%1/channels/%2")
                      .arg(QString::fromLatin1(KICK_CHANNEL_API), channelSlug);

    QNetworkRequest request(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Chatterino7");
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = this->networkManager_->get(request);

    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, callback, channelSlug] {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            qCWarning(chatterinoKick)
                << "Failed to resolve broadcaster ID for" << channelSlug
                << ":" << reply->errorString();
            callback(0, false);
            return;
        }

        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);

        if (!doc.isObject())
        {
            qCWarning(chatterinoKick) << "Invalid channel response";
            callback(0, false);
            return;
        }

        QJsonObject obj = doc.object();

        // Extract broadcaster user_id from response
        // The structure includes: { "user_id": 12345, "chatroom": {...} }
        if (obj.contains("user_id"))
        {
            int broadcasterUserId = obj["user_id"].toInt();
            qCDebug(chatterinoKick)
                << "Resolved channel" << channelSlug << "to broadcaster ID"
                << broadcasterUserId;
            callback(broadcasterUserId, true);
        }
        else if (obj.contains("user") && obj["user"].isObject())
        {
            // Alternative structure: { "user": { "id": 12345 } }
            int broadcasterUserId = obj["user"].toObject()["id"].toInt();
            qCDebug(chatterinoKick)
                << "Resolved channel" << channelSlug << "to broadcaster ID"
                << broadcasterUserId;
            callback(broadcasterUserId, true);
        }
        else
        {
            qCWarning(chatterinoKick)
                << "No user ID in channel response for" << channelSlug;
            callback(0, false);
        }
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

    // Official Kick API endpoint (from Context7: /kickengineering/kickdevdocs)
    // POST https://api.kick.com/public/v1/chat
    // Body: { "broadcaster_user_id": int, "message": string }
    QString url = QString("%1/chat").arg(QString::fromLatin1(KICK_API_BASE));

    QNetworkRequest request(QUrl(url));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Chatterino7");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader(
        "Authorization",
        QString("Bearer %1").arg(this->account_->getAccessToken()).toUtf8());

    // Request body per official API spec
    QJsonObject body;
    body["broadcaster_user_id"] = broadcasterUserId;
    body["message"] = message;
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
        // Disconnect after handling
        connection->reset();
    });

    this->account_->refreshAccessToken();
}

}  // namespace chatterino

