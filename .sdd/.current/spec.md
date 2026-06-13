# Feature Specification: MeshCore Companion Integration

**Created**: 2026-05-11
**Status**: Draft
**Model**: Claude Opus 4.6 (copilot)
**Input**: добавить функциональность meshcore приложения, которое
использует подключение ноды компаньона по bluetooth

## Assumptions

- **Device compatibility**: The implementation targets both
  Xteink X3 (792x528 display) and Xteink X4 (800x480 display).
  Both devices share the same ESP32-C3 MCU with built-in BLE
  radio. Differences are limited to screen resolution and button
  layout — both are already handled by the existing HAL
  (`HalGPIO::deviceIsX3()`, `renderer.getScreenWidth()` /
  `getScreenHeight()`). No X3-specific BLE code is required;
  orientation-aware rendering (FR-020) covers both devices.
- **Scope extension**: MeshCore messaging extends beyond the
  current "focused reading" mission defined in SCOPE.md. This
  feature is treated as an opt-in secondary mode (a separate
  Activity) that does not interfere with the reading experience.
  The user must explicitly navigate to the MeshCore activity —
  it does not run in the background while reading.
  `[NEEDS CLARIFICATION: Does the project maintainer approve
  adding a communication feature given that SCOPE.md lists
  "Active Connectivity" as out-of-scope? MeshCore is off-grid
  and decentralized (no internet), which differs from the
  rejected RSS/web categories, but it still introduces non-
  reading functionality.]`
- **External companion node**: The CrossPoint Reader (ESP32-C3)
  acts as a BLE **client** connecting to an external MeshCore
  companion radio (e.g., Heltec V3, RAK4631) running BLE
  companion firmware. The LoRa radio hardware is NOT on the
  Xteink X3/X4 itself — it is a separate physical device.
- **BLE capability**: The ESP32-C3 has a built-in BLE radio.
  Arduino-ESP32 provides the `NimBLE` or `BLE` library for
  GATT client operations. BLE and WiFi share the same radio,
  so they cannot be used simultaneously at full performance.
- **Protocol version**: Implementation targets MeshCore Companion
  Protocol v1.12.0+ as documented at
  https://docs.meshcore.io/companion_protocol/. The protocol
  uses the Nordic UART Service (NUS) BLE profile with binary
  framing.
- **Message length**: The LoRa packet payload is capped at 184
  bytes by the firmware (`MAX_PACKET_PAYLOAD`). The usable text
  length depends on message type and overhead: channel messages
  embed the sender name in the form `SenderName: body`, so the
  body length shrinks with longer node names. Direct messages
  have a different overhead. The companion protocol docs state
  133 characters as a conservative figure; Android MeshCore app
  allows 143. The implementation MUST query the practical limit
  dynamically or enforce a safe conservative cap (e.g., 133).
  No message chunking is implemented in the initial version.
- **Text input**: The CrossPoint X3/X4 has only physical buttons
  (no keyboard). Text composition uses the existing
  `KeyboardEntryActivity` (on-screen keyboard navigated with
  buttons). This limits practical message length and input speed.
- **No background operation**: BLE connection is active only
  while the MeshCore activity is in the foreground. Entering
  the reader or any other activity disconnects BLE to free RAM
  and CPU. There is no background message polling.
- **Single companion**: Only one companion node is paired and
  connected at a time.
- **RAM budget**: The MeshCore activity must operate within
  ~40 KB of heap (remaining after framebuffer and core
  allocations). Message history is NOT stored in RAM — only the
  currently visible screen of messages is held. Older messages
  are stored on SD card.
- **Default BLE pairing PIN**: `123456` (MeshCore standard
  default). Users can change this on the companion node.
- **No SET_DEVICE_TIME**: The ESP32-C3 has no battery-backed
  RTC and typically no internet access for NTP. Sending an
  inaccurate timestamp via SET_DEVICE_TIME could overwrite a
  correct time previously set by a phone app. Therefore
  SET_DEVICE_TIME is omitted from the initialization sequence.
  The companion node uses its own internal time or time set by
  other clients.
