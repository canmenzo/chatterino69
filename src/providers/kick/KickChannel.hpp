#pragma once

#include "common/Channel.hpp"
#include "providers/kick/KickMessage.hpp"

#include <pajlada/signals/signal.hpp>

#include <QString>

#include <memory>

namespace chatterino {

class MessageBuilder;
class KickWebSocket;
class KickAccount;
class KickApi;

/// Connection state for a Kick channel
enum class KickConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Failed
};

/// Represents a Kick.tv chat channel with real-time message streaming
class KickChannel : public Channel
{
public:
    explicit KickChannel(const QString &channelSlug);
    ~KickChannel() override;

    // Channel interface
    void sendMessage(const QString &message) override;
    bool isMod() const override;
    bool canSendMessage() const override;

    // Kick-specific methods
    void connect();
    void disconnect();
    void reconnect() override;
    [[nodiscard]] KickConnectionState getConnectionState() const;
    [[nodiscard]] QString getChannelSlug() const;
    [[nodiscard]] int getChatroomId() const;
    [[nodiscard]] int getBroadcasterUserId() const;

    // Authentication
    void setAuthenticated(bool authenticated);
    [[nodiscard]] bool isAuthenticated() const;

    // API access
    void setApi(std::shared_ptr<KickApi> api);
    void setAccount(std::shared_ptr<KickAccount> account);

    // Recent messages (stub - Kick history API not available)
    void fetchRecentMessages();

    /// Signal emitted when connection state changes
    pajlada::Signals::Signal<KickConnectionState> connectionStateChanged;

private:
    /// Handle incoming Kick message from WebSocket
    void onMessageReceived(const KickMessage &message);

    /// Update connection state and emit signal
    void setConnectionState(KickConnectionState state);

    /// Resolve channel slug to chatroom ID and subscribe
    void resolveAndSubscribe();

    /// Handle WebSocket connection error
    void handleConnectionError();

    /// Schedule reconnection with exponential backoff
    void scheduleReconnect();

    /// Add a system message to the channel
    void addSystemMessage(const QString &text);

    /// Parse message content and emotes into message elements
    /// @param builder The MessageBuilder to add elements to
    /// @param content The raw message content
    /// @param emotes The emote list with positions
    void parseMessageContent(MessageBuilder &builder, const QString &content,
                             const std::vector<KickEmote> &emotes);

    QString channelSlug_;
    int chatroomId_{0};         // Used for WebSocket subscription
    int broadcasterUserId_{0};  // Used for REST API (sending messages)
    KickConnectionState connectionState_;
    bool isAuthenticated_{false};

    std::unique_ptr<KickWebSocket> webSocket_;
    std::shared_ptr<KickApi> api_;
    std::shared_ptr<KickAccount> account_;

    // Reconnection state
    int reconnectAttempts_{0};
    static constexpr int MAX_RECONNECT_ATTEMPTS = 5;
};

}  // namespace chatterino
