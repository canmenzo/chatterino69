# Contract: Merged Channel Interface

**Version**: 1.0  
**Date**: 2025-12-18  
**Status**: Internal API Design  
**Purpose**: Define the public interface and behavior contract for `MergedChannel` class

---

## Overview

`MergedChannel` is a virtual channel that aggregates messages from multiple source channels (cross-platform: Twitch + Kick). It appears as a single channel to the UI but routes messages to multiple underlying platform channels.

**Key Characteristics**:
- Extends existing `Channel` base class
- Non-owning references to source channels (shared ownership)
- Subscribes to source channel message events via signals
- Merges messages chronologically by `serverReceivedTime`
- Routes outgoing messages to selected platform(s)

---

## Class Interface

### Constructor

```cpp
MergedChannel(const QString &name, std::vector<std::shared_ptr<Channel>> sources);
```

**Parameters**:
- `name`: Display name for merged channel (e.g., "xqc (Merged)")
- `sources`: Vector of source channels (min 2, max 10)

**Preconditions**:
- `sources.size() >= 2`
- All source channels non-null
- All source channels have unique platform IDs (can't merge two Twitch channels)

**Postconditions**:
- Merged channel created with subscriptions to all sources
- Default platform selection set to `Both`
- Ready to receive and display messages from sources

**Example**:
```cpp
auto twitchChannel = getApp()->getTwitch()->getOrAddChannel("xqc");
auto kickChannel = getApp()->getKick()->getOrAddChannel("xqc");

auto mergedChannel = std::make_shared<MergedChannel>(
    "xqc (Merged)",
    std::vector<std::shared_ptr<Channel>>{twitchChannel, kickChannel}
);
```

---

## Public Methods

### Channel Interface (Overridden)

#### sendMessage

```cpp
void sendMessage(const QString &message) override;
```

**Purpose**: Send message to platform(s) based on current platform selection.

**Behavior**:
- If `platformSelection_ == Both`: Send to all source channels in parallel
- If `platformSelection_ == TwitchOnly`: Send only to Twitch source
- If `platformSelection_ == KickOnly`: Send only to Kick source
- Collect success/failure status per platform
- Emit message to merged view with platform indicators showing delivery status

**Preconditions**:
- User must be authenticated on target platform(s)
- Message must pass validation (non-empty, within length limits)

**Postconditions**:
- Message sent to selected platform(s) asynchronously
- Message appears in merged view with indicators (✓ or ✗ per platform)
- `messageAppended` signal emitted with merged message

**Error Handling**:
- If send fails on one platform but succeeds on another: Show partial success
- If send fails on all platforms: Show error message in chat
- If user not authenticated: Show authentication prompt

**Example Flow**:
```
User types: "Hello chat!"
Platform selection: Both

→ Send to twitchChannel: "Hello chat!"
→ Send to kickChannel: "Hello chat!"

Wait for both responses (async)

Twitch: Success ✓
Kick: Failed (rate limited) ✗

Display in merged view:
  [T✓ K✗] YourUsername: Hello chat!
```

#### canSendMessage

```cpp
bool canSendMessage() const override;
```

**Purpose**: Check if user can send messages to at least one source platform.

**Returns**: `true` if user is authenticated on at least one platform with send permissions

**Logic**:
```cpp
bool MergedChannel::canSendMessage() const {
    for (const auto &source : this->sourceChannels_) {
        if (source->canSendMessage()) {
            return true;
        }
    }
    return false;
}
```

#### isMod

```cpp
bool isMod() const override;
```

**Purpose**: Check if user has moderator status in any source channel.

**Returns**: `true` if user is mod in ANY source channel

**Logic**:
```cpp
bool MergedChannel::isMod() const {
    for (const auto &source : this->sourceChannels_) {
        if (source->isMod()) {
            return true;
        }
    }
    return false;
}
```

**Rationale**: Showing mod badge if user is mod in at least one platform provides useful context.

---

### Merged-Specific Methods

#### addSourceChannel

```cpp
void addSourceChannel(std::shared_ptr<Channel> channel);
```

**Purpose**: Add a new source channel to the merge (dynamic expansion).

**Preconditions**:
- `channel` is non-null
- `channel->getProviderId()` not already present in sources
- Total sources < 10 (max limit)

**Postconditions**:
- Channel added to `sourceChannels_`
- Subscribed to channel's `messageAppended` signal
- Future messages from this channel appear in merged view

**Use Case**: User adds third platform support later (e.g., YouTube Gaming).

#### removeSourceChannel

```cpp
void removeSourceChannel(std::shared_ptr<Channel> channel);
```

**Purpose**: Remove a source channel from the merge.

**Preconditions**:
- `channel` exists in `sourceChannels_`
- Removing channel still leaves >= 2 sources (or destroy merged channel)

**Postconditions**:
- Channel removed from `sourceChannels_`
- Signal subscription disconnected
- Future messages from this channel no longer appear

**Warning**: If removing reduces sources to <2, merged channel should be destroyed or converted to single channel.

#### getSourceChannels

```cpp
std::vector<std::shared_ptr<Channel>> getSourceChannels() const;
```

**Purpose**: Get list of all source channels.

**Returns**: Copy of source channels vector

**Use Case**: UI displays list of sources, allows user to manage merge composition.

#### setPlatformSelection

```cpp
void setPlatformSelection(PlatformSelection selection);
```

**Purpose**: Set target platform(s) for message sending.

**Parameters**:
- `selection`: `Both`, `TwitchOnly`, or `KickOnly`

**Postconditions**:
- Future `sendMessage()` calls route to selected platform(s)
- UI updates to show current selection (highlighted button)

**Validation**: If selected platform not present in sources (e.g., select KickOnly but no Kick source), fall back to `Both`.

#### getPlatformSelection

```cpp
PlatformSelection getPlatformSelection() const;
```

**Purpose**: Get current platform selection.

**Returns**: Current selection enum value

#### getDisplayName

```cpp
QString getDisplayName() const;
```

**Purpose**: Get formatted display name for tab/header.

**Returns**: String like "T:xqc + K:xqc" or "Twitch:xqc + Kick:xqc"

**Logic**:
```cpp
QString MergedChannel::getDisplayName() const {
    QStringList parts;
    for (const auto &source : this->sourceChannels_) {
        QString prefix = source->getProviderId() == ProviderId::Twitch ? "T" : "K";
        parts.append(QString("%1:%2").arg(prefix, source->getName()));
    }
    return parts.join(" + ");
}
```

**Format Options** (configurable in settings):
- Short: "T:xqc + K:xqc"
- Long: "Twitch:xqc + Kick:xqc"
- Icon: [Twitch icon] xqc + [Kick icon] xqc

---

## Signals

### Inherited from Channel

```cpp
pajlada::Signals::Signal<const QString &, const QString &, bool &> sendMessageSignal;
pajlada::Signals::Signal<MessagePtr &, std::optional<MessageFlags>> messageAppended;
pajlada::Signals::Signal<MessagePtr &, std::optional<MessageFlags>, size_t> messagesAddedAtStart;
```

**Usage**: Merged channel emits same signals as regular channels, so UI code (ChannelView) works identically.

### Merged-Specific Signals (Future Enhancement)

```cpp
// Future: Signal when platform selection changes
pajlada::Signals::Signal<PlatformSelection> platformSelectionChanged;

// Future: Signal when source channel added/removed
pajlada::Signals::Signal<std::shared_ptr<Channel>, bool> sourceChannelChanged;  // bool: added=true, removed=false
```

---

## Behavior Contracts

### Message Aggregation

**Contract**: Messages from all source channels appear in merged view in strict chronological order.

**Implementation**:
```cpp
void MergedChannel::onSourceMessageReceived(MessagePtr msg, 
                                             std::shared_ptr<Channel> source) {
    // Add platform indicator to message
    msg->platformSource = source->getProviderId();
    
    // Use base Channel::addMessage for chronological insertion
    // (existing code handles insertion by serverReceivedTime)
    this->addMessage(msg, MessageContext::Repost);
}
```

**Ordering Guarantee**: If message A has earlier `serverReceivedTime` than message B, A appears before B regardless of source platform.

**Tiebreaker**: If timestamps identical, arrival order (which message signal fired first) determines order.

---

### Message Sending

**Contract**: When `sendMessage()` called, message is dispatched to platform(s) based on `platformSelection_`, results collected, and merged view updated.

**Atomicity**: Sending is **not** atomic across platforms. Each platform send is independent. Partial failure is possible and handled gracefully.

**Flow**:
```
1. User sends "Hello" with selection=Both
2. Dispatch:
   → twitchChannel->sendMessage("Hello")  [async]
   → kickChannel->sendMessage("Hello")     [async]
3. Wait for both results (use QFuture or callback)
4. Collect results:
   - Twitch: success ✓
   - Kick: rate limited ✗
5. Build merged message with indicators:
   Message { 
     content: "Hello",
     platformIndicators: { Twitch: success, Kick: failed },
     ...
   }
6. Emit messageAppended with merged message
7. UI displays: [T✓ K✗] YourUsername: Hello
```

**Error Display**: UI shows per-platform status (success/fail) in message itself or as separate error line.

---

### Source Channel Lifecycle

**Contract**: Source channels are **shared ownership** (std::shared_ptr). Merged channel does not own sources exclusively.

**Implications**:
- If source channel destroyed externally, merged channel must handle gracefully (remove from sources, possibly destroy self if <2 sources remain)
- If source channel disconnects, merged view continues showing messages from other sources
- Connection state indicators should show per-source (e.g., "Twitch: Connected, Kick: Disconnected")

**Signal Management**: Use `pajlada::Signals::ScopedConnectionContainer` to auto-disconnect when merged channel destroyed.

---

### Display Properties

**Contract**: Merged channel appears as a regular channel to `ChannelView` and other UI components.

**Properties**:
- `getName()`: Returns merged display name
- `getType()`: Returns custom "merged" type (for layout serialization)
- `isTwitchChannel()`: Returns `false` (not a Twitch-only channel)
- `getMessages()`: Returns merged message queue (chronologically sorted)

---

## Serialization Contract

**Purpose**: Save/restore merged channel in window layout.

**ChannelDescriptor Format**:
```cpp
ChannelDescriptor {
    type_ = "merged",
    data_ = JSON string:
    {
      "sources": [
        {"type": "twitch", "data": "xqc"},
        {"type": "kick", "data": "xqc"}
      ],
      "platformSelection": "Both"
    }
}
```

**Deserialization** (in `WindowManager::decodeChannel()`):
```cpp
if (descriptor.type_ == "merged") {
    QJsonDocument doc = QJsonDocument::fromJson(descriptor.data_.toUtf8());
    QJsonArray sources = doc["sources"].toArray();
    
    std::vector<std::shared_ptr<Channel>> sourceChannels;
    for (const auto &srcVal : sources) {
        QJsonObject src = srcVal.toObject();
        ChannelDescriptor srcDesc;
        srcDesc.type_ = src["type"].toString();
        srcDesc.data_ = src["data"].toString();
        
        auto channel = this->decodeChannel(srcDesc);  // Recursive
        if (channel) {
            sourceChannels.push_back(channel);
        }
    }
    
    if (sourceChannels.size() >= 2) {
        auto merged = std::make_shared<MergedChannel>("Merged", sourceChannels);
        QString selection = doc["platformSelection"].toString();
        merged->setPlatformSelection(parsePlatformSelection(selection));
        return merged;
    }
}
```

**Contract Guarantees**:
- Merged channel restored with same sources and platform selection
- If source channel no longer exists (e.g., removed from config), skip that source
- If <2 sources remain after filtering, don't create merged channel (fallback to first available source)

---

## Thread Safety

**Contract**: `MergedChannel` is **not thread-safe**. All methods must be called from Qt main thread (UI thread).

**Rationale**: Inherits from `QObject` and uses Qt signals/slots, which require affinity to a single thread.

**Async Operations**: Message sending via REST API is async but callbacks execute on main thread (via `QNetworkAccessManager` signal/slot mechanism).

---

## Performance Contracts

### Memory

**Message Storage**: Uses existing `LimitedQueue` from base `Channel` class.
- Default: ~1000 messages stored
- Old messages evicted automatically (LRU-style)
- Memory footprint: ~1000 messages × ~200 bytes/message ≈ 200KB per merged channel

### CPU

**Message Insertion**: O(n) worst case, O(1) best case (messages arrive in order)
- Binary search for insertion point: O(log n)
- Insert operation: O(n) (shift elements)
- **Optimization**: Most messages arrive in chronological order → O(1) append

**Benchmark Target**: <1ms latency for message insertion at 100 messages/second load.

---

## Testing Contracts

### Unit Tests

```cpp
TEST(MergedChannel, ConstructWithTwoSources) {
    auto twitch = makeMockChannel(ProviderId::Twitch, "xqc");
    auto kick = makeMockChannel(ProviderId::Kick, "xqc");
    
    MergedChannel merged("test", {twitch, kick});
    
    EXPECT_EQ(merged.getSourceChannels().size(), 2);
    EXPECT_EQ(merged.getPlatformSelection(), PlatformSelection::Both);
}

TEST(MergedChannel, SendMessageToBothPlatforms) {
    auto twitch = makeMockChannel(ProviderId::Twitch, "xqc");
    auto kick = makeMockChannel(ProviderId::Kick, "xqc");
    
    MergedChannel merged("test", {twitch, kick});
    merged.setPlatformSelection(PlatformSelection::Both);
    
    bool twitchCalled = false, kickCalled = false;
    twitch->onSendMessage = [&](QString msg) { twitchCalled = true; };
    kick->onSendMessage = [&](QString msg) { kickCalled = true; };
    
    merged.sendMessage("Hello");
    
    EXPECT_TRUE(twitchCalled);
    EXPECT_TRUE(kickCalled);
}

TEST(MergedChannel, ChronologicalMerge) {
    auto twitch = makeMockChannel(ProviderId::Twitch, "xqc");
    auto kick = makeMockChannel(ProviderId::Kick, "xqc");
    
    MergedChannel merged("test", {twitch, kick});
    
    // Send message to twitch at T=100
    auto msg1 = makeMessage("A", QDateTime::fromSecsSinceEpoch(100));
    twitch->emitMessage(msg1);
    
    // Send message to kick at T=50 (earlier)
    auto msg2 = makeMessage("B", QDateTime::fromSecsSinceEpoch(50));
    kick->emitMessage(msg2);
    
    // Merged channel should have B before A (chronological)
    auto messages = merged.getMessages();
    ASSERT_EQ(messages.size(), 2);
    EXPECT_EQ(messages[0]->messageText, "B");  // Earlier timestamp
    EXPECT_EQ(messages[1]->messageText, "A");
}
```

### Integration Tests

- Create real Twitch + Kick channels
- Merge them
- Send messages to both platforms externally
- Verify messages appear in merged view chronologically
- Send message from merged view, verify appears on both platforms
- Disconnect one source, verify merged view continues working

---

## Error Handling Contracts

### Source Channel Removal

**Scenario**: Source channel destroyed while merged channel active.

**Contract**: Merged channel detects source destruction, removes from `sourceChannels_`, continues functioning with remaining sources. If <2 sources remain, merged channel self-destructs (emits signal for UI to close).

**Implementation**: Use `QPointer` or weak_ptr semantics for source tracking (or handle in signal disconnection callback).

### Send Failure

**Scenario**: `sendMessage()` fails on one or more platforms.

**Contract**: Display partial success/failure status in UI. Message appears in merged view with platform indicators showing delivery status.

**User Experience**:
```
[T✓ K✗] YourUsername: Hello chat!
    ↑ ↑
    │ └─ Kick failed (rate limited)
    └─── Twitch success
```

---

## Extensibility Contracts

### Adding New Platforms (Future)

**Contract**: `MergedChannel` is platform-agnostic. To add new platform:
1. Create new `ProviderId` enum value
2. Implement platform-specific `Channel` subclass
3. No changes needed to `MergedChannel` logic (works with base `Channel` interface)

**Example**: Add YouTube Gaming support:
```cpp
enum class ProviderId { Twitch, Kick, YouTube };  // Add YouTube

auto youtubeChannel = getApp()->getYouTube()->getOrAddChannel("xQc");
auto merged = std::make_shared<MergedChannel>("xQc (3-way)", {twitch, kick, youtubeChannel});
```

### Custom Merge Strategies (Future)

**Contract**: `mergeStrategy_` enum allows future custom ordering:
- `Chronological` (current): Sort by timestamp
- `Interleaved`: Alternate between platforms
- `PriorityBased`: Prefer one platform over others

**Implementation Hook**: Virtual method `virtual MessagePtr mergeMessage(MessagePtr msg, Channel *source)` for custom pre-processing.

---

## References

- Base `Channel` class: `src/common/Channel.hpp`
- `ChannelView` (consumer): `src/widgets/helper/ChannelView.cpp`
- Message model: `src/messages/Message.hpp`
- Serialization: `src/singletons/WindowManager.cpp`

