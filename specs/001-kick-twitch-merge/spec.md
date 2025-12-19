# Feature Specification: Kick.tv Chat Integration with Multi-Platform Merge

**Feature Branch**: `001-kick-twitch-merge`  
**Created**: 2025-12-12  
**Status**: Draft  
**Input**: User description: "For the first feature I'd like to plan for this project, will be focused on integrating Kick to Chatterino7. I want to have a feature that combines the two chat's. So for example, Twitch chat can be added into Chatterino7, then, Kick chat can be added too. Then I'd like to have the ability to combine the two chat's into a single view so that Kick + Twitch chat's are streamed into the single view. For example (Twitch: xQc) and (Kick: xQc) are combined into a single Chatterino7 chat view and chat messages from both channels are streamed into a single view."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Add and View Individual Kick.tv Channels (Priority: P1)

A user wants to add and view Kick.tv chat channels in Chatterino7, similar to how they currently add Twitch channels. The user can browse, search, and open individual Kick.tv channels to view chat messages, emotes, and interact with the chat independently.

**Why this priority**: This is the foundational capability required before any merging functionality can work. Users must be able to add and view Kick.tv channels separately to establish the basic integration. This delivers immediate value by expanding platform support.

**Independent Test**: Can be fully tested by adding a Kick.tv channel (e.g., "kick.com/xQc") and verifying that chat messages, user names, emotes, and basic chat interactions work correctly. This delivers value as a standalone Kick.tv chat client.

**Acceptance Scenarios**:

1. **Given** the user has Chatterino7 open, **When** the user adds a Kick.tv channel by channel name or URL, **Then** the channel appears in the channel list and connects to Kick.tv chat (authentication not required for viewing)
2. **Given** a Kick.tv channel is open, **When** messages are sent in the Kick.tv chat, **Then** messages appear in the Chatterino7 view with correct formatting, usernames, and emotes
3. **Given** a Kick.tv channel is open and the user is authenticated with Kick.tv, **When** the user types a message and sends it, **Then** the message is delivered to the Kick.tv channel
4. **Given** multiple Kick.tv channels are added, **When** the user switches between them, **Then** each channel maintains its own chat history and connection state

---

### User Story 2 - Merge Channels from Different Platforms into Single View (Priority: P2)

A user wants to combine chat channels from different platforms (e.g., Twitch and Kick) for the same streamer into a single unified chat view. Messages from both platforms appear in chronological order in one view, with clear visual indicators showing which platform each message originated from.

**Why this priority**: This is the core differentiating feature that enables users to follow streamers across platforms simultaneously. It provides significant value by eliminating the need to switch between separate chat windows and allowing users to see all community interactions in one place.

**Independent Test**: Can be fully tested by merging a Twitch channel (e.g., "twitch.tv/xQc") and a Kick.tv channel (e.g., "kick.com/xQc") into a single view, then verifying that messages from both platforms appear chronologically with platform indicators. This delivers value as a unified multi-platform chat experience.

**Acceptance Scenarios**:

1. **Given** the user has both a Twitch channel and a Kick.tv channel open, **When** the user selects the merge option, **Then** the system suggests matching channels based on username (if available) and allows the user to confirm or manually select different channels, and a new merged view is created showing messages from both platforms
2. **Given** a merged view is active, **When** messages arrive from either platform, **Then** messages appear in chronological order (by timestamp, with arrival order as tiebreaker) with clear visual indicators (e.g., badges, labels) showing the source platform
3. **Given** a merged view is active, **When** the user scrolls through chat history, **Then** messages from both platforms are interleaved chronologically
4. **Given** a merged view exists, **When** one of the source channels disconnects or becomes unavailable, **Then** the merged view continues to display messages from the remaining connected platform with appropriate status indicators

---

### User Story 3 - Send Messages to Merged Channels with Platform Selection (Priority: P3)

A user wants to send chat messages when viewing a merged channel. The user can choose to send messages to both platforms simultaneously, or select a specific platform (Twitch only or Kick only) before sending.

**Why this priority**: While viewing merged chats is valuable, the ability to participate in conversations across platforms enhances the user experience. This allows users to engage with communities on both platforms without switching views. However, it's lower priority than viewing because users can still interact by switching to individual channel views.

**Independent Test**: Can be fully tested by typing a message in a merged view, selecting the target platform(s), and verifying the message appears in the selected platform(s) chat. This delivers value by enabling unified participation across platforms.

**Acceptance Scenarios**:

