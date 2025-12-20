# Developer Quickstart: Kick Integration & Merged Channels

**Date**: 2025-12-18  
**Audience**: Chatterino7 developers implementing or testing Kick features  
**Prerequisites**: Existing Chatterino7 development environment (see root CONTRIBUTING.md)

---

## Quick Links

- [Feature Spec](./spec.md)
- [Implementation Plan](./plan.md)
- [Research Decisions](./research.md)
- [Data Model](./data-model.md)
- [API Contracts](./contracts/)

---

## Getting Started (5-Minute Setup)

### 1. Build with Kick Support

Kick integration doesn't require new dependencies beyond existing Qt WebSockets (already in Chatterino7).

```bash
cd /Users/samuelbeguiristain/Repos/chatterino7
mkdir build && cd build
cmake ..
make -j8
```

**No new dependencies needed!** Kick uses:
- Qt WebSockets (already present)
- Qt Network (already present)
- Existing JSON parsing (QJsonDocument)

### 2. Enable Kick Integration in UI

1. Run Chatterino7: `./build/bin/chatterino`
2. Go to **Settings** → **General**
3. Enable "**Kick Integration**" toggle (new setting)
4. Restart Chatterino7

Kick/Merged options now appear in "Open Channel" dialog.

---

## Testing Kick Features

### A. Test Kick WebSocket Connection (Read-Only)

**No authentication needed** for viewing Kick channels.

1. Click "**+**" button (Add Channel)
2. Select "**Kick**" platform
3. Enter channel: `xqc` (or any active Kick streamer)
4. Click **Join**

**Expected Result**: Channel opens, messages start streaming in real-time.

**Troubleshooting**:
- If connection fails, check `KickWebSocket::onError()` logs
- Verify Kick app key in code (may change, extract from kick.com web client)
- Check network inspector: Should see WebSocket connection to `wss://ws-us2.pusher.com/app/...`

### B. Test Kick Message Sending (Requires Auth)

1. Click "**Login with Kick**" in settings
2. Browser opens → Log in to Kick → Approve permissions
3. Browser shows "Success" → Return to Chatterino7
4. Join your own Kick channel (or test channel where you have permissions)
5. Type message, press Enter

**Expected Result**: Message appears in Kick chat (verify in web browser or mobile app).

**Troubleshooting**:
- Check OAuth callback: `http://localhost:{port}` server must be running
- Verify access token stored: Check QSettings under `kick/access_token`
- Check REST API logs: POST to `https://api.kick.com/public/v1/chat`
- Rate limit: Max 20 messages per 30 seconds

### C. Test Merged Channel

1. Open Twitch channel: `twitch:xqc`
2. Open Kick channel: `kick:xqc`
3. Right-click one channel tab → "**Merge with...**" → Select the other channel
4. New tab appears: "**T:xqc + K:xqc**"

**Expected Result**: Messages from both Twitch and Kick appear chronologically in merged view.

**Test Message Sending**:
1. In merged view, select platform: **Both** / **Twitch** / **Kick** (toggle buttons)
2. Type message, press Enter
3. Verify message appears on selected platform(s)

---

## Development Workflow

### File Structure

New files for this feature:

```
src/providers/kick/
├── KickChannel.hpp/.cpp       # Kick channel implementation
├── KickWebSocket.hpp/.cpp     # Pusher WebSocket client
├── KickAccount.hpp/.cpp       # OAuth + token management
├── KickApi.hpp/.cpp           # REST API (send messages)
└── KickEmotes.hpp/.cpp        # Kick native emote support

src/channels/
└── MergedChannel.hpp/.cpp     # Multi-platform merge logic

tests/src/
├── KickChannel.cpp            # Unit tests
├── KickWebSocket.cpp          # Protocol tests
├── MergedChannel.cpp          # Merge logic tests
└── UrlParser.cpp              # URL parsing tests
```

### Building Specific Components

**Build only Kick provider**:
```bash
make -j8 # (full rebuild fast with ccache)
```

