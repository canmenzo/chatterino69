# Data Model: Kick.tv Integration & Multi-Platform Merge

**Date**: 2025-12-18  
**Status**: Complete  
**Research Reference**: [research.md](./research.md)

This document defines the data structures, relationships, and state management for Kick.tv integration and merged channel functionality.

---

## Core Entities

### 1. ProviderId (Extended Enum)

**Location**: `src/common/ProviderId.hpp`

**Purpose**: Identify the platform/provider for a channel or message.

```cpp
enum class ProviderId {
    Twitch,
    Kick,     // NEW: Kick.tv provider
    // Future: YouTube, etc.
};
```

**Usage**: Every Channel, Account, and Message has an associated ProviderId.

---

### 2. KickChannel (New Class)

**Location**: `src/providers/kick/KickChannel.hpp`

**Purpose**: Represents a Kick.tv chat channel with real-time message streaming.

**Inheritance**: `class KickChannel : public Channel`

#### Attributes

| Field | Type | Description | Validation |
|-------|------|-------------|------------|
| `channelId_` | `int` | Numeric Kick channel ID | > 0, required |
| `channelSlug_` | `QString` | Username/slug (e.g., "xqc") | Alphanumeric + underscore |
| `webSocket_` | `std::unique_ptr<KickWebSocket>` | WebSocket connection handler | Non-null when connected |
| `connectionState_` | `ConnectionState` enum | Current connection status | See ConnectionState enum |
| `kickAccount_` | `std::shared_ptr<KickAccount>` | Auth account (nullable for viewing) | Null OK (anonymous viewing) |
| `roomId_` | `int` | Chatroom ID for WebSocket subscribe | Resolved from channel ID |

#### ConnectionState Enum

```cpp
enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Failed
};
```

#### Key Methods

```cpp
class KickChannel : public Channel {
public:
    explicit KickChannel(const QString &channelSlug, Channeltype type);
    ~KickChannel() override;
    
    // Channel interface
    void sendMessage(const QString &message) override;
    bool isMod() const override;
    bool canSendMessage() const override;
    
    // Kick-specific
    void connect();
    void disconnect();
    void reconnect();
    ConnectionState getConnectionState() const;
    
    // Lifecycle
    void onWebSocketConnected();
    void onMessageReceived(const KickMessage &msg);
    void onWebSocketError(const QString &error);
    
private:
    void initializeWebSocket();
    void resolveChannelId();  // API call: slug → channel ID
    MessagePtr buildMessage(const KickMessage &raw);
    
    QString channelSlug_;
    int channelId_{-1};
    int roomId_{-1};
    std::unique_ptr<KickWebSocket> webSocket_;
    ConnectionState connectionState_{ConnectionState::Disconnected};
    std::shared_ptr<KickAccount> kickAccount_;
};
```

#### State Transitions

```
[Disconnected] --connect()--> [Connecting] --success--> [Connected]
                                     |
                                  fail ↓
                               [Reconnecting] --retry--> [Connecting]
                                     |
                              max retries ↓
                                  [Failed]

[Connected] --disconnect()--> [Disconnected]
[Connected] --error--> [Reconnecting]
```

---

### 3. MergedChannel (New Class)

**Location**: `src/channels/MergedChannel.hpp`

**Purpose**: Virtual channel that combines messages from multiple source channels (cross-platform).

**Inheritance**: `class MergedChannel : public Channel`

#### Attributes

| Field | Type | Description | Validation |
|-------|------|-------------|------------|
| `sourceChannels_` | `std::vector<std::shared_ptr<Channel>>` | List of source channels | Min 2, max 10 |
| `platformSelection_` | `PlatformSelection` enum | Target platform(s) for sending | Default: Both |
| `mergeStrategy_` | `MergeStrategy` enum | How to order messages | Default: Chronological |
| `channelConnections_` | `pajlada::Signals::ScopedConnectionContainer` | Manages signal subscriptions | Auto-cleanup on destroy |

#### PlatformSelection Enum

```cpp
enum class PlatformSelection {
    Both,          // Send to all connected platforms
    TwitchOnly,    // Send only to Twitch
    KickOnly,      // Send only to Kick
    // Future: per-platform selection UI
};
```

