# Research & Architecture Decisions: Kick.tv Integration

**Date**: 2025-12-18  
**Status**: Complete  
**Plan Reference**: [plan.md](./plan.md)

This document resolves all technical unknowns identified during planning Phase 0.

---

## 1. Kick WebSocket/Pusher Protocol Reverse Engineering

### Decision
**Use Pusher protocol** for Kick real-time chat ingestion. Kick uses Pusher Channels service for WebSocket communication.

### Rationale
- **Evidence**: Third-party clients (Twick, KickTalk) successfully use this approach
- **Proven Pattern**: Pusher is a well-documented, stable WebSocket protocol used by many chat platforms
- **Library Support**: Multiple Pusher client libraries exist for C++ integration
- **Context7 Findings**: Pusher protocol uses standard WebSocket handshake, subscribe/bind event pattern

### Protocol Details (from reverse engineering research)

**Connection Flow:**
1. Connect to Pusher WebSocket endpoint: `wss://ws-us2.pusher.com/app/{app_key}?protocol=7`
2. Receive `pusher:connection_established` event
3. Subscribe to channel: Send `pusher:subscribe` with channel name `chatrooms.{channel_id}.v2`
4. Bind to events: `App\\Events\\ChatMessageEvent` for messages

**Message Format:**
```json
{
  "event": "App\\Events\\ChatMessageEvent",
  "channel": "chatrooms.123456.v2",
  "data": {
    "id": "message_id",
    "chatroom_id": 123456,
    "content": "message text",
    "type": "message",
    "created_at": "2025-12-18T10:30:00Z",
    "sender": {
      "id": 789,
      "username": "user123",
      "slug": "user123",
      "identity": {
        "color": "#FF5733",
        "badges": [...]
      }
    }
  }
}
```

**Key Protocol Elements:**
- **App Key**: Publicly visible in Kick web client (extract from browser network tab)
- **Channel Format**: `chatrooms.{channel_id}.v2` (need to resolve username → channel_id via Kick REST API)
- **Ping/Pong**: Pusher handles automatically (every 30s)
- **Reconnection**: Exponential backoff (1s, 2s, 4s, 8s, max 30s)

### Implementation Notes
- Extract Kick app key from public web client (changes infrequently)
- Add fallback: if app key changes, detect connection failure and log clear error for user
- Document protocol in `contracts/kick-websocket-protocol.md` for maintenance
- Monitor community (Twick GitHub, Reddit /r/Kick) for protocol changes

### Alternatives Considered
- **REST API polling**: Rejected (high latency, rate limit issues, not real-time)
- **Official webhook API**: Rejected for MVP (requires user infrastructure deployment)

---

## 2. Pusher C++ Client Library Evaluation

### Decision
**Use Qt WebSockets with custom Pusher protocol implementation** rather than third-party Pusher library.

### Rationale
- **Qt Native**: Qt WebSockets is part of Qt framework, already a dependency
- **Control**: Full control over protocol implementation for debugging/maintenance
- **Cross-Platform**: Qt abstracts platform differences (Windows/Linux/macOS/FreeBSD)
- **Simplicity**: Pusher protocol is straightforward (JSON over WebSocket), custom implementation ~500 LOC
- **Maintenance**: No additional external dependency to track/update
- **Precedent**: Existing Twitch IRC implementation uses Qt networking primitives

