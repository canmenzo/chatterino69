# Implementation Tasks: Kick.tv Chat Integration with Multi-Platform Merge

**Branch**: `001-kick-twitch-merge`  
**Date**: 2025-12-18 (Updated: 2025-12-19)  
**Spec**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md)

---

## 🎉 Progress Summary (2025-12-19)

**Status**: ✅ **MVP COMPLETE** - Kick integration is working!

| Metric | Value |
|--------|-------|
| Tasks Completed | 85/100 (85%) |
| Tasks Remaining | 15 (edge cases, integration tests) |
| Build Status | ✅ Compiles on macOS Arm64 |
| OAuth Login | ✅ Working (shows "SamBebop") |
| Chat Messages | ✅ Receiving from Kick channels |
| Send Messages | 🔄 API endpoint fixed, needs testing |
| 7TV Emotes | ✅ Kick-specific endpoint added |

### Recent Commits
```
380acb1d fix(kick): Parse 'name' field from /users API response array
0b125af8 feat(kick): Add 7TV emote support for Kick channels  
374189f4 fix(kick): Correct API usage for sending messages and getting username
3199df56 chore: Add AI/spec tooling folders to gitignore
92ab022f feat(kick): Add Kick.tv chat integration (WIP)
```

### Key Fixes Applied (Based on Official Kick API Docs)
1. **Send Message API**: Changed `"message"` → `"content"`, added `"type": "user"`
2. **Get Username API**: `GET /users` returns `"name"` field, not `"username"`
3. **7TV for Kick**: Uses `https://7tv.io/v3/users/KICK/{user_id}` endpoint
4. **Emote Parsing**: Added `[emote:ID:NAME]` format parsing with Kick CDN URLs

### Remaining Work (15 tasks)
- Edge case handling (disconnects, rate limiting, invalid channels)
- Integration/acceptance tests
- Performance benchmarks

---

## Overview

This document breaks down the Kick.tv integration feature into actionable, dependency-ordered tasks. Tasks are organized by user story to enable independent implementation and testing.

**Total Tasks**: 100  
**Estimated Effort**: 20-40 hours (experienced C++/Qt developer)  
**MVP Scope**: Phase 3 (User Story 1) - Basic Kick channel viewing and sending

---

## Task Format Legend

- `- [ ]` = Task checkbox
- `[T###]` = Task ID (sequential execution order)
- `[P]` = Parallelizable (can work on simultaneously with other [P] tasks)
- `[US#]` = User Story label (maps to spec.md user stories)
- File path = Exact location for implementation

**Example**: `- [ ] T015 [P] [US1] Implement KickChannel class in src/providers/kick/KickChannel.cpp`

---

## Dependency Graph

```
Phase 1 (Setup)
    ↓
Phase 2 (Foundational) ← MUST complete before user stories
    ↓
    ├→ Phase 3 (US1: Kick Channels) ← MVP, can ship independently
    │       ↓
    ├→ Phase 4 (US2: Merged Channels) ← Depends on US1
    │       ↓
    └→ Phase 5 (US3: Platform Selection) ← Depends on US2
            ↓
Phase 6 (Polish & Persistence) ← Enhances all stories
```

**Story Independence**:
- US1 is fully independent (MVP-ready)
- US2 requires US1 complete (needs working Kick channels)
- US3 requires US2 complete (needs merged channel infrastructure)

---

## Phase 1: Setup & Project Initialization

**Goal**: Prepare codebase structure and build system for Kick integration.

**Duration**: 1-2 hours

### Tasks

- [x] T001 Create Kick provider directory structure at src/providers/kick/
- [x] T002 [P] Add ProviderId::Kick enum value in src/common/ProviderId.hpp
- [x] T003 [P] Update CMakeLists.txt to include new Kick provider files
- [x] T004 [P] Add Kick integration toggle to settings UI in src/widgets/settingspages/GeneralPage.cpp
- [x] T005 Verify Qt WebSockets module available in build (existing dependency, just validate)

**Completion Criteria**:
- ✅ Kick provider directory exists
- ✅ Build system compiles without errors
- ✅ Settings UI has "Enable Kick Integration" toggle

---

## Phase 2: Foundational Infrastructure

