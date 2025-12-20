#pragma once

#include <functional>

#include <QStringList>

class QString;
class QJsonObject;

namespace chatterino {

class NetworkResult;

class SeventvAPI final
{
    using ErrorCallback = std::function<void(const NetworkResult &)>;
    template <typename... T>
    using SuccessCallback = std::function<void(T...)>;

public:
    SeventvAPI() = default;
    ~SeventvAPI() = default;

    SeventvAPI(const SeventvAPI &) = delete;
    SeventvAPI(SeventvAPI &&) = delete;
    SeventvAPI &operator=(const SeventvAPI &) = delete;
    SeventvAPI &operator=(SeventvAPI &&) = delete;

    void getUserByTwitchID(const QString &twitchID,
                           SuccessCallback<const QJsonObject &> &&onSuccess,
                           ErrorCallback &&onError);

    /// Get 7TV user by Kick user ID
    /// Uses https://7tv.io/v3/users/KICK/{user_id}
    /// Note: Returns a "connection" object, not full user profile
    void getUserByKickID(const QString &kickUserID,
                         SuccessCallback<const QJsonObject &> &&onSuccess,
                         ErrorCallback &&onError);

    /// Get 7TV user by their 7TV user ID (full profile with cosmetics)
    /// Uses https://7tv.io/v3/users/{user_id}
    void getUserByID(const QString &seventvUserID,
                     SuccessCallback<const QJsonObject &> &&onSuccess,
                     ErrorCallback &&onError);

    /// Get cosmetics (badges, paints) by their IDs via GraphQL
    /// @param ids List of cosmetic IDs to fetch
    void getCosmetics(const QStringList &ids,
                      SuccessCallback<const QJsonObject &> &&onSuccess,
                      ErrorCallback &&onError);

    void getEmoteSet(const QString &emoteSet,
                     SuccessCallback<const QJsonObject &> &&onSuccess,
                     ErrorCallback &&onError);

    void updatePresence(const QString &twitchChannelID,
                        const QString &seventvUserID,
                        SuccessCallback<> &&onSuccess, ErrorCallback &&onError);
};

}  // namespace chatterino