**Run unit tests**:
```bash
cd build
ctest -R Kick  # Run only Kick-related tests
ctest -V       # Verbose output
```

**Run specific test**:
```bash
./build/bin/chatterino-test --gtest_filter=KickWebSocket.ParseConnectionEstablished
```

---

## Debugging Techniques

### 1. WebSocket Protocol Debugging

**Enable WebSocket logs**:
```cpp
// In KickWebSocket.cpp
void KickWebSocket::onTextMessageReceived(const QString &message) {
    qDebug() << "Kick WS <<" << message;  // Log all incoming messages
    // ... parse message ...
}
```

**Monitor in browser** (for comparison):
1. Open kick.com/xqc in Chrome
2. Open DevTools → Network → WS (WebSocket)
3. Click connection to see messages
4. Compare format with Chatterino7 logs

**Common Issues**:
- `pusher:error` with code 4001 → App key outdated (extract new key from web client)
- No messages received → Check channel ID resolution (`chatrooms.{id}.v2` format)
- Connection drops → Check ping/pong timing (should ping every 60s)

### 2. OAuth Flow Debugging

**Enable OAuth logs**:
```cpp
void KickOAuthFlow::startAuthorization() {
    qDebug() << "Opening auth URL:" << authUrl.toString();
    qDebug() << "Redirect URI:" << this->redirectUri_;
    qDebug() << "Code challenge:" << this->codeChallenge_;
    // ...
}
```

**Test OAuth manually**:
1. Copy authorization URL from logs
2. Paste in browser, complete flow
3. Copy callback URL from browser address bar (`http://localhost:xxx?code=...`)
4. Verify `code` parameter present
5. Check token exchange logs

**Common Issues**:
- Browser doesn't open → Manually copy URL, open in browser
- Callback timeout → Check local server running: `netstat -an | grep LISTEN`
- Token exchange fails → Verify `code_verifier` matches `code_challenge` (PKCE)
- "Unauthorized" → Client ID/secret incorrect (check Kick developer dashboard)

### 3. Merged Channel Debugging

**Enable merge logs**:
```cpp
void MergedChannel::onSourceMessageReceived(MessagePtr msg, std::shared_ptr<Channel> source) {
    qDebug() << "Merged channel received from" 
             << source->getName() 
             << ":" << msg->messageText 
             << "at" << msg->serverReceivedTime;
    // ...
}
```

**Verify chronological order**:
```bash
# In logs, check timestamps are sorted:
Merged channel received from twitch:xqc: "Hello" at 2025-12-18T10:00:00Z
Merged channel received from kick:xqc: "Hi" at 2025-12-18T10:00:01Z
Merged channel received from twitch:xqc: "World" at 2025-12-18T10:00:02Z
```

**Common Issues**:
- Messages out of order → Check `serverReceivedTime` set correctly on both platforms
- Duplicate messages → Ensure signal connections don't double-subscribe
- Missing platform indicator → Check `msg->platformSource` set in source channel

---

## Testing Checklist

### Unit Tests (Run Before PR)

```bash
cd build
ctest -R Kick     # All Kick tests
ctest -R Merged   # Merged channel tests
```

**Key tests**:
- `KickWebSocket.ParseConnectionEstablished` - Protocol parsing
- `KickWebSocket.ParseChatMessage` - Message format
- `KickOAuthFlow.GenerateCodeVerifier` - PKCE generation
- `KickApi.BuildSendMessageRequest` - REST API request format
- `MergedChannel.ChronologicalMerge` - Message ordering
- `MergedChannel.SendMessageToBothPlatforms` - Multi-platform send

### Integration Tests (Manual)

- [ ] Open Kick channel (read-only, no auth)
- [ ] Verify messages stream in real-time
- [ ] Login with Kick (OAuth flow)
- [ ] Send message to Kick channel
- [ ] Merge Twitch + Kick channels
- [ ] Verify chronological message order
- [ ] Send message to "Both" platforms
- [ ] Send message to "Twitch Only"
- [ ] Send message to "Kick Only"
- [ ] Restart app, verify merged channel restored
- [ ] Test on all platforms (Windows, macOS, Linux)