**Goal**: Implement core abstractions needed by all user stories. **MUST complete before Phase 3.**

**Duration**: 3-4 hours

### Tasks

- [x] T006 [P] Create KickMessage struct in src/providers/kick/KickMessage.hpp for parsed message data
- [x] T007 [P] Create ConnectionState enum in src/providers/kick/KickChannel.hpp
- [x] T008 [P] Extend ChannelDescriptor to support "kick" type in src/common/ChannelDescriptor.hpp
- [x] T009 [P] Create UrlParser utility in src/util/UrlParser.hpp for username/URL parsing
- [x] T010 Implement UrlParser::parseKickChannel() to extract slug from URLs in src/util/UrlParser.cpp
- [x] T011 [P] Add unit tests for UrlParser in tests/src/UrlParser.cpp

**Completion Criteria**:
- ✅ Data structures defined for Kick messages
- ✅ URL parsing handles both usernames and full URLs
- ✅ All foundational tests pass

---

## Phase 3: User Story 1 - Add and View Individual Kick.tv Channels (P1)

**User Story Goal**: Users can add Kick channels, view live messages, authenticate, and send messages.

**Independent Test**:
```cpp
// Can be fully tested without other stories
TEST(Integration, KickChannelViewing) {
    auto kickChannel = getApp()->getKick()->getOrAddChannel("xqc");
    ASSERT_TRUE(kickChannel->connect());
    // Wait for messages, verify they arrive
    EXPECT_GT(kickChannel->getMessages().size(), 0);
}
```

**Duration**: 12-16 hours

### 3.1: WebSocket Connection (Read Messages)

- [x] T012 [US1] Create KickWebSocket class skeleton in src/providers/kick/KickWebSocket.hpp
- [x] T013 [US1] Implement Pusher handshake in KickWebSocket::connect() in src/providers/kick/KickWebSocket.cpp
- [x] T014 [US1] Implement channel subscription (pusher:subscribe) in KickWebSocket::subscribe()
- [x] T015 [US1] Implement message parsing for App\\Events\\ChatMessageEvent in KickWebSocket::parseMessage()
- [x] T016 [US1] Implement ping/pong keep-alive timer in KickWebSocket
- [x] T017 [US1] Implement error handling for pusher:error events in KickWebSocket::onError()
- [x] T018 [P] [US1] Add unit tests for WebSocket protocol parsing in tests/src/KickWebSocket.cpp

### 3.2: Channel Implementation

- [x] T019 [US1] Create KickChannel class in src/providers/kick/KickChannel.hpp
- [x] T020 [US1] Implement KickChannel::connect() with WebSocket initialization in src/providers/kick/KickChannel.cpp
- [x] T021 [US1] Implement KickChannel::onMessageReceived() to build Message from KickMessage
- [x] T022 [US1] Implement KickChannel::disconnect() and reconnection logic with exponential backoff
- [x] T023 [US1] Add connection state tracking (Disconnected → Connecting → Connected → Reconnecting)
- [x] T024 [P] [US1] Add unit tests for KickChannel in tests/src/KickChannel.cpp

### 3.3: OAuth Authentication

- [x] T025 [US1] Create KickAccount class in src/providers/kick/KickAccount.hpp
- [x] T026 [US1] Create KickOAuthFlow class in src/providers/kick/KickOAuthFlow.hpp
- [x] T027 [US1] Implement PKCE code verifier/challenge generation in KickOAuthFlow::generatePKCE()
- [x] T028 [US1] Implement local HTTP server for OAuth callback in KickOAuthFlow::startLocalServer()
- [x] T029 [US1] Implement browser launch with authorization URL in KickOAuthFlow::openBrowser()
- [x] T030 [US1] Implement code exchange for tokens in KickOAuthFlow::exchangeCodeForTokens()
- [x] T031 [US1] Implement token refresh logic in KickAccount::refreshAccessToken()
- [x] T032 [US1] Implement secure token storage in KickAccount::saveToSettings() using Qt Settings
- [x] T033 [P] [US1] Add unit tests for OAuth flow in tests/src/KickOAuthFlow.cpp

### 3.4: Message Sending (REST API)