- **UI structure — Discovered vs Contacts**: Following the
  standard MeshCore app UX pattern, the UI separates
  "discovered nodes" (all nodes heard via advertisements) from
  "saved contacts" (nodes the user explicitly saves). This
  prevents transient advertisement nodes from cluttering the
  user's conversation list. Direct messages from nodes not in
  the saved contacts list are silently ignored.

## User Scenarios & Testing

### User Story 1 - Connect to Companion Node (Priority: P1)

A user powers on their MeshCore companion radio (flashed with
BLE companion firmware) and places it near the Xteink X3/X4.
From the CrossPoint Reader home screen, the user navigates to a
"MeshCore" menu item. The device scans for nearby MeshCore BLE
companions, displays discovered devices, and the user selects
one to connect. After BLE pairing and connection, the app
performs the initialization handshake (APP_START, DEVICE_QUERY,
GET_CONTACTS, GET_CHANNELS) and shows the
main MeshCore screen with connection status.

**Why this priority**: Without a working BLE connection, no
other MeshCore functionality is possible. This is the
foundation for all subsequent features.

**Independent Test**: Flash a MeshCore BLE companion on a
Heltec V3 or similar device. Open MeshCore activity on the
X3/X4, verify the companion appears in scan results, connect, and
confirm the main screen shows "Connected" with the companion
node name and radio info.

**Acceptance Scenarios**:

1. **Given** the MeshCore activity is open and no companion is
   connected, **When** the user initiates a scan, **Then** all
   nearby MeshCore BLE companions are listed with their names
   and signal strength.
2. **Given** a companion is listed in scan results, **When** the
   user selects it, **Then** BLE pairing and GATT connection
   are established, initialization commands are exchanged, and
   the main screen displays "Connected" with node name.
3. **Given** a companion was previously connected, **When** the
   user re-enters the MeshCore activity, **Then** the device
   attempts automatic reconnection to the last known companion.
4. **Given** BLE scanning is in progress, **When** no companions
   are found within 10 seconds, **Then** a "No devices found"
   message is displayed with an option to retry.
5. **Given** a connection is active, **When** the companion
   powers off or goes out of range, **Then** the UI shows
   "Disconnected" and offers reconnection.

---

### User Story 2a - View Channel Messages (Priority: P2)

The user navigates to a channel list screen showing all
configured channels (public, hashtag, private). Selecting a
channel opens a per-channel message thread displaying messages
in chronological order with sender name (embedded in the
message payload), timestamp, and text. New messages in
any channel are indicated on the channel list. The user can
scroll through the channel's message history. The channel list
also provides a management menu for adding, removing, and
configuring channel slots (see US6).

**Why this priority**: Channel messages are the most common
MeshCore communication mode. The public channel is active by
default on every companion.

**Independent Test**: Connect to a companion. From another
MeshCore device, send a message to the public channel. Verify
the public channel shows an unread indicator. Open it and
confirm the message appears with correct sender and timestamp.

**Acceptance Scenarios**:

1. **Given** the user is connected and opens the channel list,
   **When** channels are loaded, **Then** each configured
   channel is shown with name, type (public/private), and an
   unread message count badge if new messages exist.
2. **Given** the user selects a channel, **When** the channel
   thread opens, **Then** messages are displayed in
   chronological order with sender name, timestamp, and text.
3. **Given** a channel thread is open, **When** a new message
   arrives for that channel, **Then** it appends to the thread
   after the next screen refresh cycle.
4. **Given** there are more messages than fit on one screen,
   **When** the user presses the page navigation buttons,
   **Then** the thread scrolls to show older/newer messages.
5. **Given** a channel thread is open, **When** the user
   selects the "Send" action, **Then** the keyboard opens for
   composing a message to that specific channel (see US3).
6. **Given** channel messages have been received, **When** the
   user exits and re-enters the MeshCore activity, **Then**
   previously received messages are loaded from SD card cache.

---

### User Story 2b - View Direct Messages (Priority: P2)

