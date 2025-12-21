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

/// Error types for Kick API operations (matches Twitch error patterns)
enum class KickError {
    MissingText,       // Empty message attempted
    BadRequest,        // Invalid request (400)
    Forbidden,         // Permission denied (403)
    RateLimited,       // Rate limit exceeded (429)
    UserMissingScope,  // OAuth scope missing (401)
    TokenExpired,      // Access token expired
    ConnectionFailed,  // WebSocket connection failure
    ChannelNotFound,   // Channel does not exist
    MessageTooLong,    // Message exceeds character limit
    Unknown            // Unhandled error
};

/// Translate a KickError to a user-friendly message (matches Twitch error patterns)
/// @param error The error type
/// @param apiMessage Optional raw API error message for context
/// @param secondsUntilReset For rate limits, seconds until reset (optional)
/// @return User-friendly error message
QString translateKickError(KickError error, const QString &apiMessage = {},
                           int secondsUntilReset = 0);

/// Map HTTP status code to KickError enum
/// @param httpStatus The HTTP status code
/// @param apiMessage Optional API error message for disambiguation
/// @return The corresponding KickError
KickError mapHttpStatusToKickError(int httpStatus,
                                   const QString &apiMessage = {});

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
    void resolveBroadcasterId(
        const QString &channelSlug,
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

    /// Kick channel emote info
    struct KickEmoteInfo {
        int id{0};
        QString name;
        bool subscribersOnly{false};
    };

    /// Result of fetching channel emotes
    struct ChannelEmotesResult {
        bool success{false};
        QString channelSlug;
        std::vector<KickEmoteInfo> emotes;
        QString errorMessage;
    };

    /// Fetch channel emotes from Kick
    /// Uses public endpoint: https://kick.com/emotes/{channel}
    /// @param channelSlug The channel username/slug
    /// @param callback Called with the emotes list
    void fetchChannelEmotes(
        const QString &channelSlug,
        std::function<void(ChannelEmotesResult result)> callback);

    /// User role in a channel (null = not special, subscriber, moderator, etc.)
    struct UserRoleResult {
        bool success{false};
        QString channelSlug;
        QString role;  // empty = no role, "subscriber", "moderator", "broadcaster", etc.
        bool isSubscribed{false};  // true if role indicates subscription access
        QString errorMessage;
    };

    /// Fetch the current user's role in a channel
    /// Uses public endpoint: https://kick.com/api/v2/channels/{channel}
    /// The "role" field indicates subscription/mod status
    /// @param channelSlug The channel username/slug
    /// @param callback Called with the role info
    void fetchUserRoleInChannel(
        const QString &channelSlug,
        std::function<void(UserRoleResult result)> callback);

    /// Signal emitted when rate limited
    pajlada::Signals::Signal<int> rateLimited;  // seconds until reset

    // API configuration (from Context7: /kickengineering/kickdevdocs)
    // Official API base: https://api.kick.com
    static constexpr const char *KICK_API_BASE =
        "https://api.kick.com/public/v1";
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
