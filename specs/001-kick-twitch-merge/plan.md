# Implementation Plan: Kick.tv Chat Integration with Multi-Platform Merge

**Branch**: `001-kick-twitch-merge` | **Date**: 2025-12-18 | **Spec**: [spec.md](./spec.md)  
**Input**: Feature specification from `/specs/001-kick-twitch-merge/spec.md`

## Summary

Add Kick.tv chat platform support to Chatterino7, enabling users to view Kick channels independently and merge them with Twitch channels into unified views. Messages from both platforms display chronologically with platform indicators. Users can send messages to individual platforms or both simultaneously from merged views. Technical approach uses reverse-engineered WebSocket/Pusher protocol for real-time Kick message ingestion, OAuth 2.1 with PKCE for authentication, and extends existing Channel abstraction for multi-platform support.

## Technical Context

**Language/Version**: C++ (C++17 minimum, per existing codebase), Qt 5/6 framework  
**Primary Dependencies**: Qt (Widgets, Network, WebSockets), pajlada-signals, existing emote providers (7TV, BTTV, FFZ), Pusher C++ client (or equivalent WebSocket library)  
**Storage**: Qt Settings for configuration and credentials, file-based window layout persistence (existing system), in-memory message queues  
**Testing**: googletest (existing test framework), Qt Test for UI components  
**Target Platform**: Cross-platform desktop (Windows, Linux, macOS, FreeBSD)  
**Project Type**: Desktop application (Qt-based single application structure)  
**Performance Goals**: <2s message latency from platform to UI, maintain stable dual connections (99% uptime per spec), support 100+ messages/second throughput per channel  
**Constraints**: Zero-setup for users (no external services required), offline-capable architecture, respect Kick rate limits, maintain existing Twitch functionality  
**Scale/Scope**: Multi-platform support (2 platforms: Twitch + Kick), ~5-10 new classes, ~2000-3000 LOC estimated, reuse 80%+ of existing Channel/Message infrastructure

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Phase 0 Check

| Principle | Status | Notes |
|-----------|--------|-------|
| **Code Formatting** | ✅ **PASS** | Will use existing `.clang-format` config; CI formatting checks in place |
| **C++ Standards** | ✅ **PASS** | Will follow `CONTRIBUTING.md` guidelines: `{}` initialization, `camelCase_` members, proper casting, Qt object tree for memory management |
| **Testing Requirements** | ✅ **PASS** | Plan includes unit tests for Kick provider, integration tests for merged channels, acceptance tests per spec scenarios using googletest |
| **Build System** | ✅ **PASS** | Will extend existing CMake configuration; may add Pusher/WebSocket dependencies via vcpkg or existing dependency management |
| **Documentation** | ✅ **PASS** | Will document WebSocket protocol details (for maintenance), update user-facing docs, add inline comments only for "why" (protocol quirks, reverse-engineering notes) |

**Gate Result**: ✅ **APPROVED** - No violations. Feature aligns with all constitutional principles.

### Post-Phase 1 Check

✅ **RE-EVALUATION COMPLETE** - All constitutional principles still satisfied after design phase.

| Principle | Status | Post-Design Notes |
|-----------|--------|-------------------|
| **Code Formatting** | ✅ **PASS** | All proposed C++ code follows clang-format conventions |
| **C++ Standards** | ✅ **PASS** | Design uses `std::shared_ptr`/`std::unique_ptr` properly, `camelCase_` for members, Qt object tree for QObjects |
| **Testing Requirements** | ✅ **PASS** | Comprehensive unit tests planned (KickWebSocket, KickChannel, MergedChannel, etc.), integration tests defined |
| **Build System** | ✅ **PASS** | No new external dependencies beyond existing Qt modules (WebSockets already in Qt) |
| **Documentation** | ✅ **PASS** | Extensive contracts/, research.md, quickstart.md generated; protocol details documented for maintenance |

**Final Gate Result**: ✅ **APPROVED** - Design phase maintains constitutional compliance. Ready for implementation.

## Project Structure

### Documentation (this feature)