### Performance Tests

```bash
# Load test: Simulate 100 messages/second
./scripts/load-test-merged-channel.sh
```

**Metrics to verify**:
- Message insertion latency: <10ms per message
- Memory usage: <200KB per merged channel
- CPU usage: <5% during idle (only WebSocket keep-alive)

---

## Common Development Tasks

### Task: Add New Kick Event Type

**Example**: Support subscription events (currently only chat messages).

1. **Define event struct** in `KickMessage.hpp`:
```cpp
struct KickSubscriptionEvent {
    QString id;
    int months;
    QString subscriberName;
    // ...
    static KickSubscriptionEvent fromJson(const QJsonObject &json);
};
```

2. **Parse in WebSocket handler** (`KickWebSocket.cpp`):
```cpp
if (event == "App\\Events\\SubscriptionEvent") {
    auto sub = KickSubscriptionEvent::fromJson(data);
    emit subscriptionReceived(sub);
}
```

3. **Handle in channel** (`KickChannel.cpp`):
```cpp
connect(webSocket_, &KickWebSocket::subscriptionReceived,
        this, &KickChannel::onSubscriptionReceived);

void KickChannel::onSubscriptionReceived(const KickSubscriptionEvent &sub) {
    // Build system message: "X subscribed for Y months"
    auto msg = buildSubscriptionMessage(sub);
    this->addMessage(msg);
}
```

4. **Add unit test** (`tests/src/KickWebSocket.cpp`):
```cpp
TEST(KickWebSocket, ParseSubscriptionEvent) {
    QString json = R"({"event":"App\\Events\\SubscriptionEvent",...)";
    auto sub = KickWebSocket::parseSubscriptionEvent(json);
    EXPECT_EQ(sub.months, 3);
}
```

### Task: Add Platform Badge to Messages

**Goal**: Show Twitch/Kick icon badge before username in merged views.

1. **Add badge to message** (`MessageBuilder.cpp`):
```cpp
// In buildMessage() for Kick messages:
if (channel->getProviderId() == ProviderId::Kick && channel->isMergedView()) {
    builder.addElement(makePlatformBadge(ProviderId::Kick));
}
```

2. **Create badge element** (`MessageLayoutElement.hpp`):
```cpp
std::unique_ptr<MessageElement> makePlatformBadge(ProviderId platform) {
    QString iconPath = platform == ProviderId::Twitch 
        ? ":/images/badge-twitch.svg"
        : ":/images/badge-kick.svg";
    return std::make_unique<ImageElement>(iconPath, ImageType::Badge);
}
```

3. **Add badge resources** (`resources.qrc`):
```xml
<file>images/badge-twitch.svg</file>
<file>images/badge-kick.svg</file>
```

4. **Test**: Open merged view, verify badges appear before usernames.

### Task: Implement Kick History API (Future)

**If Kick adds history endpoint** (currently unavailable):

1. **Add method to `KickApi`**:
```cpp
void KickApi::fetchRecentMessages(int channelId, int limit,
                                    std::function<void(QVector<KickMessage>)> callback);
```

2. **Call in `KickChannel::connect()`**:
```cpp
void KickChannel::connect() {
    // After WebSocket connected:
    this->api_->fetchRecentMessages(this->channelId_, 100, [this](auto messages) {
        for (const auto &msg : messages) {
            auto builtMsg = this->buildMessage(msg);
            this->addMessage(builtMsg, MessageContext::History);
        }
    });
}
```

3. **Update FR-031 in spec**: Remove "SHOULD" caveat, mark as "MUST" once API available.

---

## Useful Commands

### Extract Kick App Key (When Outdated)