#### MergeStrategy Enum

```cpp
enum class MergeStrategy {
    Chronological,  // Sort by serverReceivedTime (current implementation)
    // Future: Interleaved (alternate), Custom (user-defined)
};
```

#### Key Methods

```cpp
class MergedChannel : public Channel {
public:
    explicit MergedChannel(const QString &name,
                           std::vector<std::shared_ptr<Channel>> sources);
    ~MergedChannel() override;
    
    // Channel interface
    void sendMessage(const QString &message) override;
    bool canSendMessage() const override;
    bool isMod() const override;  // True if mod in ANY source
    
    // Merged-specific
    void addSourceChannel(std::shared_ptr<Channel> channel);
    void removeSourceChannel(std::shared_ptr<Channel> channel);
    std::vector<std::shared_ptr<Channel>> getSourceChannels() const;
    
    void setPlatformSelection(PlatformSelection selection);
    PlatformSelection getPlatformSelection() const;
    
    // Display name for tab/header
    QString getDisplayName() const;  // E.g., "T:xqc + K:xqc"
    
private:
    void subscribeToSourceChannel(std::shared_ptr<Channel> channel);
    void onSourceMessageReceived(MessagePtr msg, std::shared_ptr<Channel> source);
    void sendToSelectedPlatforms(const QString &message);
    void handleSendResult(ProviderId platform, bool success, const QString &error);
    
    std::vector<std::shared_ptr<Channel>> sourceChannels_;
    PlatformSelection platformSelection_{PlatformSelection::Both};
    MergeStrategy mergeStrategy_{MergeStrategy::Chronological};
    pajlada::Signals::ScopedConnectionContainer channelConnections_;
};
```

#### Message Flow

```
[Source Channel 1] --messageAppended signal--> [MergedChannel::onSourceMessageReceived]
[Source Channel 2] --messageAppended signal--> ↓
                                         [Add to messages_ with chronological insert]
                                                        ↓
                                              [Emit messageAppended]
                                                        ↓
                                                  [ChannelView]
```

#### Sending Flow

```
User sends message in MergedChannel
        ↓
[Check platformSelection_]
        ↓
    [Both] → Send to Channel 1, Send to Channel 2 (parallel)
        ↓
Collect results (success/fail per platform)
        ↓
Display message with platform indicators showing delivery status
```

---

### 4. KickAccount (New Class)

**Location**: `src/providers/kick/KickAccount.hpp`

**Purpose**: Manages Kick authentication, OAuth tokens, and user identity.

**Inheritance**: `class KickAccount : public Account` (or similar existing pattern)

#### Attributes

| Field | Type | Description | Validation |
|-------|------|-------------|------------|
| `userId_` | `QString` | Kick user ID | Non-empty, numeric string |
| `username_` | `QString` | Kick username | Non-empty, alphanumeric |
| `accessToken_` | `QString` | OAuth access token | JWT format, ~1hr expiry |
| `refreshToken_` | `QString` | OAuth refresh token | Long-lived, secure storage |
| `tokenExpiry_` | `QDateTime` | Access token expiration time | Future timestamp |
| `isAnonymous_` | `bool` | True if not authenticated | Default: true |

#### Key Methods

```cpp
class KickAccount {
public:
    // Authentication
    bool isLoggedIn() const;
    bool isAnonymous() const;
    QString getAccessToken() const;
    QString getUserId() const;
    QString getUsername() const;
    
    // Token management
    void setTokens(const QString &access, const QString &refresh, int expiresIn);
    bool isTokenExpired() const;
    void refreshAccessToken();  // Async, emits signal on complete
    
    // Persistence
    void loadFromSettings();
    void saveToSettings();
    void logout();  // Clears tokens
    
signals:
    void loginStateChanged(bool loggedIn);
    void tokenRefreshed();
    void tokenRefreshFailed(const QString &error);
    
private:
    QString userId_;
    QString username_;
    QString accessToken_;
    QString refreshToken_;
    QDateTime tokenExpiry_;
    bool isAnonymous_{true};
};
```