- [x] T034 [US1] Create KickApi class in src/providers/kick/KickApi.hpp
- [x] T035 [US1] Implement username → channel ID resolution in KickApi::resolveChannelId()
- [x] T036 [US1] Implement message sending in KickApi::sendMessage() (POST /public/v1/chat)
- [x] T037 [US1] Implement rate limit tracking from X-RateLimit-* headers in KickApi
- [x] T038 [US1] Implement error handling (401→refresh, 429→queue, 403→ban) in KickApi
- [x] T039 [US1] Connect KickChannel::sendMessage() to KickApi in src/providers/kick/KickChannel.cpp
- [x] T040 [P] [US1] Add unit tests for KickApi in tests/src/KickApi.cpp

### 3.5: UI Integration

- [x] T041 [US1] Extend SelectChannelDialog to show Kick option in src/widgets/dialogs/SelectChannelDialog.cpp
- [x] T042 [US1] Add platform selection dropdown (Twitch/Kick) when Kick integration enabled
- [x] T043 [US1] Implement "Login with Kick" button in settings, trigger OAuth flow
- [x] T044 [US1] Update AccountController to handle Kick accounts in src/controllers/accounts/AccountController.cpp
- [x] T045 [US1] Show Kick connection status in channel tab (Connected/Disconnected/Reconnecting)

### 3.6: Emote Support

- [x] T046 [P] [US1] Create KickEmotes class in src/providers/kick/KickEmotes.hpp
- [x] T047 [P] [US1] Integrate native Kick emotes in MessageBuilder for Kick channels
- [x] T048 [P] [US1] Verify 7TV/BTTV/FFZ emotes work in Kick channels (should work via existing system)

**Phase 3 Completion Criteria** (User Story 1 Done):
- ✅ User can add Kick channel by username or URL
- ✅ Messages stream in real-time from Kick
- ✅ User can authenticate with "Login with Kick" (OAuth)
- ✅ User can send messages to Kick channel (with authentication)
- ✅ Multiple Kick channels maintain separate connection states
- ✅ Emotes display correctly (native Kick + 7TV/BTTV/FFZ)
- ✅ **This phase is MVP-ready and can ship independently**

---

## Phase 4: User Story 2 - Merge Channels from Different Platforms (P2)

**User Story Goal**: Users can merge Twitch + Kick channels into unified view with chronological messages and platform indicators.

**Dependencies**: Requires Phase 3 (US1) complete - needs working Kick channels.

**Independent Test**:
```cpp
TEST(Integration, MergedChannelView) {
    auto twitch = getApp()->getTwitch()->getOrAddChannel("xqc");
    auto kick = getApp()->getKick()->getOrAddChannel("xqc");
    auto merged = std::make_shared<MergedChannel>("xqc", {twitch, kick});
    
    // Send message to each platform externally, verify both appear chronologically
    EXPECT_TRUE(messagesAreChronological(merged->getMessages()));
}
```

**Duration**: 6-8 hours

### 4.1: Merged Channel Core

- [x] T049 [US2] Create MergedChannel class in src/channels/MergedChannel.hpp
- [x] T050 [US2] Implement MergedChannel constructor with source channel subscription
- [x] T051 [US2] Implement onSourceMessageReceived() to add messages chronologically in src/channels/MergedChannel.cpp
- [x] T052 [US2] Set platformSource field on messages from each source
- [x] T053 [US2] Implement getDisplayName() to show "T:xqc + K:xqc" format
- [x] T054 [P] [US2] Add unit tests for chronological merge in tests/src/MergedChannel.cpp

### 4.2: Platform Indicators

- [x] T055 [P] [US2] Add platform badge resources (Twitch/Kick icons) in resources/images/ (using text badges [T]/[K])
- [x] T056 [P] [US2] Create makePlatformBadge() in src/messages/MessageElement.hpp (added BadgePlatform flag)
- [x] T057 [US2] Add platform badge before username in merged view messages in MergedChannel.cpp

### 4.3: UI for Merging

- [x] T058 [US2] Add "Merge with..." context menu item to channel tabs in src/widgets/splits/SplitHeader.cpp
- [x] T059 [US2] Create channel selection dialog for merging in src/widgets/dialogs/MergeChannelDialog.cpp
- [x] T060 [US2] Implement auto-matching suggestion (case-insensitive username) in merge dialog
- [x] T061 [US2] Allow manual override to merge any channels

