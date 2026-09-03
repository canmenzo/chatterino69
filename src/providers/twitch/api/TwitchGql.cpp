#include "providers/twitch/api/TwitchGql.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "singletons/Settings.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

#include <utility>

namespace {

using namespace chatterino;

constexpr int TIMEOUT_MS = 20000;
constexpr const char *ENDPOINT = "https://gql.twitch.tv/gql";
constexpr const char *WEB_CLIENT_ID = "kimne78kx3ncx6brgo4mv6wki5h1ko";

/// Twitch expects a stable per-install id on these requests.
QString deviceId()
{
    static QString id =
        QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-').left(32);
    return id;
}

NetworkRequest makeRequest(const QJsonObject &payload)
{
    return NetworkRequest(ENDPOINT, NetworkRequestType::Post)
        .timeout(TIMEOUT_MS)
        .header("Client-Id", WEB_CLIENT_ID)
        .header("Content-Type", "application/json")
        .header("X-Device-Id", deviceId().toUtf8())
        .header("Authorization", ("OAuth " + gql::token()).toUtf8())
        .payload(
            QJsonDocument(QJsonArray{payload}).toJson(QJsonDocument::Compact));
}

/// gql answers a batched request with an array of results.
QJsonObject firstResult(const QJsonValue &response)
{
    if (response.isArray())
    {
        return response.toArray().first().toObject();
    }
    return response.toObject();
}

void run(const QJsonObject &payload, gql::SuccessCallback onSuccess,
         gql::FailureCallback onFailure)
{
    auto reason = gql::unavailableReason();
    if (!reason.isEmpty())
    {
        onFailure(reason);
        return;
    }

    makeRequest(payload)
        .onSuccess([onSuccess = std::move(onSuccess),
                    onFailure](const NetworkResult &result) mutable {
            auto root = firstResult(result.parseJsonValue());
            if (root.isEmpty())
            {
                onFailure("Could not read Twitch's response");
                return;
            }

            auto error = gql::firstError(root);
            if (!error.isEmpty())
            {
                onFailure(error);
                return;
            }

            onSuccess(root.value("data").toObject());
        })
        .onError(
            [onFailure = std::move(onFailure)](const NetworkResult &result) {
                // 401 here means the pasted token is stale, which is the common case
                if (result.status() == 401)
                {
                    onFailure(
                        "Twitch rejected the saved token. Paste a fresh one in "
                        "Settings.");
                    return;
                }
                onFailure(result.formatError());
            })
        .execute();
}

}  // namespace

namespace chatterino::gql {

QString token()
{
    auto raw = getSettings()->twitchGqlToken.getValue().trimmed();

    if (raw.startsWith("Authorization:", Qt::CaseInsensitive))
    {
        raw = raw.mid(QStringLiteral("Authorization:").size()).trimmed();
    }
    if (raw.startsWith("OAuth ", Qt::CaseInsensitive))
    {
        raw = raw.mid(QStringLiteral("OAuth ").size()).trimmed();
    }

    return raw;
}

bool isEnabled()
{
    return getSettings()->enableTwitchGql && !token().isEmpty();
}

QString unavailableReason()
{
    if (!getSettings()->enableTwitchGql)
    {
        return "This needs Twitch's private API, which is off. Turn it on in "
               "Settings if you accept the risk.";
    }
    if (token().isEmpty())
    {
        return "No Twitch token saved. Paste one in Settings to use this.";
    }
    return {};
}

QString firstError(const QJsonObject &response)
{
    auto errors = response.value("errors").toArray();
    if (errors.isEmpty())
    {
        return {};
    }

    auto message = errors.first().toObject().value("message").toString();
    return message.isEmpty() ? QStringLiteral("Twitch rejected the request")
                             : message;
}

void persistedQuery(const QString &operationName, const QString &sha256Hash,
                    const QJsonObject &variables, SuccessCallback onSuccess,
                    FailureCallback onFailure)
{
    run(
        QJsonObject{
            {"operationName", operationName},
            {"variables", variables},
            {"extensions",
             QJsonObject{
                 {"persistedQuery",
                  QJsonObject{{"version", 1}, {"sha256Hash", sha256Hash}}}}},
        },
        std::move(onSuccess), std::move(onFailure));
}

void query(const QString &operationName, const QString &document,
           const QJsonObject &variables, SuccessCallback onSuccess,
           FailureCallback onFailure)
{
    run(
        QJsonObject{
            {"operationName", operationName},
            {"query", document},
            {"variables", variables},
        },
        std::move(onSuccess), std::move(onFailure));
}

}  // namespace chatterino::gql
