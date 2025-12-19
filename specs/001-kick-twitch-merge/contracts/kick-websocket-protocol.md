# Contract: Kick WebSocket Protocol (Pusher)

**Version**: 1.0  
**Date**: 2025-12-18  
**Status**: Reverse-Engineered (Unofficial)  
**Source**: Analysis of Twick, KickTalk, and Kick web client network traffic

⚠️ **Warning**: This protocol is unofficial and may change without notice. Monitor third-party client communities for updates.

---

## Overview

Kick.com uses the Pusher Channels service for real-time WebSocket communication. Kick chat messages are broadcast as Pusher events on channel-specific subscriptions.

**Protocol Version**: Pusher Protocol v7  
**Transport**: WebSocket (wss://)  
**Message Format**: JSON

---

## Connection Establishment

### 1. WebSocket Handshake

**Endpoint**:
```
wss://ws-us2.pusher.com/app/{APP_KEY}?protocol=7&client=js&version=7.4.0&flash=false
```

**Parameters**:
- `{APP_KEY}`: Kick's Pusher application key (publicly visible, extract from Kick web client)
- `protocol`: Must be `7` (Pusher Protocol v7)
- `client`: Client identifier (use "chatterino" or "js")
- `version`: Pusher JS client version (use "7.4.0" or current)

**Example**:
```
wss://ws-us2.pusher.com/app/eb1d5f283081a78b932c?protocol=7&client=chatterino&version=7.4.0&flash=false
```

### 2. Connection Established Event

**Direction**: Server → Client

**Event**: `pusher:connection_established`

**Payload**:
```json
{
  "event": "pusher:connection_established",
  "data": "{\"socket_id\":\"123456.789012\",\"activity_timeout\":120}"
}
```

**Fields**:
- `socket_id`: Unique socket identifier for this connection
- `activity_timeout`: Seconds of inactivity before server closes connection (typically 120)

**Client Action**: Store `socket_id`, start ping timer (send ping before `activity_timeout`)

---

## Channel Subscription

### 1. Subscribe to Channel

**Direction**: Client → Server

**Event**: `pusher:subscribe`

**Payload**:
```json
{
  "event": "pusher:subscribe",
  "data": {
    "channel": "chatrooms.{CHANNEL_ID}.v2"
  }
}
```

**Channel Format**: `chatrooms.{CHANNEL_ID}.v2`
- `{CHANNEL_ID}`: Numeric Kick channel ID (not username slug, requires API resolution)
- `.v2`: Version indicator (current version)

**Example**:
```json
{
  "event": "pusher:subscribe",
  "data": {
    "channel": "chatrooms.123456.v2"
  }
}
```

### 2. Subscription Succeeded Event

**Direction**: Server → Client

**Event**: `pusher_internal:subscription_succeeded`

**Payload**:
```json
{
  "event": "pusher_internal:subscription_succeeded",
  "channel": "chatrooms.123456.v2",
  "data": "{}"
}
```

**Client Action**: Mark channel as subscribed, ready to receive messages

---

## Chat Message Events

### Event Type: `App\\Events\\ChatMessageEvent`

**Direction**: Server → Client

**Event**: `App\\Events\\ChatMessageEvent`

**Payload Structure**:
```json
{
  "event": "App\\Events\\ChatMessageEvent",
  "channel": "chatrooms.123456.v2",
  "data": "{\"id\":\"01HQZX9KMJNP8QRST9UVWXYZA1\",\"chatroom_id\":123456,\"content\":\"Hello chat!\",\"type\":\"message\",\"created_at\":\"2025-12-18T10:30:00.000000Z\",\"sender\":{\"id\":789,\"username\":\"user123\",\"slug\":\"user123\",\"identity\":{\"color\":\"#FF5733\",\"badges\":[{\"type\":\"subscriber\",\"text\":\"Subscriber\",\"count\":3}]}}}"
}
```

**Data Field** (nested JSON string, must be parsed):
```json
{
  "id": "01HQZX9KMJNP8QRST9UVWXYZA1",
  "chatroom_id": 123456,
  "content": "Hello chat!",
  "type": "message",
  "created_at": "2025-12-18T10:30:00.000000Z",
  "sender": {
    "id": 789,
    "username": "user123",
    "slug": "user123",
    "identity": {
      "color": "#FF5733",
      "badges": [
        {
          "type": "subscriber",
          "text": "Subscriber",
          "count": 3
        },
        {
          "type": "moderator",
          "text": "Moderator"
        }
      ]
    }
  }
}
```

### Message Field Descriptions

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| `id` | string | ULID message identifier | Unique, sortable by time |
| `chatroom_id` | integer | Channel chatroom ID | Matches subscription channel |
| `content` | string | Message text content | May include emote codes |
| `type` | string | Message type | "message", "subscription", "gifted-subscriptions" |
| `created_at` | string | ISO 8601 timestamp with microseconds | Server time, use for `serverReceivedTime` |
| `sender.id` | integer | User ID | Unique user identifier |
| `sender.username` | string | Display username | May differ from slug (capitalization) |
| `sender.slug` | string | Canonical username slug | Lowercase, URL-safe |
| `sender.identity.color` | string | Hex color code for username | Nullable if user hasn't set color |
| `sender.identity.badges` | array | User badges | Empty array if no badges |
| `badges[].type` | string | Badge type identifier | "broadcaster", "moderator", "subscriber", "vip", "og", "sub_gifter" |
| `badges[].text` | string | Display text for badge | Human-readable label |
| `badges[].count` | integer | Badge count (for subscribers) | Months subscribed, nullable |

### Other Event Types (Future Expansion)

Kick sends other event types for subscriptions, raids, etc.:
- `App\\Events\\SubscriptionEvent`
- `App\\Events\\GiftedSubscriptionsEvent`
- `App\\Events\\ChatroomClearEvent`

**MVP**: Only implement `ChatMessageEvent`. Document others for future enhancement.

---

## Keep-Alive (Ping/Pong)

### Ping

**Direction**: Client → Server

**Event**: `pusher:ping`

**Payload**:
```json
{
  "event": "pusher:ping",
  "data": {}
}
```

**Frequency**: Send every ~60 seconds (before `activity_timeout` of 120s)

### Pong

**Direction**: Server → Client

**Event**: `pusher:pong`

**Payload**:
```json
{
  "event": "pusher:pong",
  "data": {}
}
```

**Client Action**: Reset activity timer, connection is alive

---

## Error Handling

### Error Event

**Direction**: Server → Client

**Event**: `pusher:error`

**Payload**:
```json
{
  "event": "pusher:error",
  "data": {
    "message": "Error message here",
    "code": 4001
  }
}
```

### Common Error Codes

| Code | Meaning | Client Action |
|------|---------|---------------|
| 4000 | Application only accepts SSL connections | Use wss:// (should never occur) |
| 4001 | Application does not exist | App key incorrect, update app key |
| 4004 | Application disabled | Kick API issue, retry after delay |
| 4005 | Path not found | Incorrect WebSocket path, check URL |
| 4006 | Invalid version | Protocol version not supported, update client |
| 4007 | Unsupported protocol version | Use protocol=7 |
| 4008 | No protocol version supplied | Add protocol parameter to URL |
| 4009 | Connection is unauthorized | Channel auth failed (shouldn't occur for public channels) |

### Client Error Handling Strategy

```cpp
void KickWebSocket::onError(int code, const QString &message) {
    switch (code) {
        case 4001:
            // App key invalid - critical error
            emit fatalError("Kick app key is outdated. Please report to developers.");
            this->disconnect();
            break;
        case 4004:
        case 4005:
            // Transient errors - reconnect with backoff
            this->scheduleReconnect();
            break;
        default:
            // Unknown error - log and reconnect
            qWarning() << "Kick WebSocket error:" << code << message;
            this->scheduleReconnect();
            break;
    }
}
```

---

## Unsubscribe

**Direction**: Client → Server

**Event**: `pusher:unsubscribe`

**Payload**:
```json
{
  "event": "pusher:unsubscribe",
  "data": {
    "channel": "chatrooms.123456.v2"
  }
}
```

**When**: Client no longer wants updates for a channel (e.g., user closes tab)

---

## Connection Lifecycle

```
[Disconnected]
      ↓
    connect()
      ↓
[WebSocket Handshake]
      ↓
[pusher:connection_established] ← Server
      ↓
[Send pusher:subscribe]
      ↓
[pusher_internal:subscription_succeeded] ← Server
      ↓
[Connected - Receiving Messages]
      │
      │ Every 60s: Send pusher:ping
      │ ← Receive pusher:pong
      │
      │ On message: App\\Events\\ChatMessageEvent
      │
      │ On error or timeout:
      ↓
[Reconnecting with exponential backoff]
      ↓
[Try handshake again...]
```

---

## Implementation Checklist

- [ ] WebSocket connection with wss:// to Pusher endpoint
- [ ] Parse `pusher:connection_established` and store `socket_id`
- [ ] Send `pusher:subscribe` with channel format `chatrooms.{id}.v2`
- [ ] Handle `pusher_internal:subscription_succeeded`
- [ ] Parse `App\\Events\\ChatMessageEvent` (double JSON decode!)
- [ ] Implement ping timer (60s interval)
- [ ] Handle `pusher:pong` responses
- [ ] Parse error events and handle error codes
- [ ] Implement reconnection with exponential backoff (1s, 2s, 4s, 8s, 30s)
- [ ] Send `pusher:unsubscribe` on channel close
- [ ] Close WebSocket cleanly on app shutdown

---

## Testing Strategy

### Unit Tests

```cpp
TEST(KickWebSocket, ParseConnectionEstablished) {
    QString json = R"({"event":"pusher:connection_established","data":"{\"socket_id\":\"123.456\"}"})";
    auto event = KickWebSocket::parseEvent(json);
    EXPECT_EQ(event.event, "pusher:connection_established");
    EXPECT_EQ(event.socketId, "123.456");
}

TEST(KickWebSocket, ParseChatMessage) {
    QString json = R"({"event":"App\\Events\\ChatMessageEvent","channel":"chatrooms.123.v2","data":"{\"id\":\"abc\",\"content\":\"test\"}"})";
    auto msg = KickWebSocket::parseChatMessage(json);
    EXPECT_EQ(msg.id, "abc");
    EXPECT_EQ(msg.content, "test");
}
```

### Integration Tests

- Connect to real Kick channel (use test account)
- Verify subscription succeeds
- Send test message via REST API, verify received via WebSocket
- Test reconnection by dropping connection
- Test error handling with invalid app key

---

## Maintenance Notes

**App Key Extraction**: Currently extracted from Kick web client (`https://kick.com`). Inspect network tab → WebSocket connection → app key in URL.

**Protocol Changes**: Monitor:
- [Twick GitHub Issues](https://github.com/twicklabs/twick/issues)
- Reddit [/r/Kick](https://reddit.com/r/kick)
- Discord: Kick third-party developer communities

**Versioning**: If Pusher protocol updates (v8, v9), test backward compatibility. Update `protocol` parameter if necessary.

**Fallback Plan**: If WebSocket protocol breaks frequently, implement webhook relay architecture (see future-enhancements in spec.md).

---

## References

- [Pusher Channels Protocol](https://pusher.com/docs/channels/library_auth_reference/pusher-websockets-protocol/)
- [Pusher JavaScript Client (reference implementation)](https://github.com/pusher/pusher-js)
- [Twick iOS App (open source, Kick client)](https://github.com/twicklabs/twick)
- [RFC 6455: The WebSocket Protocol](https://tools.ietf.org/html/rfc6455)