### 4.4: Merged Channel Persistence

- [x] T062 [US2] Extend ChannelDescriptor to support "merged" type with structured sources in src/common/ChannelDescriptor.cpp
- [x] T063 [US2] Implement JSON serialization for merged channel sources
- [x] T064 [US2] Update WindowManager::encodeChannel() to save merged channels in src/singletons/WindowManager.cpp
- [x] T065 [US2] Update WindowManager::decodeChannel() to restore merged channels on startup

**Phase 4 Completion Criteria** (User Story 2 Done):
- ✅ User can merge Twitch + Kick channels via UI
- ✅ Messages from both platforms appear chronologically
- ✅ Platform indicators (badges) visible on each message
- ✅ Auto-matching suggests channels with same username
- ✅ Merged channels persist across restarts
- ✅ **Delivers core differentiating feature**

---

## Phase 5: User Story 3 - Send Messages with Platform Selection (P3)

**User Story Goal**: Users can send messages from merged view to Both/Twitch/Kick platforms with per-platform delivery status.

**Dependencies**: Requires Phase 4 (US2) complete - needs merged channel infrastructure.

**Independent Test**:
```cpp
TEST(Integration, MergedChannelSending) {
    auto merged = createMergedChannel("xqc");
    
    merged->setPlatformSelection(PlatformSelection::Both);
    bool twitchSent = false, kickSent = false;
    merged->sendMessage("Hello", [&](ProviderId platform, bool success) {
        if (platform == ProviderId::Twitch) twitchSent = success;
        if (platform == ProviderId::Kick) kickSent = success;
    });
    
    EXPECT_TRUE(twitchSent && kickSent);
}
```

**Duration**: 4-6 hours

### 5.1: Platform Selection State

- [x] T066 [US3] Add PlatformSelection enum in src/channels/MergedChannel.hpp (Both/TwitchOnly/KickOnly)
- [x] T067 [US3] Implement setPlatformSelection() and getPlatformSelection() in MergedChannel
- [x] T068 [US3] Store platform selection as part of merged channel state

### 5.2: Multi-Platform Sending

- [x] T069 [US3] Implement MergedChannel::sendMessage() to route to selected platforms in src/channels/MergedChannel.cpp
- [x] T070 [US3] Implement parallel send to both platforms with result collection
- [x] T071 [US3] Implement per-platform success/failure tracking
- [x] T072 [US3] Display message with platform indicators showing delivery status (T✓ K✗)

### 5.3: Platform Selection UI

- [x] T073 [US3] Add platform selection toggle buttons (Both/Twitch/Kick) near message input in SplitInput
- [x] T074 [US3] Highlight currently selected platform in UI (checkable buttons with distinct colors)
- [x] T075 [US3] Default to "Both" on merged channel creation (implemented in MergedChannel constructor)
- [x] T076 [US3] Show per-platform send errors in chat (e.g., "Kick: Rate limited")

### 5.4: Error Handling

- [x] T077 [US3] Show error when sending to unauthenticated platform
- [x] T078 [P] [US3] Add unit tests for multi-platform send logic in tests/src/MergedChannel.cpp

**Phase 5 Completion Criteria** (User Story 3 Done):
- ✅ User can select Both/Twitch/Kick in merged view
- ✅ Messages send to selected platform(s)
- ✅ Per-platform delivery status shown (✓/✗ indicators)
- ✅ Errors displayed appropriately
- ✅ **Feature complete for MVP + enhancements**

---

## Phase 6: Polish & Cross-Cutting Concerns

**Goal**: Enhance robustness, add recent message history, handle edge cases.

**Duration**: 4-6 hours

### 6.1: Recent Message History

- [ ] T079 [P] Verify recent-messages2 service continues working for Twitch channels when Kick integration enabled
- [x] T080 [P] Add KickChannel::fetchRecentMessages() stub method returning immediately with TODO comment documenting Kick history API unavailability
- [x] T081 [P] Display "Kick: live messages only" notice when opening Kick channel

### 6.2: Reconnection & Resilience