---

### 5. ChannelDescriptor (Extended Struct)

**Location**: `src/common/ChannelDescriptor.hpp`

**Purpose**: Serializable representation of a channel for layout persistence.

#### Extended Fields

```cpp
struct ChannelDescriptor {
    QString type_;       // Existing: "twitch", "mentions", "whispers"
                         // NEW: "kick", "merged"
    QString data_;       // Existing: channel name
                         // NEW (kick): channel slug
                         // NEW (merged): structured JSON (see below)
    
    // NEW: For merged channels
    struct MergedChannelData {
        std::vector<ChannelDescriptor> sources;  // List of source channel descriptors
        PlatformSelection platformSelection;
    };
};
```

#### Merged Channel Serialization Format

**JSON Structure** (stored in `data_` field when `type_ == "merged"`):
```json
{
  "sources": [
    { "type": "twitch", "data": "xqc" },
    { "type": "kick", "data": "xqc" }
  ],
  "platformSelection": "Both"
}
```

**Parsing**: `WindowManager::decodeChannel()` deserializes this JSON to reconstruct MergedChannel with correct sources.

---

### 6. Message (Extended Struct)

**Location**: `src/messages/Message.hpp`

**Purpose**: Represents a single chat message. Extend to include platform source.

#### New Field

```cpp
struct Message {
    // Existing fields (from current codebase)
    QString messageText;
    QString loginName;
    QString displayName;
    QColor usernameColor;
    QDateTime serverReceivedTime;  // KEY: Used for chronological ordering
    std::vector<std::unique_ptr<MessageElement>> elements_;
    // ... other existing fields ...
    
    // NEW: Platform source identifier
    ProviderId platformSource;  // NEW: Twitch, Kick, etc.
};
```

**Usage**: MergedChannel uses `platformSource` to display platform badge. Message builder sets this when constructing messages from raw platform data.

---

### 7. KickMessage (New Struct)

**Location**: `src/providers/kick/KickMessage.hpp`

**Purpose**: Raw Kick message structure (maps to Pusher WebSocket event payload).

#### Structure

```cpp
struct KickMessage {
    QString id;                 // Message ID (ULID format)
    int chatroomId;
    QString content;            // Message text
    QString type;               // "message", "subscription", "gifted-subscriptions", etc.
    QDateTime createdAt;        // ISO 8601 timestamp
    
    struct Sender {
        int id;
        QString username;
        QString slug;
        Identity identity;      // Color, badges
    } sender;
    
    struct Identity {
        QString color;          // Hex color (e.g., "#FF5733")
        std::vector<Badge> badges;
    };
    
    struct Badge {
        QString type;           // "broadcaster", "moderator", "subscriber", etc.
        QString text;           // Display text
        int count;              // For subscriber badges (months)
    };
    
    // Parsed from WebSocket JSON
    static KickMessage fromJson(const QJsonObject &json);
};
```

---

## Relationships

### Entity Relationship Diagram

```
                     ┌─────────────┐
                     │ ProviderId  │
                     │   (enum)    │
                     └──────┬──────┘
                            │
           ┌────────────────┼────────────────┐
           │                │                │
           ▼                ▼                ▼
    ┌──────────┐    ┌──────────┐    ┌──────────────┐
    │  Twitch  │    │   Kick   │    │   Merged     │
    │ Channel  │    │ Channel  │    │   Channel    │
    └────┬─────┘    └────┬─────┘    └──────┬───────┘
         │               │                  │
         │               │                  │ has 1..n
         │               │                  ▼
         │               │           ┌─────────────┐
         │               │           │   sources   │
         │               │           │ (Channels)  │
         │               │           └─────────────┘
         │               │
         ▼               ▼
    ┌─────────────────────────┐
    │      Message            │
    │  + platformSource       │
    │  + serverReceivedTime   │
    └─────────────────────────┘
              │
              │ rendered by
              ▼
       ┌─────────────┐
       │ ChannelView │
       └─────────────┘
```

### Key Relationships

