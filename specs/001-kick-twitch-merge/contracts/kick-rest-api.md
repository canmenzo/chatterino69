# Contract: Kick REST API (Message Sending)

**Version**: 1.0  
**Date**: 2025-12-18  
**Status**: Official API (Documented by Kick)  
**Source**: [Kick Developer Documentation](https://docs.kick.com/apis/chat)

---

## Overview

This contract defines the REST API for sending chat messages to Kick channels. This is the **only** write operation required for MVP (reading messages uses WebSocket - see kick-websocket-protocol.md).

**Base URL**: `https://api.kick.com/public/v1`  
**Authentication**: Bearer token (OAuth 2.1 access token)  
**Content Type**: `application/json`

---

## Send Chat Message

### Endpoint

`POST /chat`

### Purpose

Post a message to a Kick channel's chat as an authenticated user.

### Authentication

**Required**: Yes (OAuth 2.1 access token with `chat:write` scope)

**Header**:
```
Authorization: Bearer {ACCESS_TOKEN}
```

### Request

#### Headers

| Header | Value | Required |
|--------|-------|----------|
| `Authorization` | `Bearer {access_token}` | Yes |
| `Content-Type` | `application/json` | Yes |

#### Body

```json
{
  "broadcaster_user_id": 123456,
  "message": "Hello chat! This is my message."
}
```

**Fields**:

| Field | Type | Required | Description | Validation |
|-------|------|----------|-------------|------------|
| `broadcaster_user_id` | integer | Yes | Channel ID (not username slug) | Positive integer |
| `message` | string | Yes | Message content | 1-500 characters, no leading/trailing whitespace |
| `reply_to_message_id` | string | No | ID of message to reply to (for threading) | ULID format (if provided) |

**Example Request**:
```http
POST /public/v1/chat HTTP/1.1
Host: api.kick.com
Authorization: Bearer eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9...
Content-Type: application/json

{
  "broadcaster_user_id": 987654321,
  "message": "Hello chat! This is a bot message."
}
```

### Response

#### Success (200 OK)

```json
{
  "data": {
    "message_id": "01HQZX9KMJNP8QRST9UVWXYZA1",
    "broadcaster_user_id": 987654321,
    "sender": {
      "user_id": 123456789,
      "username": "my_username",
      "profile_picture": "https://files.kick.com/images/user/123456789/profile.jpeg"
    },
    "content": "Hello chat! This is a bot message.",
    "created_at": "2025-12-18T14:35:22Z"
  }
}
```

**Response Fields**:

| Field | Type | Description |
|-------|------|-------------|
| `data.message_id` | string | ULID identifier for sent message |
| `data.broadcaster_user_id` | integer | Channel ID where message was sent |
| `data.sender.user_id` | integer | User ID of sender (your user) |
| `data.sender.username` | string | Username of sender |
| `data.sender.profile_picture` | string | Profile picture URL |
| `data.content` | string | Message content (echoed back) |
| `data.created_at` | string | ISO 8601 timestamp when message was created |

#### Error Responses

##### 400 Bad Request

**Missing or invalid parameters**:
```json
{
  "error": {
    "message": "Validation failed",
    "errors": {
      "broadcaster_user_id": ["The broadcaster user id field is required."],
      "message": ["The message field is required."]
    }
  }
}
```

**Message too long**:
```json
{
  "error": {
    "message": "Validation failed",
    "errors": {
      "message": ["The message may not be greater than 500 characters."]
    }
  }
}
```

##### 401 Unauthorized

**Missing or invalid access token**:
```json
{
  "error": {
    "message": "Unauthenticated."
  }
}
```

**Token expired**:
```json
{
  "error": {
    "message": "Token has expired."
  }
}
```

##### 403 Forbidden

**Insufficient scope** (missing `chat:write`):
```json
{
  "error": {
    "message": "Insufficient scope",
    "required_scope": "chat:write"
  }
}
```

**User banned from channel**:
```json
{
  "error": {
    "message": "You are banned from this channel."
  }
}
```

**User timed out**:
```json
{
  "error": {
    "message": "You are timed out from this channel.",
    "timeout_until": "2025-12-18T15:00:00Z"
  }
}
```

##### 429 Too Many Requests

**Rate limit exceeded**:
```json
{
  "error": {
    "message": "Too many requests. Please slow down.",
    "retry_after": 30
  }
}
```

**Rate Limit Headers** (included in response):
```
X-RateLimit-Limit: 20
X-RateLimit-Remaining: 0
X-RateLimit-Reset: 1671368700
Retry-After: 30
```

##### 500 Internal Server Error

**Server error**:
```json
{
  "error": {
    "message": "An unexpected error occurred. Please try again later."
  }
}
```

---

## Rate Limiting

### Limits

**Per-User Limits**:
- **20 messages per 30 seconds** per channel
- **100 messages per hour** across all channels

**Detection**: Monitor `X-RateLimit-*` headers in responses

### Headers

| Header | Description | Example |
|--------|-------------|---------|
| `X-RateLimit-Limit` | Maximum requests allowed in window | `20` |
| `X-RateLimit-Remaining` | Requests remaining in current window | `15` |
| `X-RateLimit-Reset` | Unix timestamp when window resets | `1671368700` |
| `Retry-After` | Seconds to wait before retrying (429 only) | `30` |

### Client Implementation

```cpp
class KickApi {
public:
    struct RateLimitInfo {
        int limit{20};
        int remaining{20};
        QDateTime resetTime;
    };
    
    void sendMessage(int channelId, const QString &message) {
        // Check rate limit before sending
        if (this->rateLimitInfo_.remaining <= 0) {
            if (QDateTime::currentDateTime() < this->rateLimitInfo_.resetTime) {
                emit sendFailed("Rate limit exceeded. Please wait.");
                return;
            }
        }
        
        // Send request...
    }
    
private:
    void updateRateLimitInfo(QNetworkReply *reply) {
        if (reply->hasRawHeader("X-RateLimit-Limit")) {
            this->rateLimitInfo_.limit = reply->rawHeader("X-RateLimit-Limit").toInt();
        }
        if (reply->hasRawHeader("X-RateLimit-Remaining")) {
            this->rateLimitInfo_.remaining = reply->rawHeader("X-RateLimit-Remaining").toInt();
        }
        if (reply->hasRawHeader("X-RateLimit-Reset")) {
            qint64 resetUnix = reply->rawHeader("X-RateLimit-Reset").toLongLong();
            this->rateLimitInfo_.resetTime = QDateTime::fromSecsSinceEpoch(resetUnix);
        }
    }
    
    RateLimitInfo rateLimitInfo_;
};
```

---

## Implementation

### Qt Network Example

```cpp
void KickApi::sendMessage(int channelId, const QString &message,
                          std::function<void(bool, QString)> callback) {
    // Validate message
    if (message.trimmed().isEmpty() || message.length() > 500) {
        callback(false, "Message must be 1-500 characters");
        return;
    }
    
    // Check authentication
    if (!this->account_ || !this->account_->isLoggedIn()) {
        callback(false, "Not logged in");
        return;
    }
    
    // Ensure valid token
    if (this->account_->isTokenExpired()) {
        this->account_->refreshAccessToken();
        // Wait for refresh, then retry (or fail if refresh fails)
        return;
    }
    
    // Build request
    QNetworkRequest request(QUrl("https://api.kick.com/public/v1/chat"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", 
                         QString("Bearer %1").arg(this->account_->getAccessToken()).toUtf8());
    
    // Build JSON body
    QJsonObject body;
    body["broadcaster_user_id"] = channelId;
    body["message"] = message;
    QJsonDocument doc(body);
    
    // Send request
    QNetworkReply *reply = this->networkManager_->post(request, doc.toJson());
    
    // Handle response
    connect(reply, &QNetworkReply::finished, [this, reply, callback]() {
        this->handleSendMessageResponse(reply, callback);
        reply->deleteLater();
    });
}

void KickApi::handleSendMessageResponse(QNetworkReply *reply,
                                         std::function<void(bool, QString)> callback) {
    // Update rate limit info
    this->updateRateLimitInfo(reply);
    
    // Check HTTP status
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    
    if (statusCode == 200) {
        // Success
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QString messageId = doc["data"]["message_id"].toString();
        callback(true, messageId);
    } else if (statusCode == 401) {
        // Token expired, try refresh
        this->account_->refreshAccessToken();
        callback(false, "Token expired. Refreshing...");
    } else if (statusCode == 429) {
        // Rate limited
        int retryAfter = reply->rawHeader("Retry-After").toInt();
        callback(false, QString("Rate limited. Retry in %1 seconds.").arg(retryAfter));
    } else {
        // Other error
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QString errorMsg = doc["error"]["message"].toString();
        callback(false, errorMsg);
    }
}
```

---

## Error Handling Strategy

### Client Error Handling Table

| Status Code | Error | Client Action |
|-------------|-------|---------------|
| 200 | Success | Display message in chat with success indicator |
| 400 | Validation error | Show error to user, don't retry |
| 401 | Token expired | Auto-refresh token, retry once |
| 401 | Token invalid | Force re-login, show login prompt |
| 403 | Banned/timed out | Show error to user, disable send for timeout duration |
| 403 | Insufficient scope | Show error, prompt re-authorization with chat:write |
| 429 | Rate limited | Queue message, retry after Retry-After seconds |
| 500 | Server error | Retry with exponential backoff (1s, 2s, 4s), max 3 retries |
| Network error | Connection failed | Retry with backoff, show "offline" indicator |

### User Feedback

**Success**:
```
[✓] Message sent to Kick
```

**Rate Limited**:
```
[!] Sending too fast. Please wait 30 seconds.
```

**Banned**:
```
[✗] You are banned from this channel.
```

**Network Error**:
```
[!] Could not send message. Check connection. (Retrying...)
```

---

## Testing Strategy

### Unit Tests

```cpp
TEST(KickApi, BuildSendMessageRequest) {
    KickApi api;
    QNetworkRequest req = api.buildSendMessageRequest(123, "test");
    EXPECT_EQ(req.url().toString(), "https://api.kick.com/public/v1/chat");
    EXPECT_TRUE(req.hasRawHeader("Authorization"));
    EXPECT_EQ(req.header(QNetworkRequest::ContentTypeHeader), "application/json");
}

TEST(KickApi, ValidateMessageLength) {
    KickApi api;
    EXPECT_TRUE(api.isValidMessage("Hello"));
    EXPECT_FALSE(api.isValidMessage(""));  // Empty
    EXPECT_FALSE(api.isValidMessage(QString(501, 'x')));  // Too long
}

TEST(KickApi, ParseRateLimitHeaders) {
    QNetworkReply *mockReply = createMockReply();
    mockReply->setRawHeader("X-RateLimit-Remaining", "15");
    
    KickApi api;
    api.updateRateLimitInfo(mockReply);
    
    EXPECT_EQ(api.getRateLimitRemaining(), 15);
}
```

### Integration Tests

```cpp
TEST(KickApiIntegration, SendMessageToTestChannel) {
    // Use test Kick account
    KickAccount testAccount = createTestAccount();
    KickApi api(&testAccount);
    
    bool success = false;
    QString messageId;
    
    api.sendMessage(TEST_CHANNEL_ID, "Integration test message", 
                    [&](bool s, QString id) { success = s; messageId = id; });
    
    // Wait for async response (use QSignalSpy or QEventLoop)
    ASSERT_TRUE(success);
    ASSERT_FALSE(messageId.isEmpty());
}
```

### Manual Testing

1. Send message to own Kick channel, verify appears in chat
2. Send rapid messages, verify rate limiting kicks in
3. Send with expired token, verify auto-refresh and retry
4. Send while banned, verify error shown
5. Send with invalid channel ID, verify error handling
6. Send while offline, verify queuing and retry on reconnect

---

## Resolve Username → Channel ID

**Problem**: Send message API requires `broadcaster_user_id` (numeric channel ID), but users input username slugs.

**Solution**: Look up channel ID from username before first message send, cache result.

### Endpoint (Unofficial, from observation)

`GET https://kick.com/api/v2/channels/{username}`

**Example**:
```http
GET /api/v2/channels/xqc HTTP/1.1
Host: kick.com
```

**Response**:
```json
{
  "id": 987654,
  "user_id": 123456,
  "slug": "xqc",
  "is_banned": false,
  "chatroom": {
    "id": 789012,
    "chatable_type": "App\\Models\\Channel",
    "channel_id": 987654,
    "chat_mode": "public"
  }
}
```

**Extract**: `id` field (987654) is the `broadcaster_user_id` needed for send message API.

**Implementation**:
```cpp
void KickApi::resolveChannelId(const QString &username,
                                std::function<void(int)> callback) {
    QUrl url(QString("https://kick.com/api/v2/channels/%1").arg(username));
    QNetworkRequest req(url);
    
    QNetworkReply *reply = this->networkManager_->get(req);
    connect(reply, &QNetworkReply::finished, [reply, callback]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            int channelId = doc["id"].toInt();
            callback(channelId);
        } else {
            callback(-1);  // Error
        }
        reply->deleteLater();
    });
}
```

**Caching**: Store `username → channelId` mapping in `KickChannel` after first resolution. Don't re-resolve on every message send.

---

## Implementation Checklist

- [ ] Implement `KickApi::sendMessage(channelId, message, callback)`
- [ ] Add Bearer token authentication header
- [ ] Build JSON request body with broadcaster_user_id and message
- [ ] Parse success response (200 OK), extract message_id
- [ ] Handle 401 Unauthorized → auto-refresh token, retry
- [ ] Handle 403 Forbidden → show ban/timeout error
- [ ] Handle 429 Rate Limited → queue message, respect Retry-After
- [ ] Handle 400 Bad Request → show validation error
- [ ] Handle 500 Server Error → retry with exponential backoff
- [ ] Parse and track rate limit headers (X-RateLimit-*)
- [ ] Implement username → channel ID resolution (cache result)
- [ ] Add message validation (1-500 chars, non-empty after trim)
- [ ] Implement retry logic with backoff
- [ ] Add unit tests for request building and response parsing
- [ ] Add integration tests with test Kick account
- [ ] Test on all error scenarios (ban, rate limit, etc.)

---

## References

- [Kick Chat API Documentation](https://docs.kick.com/apis/chat)
- [Kick OpenAPI Specification](https://api.kick.com/swagger/v1/doc.json)
- [RFC 6749: OAuth 2.0 Authorization Framework](https://tools.ietf.org/html/rfc6749)