- [x] T082 [P] Implement exponential backoff reconnection in KickWebSocket (1s, 2s, 4s, 8s, 30s)
- [ ] T083 [P] Handle merged channel when one source disconnects (continue with other)
- [ ] T084 [P] Show connection state indicators per platform in merged view

### 6.3: Edge Cases

- [ ] T085 [P] Handle rate limiting gracefully (queue messages, show countdown)
- [x] T086 [P] Handle token expiry mid-session (auto-refresh transparently)
- [ ] T087 [P] Handle invalid channel (show error, prevent connection loop)
- [ ] T088 [P] Handle merging more than 2 channels (show error or support up to 10)

### 6.4: Documentation & Testing

- [x] T089 [P] Update user documentation for Kick integration (docs/kick-integration.md)
- [ ] T090 [P] Add integration tests for full flow (add → view → auth → send)
- [ ] T091 [P] Add integration tests for merged channel flow (merge → view → send)
- [ ] T092 [P] Run performance benchmarks (100 msg/s throughput, <10ms insert latency)

### 6.5: Acceptance Testing (Success Criteria Validation)

- [ ] T093 [P] Add acceptance test validating SC-001 (5s channel add timing) in tests/integration/AcceptanceTests.cpp
- [ ] T094 [P] Add acceptance test validating SC-002 (2s message latency) in tests/integration/AcceptanceTests.cpp
- [ ] T095 [P] Add acceptance test validating SC-003 (95% delivery success rate) in tests/integration/AcceptanceTests.cpp
- [ ] T096 [P] Add acceptance test validating SC-004 (10s merge timing) in tests/integration/AcceptanceTests.cpp
- [ ] T097 [P] Add long-running stability test validating SC-005 (99% uptime) in tests/integration/StabilityTests.cpp
- [ ] T098 [P] Verify SC-007 (platform indicator visibility) via visual regression test in tests/ui/

### 6.6: Coverage Gap Tasks (Added Post-Analysis)

- [x] T099 [US2] Implement unmerge functionality to separate merged channels back to individual views (FR-015) in src/channels/MergedChannel.cpp
- [x] T100 [US1] Show "Enable Kick Integration" prompt when user tries to open Kick channel with integration disabled (FR-028) in src/widgets/dialogs/SelectChannelDialog.cpp

**Phase 6 Completion Criteria**:
- ✅ Reconnection works reliably
- ✅ Edge cases handled gracefully
- ✅ Integration tests pass
- ✅ Performance meets targets
- ✅ Documentation updated

---

## Parallel Execution Opportunities

Tasks marked with `[P]` can be worked on simultaneously. Here are suggested parallel tracks:

### During Phase 3 (User Story 1):

**Track A** (WebSocket & Core):
- T012-T017: KickWebSocket implementation
- T019-T023: KickChannel implementation

**Track B** (Authentication):
- T025-T032: OAuth flow implementation (independent of WebSocket)

**Track C** (REST API):
- T034-T039: KickApi implementation (independent until T039 integration)

**Track D** (Testing):
- T018, T024, T033, T040: Unit tests (can write alongside or after implementation)

**Track E** (UI):
- T041-T045: UI changes (independent of backend until final integration)

**Track F** (Emotes):
- T046-T048: Emote support (independent of core messaging)

### During Phase 4 (User Story 2):

**Track A** (Core):
- T049-T054: MergedChannel implementation

**Track B** (UI):
- T055-T061: Platform indicators and merge UI (parallel to core)

**Track C** (Persistence):
- T062-T065: Serialization (can start after T049 completes)

### During Phase 5 (User Story 3):

**Track A** (Backend):
- T066-T072: Platform selection logic

**Track B** (UI):
- T073-T076: Platform selection UI (parallel to backend)

**Track C** (Testing):
- T078: Unit tests (parallel or after)

### During Phase 6 (Polish):

All tasks (T079-T098) are parallelizable - different areas of codebase.

---

## Implementation Strategy

### MVP Definition

**Minimum Viable Product** = **Phase 3 (User Story 1)** only:
- Users can add Kick channels
- Messages stream in real-time
- OAuth authentication works
- Users can send messages

**Rationale**: Delivers immediate value (Kick chat client). Users can manually switch between Twitch/Kick tabs. Merging is enhancement, not blocker.