1. **Given** a merged view is active with platform selection set to "Both" (default), **When** the user types a message and sends it, **Then** the system attempts to send to both platforms independently, the message appears in the merged view with indicators showing which platform(s) successfully received it, and any failures are displayed with appropriate error messages
2. **Given** a merged view is active, **When** the user changes the platform selection to "Twitch Only" using the toggle buttons and sends a message, **Then** the message is delivered only to the Twitch channel and appears in the merged view with a Twitch indicator
3. **Given** a merged view is active, **When** the user changes the platform selection to "Kick Only" using the toggle buttons and sends a message, **Then** the message is delivered only to the Kick.tv channel and appears in the merged view with a Kick indicator
4. **Given** a merged view is active, **When** the user sends a message to a platform where they are not authenticated or lack permission, **Then** an appropriate error message is displayed and the message is not sent

---

### Edge Cases

- What happens when a user manually merges channels for different streamers (e.g., Twitch: xQc and Kick: pokimane)? (Answer: System allows this via manual override)
- How does the system handle rate limiting or message sending restrictions from one platform while the other platform is functioning normally? (Answer: System attempts to send to both platforms independently; if one fails due to rate limiting, the other still succeeds, and the message is displayed with indicators showing which platform received it)
- What happens when one platform's connection drops while the other remains connected in a merged view?
- How are duplicate messages handled if the same message appears on both platforms (if such a scenario is possible)?
- What happens when a user tries to merge more than two channels (e.g., three different platforms)?
- How does the system handle timezone differences or clock synchronization issues between platforms when ordering messages chronologically? (Answer: Messages ordered by timestamp with arrival order as tiebreaker, so messages appear in the order received from each platform's stream)
- What happens when a merged channel is created but one of the source channels doesn't exist or is invalid?
- How are platform-specific features (e.g., Twitch-specific emotes, Kick-specific badges) displayed in a merged view?

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST allow users to add Kick.tv channels by channel name or URL, similar to existing Twitch channel addition
- **FR-002**: System MUST display Kick.tv chat messages with correct formatting, including usernames, message content, timestamps, and emotes
- **FR-003**: System MUST maintain separate connection states for each Kick.tv channel
- **FR-004**: System MUST allow authenticated users to send messages to individual Kick.tv channels (requires OAuth 2.1 authentication with `chat:write` scope)
- **FR-005**: System MUST provide a mechanism to merge two or more channels from different platforms into a single unified view, with automatic username matching suggestions and manual selection capability
- **FR-006**: System MUST display messages from merged channels in chronological order based on message timestamp, with arrival order in Chatterino7 used as a tiebreaker when timestamps are identical
- **FR-007**: System MUST clearly indicate the source platform for each message in a merged view (e.g., via badges, labels, or visual indicators)
- **FR-008**: System MUST allow users to send messages from a merged view to one or more of the source platforms
- **FR-009**: System MUST provide persistent toggle buttons or selection controls near the message input field in merged views, allowing users to select target platform(s) (options: both platforms, Twitch only, Kick only) with "Both" as the default selection
- **FR-010**: System MUST handle authentication requirements separately for each platform (Twitch and Kick.tv)
- **FR-016**: System MUST allow users to view Kick.tv channels and receive chat messages without authentication (anonymous viewing mode)
- **FR-017**: System MUST require user authentication with `chat:write` scope for sending messages to Kick.tv channels
- **FR-011**: System MUST gracefully handle disconnections or unavailability of one platform while maintaining functionality for the other platform(s) in a merged view
- **FR-012**: System MUST provide automatic username matching (case-insensitive) to suggest matching channels across platforms when merging
- **FR-018**: System MUST allow users to manually override automatic matching and merge any channels they choose, regardless of username match
- **FR-013**: System MUST display appropriate error messages when message sending fails for a specific platform
- **FR-019**: When sending to "Both" platforms, System MUST attempt to send to each platform independently and display the message in the merged view with indicators showing which platform(s) successfully received the message
- **FR-020**: System MUST show per-platform success/failure status when sending messages to multiple platforms, allowing users to see which platforms received the message
- **FR-014**: System MUST maintain chat history for merged views that includes messages from all source platforms
- **FR-015**: System MUST allow users to unmerge or separate merged channels back into individual channel views
- **FR-021**: System MUST persist Kick channels and merged views in the saved window layout and restore them across application restarts
- **FR-022**: System MUST persist merged views as an explicit set of source channels (structured sources), not as a single free-form merged string that cannot be reliably parsed
- **FR-023**: System MUST provide a Kick integration enablement toggle; when disabled, the “Open channel” UI remains in its existing Twitch-only form
- **FR-024**: When Kick integration is enabled, System MUST allow users to choose Twitch/Kick/Merged when opening a new view, and MUST accept both channel usernames and full platform URLs (e.g., "xqc" or "kick.com/xqc") with intelligent parsing
- **FR-025**: System MUST provide a “Login with Kick” flow that uses the system browser for authorization and completes login in-app
- **FR-026**: System MUST securely store Kick authentication credentials so users remain logged in across application restarts
- **FR-027**: System MUST receive real-time Kick chat messages using Kick's WebSocket/Pusher protocol (reverse-engineered from third-party clients, as Kick's official webhook API is unsuitable for desktop applications)
- **FR-028**: If a user attempts to open a Kick or merged view while Kick integration is disabled, System MUST present a clear prompt explaining how to enable Kick integration
- **FR-029**: System MUST support native Kick emotes as well as 7TV/BTTV/FFZ emotes in Kick channels, leveraging existing Chatterino7 emote infrastructure
- **FR-030**: System MUST display merged view tab titles and split headers showing both source channels with platform prefixes (format: "Twitch:username + Kick:username" or abbreviated "T:username + K:username")
- **FR-031**: System SHOULD fetch and display recent chat history (last 50-100 messages) when opening a channel, using recent-messages2 service for Twitch channels and an equivalent solution for Kick channels
- **FR-032**: System MUST accept Kick channel identifiers in multiple formats (username slug like "xqc", or full URLs like "kick.com/xqc" or "https://kick.com/xqc") and intelligently parse them to extract the channel slug