The user navigates to the saved contacts list (US4b).
Selecting a contact opens a per-contact direct message thread
showing the conversation history in chronological order. Each
message shows direction (sent/received), timestamp, text, and
delivery status (for sent messages). New messages from any
contact are indicated on the contacts list with an unread
badge.

**Why this priority**: Direct messaging is the second core
communication mode. Separating DMs per-contact avoids the
problem of searching for a specific conversation in a mixed
message list.

**Independent Test**: Connect to a companion. From another
MeshCore device, send a direct message to the companion's
identity. Verify the sender appears in the contacts list with
an unread indicator. Open the conversation and confirm the
message is displayed correctly.

**Acceptance Scenarios**:

1. **Given** the user is connected and opens the contacts list,
   **When** contacts are loaded, **Then** saved contacts are
   displayed with name, type icon, last seen time, and unread
   message count badge.
2. **Given** the user selects a contact, **When** the DM thread
   opens, **Then** the conversation history is displayed in
   chronological order with direction, timestamp, text, and
   delivery status (sent/ACK/failed) for outgoing messages.
3. **Given** a DM thread is open, **When** a new direct message
   arrives from that contact, **Then** it appends to the thread
   after the next screen refresh cycle.
4. **Given** there are more messages than fit on one screen,
   **When** the user presses the page navigation buttons,
   **Then** the thread scrolls to show older/newer messages.
5. **Given** a contact's DM thread is open, **When** the user
   selects the "Send" action, **Then** the keyboard opens for
   composing a direct message to that contact (see US3).
6. **Given** direct messages have been received, **When** the
   user exits and re-enters the MeshCore activity, **Then**
   previously received messages are loaded from SD card cache.

---

### User Story 3 - Send Messages (Priority: P3)

The user sends a message from within a conversation thread —
either a channel thread (US2a) or a contact DM thread (US2b).
The "Send" action opens the on-screen keyboard, the user types
a message, and confirms. The message is sent to the
appropriate target (channel index or contact public key).
A delivery confirmation or send status is displayed in the
thread.

**Why this priority**: Sending completes the two-way
communication loop. It is lower priority than receiving because
input on the X3/X4 is slow (button-based keyboard), so users
will primarily read.

**Independent Test**: Connect to a companion. Open the public
channel thread, compose "Hello from X4", send. Verify the
message appears on another MeshCore client connected to the
same mesh.

**Acceptance Scenarios**:

1. **Given** the user is in a channel thread and selects "Send",
   **When** they type a message (within the enforced character
   limit) and confirm, **Then** the message is sent via
   CMD_SEND_CHANNEL_MESSAGE and a PACKET_MSG_SENT confirmation
   is shown in the thread.
2. **Given** the user is in a contact's DM thread and selects
   "Send", **When** they type a message and confirm, **Then**
   the message is sent as a direct message and delivery status
   (sent/ACK) is shown in the thread.
3. **Given** the message reaches the character limit during
   input, **When** the user types, **Then** further input is
   blocked and a visible counter shows remaining characters.
4. **Given** the companion is disconnected, **When** the user
   attempts to send, **Then** an error message is shown and the
   message is NOT queued (no offline queue in v1).

---

### User Story 4a - Discovered Nodes (Priority: P4)

The user views a list of all nodes heard via advertisements
(the "discover list"). This includes all MeshCore nodes the
companion has heard — companions, repeaters, room servers,
sensors — regardless of whether the user has saved them as
contacts. Each entry shows name, type icon, public key prefix,
last seen time, and path length. The user can select a
discovered node and add it to their saved contacts for easy
DM access.

**Why this priority**: Node discovery is how users find new
participants in the mesh. It must be separate from the saved
contacts list so the user is not overwhelmed by transient
nodes when looking for their conversation partners.

**Independent Test**: Connect to a companion that has received
advertisements from other nodes. Verify the discover list
shows all heard nodes. Select one and add it to contacts.
Verify it now appears in the contacts list (US4b).

**Acceptance Scenarios**:

1. **Given** the companion has received advertisements, **When**
   the user navigates to the discovered nodes list, **Then**
   all heard nodes are displayed with name, type icon, public
   key prefix, last seen time, and path length.