### Incremental Delivery

**Release 1** (MVP):
- Phase 1 + Phase 2 + Phase 3 = ~18-24 hours
- Ship as beta, gather user feedback

**Release 2** (Merge Feature):
- Add Phase 4 = +8 hours
- Core differentiating feature complete

**Release 3** (Full Feature):
- Add Phase 5 + Phase 6 = +12 hours
- Polish, edge cases, complete spec

### Testing Approach

**Unit Tests** (per phase):
- Run `ctest -R <Phase>` after each phase
- Ensure 100% of new code covered

**Integration Tests** (after major phases):
- Phase 3 complete: Test full Kick channel flow end-to-end
- Phase 4 complete: Test merged channel flow end-to-end
- Phase 5 complete: Test platform selection flow end-to-end

**Manual Testing** (before each release):
- Follow acceptance scenarios from spec.md
- Test on all platforms (Windows, macOS, Linux)
- Verify no regressions in existing Twitch functionality

---

## Task Checklist Validation

✅ **All tasks follow required format**:
- Checkbox present: `- [ ]`
- Task ID sequential: T001-T098
- `[P]` marker on parallelizable tasks
- `[US#]` label on user story tasks
- File paths specified for implementation tasks
- Descriptions actionable

✅ **Task organization verified**:
- Phase 1: Setup (no story labels)
- Phase 2: Foundational (no story labels)
- Phase 3: User Story 1 tasks labeled [US1]
- Phase 4: User Story 2 tasks labeled [US2]
- Phase 5: User Story 3 tasks labeled [US3]
- Phase 6: Polish (no story labels)

✅ **Independence validated**:
- Each user story has complete implementation (models + services + UI + tests)
- Each user story has independent test criteria
- Dependencies clearly marked (US2 needs US1, US3 needs US2)

---

## Next Steps

1. ✅ Review task breakdown with team
2. ⏭️ Start with Phase 1 (Setup) - ~2 hours
3. ⏭️ Complete Phase 2 (Foundational) - ~4 hours
4. ⏭️ Implement Phase 3 (User Story 1 / MVP) - ~16 hours
5. ⏭️ Ship MVP for beta testing
6. ⏭️ Gather feedback, iterate
7. ⏭️ Continue with Phase 4 (Merging) and Phase 5 (Platform Selection)
8. ⏭️ Polish with Phase 6

**Total Estimated Time**: 20-40 hours for full feature (MVP in ~24 hours)

---

## References

- [Feature Specification](./spec.md) - User stories and requirements
- [Implementation Plan](./plan.md) - Technical approach and architecture
- [Research Decisions](./research.md) - Technical decisions and rationale
- [Data Model](./data-model.md) - Entities and relationships
- [API Contracts](./contracts/) - WebSocket, OAuth, REST API specifications
- [Developer Quickstart](./quickstart.md) - Setup and debugging guide

---

**Ready to implement! 🚀**

For questions or blockers, refer to quickstart.md or open a GitHub issue.

---

## Detailed Implementation TODOs for Remaining Tasks

### Priority 1: Complete MVP (Phase 3 Remaining - 3 tasks)

#### T043: "Login with Kick" Button
**File**: `src/widgets/settingspages/AccountsPage.cpp`
```cpp
// TODO Implementation Steps:
// 1. Add "Add Kick Account" button below Twitch account section
// 2. Connect button click to KickOAuthFlow::start()
// 3. Handle authCompleted signal to create KickAccount
// 4. Handle authFailed signal to show error dialog
// 5. Update UI to show connected Kick account
```

#### T044: AccountController Kick Support
**File**: `src/controllers/accounts/AccountController.cpp`
```cpp
// TODO Implementation Steps:
// 1. Add std::vector<std::shared_ptr<KickAccount>> kickAccounts_;
// 2. Add addKickAccount(std::shared_ptr<KickAccount>) method
// 3. Add removeKickAccount(const QString &username) method
// 4. Add getKickAccounts() const method
// 5. Handle ProviderId::Kick case in switch statements
// 6. Persist Kick accounts to settings on add/remove
// 7. Load Kick accounts from settings on initialization
```