### Implementation Approach
**Class**: `KickWebSocket` (inherits `QObject`)
- Use `QWebSocket` for connection
- Implement Pusher protocol handshake (connection_established, subscribe, bind)
- Parse JSON events using existing JSON library (Qt's `QJsonDocument` or nlohmann/json if already in use)
- Emit Qt signals for chat events: `messageReceived(KickMessage msg)`

**Connection Lifecycle:**
```cpp
class KickWebSocket : public QObject {
    Q_OBJECT
public:
    void connect(const QString &appKey);
    void subscribe(int channelId);
    void disconnect();
    
signals:
    void connected();
    void messageReceived(const KickMessage &msg);
    void error(const QString &error);
    void connectionStateChanged(ConnectionState state);
    
private slots:
    void onWebSocketConnected();
    void onTextMessageReceived(const QString &message);
    void onWebSocketError(QAbstractSocket::SocketError error);
    
private:
    QWebSocket *webSocket_;
    QString appKey_;
    int channelId_;
    QTimer *pingTimer_;
};
```

### Alternatives Considered
- **pusher-websocket-java** (port to C++): Rejected (complex port, Java-specific patterns)
- **websocketpp**: Rejected (adds dependency, Qt WebSockets sufficient)
- **Third-party Pusher C++ lib**: Rejected (none found with active maintenance + Qt support)

---

## 3. Kick Recent Messages Service

### Decision
**No recent message history for Kick channels in MVP**. Start fresh from connection time.

### Rationale
- **Research Findings**: No public Kick history API found; no equivalent to recent-messages2 for Kick
- **Third-Party Evidence**: Twick, KickTalk also start fresh (no history shown on channel open)
- **Scope Management**: Out of MVP scope; can be added post-launch if Kick provides history API
- **Consistent UX**: Match existing Chatterino behavior for Twitch when recent-messages2 unavailable

### Implementation Notes
- Document limitation in user-facing docs: "Kick channels display messages from connection time forward"
- Add infrastructure hook for future history: `KickChannel::fetchRecentMessages()` stub (returns immediately)
- Revisit post-MVP if:
  1. Kick releases official history API
  2. Community discovers history endpoint via reverse engineering
  3. Users request feature strongly

### Mitigation
- Fetch history for **Twitch** side of merged views using existing recent-messages2 integration
- Clearly label in UI: "Kick: live messages only" vs "Twitch: last 100 messages"

### Alternatives Considered
- **Scrape Kick website**: Rejected (violates TOS, brittle, rate limits)
- **Build community history service**: Rejected (out of scope, requires infrastructure)
- **Cache locally**: Considered for future (store messages locally, show on re-open)

---

## 4. OAuth 2.1 with PKCE in Qt/Desktop Apps

### Decision
**System browser OAuth flow with localhost callback** using Qt's `QDesktopServices` and local HTTP server.

### Rationale
- **Best Practice**: Standard OAuth 2.1 pattern for desktop apps (RFC 8252)
- **Security**: PKCE protects against authorization code interception
- **User Trust**: System browser shows real Kick URL (not embedded browser)
- **No Qt WebEngine**: Avoids large Qt WebEngine dependency (~100MB+)
- **Proven Pattern**: Used by VS Code, GitHub Desktop, and other desktop OAuth apps

### Implementation Flow
1. **Generate PKCE code verifier** (random 128-char string) and code challenge (SHA256 hash)
2. **Start local HTTP server** on `http://localhost:PORT` (random ephemeral port 49152-65535)
3. **Open system browser** with Kick authorization URL:
   ```
   https://kick.com/oauth2/authorize?
     client_id={app_client_id}&
     redirect_uri=http://localhost:{PORT}&
     response_type=code&
     code_challenge={challenge}&
     code_challenge_method=S256&
     scope=chat:write
   ```
4. **User authenticates** in browser, Kick redirects to `http://localhost:{PORT}?code={auth_code}`
5. **Local server receives** callback, extracts auth code, returns success HTML page
6. **Exchange code for tokens**: POST to Kick token endpoint with code verifier
7. **Store tokens** securely (see Decision 7), shutdown local server

### Implementation Class
```cpp
class KickOAuthFlow : public QObject {
    Q_OBJECT
public:
    void startAuthorization();
    
signals:
    void authorizationSucceeded(const KickAccount &account);
    void authorizationFailed(const QString &error);
    
private:
    QTcpServer *localServer_;  // Localhost HTTP server
    QString codeVerifier_;
    QString codeChallenge_;
    
    void openBrowser(const QUrl &authUrl);
    void handleCallback(const QString &authCode);
    void exchangeCodeForTokens(const QString &code);
};
```

### Error Handling
- **Browser doesn't open**: Show manual URL copy button
- **Timeout (5 min)**: Auto-cancel, show error message
- **Network error**: Retry with backoff, fallback error guidance
- **Token exchange fails**: Log reason, show user-friendly error

### Alternatives Considered
- **Qt WebEngine embedded browser**: Rejected (large dependency, security risks, user trust issues)
- **Custom URL scheme** (`chatterino7://callback`): Rejected (OS registration complexity, security concerns on Windows/Linux)
- **Manual token paste**: Rejected (poor UX, error-prone)

---

## 5. Merged Channel Message Ordering Performance

### Decision
**Merge on arrival** with optimized insertion into `LimitedQueue` using binary search.

### Rationale
- **Existing Pattern**: `Channel.cpp` already implements chronological insertion with `serverReceivedTime` comparison
- **Performance**: Binary search for insertion point: O(log n) find + O(n) insert, acceptable for 100-200 msg history
- **Correctness**: Maintains strict chronological order even with clock skew between platforms
- **Simplicity**: Reuse existing `LimitedQueue::insertBefore()` mechanism

### Implementation Approach
**MergedChannel** subscribes to source channels, re-emits with chronological insertion:
```cpp
void MergedChannel::onSourceMessageReceived(MessagePtr msg) {
    // msg already has serverReceivedTime set by source channel
    this->addMessage(msg);  // Uses existing chronological insert logic
}
```

**Optimization**: Source channels (Twitch, Kick) already set `serverReceivedTime` from platform timestamp. MergedChannel just forwards messages, letting base `Channel::addMessage()` handle insertion.

### Benchmark Target
- **Scenario**: 2 channels × 50 msg/s = 100 msg/s total
- **Target**: <10ms latency from msg arrival to UI update
- **Validation**: Performance test in test suite

### Alternatives Considered
- **Lazy merge on display**: Rejected (complex invalidation logic, doesn't simplify code)
- **Parallel queues**: Rejected (breaks chronological guarantee, complicates UI rendering)
- **External merge library**: Rejected (overkill, existing code sufficient)

---

## 6. Qt WebSockets vs. Third-Party Library

### Decision
**Use Qt WebSockets** (already decided in Decision 2, documented here for completeness).

### Rationale
See Decision 2: "Pusher C++ Client Library Evaluation"

---

## 7. Settings Storage for Kick Credentials

### Decision
**Use Qt's built-in encrypted storage** with `QSettings` + OS keychain integration (optional).

### Rationale
- **Qt Native**: No additional dependency
- **Encryption**: Qt can use OS-level encryption (Windows DPAPI, macOS Keychain, Linux Secret Service)
- **Fallback**: If OS keychain unavailable, use obfuscated storage (base64 + XOR, not secure but better than plaintext)
- **Precedent**: Existing Twitch credential storage uses similar approach

### Implementation
**Secure Storage Class**:
```cpp
class SecureSettings {
public:
    static void storeToken(const QString &key, const QString &token);
    static QString retrieveToken(const QString &key);
    static void deleteToken(const QString &key);
private:
    static bool useKeychain();  // Detect OS keychain availability
    static QString obfuscate(const QString &data);  // Fallback obfuscation
};
```

**Storage Keys**:
- `kick/access_token`
- `kick/refresh_token`
- `kick/token_expiry` (Unix timestamp)
- `kick/user_id`

### Security Notes
- **Refresh Tokens**: Store refresh token securely; access tokens are short-lived (1 hour typically)
- **Token Rotation**: Implement automatic refresh using refresh token before expiry
- **Clear on Logout**: Delete tokens when user explicitly logs out

### Alternatives Considered
- **Qt Keychain library**: Rejected (adds dependency, Qt's built-in sufficient for our use case)
- **Plaintext storage**: Rejected (security risk, especially for persistent tokens)
- **Custom encryption**: Rejected (complex, easy to implement incorrectly)

---

## 8. Platform Badge Rendering

### Decision
**Extend existing badge system** with new platform badge type.

### Rationale
- **Existing Infrastructure**: Chatterino7 already renders badges (subscriber, mod, etc.) in messages
- **Minimal Changes**: Add new badge type `PLATFORM_BADGE` with Twitch/Kick icons
- **Visual Consistency**: Matches existing badge style and placement

### Implementation Approach
**Badge Definition**:
- Add platform indicator as small icon badge before username (similar to subscriber badge)
- Icons: "T" in purple circle (Twitch), "K" in green circle (Kick)
- Only show in merged views (not in individual platform channels)

**Code Changes**:
```cpp
// In MessageBuilder or MessageLayoutElement
if (isMergedView) {
    auto platformBadge = makePlatformBadge(message->platformSource);
    builder.addElement(platformBadge);
}
```

**Badge Resources**:
- Add SVG icons to resources: `:/images/badge-twitch.svg`, `:/images/badge-kick.svg`
- Use existing `ImageElement` rendering pipeline

### Alternatives Considered
- **Text prefix** ("[T]", "[K]"): Rejected (not as visually clean, takes more space)
- **Color-coded usernames**: Rejected (conflicts with username color customization)
- **Separate columns**: Rejected (breaks existing message layout, requires UI redesign)

---

## Summary of Decisions

| Research Area | Decision | Risk Level | Fallback Plan |
|---------------|----------|------------|---------------|
| 1. Kick WebSocket Protocol | Pusher protocol (reverse-engineered) | 🟡 Medium | Monitor community, webhook relay (future) |
| 2. Pusher Library | Qt WebSockets + custom impl | 🟢 Low | Well-supported, full control |
| 3. Recent Messages | No history for Kick in MVP | 🟢 Low | Add post-MVP if API available |
| 4. OAuth Flow | System browser + localhost callback | 🟢 Low | Standard pattern, proven approach |
| 5. Message Ordering | Merge on arrival, binary search insert | 🟢 Low | Existing code pattern |
| 6. WebSocket Library | Qt WebSockets (duplicate of #2) | 🟢 Low | N/A |
| 7. Credential Storage | Qt encrypted storage + OS keychain | 🟢 Low | Fallback to obfuscation |
| 8. Platform Badges | Extend existing badge system | 🟢 Low | Minimal changes required |

**Overall Risk Assessment**: 🟢 **LOW** - All major technical unknowns resolved with low-risk solutions. Only medium-risk item is protocol stability, mitigated by monitoring and fallback plan.

---

## Next Phase

✅ **Phase 0 Complete** - All research tasks resolved, decisions documented.

⏭️ **Proceed to Phase 1**: Generate data-model.md, contracts/, and quickstart.md based on these decisions.