2. **Given** a discovered node is selected, **When** the user
   presses confirm, **Then** a detail view shows full info and
   offers "Add to Contacts" and "Send Message" actions.
3. **Given** the user selects "Add to Contacts", **When**
   confirmed, **Then** the node is saved to the contacts list
   and is available for DM (US2b/US4b).
4. **Given** a new advertisement is received while viewing the
   list, **When** the list refreshes, **Then** the new or
   updated node appears.

---

### User Story 4b - Saved Contacts (Priority: P4)

The user views a curated list of saved contacts — nodes that
have been explicitly added from the discover list (US4a). This
is the same list used for DM threads (US2b). Each contact shows name,
type icon, last seen time, and unread message badge. The user
can remove a contact from the saved list.

**Why this priority**: A separate saved contacts list provides
quick access to conversation partners without scrolling through
all discovered nodes. This matches the standard MeshCore app
UX pattern where "contacts" and "discovered nodes" are
separate views.

**Independent Test**: Add a node from the discover list to
contacts. Verify it appears in the saved contacts list with
correct info. Remove it and verify it disappears from contacts
but remains in the discover list.

**Acceptance Scenarios**:

1. **Given** the user navigates to saved contacts, **When** the
   list loads, **Then** only explicitly saved contacts are
   shown (not all discovered nodes).
2. **Given** a saved contact is selected, **When** the user
   presses confirm, **Then** the contact's DM thread opens
   (same as US2b).
3. **Given** a saved contact is selected, **When** the user
   chooses "Remove from Contacts", **Then** the contact is
   removed from the saved list but remains in the discover
   list (US4a). DM history on SD card is NOT deleted.
4. **Given** a direct message arrives from a node not in
   saved contacts, **When** the message is received, **Then**
   the message is silently discarded (not stored, not
   displayed).

---

### User Story 5 - View Companion Status (Priority: P5)

The user views the connected companion's status: battery
voltage, storage usage, radio settings (frequency, bandwidth,
spreading factor), firmware version, and device model.

**Why this priority**: Status information helps the user manage
their companion hardware but is not essential for messaging.

**Independent Test**: Connect to a companion. Navigate to
status screen. Verify displayed battery voltage matches the
companion's actual battery level (within reasonable tolerance).

**Acceptance Scenarios**:

1. **Given** a companion is connected, **When** the user
   navigates to companion status, **Then** battery voltage,
   used/total storage, radio frequency, bandwidth, spreading
   factor, coding rate, firmware version, and model are
   displayed.
2. **Given** the companion status is displayed, **When** the
   user presses a refresh button, **Then** the values are
   re-queried from the companion and updated.

---

### User Story 6 - Channel Management (Priority: P6)

The channel list screen (US2a) provides a management menu
accessible via a dedicated button or long-press action. From
this menu the user can add a new channel (entering name and
secret via on-screen keyboard), delete an existing channel,
or view channel details. This is NOT a separate screen — it
is an overlay or sub-menu on the channel list.

**Why this priority**: Most users will use the default public
channel. Channel management is an advanced feature.

**Independent Test**: Connect to a companion. Open the channel
list (US2a). Open the management menu, add a private channel
with a known secret. Verify the channel appears in the list
and messages sent to it are received.

**Acceptance Scenarios**:

1. **Given** the user is on the channel list screen, **When**
   the user opens the management menu, **Then** options to
   add a new channel and manage existing channels are shown.
2. **Given** an empty channel slot exists, **When** the user
   selects "Add Channel" and enters a name and 16-byte
   secret, **Then** the channel is configured on the
   companion and appears in the channel list.
3. **Given** a private channel is selected, **When** the user
   chooses "Delete Channel" from the menu, **Then** the
   channel slot is cleared on the companion (name and secret
   zeroed) and removed from the list.

---

### Edge Cases

- What happens when BLE connection drops during message send?
  The send fails with a timeout; the user is notified and can
  retry manually.