### Key Entities *(include if feature involves data)*

- **Channel**: Represents a chat channel from a specific platform (Twitch or Kick.tv). Attributes include platform type, channel name/identifier, connection state, and authentication status. A channel can exist independently or as part of a merged view.

- **Merged View**: Represents a unified chat view that combines messages from multiple source channels (typically one from each platform for the same streamer). Attributes include source channels, message ordering, and display preferences. Relationships: contains multiple Channels, displays Messages from those channels.

- **Message**: Represents a chat message that can originate from any platform. Attributes include content, timestamp, sender information, platform source, and any platform-specific metadata (emotes, badges, etc.). Relationships: belongs to a Channel, displayed in Merged View.

- **Platform Selection**: Represents the user's choice of target platform(s) when sending a message from a merged view. Options include: both platforms, Twitch only, or Kick only. This is a transient entity used during message composition.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can add a Kick.tv channel and view messages within 5 seconds of entering the channel name or URL
- **SC-002**: Messages from merged channels appear in the unified view within 2 seconds of being sent on the source platform
- **SC-003**: 95% of messages sent from merged views are successfully delivered to the selected target platform(s)
- **SC-004**: Users can successfully merge channels for the same streamer across platforms in under 10 seconds
- **SC-005**: The system maintains stable connections to both platforms simultaneously for at least 99% of active merged view sessions
- **SC-006**: 90% of users who test the merge feature can successfully send messages to their selected platform(s) on the first attempt
- **SC-007**: Platform indicators (badges/labels) are visible and correctly identify the source platform for 100% of messages in merged views

## Assumptions

- Kick's WebSocket/Pusher protocol can be reverse-engineered from existing third-party clients (Twick, KickTalk, etc.) and remains relatively stable
- Users will have separate authentication credentials for Twitch and Kick.tv platforms
- The same streamer username may exist on both platforms (e.g., "xQc" on both Twitch and Kick)
- Message timestamps from both platforms can be normalized to enable chronological ordering
- Platform-specific features (emotes, badges, moderation tools) can be displayed alongside messages in merged views
- Users understand that merged views combine chats from different platforms and may have different moderation rules or community standards
- Users prefer zero-setup experience (direct WebSocket connection) over official API requiring infrastructure deployment

## Technical Risks & Mitigation

**Risk: Unofficial WebSocket/Pusher Protocol**
- **Nature**: Kick's real-time protocol is reverse-engineered and undocumented
- **Impact**: Protocol changes by Kick could break Chatterino7 integration
- **Likelihood**: Medium (other apps use same approach successfully)
- **Mitigation Strategies**:
  1. Monitor third-party client communities (Twick, KickTalk) for protocol changes
  2. Document protocol details for quick updates
  3. Provide clear error messages when connection fails
  4. Maintain webhook + CF Worker relay option as documented fallback (Future Enhancement)
  5. Plan migration to official API when/if Kick releases one (similar to Twitch EventSub)

## Dependencies