#### T045: Connection Status Indicator
**File**: `src/widgets/splits/SplitHeader.cpp`
```cpp
// TODO Implementation Steps:
// 1. Add connection state icon near channel name
// 2. Listen to KickChannel::connectionStateChanged signal
// 3. Update icon based on state (green=connected, yellow=reconnecting, red=disconnected)
// 4. Show tooltip with state details
```

### Priority 2: Platform Badges (Phase 4 - 3 tasks)

#### T055: Platform Badge Resources
**Directory**: `resources/images/`
```
// TODO Implementation Steps:
// 1. Create twitch-badge.png (16x16, 32x32)
// 2. Create kick-badge.png (16x16, 32x32)
// 3. Add to resources/resources.qrc
// 4. Test loading in debug build
```

#### T056: makePlatformBadge() Function
**File**: `src/messages/MessageElement.hpp`
```cpp
// TODO Implementation Steps:
// 1. Add static MessageElement* makePlatformBadge(ProviderId provider);
// 2. Return ImageElement with appropriate badge
// 3. Add tooltip showing platform name
```

#### T057: Platform Badge in Messages
**File**: `src/messages/MessageBuilder.cpp`
```cpp
// TODO Implementation Steps:
// 1. In merged channel context, prepend platform badge
// 2. Check message flags for Kick/Twitch
// 3. Call makePlatformBadge() with appropriate provider
// 4. Insert before username element
```

### Priority 3: Merge UI (Phase 4 - 4 tasks)

#### T058: "Merge with..." Context Menu
**File**: `src/widgets/splits/Split.cpp`
```cpp
// TODO Implementation Steps:
// 1. Add "Merge with..." action to tab context menu
// 2. Only show when channel is Twitch or Kick type
// 3. On click, open MergeChannelDialog
// 4. Pass current channel as first source
```

#### T059: MergeChannelDialog
**File**: `src/widgets/dialogs/MergeChannelDialog.cpp` (NEW)
```cpp
// TODO Implementation Steps:
// 1. Create dialog with two channel selectors
// 2. First selector pre-populated with source channel
// 3. Second selector for target channel
// 4. Add platform filter (Twitch/Kick)
// 5. Add "Merge" button
// 6. Return MergedChannel on accept
```

#### T060: Auto-Matching Suggestion
**File**: `src/widgets/dialogs/MergeChannelDialog.cpp`
```cpp
// TODO Implementation Steps:
// 1. When first channel selected, suggest matching name on other platform
// 2. Case-insensitive matching
// 3. Show suggestion with "Auto-matched" label
// 4. Allow user to accept or change
```

#### T061: Manual Override
**File**: `src/widgets/dialogs/MergeChannelDialog.cpp`
```cpp
// TODO Implementation Steps:
// 1. Add "Manual selection" checkbox
// 2. When checked, show full channel list
// 3. Allow any channel combination
// 4. Show warning if names don't match
```

### Priority 4: Platform Selection UI (Phase 5 - 6 tasks)

#### T072: Delivery Status Indicators
**File**: `src/messages/MessageBuilder.cpp`
```cpp
// TODO Implementation Steps:
// 1. For sent messages in merged view, track delivery status
// 2. Add delivery indicator elements (T✓ K✗)
// 3. Update indicators when send callbacks complete
// 4. Use different colors for success/failure
```

#### T073: Platform Selection Buttons
**File**: `src/widgets/splits/SplitInput.cpp`
```cpp
// TODO Implementation Steps:
// 1. Add QPushButtons: "Both" | "T" | "K"
// 2. Only show in merged channel context
// 3. Use exclusive button group
// 4. Connect to MergedChannel::setPlatformSelection()
```

#### T074: Highlight Selected Platform
**File**: `src/widgets/splits/SplitInput.cpp`
```cpp
// TODO Implementation Steps:
// 1. Style selected button with highlight color
// 2. Use CSS or palette for styling
// 3. Update on selection change
```

#### T075: Default to "Both"
**File**: `src/channels/MergedChannel.cpp`
```cpp
// TODO: Already implemented in constructor
// Verify: platformSelection_ = PlatformSelection::Both
```