- What happens when the companion's message queue overflows?
  The companion firmware handles this internally. The app polls
  GET_MESSAGE until PACKET_NO_MORE_MSGS; oldest undelivered
  messages may be lost on the companion side.
- What happens when heap is insufficient for BLE operations?
  The MeshCore activity checks `ESP.getFreeHeap()` on entry. If
  below a safety threshold (e.g., 30 KB), it shows an error and
  refuses to start BLE.
- What happens during e-ink refresh while a message arrives?
  Messages are queued in a small ring buffer (companion-side).
  The app polls for new messages after each screen refresh
  cycle, not in real-time.
- How is the contact list handled if it exceeds screen space?
  Both the discovered nodes list and saved contacts list are
  scrollable using page buttons. Only the visible page is held
  in RAM.
- What happens if WiFi is active when entering MeshCore?
  WiFi is disconnected before BLE initialization, since they
  share the ESP32-C3 radio. A warning is shown to the user.
- What happens when a DM arrives from a node not in contacts?
  The message is silently discarded — not stored, not displayed.
  The user must first add the node to saved contacts (via the
  discovered nodes list, US4a) to receive DMs from it.

## Requirements

### Functional Requirements

- **FR-001**: System MUST scan for BLE devices advertising the
  MeshCore NUS service UUID
  (`6E400001-B5A3-F393-E0A9-E50E24DCCA9E`) and display
  discovered companions by name and RSSI.
- **FR-002**: System MUST establish a BLE GATT connection to a
  selected companion, discover NUS characteristics, enable TX
  notifications, and perform the MeshCore initialization
  sequence (APP_START → DEVICE_QUERY →  GET_CONTACTS → GET_CHANNELS).
- **FR-003**: System MUST implement a command queue that sends
  one command at a time and waits for a response (or 5-second
  timeout) before sending the next.
- **FR-004**: System MUST display a channel list showing all
  configured channels with name, type, and unread message
  count. Selecting a channel opens a per-channel message
  thread (PACKET_CHANNEL_MSG_RECV / V3) with sender name
  (embedded in message payload), timestamp, and text.
- **FR-005**: System MUST display a saved contacts list with
  name, type icon, last seen time, and unread badge. Selecting
  a contact opens a per-contact DM thread
  (PACKET_CONTACT_MSG_RECV / V3) with direction, timestamp,
  text, and delivery status.
- **FR-006**: System MUST poll for queued messages
  (CMD_GET_MESSAGE) when PACKET_MESSAGES_WAITING (0x83) push
  notification is received, continuing until
  PACKET_NO_MORE_MSGS. Incoming messages MUST be routed to
  the correct channel thread or contact DM thread.
- **FR-007**: System MUST allow sending channel messages from
  within a channel thread via CMD_SEND_CHANNEL_MESSAGE with
  user-composed text and current Unix timestamp. The character
  limit MUST be enforced at input time. The limit is derived
  from `MAX_PACKET_PAYLOAD` (184 bytes) minus protocol overhead
  and sender node name length; a safe conservative default
  (e.g., 140) is used when the exact limit is unknown.
- **FR-008**: System MUST allow sending direct messages from
  within a contact's DM thread.
- **FR-009**: System MUST display a discovered nodes list
  (all nodes received via PACKET_CONTACT_START /
  PACKET_CONTACT / PACKET_CONTACT_END and PACKET_ADVERTISEMENT)
  with name, type, public key prefix, last seen time, and
  path length. This list is separate from saved contacts.
- **FR-010**: System MUST allow the user to add a discovered
  node to saved contacts and remove a saved contact (without
  deleting DM history).
- **FR-011**: System MUST silently discard direct messages
  received from nodes not in the saved contacts list. Such
  messages are not stored, not displayed, and do not trigger
  any notification.
- **FR-012**: System MUST display companion status information:
  battery voltage, storage usage, radio parameters (frequency,
  BW, SF, CR), firmware version, and model name.
- **FR-013**: System MUST allow viewing, adding, and deleting
  channels (indices 0-7) via CMD_GET_CHANNEL / CMD_SET_CHANNEL.
