#pragma once

#include <pajlada/signals/signal.hpp>

#include <QDateTime>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace chatterino {

class KickAccount;

/// Rate limit information from Kick API responses
struct KickRateLimitInfo {
    int limit{0};
    int remaining{0};
    QDateTime resetAt;
};

/// Result of a Kick API call
struct KickApiResult {
    bool success{false};
    QString errorMessage;
    int httpStatus{0};
    std::optional<KickRateLimitInfo> rateLimit;
};

/// Kick.com REST API client for sending messages and resolving channel info
class KickApi : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(KickApi)

public:
    explicit KickApi(QObject *parent = nullptr);
    ~KickApi() override;

    /// Set the account to use for authenticated requests
    void setAccount(std::shared_ptr<KickAccount> account);

    /// Get the current account
    [[nodiscard]] std::shared_ptr<KickAccount> getAccount() const;

    /// Channel info resolved from Kick API
    struct ChannelInfo {
        int broadcasterUserId{0};
        int chatroomId{0};
        QString slug;
        QString displayName;
        bool isLive{false};
        QString streamTitle;
        int viewerCount{0};
        bool success{false};
    };

    /// Resolve a channel slug to its broadcaster user ID and chatroom ID
    /// @param channelSlug The channel username/slug
    /// @param callback Called with channel info
    void resolveChannelInfo(const QString &channelSlug,
                            std::function<void(ChannelInfo info)> callback);

    /// Resolve a channel slug to its broadcaster user ID (legacy, for compatibility)
    /// @param channelSlug The channel username/slug
    /// @param callback Called with (broadcasterUserId, success)
    void resolveBroadcasterId(const QString &channelSlug,
                              std::function<void(int broadcasterUserId, bool success)> callback);

    /// Send a chat message to a channel
    /// (Source: Context7 /kickengineering/kickdevdocs - POST /public/v1/chat)
    /// @param broadcasterUserId The broadcaster's user ID
    /// @param message The message content
    /// @param callback Called with the result
    void sendMessage(int broadcasterUserId, const QString &message,
                     std::function<void(KickApiResult result)> callback);

    /// Get current rate limit info
    [[nodiscard]] KickRateLimitInfo getRateLimitInfo() const;

    /// Check if currently rate limited
    [[nodiscard]] bool isRateLimited() const;

    /// Signal emitted when rate limited
    pajlada::Signals::Signal<int> rateLimited;  // seconds until reset

    // API configuration (from Context7: /kickengineering/kickdevdocs)
    // Official API base: https://api.kick.com
    static constexpr const char *KICK_API_BASE = "https://api.kick.com/public/v1";
    // Channel info endpoint (unofficial, for resolving channel slug to user ID)
    static constexpr const char *KICK_CHANNEL_API = "https://kick.com/api/v2";

private:
    /// Update rate limit info from response headers
    void updateRateLimitFromReply(QNetworkReply *reply);

    /// Handle API error responses
    KickApiResult handleErrorResponse(QNetworkReply *reply);

    /// Refresh token if expired and retry request
    void refreshAndRetry(std::function<void()> retryFn);

    std::unique_ptr<QNetworkAccessManager> networkManager_;
    std::shared_ptr<KickAccount> account_;
    KickRateLimitInfo rateLimitInfo_;
};

}  // namespace chatterino