1. **Channel → Messages**: 1:N (one channel has many messages)
2. **MergedChannel → Source Channels**: 1:N (one merged channel aggregates N sources)
3. **Channel → ProviderId**: 1:1 (every channel belongs to one provider)
4. **Message → ProviderId**: 1:1 (every message originated from one platform)
5. **KickChannel → KickAccount**: N:1 (many channels can use same account)
6. **KickChannel → KickWebSocket**: 1:1 (one channel, one WebSocket connection)

---

## State Management

### Channel Connection Lifecycle

```cpp
// Managed by KickChannel
ConnectionState state_;
QTimer *reconnectTimer_;
int reconnectAttempts_{0};
static constexpr int MAX_RECONNECT_ATTEMPTS = 5;
static constexpr std::array<int, 5> RECONNECT_DELAYS = {1, 2, 4, 8, 30};  // seconds
```

### OAuth Token Lifecycle

```cpp
// Managed by KickAccount
void KickAccount::ensureValidToken() {
    if (isTokenExpired()) {
        refreshAccessToken();  // Async, blocks sends until refreshed
    }
}

void KickAccount::refreshAccessToken() {
    // POST to https://kick.com/oauth2/token
    // grant_type=refresh_token
    // Emit tokenRefreshed() or tokenRefreshFailed()
}
```

### Merged Channel Subscription Lifecycle

```cpp
// In MergedChannel constructor
for (auto &source : sourceChannels_) {
    subscribeToSourceChannel(source);
}

void MergedChannel::subscribeToSourceChannel(std::shared_ptr<Channel> channel) {
    // Use pajlada signals for automatic cleanup
    this->channelConnections_.managedConnect(
        channel->messageAppended,
        [this, channel](MessagePtr &msg, std::optional<MessageFlags> flags) {
            this->onSourceMessageReceived(msg, channel);
        }
    );
}
```

---

## Data Validation Rules

| Entity | Field | Rule | Error Handling |
|--------|-------|------|----------------|
| KickChannel | channelSlug_ | Alphanumeric + underscore, 3-25 chars | Show error in UI, prevent connection |
| KickChannel | channelId_ | Positive integer | API resolve failure → show error |
| MergedChannel | sourceChannels_ | Min 2 sources, all non-null | Disable merge UI if <2 channels selected |
| KickAccount | accessToken_ | Valid JWT format | Refresh token, logout if refresh fails |
| Message | serverReceivedTime | Valid QDateTime | Use current time as fallback |
| ChannelDescriptor (merged) | sources JSON | Valid JSON, non-empty sources array | Skip invalid descriptors, log warning |

---

## Performance Considerations

### Message Insertion Complexity

```cpp
// In Channel::addMessage() (existing code, reused by MergedChannel)
// Chronological insertion: O(n) worst case, O(log n) average with binary search
for (size_t i = 0; i < this->messages_.size(); ++i) {
    if (msg->serverReceivedTime < this->messages_[i]->serverReceivedTime) {
        this->messages_.insertBefore(this->messages_[i], msg);
        inserted = true;
        break;
    }
}
if (!inserted) {
    this->messages_.pushBack(msg);  // Most common case (messages arrive in order)
}
```

**Optimization**: Messages typically arrive in order, so `pushBack()` is O(1) fast path.

### Memory Management

- **Qt Object Tree**: Channels, WebSockets use Qt parent-child for automatic cleanup
- **Shared Pointers**: `std::shared_ptr<Channel>` for source channels in MergedChannel (safe shared ownership)
- **Unique Pointers**: `std::unique_ptr<KickWebSocket>` for exclusive ownership
- **Message Lifetime**: Messages in `LimitedQueue` auto-evict old messages (configurable, default ~1000 messages)

---

## Migration & Compatibility

### Backward Compatibility

- **Existing layout files**: Unaffected. "twitch" channels load as before.
- **New channel types**: "kick" and "merged" descriptors only appear after user creates them.
- **Settings**: New settings keys under `kick/` namespace, no conflicts with existing Twitch settings.

### Data Migration

No migration required. This is additive functionality.

---

## Next Phase

✅ **Data Model Complete**

⏭️ **Next**: Generate contracts/ with API protocol specifications and quickstart.md.