- **FR-014**: System MUST persist received messages to SD card
  (under `.crosspoint/meshcore/`) organized per-channel and
  per-contact so they survive activity exit/re-entry.
- **FR-015**: System MUST persist the saved contacts list to
  SD card separately from the discovered nodes list.
- **FR-016**: System MUST store the last connected companion's
  BLE address for auto-reconnection on next entry.
- **FR-017**: System MUST disconnect BLE cleanly in `onExit()`
  and free all BLE-related resources.
- **FR-018**: System MUST show connection state (Scanning /
  Connecting / Connected / Disconnected) at all times in the
  MeshCore UI.
- **FR-019**: System SHOULD handle BLE MTU negotiation,
  requesting 512 bytes to support larger commands like
  SET_CHANNEL (50 bytes).
- **FR-020**: System MUST disconnect WiFi before starting BLE
  operations and warn the user if WiFi was active.
- **FR-021**: System MUST limit total heap usage of the
  MeshCore activity to remain above 30 KB free heap at all
  times.
- **FR-022**: System MUST use the `tr()` macro for all user-
  facing strings (connection status, menu labels, error
  messages).
- **FR-023**: System MUST render the MeshCore UI using
  orientation-aware coordinates (`renderer.getScreenWidth()` /
  `getScreenHeight()`, `getOrientedViewableTRBL()`).

### Key Entities

- **Companion**: The external MeshCore LoRa radio connected via
  BLE. Attributes: name, public key, BLE address, radio
  settings (freq, BW, SF, CR), firmware version, model,
  battery voltage, storage usage.
- **Discovered Node**: A MeshCore node heard via advertisement.
  Present in the discover list regardless of saved status.
  Attributes: name, public key (32 bytes), type (companion /
  repeater / room server / sensor), last seen timestamp, path
  length, RSSI/SNR.
- **Saved Contact**: A node explicitly saved by the user from
  the discovered nodes list. Provides quick access to DM
  threads. DMs from nodes not in saved contacts are discarded.
  Attributes: same as Discovered Node, plus: saved flag,
  unread DM count.
- **Channel**: A group communication channel. Attributes: index
  (0-7), name (up to 32 bytes), secret (16 bytes), type
  (public/hashtag/private), unread message count.
- **Message**: A text message sent or received over the mesh.
  Attributes: direction (sent/received), type (channel/direct),
  sender public key prefix, sender name (resolved), channel
  index (if channel), contact public key (if direct),
  timestamp, text content, SNR (if V3), path length, delivery
  status (sent/ACK/failed, for outgoing direct messages).
- **BLE Connection**: The active BLE GATT session. Attributes:
  state (disconnected/scanning/connecting/connected), companion
  BLE address, MTU size, RSSI.

## Success Criteria

### Measurable Outcomes

- **SC-001**: User completes BLE scan → connect → see main
  screen flow in under 15 seconds (excluding BLE pairing PIN
  entry on first connect).
- **SC-002**: Incoming messages appear in the correct thread
  (channel or contact DM) within 3 seconds of the companion
  receiving them (1 second BLE transfer + up to 2 seconds
  e-ink refresh).
- **SC-003**: Free heap remains above 30 KB at all times during
  MeshCore activity operation, verified via
  `ESP.getFreeHeap()` logging.
- **SC-004**: BLE disconnection and resource cleanup in
  `onExit()` completes without memory leaks — heap returns to
  pre-MeshCore-entry levels (± 1 KB).
- **SC-005**: Message history of at least 100 messages per
  channel and per contact persists on SD card and loads
  correctly on activity re-entry.
- **SC-006**: All four screen orientations render the MeshCore
  UI correctly on both X3 (792x528) and X4 (800x480) devices
  (channel list, channel thread, contacts list, DM thread,
  discovered nodes, status screen).
- **SC-007**: A complete send-message flow (open thread →
  type text → confirm → see sent status) completes in under
  60 seconds for a 50-character message.
- **SC-008**: The MeshCore activity does not interfere with
  reading functionality — entering and exiting the reader
  before/after MeshCore shows no degradation in EPUB rendering
  or navigation performance.