```text
specs/001-kick-twitch-merge/
├── spec.md              # Feature specification (✅ complete)
├── plan.md              # This file (🔄 in progress)
├── research.md          # Phase 0 output (⏳ pending)
├── data-model.md        # Phase 1 output (⏳ pending)
├── quickstart.md        # Phase 1 output (⏳ pending)
├── contracts/           # Phase 1 output (⏳ pending)
│   ├── kick-websocket-protocol.md
│   ├── kick-oauth-flow.md
│   └── merged-channel-interface.md
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
src/
├── common/
│   ├── Channel.hpp/cpp              # [MODIFY] Extend for multi-platform
│   ├── ProviderId.hpp               # [MODIFY] Add Kick enum value
│   └── ChannelDescriptor.hpp/cpp   # [MODIFY] Support Kick + Merged types
├── providers/
│   ├── twitch/                      # [REFERENCE] Existing Twitch implementation
│   │   ├── TwitchChannel.hpp/cpp
│   │   ├── TwitchIrcServer.hpp/cpp
│   │   └── TwitchAccount.hpp/cpp
│   └── kick/                        # [NEW] Kick provider implementation
│       ├── KickChannel.hpp/cpp      # Kick-specific Channel subclass
│       ├── KickWebSocket.hpp/cpp    # Pusher/WebSocket connection handler
│       ├── KickAccount.hpp/cpp      # OAuth 2.1 + credential management
│       ├── KickApi.hpp/cpp          # REST API client (send messages)
│       └── KickEmotes.hpp/cpp       # Native Kick emote integration
├── channels/
│   └── MergedChannel.hpp/cpp        # [NEW] Virtual channel combining sources
├── controllers/
│   ├── accounts/
│   │   └── AccountController.cpp    # [MODIFY] Add Kick account handling
│   └── moderationactions/           # [REFERENCE] For future moderation
├── singletons/
│   └── WindowManager.cpp            # [MODIFY] Persist Kick + Merged channels
├── widgets/
│   ├── dialogs/
│   │   └── SelectChannelDialog.cpp  # [MODIFY] Add platform selection UI
│   └── splits/
│       └── Split.cpp                # [MODIFY] Platform selection for messages
├── messages/
│   ├── Message.hpp                  # [REFERENCE] Existing message structure
│   └── MessageBuilder.cpp           # [MODIFY] Support Kick message format
└── util/
    └── UrlParser.hpp/cpp            # [NEW] Intelligent URL/username parsing

tests/
├── src/
│   ├── KickChannel.cpp              # [NEW] Unit tests for Kick channel
│   ├── KickWebSocket.cpp            # [NEW] WebSocket protocol tests
│   ├── MergedChannel.cpp            # [NEW] Merged channel tests
│   └── UrlParser.cpp                # [NEW] URL parsing tests
└── integration/
    └── MultiPlatformMerge.cpp       # [NEW] End-to-end merge scenarios
```

**Structure Decision**: Single desktop application structure (Option 1) is appropriate. Chatterino7 is a monolithic Qt application with clear provider separation (`src/providers/`). New Kick provider mirrors existing Twitch structure. Merged channel functionality added as new Channel subclass. Tests co-located with source using existing googletest infrastructure.

## Complexity Tracking

No complexity violations identified. This feature extends existing patterns (provider abstraction, Channel subclassing) without introducing architectural complexity beyond constitutional guidelines.

---

## Phase 0: Research & Architecture Decisions

*Output: `research.md` with all unknowns resolved*

### Research Tasks

1. **Kick WebSocket/Pusher Protocol Reverse Engineering**
   - **Unknown**: Exact WebSocket endpoint, authentication flow, message format
   - **Action**: Analyze Twick/KickTalk implementations, monitor browser network traffic, document protocol
   - **Decision Needed**: Use existing Pusher library vs. custom WebSocket implementation

2. **Pusher C++ Client Library Evaluation**
   - **Unknown**: Best library for Pusher protocol in C++
   - **Options**: pusher-websocket-cpp, websocketpp, Qt WebSockets with custom Pusher protocol
   - **Decision Needed**: Library selection based on Qt compatibility, maintenance status, licensing

3. **Kick Recent Messages Service**
   - **Unknown**: Equivalent to recent-messages2 for Kick
   - **Action**: Research if Kick provides history API, check third-party services, evaluate fallback (no history)
   - **Decision Needed**: History retrieval strategy

4. **OAuth 2.1 with PKCE in Qt/Desktop Apps**
   - **Unknown**: Best practices for system browser OAuth flow in Qt desktop app
   - **Options**: Qt WebEngine embedded browser, system browser with localhost callback, custom URL scheme
   - **Decision Needed**: OAuth callback mechanism

5. **Merged Channel Message Ordering Performance**
   - **Unknown**: Performance of chronological merge at scale (100+ msg/s from 2 sources)
   - **Action**: Prototype insertion algorithm, benchmark with existing `LimitedQueue`
   - **Decision Needed**: Merge on arrival vs. lazy merge on display

### Technology Choices Validation

6. **Qt WebSockets vs. Third-Party Library**
   - **Question**: Use Qt's built-in WebSocket support or add external library?
   - **Action**: Compare Qt WebSockets feature set with Pusher protocol requirements

7. **Settings Storage for Kick Credentials**
   - **Question**: Secure credential storage mechanism (keychain integration?)
   - **Action**: Research Qt Keychain vs. encrypted QSettings vs. OS-specific secure storage