- Existing Twitch chat integration and channel management system
- Authentication system for platform credentials
- Message display and rendering system
- Channel connection and reconnection logic
- UI components for channel management and view configuration
- Recent-messages2 service (https://recent-messages.robotty.de/) for Twitch chat history
- Kick chat history service or equivalent mechanism (to be determined during implementation)

## Clarifications

### Session 2025-12-12

- Q: Is authentication required to view Kick.tv channels, or can users view messages without logging in (with authentication only required for sending messages)? → A: Viewing allowed without authentication; sending requires authentication (matches Twitch behavior)
- Q: How should the system validate that a Twitch channel and a Kick.tv channel represent the same streamer? What matching criteria should be used? → A: Username matching with manual override option (auto-match by case-insensitive username, but allow users to manually select any channels to merge regardless of username match)
- Q: How should users select which platform(s) to send messages to in a merged view? What UI pattern should be used? → A: Persistent toggle/buttons near input field (Both/Twitch/Kick) with "Both" as default
- Q: When messages from Twitch and Kick have identical timestamps, how should they be ordered in the merged view? → A: Messages ordered primarily by timestamp, with arrival order in Chatterino7 as tiebreaker (messages appear in the order they are received from the stream)
- Q: When sending to "Both" platforms and one succeeds while the other fails, how should the system handle this? → A: Attempt both, show success/failure per platform, display message with platform indicators showing which platform(s) it was successfully sent to

### Session 2025-12-18

- Q: When saving/restoring layout, how should a merged (Twitch+Kick) view be stored? → A: Persist as a merged view with structured sources (explicit list of source channels, not encoded into a single free-form string)
- Q: How should users add Kick vs Twitch vs Merged in the "Join channel" UX? → A: Add platform selection (Twitch/Kick/Merged) with appropriate inputs; accept URLs as convenience; keep default UI unchanged unless Kick integration is enabled
- Q: How should users authenticate Kick in Chatterino7? → A: "Login with Kick" via system browser OAuth authorization flow (PKCE), app receives callback, and stores reusable credentials securely for future sessions
- Q: When should the app show Kick/Merged options in the "Open channel" dialog and related UI? → A: Only after Kick integration is enabled (via settings toggle), regardless of whether the user is logged in
- Q: For the desktop app, how should we receive real-time Kick messages? → A: Use Kick's official real-time chat stream mechanism documented in Context7 (not webhooks as primary)

### Session 2025-12-18 (Round 2)

- Q: Since Kick's official API documentation only covers webhooks (requiring public URLs unsuitable for desktop apps), how should Chatterino7 receive real-time Kick chat messages? → A: Use unofficial/reverse-engineered WebSocket or Pusher protocol (similar to Twick and other third-party clients that successfully integrate Kick chat). Alternative approach using Cloudflare Worker webhook relay documented as future enhancement for users who prefer official APIs.
- Q: What emote support should Kick channels have in Chatterino7? → A: Native Kick emotes + 7TV/BTTV/FFZ support (both Kick and Twitch use 7TV, and Chatterino7 already supports these emote providers, so existing infrastructure can be reused)
- Q: What should display in the tab title and split header for a merged view? → A: Both sources with platform prefix (e.g., "Twitch:xQc + Kick:xQc" or "T:xQc + K:xQc")
- Q: When a user opens a Kick channel for the first time (initial connection), should Chatterino7 fetch and display recent chat history? → A: Fetch and display last 50-100 messages for better UX; use recent-messages2 service (https://recent-messages.robotty.de/) for Twitch; investigate equivalent solution for Kick
- Q: When users add a Kick channel, what input format(s) should be accepted in the "Join channel" dialog? → A: Both username and full URL accepted with intelligent parsing (e.g., accept both "xqc" and "kick.com/xqc" or "https://kick.com/xqc")
- Q: Should we use Kick's official webhook API with Cloudflare Worker relay instead of reverse-engineered WebSocket protocol? → A: Start with WebSocket/Pusher approach (Option A) for simplicity and zero-setup user experience; document webhook + CF Worker relay (Option B) as future enhancement for users who prefer official APIs or if reverse-engineering becomes unmaintainable

## Out of Scope

- Merging more than two platforms (this feature focuses on Twitch + Kick.tv only)
- Synchronization of user preferences or settings across platforms
- Platform-specific moderation tools or commands in merged views (basic message sending only)
- Historical message retrieval from platforms (only real-time messages in merged views)
- Webhook-based ingestion with Cloudflare Worker relay (documented as future enhancement; see below)

## Future Enhancements

### Alternative Ingestion Architecture (Post-MVP)

While the MVP uses direct WebSocket/Pusher connection to Kick (reverse-engineered protocol), a future enhancement could support Kick's official webhook API with a relay architecture:

**Option: Webhook + Personal Cloudflare Worker Relay**
- Users deploy their own Cloudflare Worker
- Worker receives webhooks from Kick's official API
- Worker forwards events to desktop app via WebSocket
- **Benefits**: Uses official API, stable, versioned, supported by Kick
- **Tradeoffs**: Requires user setup (CF account, worker deployment), adds latency hop, infrastructure cost

**Implementation approach:**
- Add "Webhook Mode" toggle in Kick settings
- User provides their CF Worker WebSocket URL
- Desktop app connects to user's worker instead of Kick directly
- Provide CF Worker template and deployment guide

**When to implement:**
- If Kick changes their WebSocket protocol frequently (breaking third-party clients)
- If users request official API support
- As migration path when Kick releases official real-time API (like Twitch EventSub)