```bash
# Open Kick.com in browser, inspect WebSocket connection
# Look for URL: wss://ws-us2.pusher.com/app/{APP_KEY}?protocol=7
# Update in KickWebSocket.cpp: const QString APP_KEY = "...";
```

**Alternative**: Use browser console:
```javascript
// In kick.com console:
window.Pusher.instances[0].connection.key
// Copy result to code
```

### Monitor All WebSocket Traffic

```bash
# Use mitmproxy to intercept WebSocket connections:
mitmproxy --mode reverse:https://ws-us2.pusher.com --ssl-insecure
```

### Profile Message Insertion Performance

```cpp
#include <QElapsedTimer>

void MergedChannel::onSourceMessageReceived(MessagePtr msg, ...) {
    QElapsedTimer timer;
    timer.start();
    
    this->addMessage(msg);
    
    qint64 elapsed = timer.nsecsElapsed();
    if (elapsed > 10000000) {  // > 10ms
        qWarning() << "Slow message insert:" << elapsed << "ns";
    }
}
```

---

## Resources

### Documentation

- [Kick API Docs](https://docs.kick.com) - Official Kick API reference
- [Pusher Protocol](https://pusher.com/docs/channels/library_auth_reference/pusher-websockets-protocol/) - WebSocket protocol spec
- [Qt WebSockets](https://doc.qt.io/qt-5/qtwebsockets-index.html) - Qt WebSocket documentation
- [OAuth 2.1 RFC](https://datatracker.ietf.org/doc/html/draft-ietf-oauth-v2-1-08) - OAuth specification

### Community Resources

- [Twick GitHub](https://github.com/twicklabs/twick) - iOS Kick client (reference implementation)
- [r/Kick Subreddit](https://reddit.com/r/kick) - Community discussions, protocol changes
- [Kick Developer Discord](https://discord.gg/kick) - Developer community (unofficial)

### Tools

- **Postman**: Test Kick REST API endpoints
- **WebSocket King**: Test WebSocket connections manually
- **mitmproxy**: Intercept and inspect WebSocket traffic
- **Qt Creator**: IDE with excellent Qt debugging support

---

## Getting Help

### Internal (Chatterino7)

- Check existing Twitch implementation: `src/providers/twitch/` (similar patterns)
- Review existing Channel tests: `tests/src/Channel.cpp`
- Ask in team chat/Discord

### External (Kick API)

- Kick API issues → GitHub: [kickengineering/kickdevdocs](https://github.com/kickengineering/kickdevdocs/issues)
- Protocol questions → Monitor Twick/KickTalk issues (they hit same problems)
- General questions → r/Kick subreddit or Kick developer Discord

---

## Next Steps

1. ✅ Read this quickstart
2. ✅ Build Chatterino7 with Kick support
3. ✅ Test basic Kick channel viewing (no auth)
4. ⏭️ Implement your assigned task (see tasks.md)
5. ⏭️ Write unit tests for your changes
6. ⏭️ Submit PR with tests passing

---

## Troubleshooting FAQ

**Q: Kick channel won't connect**  
A: Check logs for WebSocket error code. 4001 = app key outdated (extract new one from web client).

**Q: OAuth callback times out**  
A: Verify local HTTP server started (check port in logs). Try manually pasting callback URL.

**Q: Messages out of order in merged view**  
A: Verify `serverReceivedTime` set correctly on both platforms. Check clock sync between platforms.

**Q: Rate limited when sending**  
A: Kick limit: 20 msg/30s. Wait for rate limit reset (check `X-RateLimit-Reset` header).

**Q: Merged channel not restored after restart**  
A: Check `WindowManager::decodeChannel()` handles "merged" type. Verify JSON serialization format.

**Q: Unit tests fail on CI but pass locally**  
A: Check timezone issues (`serverReceivedTime` uses UTC). Mock time in tests.

---

**Happy coding! 🚀**

For questions or issues, see [CONTRIBUTING.md](../../CONTRIBUTING.md) or open a GitHub issue.