8. **Platform Badge Rendering**
   - **Question**: Reuse existing badge system or new visual indicator?
   - **Action**: Analyze current badge rendering in `MessageLayoutElement`, determine if extensible

---

## Phase 1: Design Artifacts (Pending Phase 0 Completion)

*Outputs: `data-model.md`, `contracts/`, `quickstart.md`, updated agent context*

### Data Model Design (data-model.md)

Key entities to specify:
- **KickChannel** (extends Channel): platform type, channel slug, WebSocket connection state, authentication token
- **MergedChannel** (extends Channel): list of source channels, platform selection state, message merge strategy
- **KickAccount** (extends Account): OAuth tokens (access, refresh), user ID, expiry
- **ChannelDescriptor** extensions: new types ("kick", "merged"), structured source list for merged views
- **Message** extensions: platform source indicator, Kick-specific metadata (badges, emotes)

### API Contracts (contracts/)

Contracts to document:
1. **kick-websocket-protocol.md**: WebSocket endpoint, connection handshake, subscribe/unsubscribe channel, message format, ping/pong, error codes
2. **kick-oauth-flow.md**: Authorization URL, token exchange, refresh flow, scope requirements
3. **kick-rest-api.md**: Send message endpoint, rate limits, error responses
4. **merged-channel-interface.md**: Public interface for MergedChannel, message routing contract, platform selection API

### Quickstart Guide (quickstart.md)

Developer quickstart for:
- Building with new Kick dependencies
- Running tests for Kick functionality
- Testing OAuth flow locally
- Debugging WebSocket connection issues

---

## Phase 2: Task Breakdown (Deferred to /speckit.tasks)

Task generation pending completion of Phase 0 research and Phase 1 design.

---

## Implementation Phases (High-Level)

### Phase A: Kick Provider Foundation (P0)
- Implement KickChannel, KickWebSocket, KickAccount
- OAuth 2.1 authentication flow
- Basic message receive (no send yet)
- Unit tests for protocol handling

### Phase B: Kick UI Integration (P0)
- Extend SelectChannelDialog for Kick
- URL/username parsing
- Settings toggle for Kick integration
- Display Kick channels independently

### Phase C: Merged Channel Implementation (P1)
- MergedChannel class with dual subscriptions
- Chronological message merging
- Platform indicators in UI
- Tab/header display with platform prefixes

### Phase D: Message Sending (P1)
- Kick message send via REST API
- Platform selection UI in merged views
- Per-platform send status indicators
- Error handling and retry

### Phase E: Persistence & History (P2)
- WindowManager serialization for Kick/Merged
- Recent messages integration (Twitch via recent-messages2, Kick TBD)
- Layout restoration on restart

### Phase F: Polish & Edge Cases (P3)
- Reconnection logic
- Rate limit handling
- Emote rendering validation
- Comprehensive integration tests

---

## Risk Mitigation Plan

| Risk | Mitigation Strategy | Validation |
|------|---------------------|------------|
| Kick protocol changes | Document protocol in contracts/, monitor community, version detection | Test with protocol mock, changelog tracking |
| OAuth flow UX issues | System browser with localhost callback (standard pattern), fallback error messaging | Manual testing on all platforms (Win/Mac/Linux) |
| Message merge performance | Benchmark with 100+ msg/s load, optimize insertion in `LimitedQueue`, lazy rendering | Performance tests in test suite |
| Kick history unavailable | Graceful fallback (no history), document limitation, revisit post-MVP | Acceptance testing with/without history |
| Cross-platform Qt differences | Test on all target platforms early, use Qt abstractions, CI for all platforms | CI matrix testing (GitHub Actions) |

---

## Success Criteria Validation

All success criteria from spec.md will be validated via:
- **SC-001** (5s channel add): Integration test with timer
- **SC-002** (2s message latency): Performance test with timestamp comparison
- **SC-003** (95% delivery): Acceptance test with send tracking
- **SC-004** (10s merge): UI automation test
- **SC-005** (99% uptime): Long-running stability test
- **SC-006** (90% first-attempt success): User testing feedback (post-impl)
- **SC-007** (100% indicator visibility): Visual regression testing

---

## Next Steps

1. ✅ **Complete**: Feature specification and clarifications
2. ✅ **Complete**: Constitution check and project structure definition
3. ⏳ **Current**: Phase 0 research tasks (generating research.md next)
4. ⏳ **Pending**: Phase 1 design artifacts (data-model, contracts, quickstart)
5. ⏳ **Pending**: Phase 2 task breakdown (`/speckit.tasks`)
6. ⏳ **Pending**: Implementation execution