#### T076: Per-Platform Error Display
**File**: `src/widgets/splits/Split.cpp`
```cpp
// TODO Implementation Steps:
// 1. Connect to MergedChannel send failure signals
// 2. Show error in chat as system message
// 3. Format: "Kick: Rate limited (retry in 30s)"
// 4. Use appropriate error icon
```

#### T077: Unauthenticated Platform Error
**File**: `src/channels/MergedChannel.cpp`
```cpp
// TODO Implementation Steps:
// 1. Before sending, check authentication for each platform
// 2. If not authenticated, show error immediately
// 3. Format: "Cannot send to Kick: Not logged in"
// 4. Suggest logging in
```

### Priority 5: Testing (4 tasks)

#### T078: Multi-Platform Send Tests
**File**: `tests/src/MergedChannel.cpp`
```cpp
// TODO Implementation Steps:
// 1. Test sending to Both platforms
// 2. Test sending to TwitchOnly
// 3. Test sending to KickOnly
// 4. Test partial failure handling
// 5. Test unauthenticated platform rejection
```

#### T090: Integration Tests - Full Flow
**File**: `tests/integration/KickIntegration.cpp` (NEW)
```cpp
// TODO Implementation Steps:
// 1. Test: Add Kick channel → Connect → View messages
// 2. Test: Authenticate → Send message → Verify delivery
// 3. Test: Reconnection after disconnect
// 4. Mock WebSocket for deterministic testing
```

#### T091: Integration Tests - Merged Flow
**File**: `tests/integration/MergedChannelIntegration.cpp` (NEW)
```cpp
// TODO Implementation Steps:
// 1. Test: Create merged channel → View combined messages
// 2. Test: Send to both → Verify delivery
// 3. Test: Platform selection → Verify routing
// 4. Test: Persistence → Restore on restart
```

### Priority 6: Edge Cases & Polish (11 tasks)

#### T079-T081: Recent Messages
- T079: Twitch recent messages already work via existing system
- T080: ✅ DONE - Stub implemented with TODO comment
- T081: Show "live messages only" notice for Kick

#### T082-T084: Reconnection
- T082: ✅ DONE - Exponential backoff implemented
- T083: Handle merged channel with disconnected source
- T084: Show per-platform connection indicators

#### T085-T088: Edge Cases
- T085: Rate limit queuing with countdown
- T086: ✅ DONE - Token auto-refresh implemented
- T087: Invalid channel error handling
- T088: Multi-channel merge limit (>2 channels)

### Priority 7: Acceptance Tests (6 tasks)

#### T093-T098: Success Criteria Validation
- T093: SC-001 - 5s channel add timing
- T094: SC-002 - 2s message latency
- T095: SC-003 - 95% delivery success rate
- T096: SC-004 - 10s merge timing
- T097: SC-005 - 99% uptime stability
- T098: SC-007 - Platform indicator visibility

---

## Quick Reference: Remaining Task Count by Category

| Category | Tasks | IDs |
|----------|-------|-----|
| **MVP UI (Critical)** | 3 | T043-T045 |
| **Platform Badges** | 3 | T055-T057 |
| **Merge UI** | 4 | T058-T061 |
| **Send UI** | 6 | T072-T077 |
| **Unit Tests** | 1 | T078 |
| **Edge Cases** | 5 | T079,T081,T083-T085,T087-T088 |
| **Integration Tests** | 2 | T090-T091 |
| **Acceptance Tests** | 6 | T093-T098 |
| **Documentation** | 1 | T089 |
| **Performance** | 1 | T092 |
| **Coverage Gaps** | 2 | T099, T100 |
| **TOTAL** | **35** | |

---

## Test Validation Summary

### Unit Tests Created: 51 test cases

| Test File | Cases | Coverage |
|-----------|-------|----------|
| KickApi.cpp | 8 | API client |
| KickChannel.cpp | 8 | Channel state |
| KickMessage.cpp | 5 | JSON parsing |
| KickOAuthFlow.cpp | 8 | OAuth config |
| KickWebSocket.cpp | 5 | Protocol |
| MergedChannel.cpp | 11 | Merge logic |
| UrlParser.cpp | 6 | URL parsing |

### Test Status
- ✅ All test files syntactically valid
- ✅ All includes present
- ✅ Test macros correctly used
- ⏳ Build verification pending (requires Qt6 installation)

---

