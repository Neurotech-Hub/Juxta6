# Juxta Channel-Sounding Architecture Brief

## Intended use case

Juxta devices are small battery-powered tags primarily intended for animal social-proximity sensing.

The primary use case is **mobile-to-mobile ranging** among **3–30 tags**:

- Multiple animals each carry an identical Juxta tag.
- Contact is unpredictable and often brief. Peers must not be assumed available at a planned time.
- Tags continuously advertise identity and opportunistically discover nearby Juxta devices.
- When a newly observed peer qualifies, a pair may attempt Bluetooth Channel Sounding to estimate separation distance.
- Either tag may need to participate on either side of a ranging procedure.
- The goal is to log social encounters as time-series data such as:
  - peer identity
  - timestamp
  - estimated distance
  - ranging quality/confidence information exposed by the SDK
  - optional motion/context information

The same hardware will also be used as **static anchors**. Anchors should preferably use the same firmware image, with their different behavior determined by configuration rather than a separate codebase.

Do not assume anchors are required for mobile-to-mobile ranging. Their purpose is to provide optional fixed spatial references, higher-duty-cycle infrastructure, data collection, or future localization capabilities.

## Relevant hardware

The platform is based on the Nordic nRF54L15 Tag.

Important onboard capabilities:

- nRF54L15 SoC
- Bluetooth LE and Bluetooth Channel Sounding support
- two 2.4-GHz antennas connected through an RF switch
- BMI270 six-axis accelerometer + gyroscope over SPI
- ADXL367 low-power three-axis accelerometer over TWI/I2C
- BME688 environmental sensor over TWI/I2C
- RGB LED
- user button
- exposed GPIO/test points
- CR2032 power
- SWD programming/debugging

The board documentation explicitly identifies the two antennas as being provided for Bluetooth Channel Sounding. The RF antenna selection is performed by an onboard RF switch controlled by the nRF54L15.

Sensor interfaces are:

- BME688: defer for now
- ADXL367: TWI address 0x1D
- BMI270: defer for now

For this Juxta build, assume external SPI NOR on U8 is populated. Project modules use **MX25L3233F** (32 Mbit / 4 MiB); stock Nordic BOM may list MX25R6435 (not fitted).

## Core design requirement

Every mobile Juxta should be capable of participating in Channel Sounding in **both applicable peer roles**.

Do not create separate "initiator hardware" and "receiver hardware."

The firmware architecture should allow a device to dynamically assume the appropriate Channel Sounding role for a particular peer interaction.

The exact Nordic SDK APIs, constraints, connection topology, timing requirements, and terminology should be derived directly from the current nRF Connect SDK rather than hard-coded from this brief.

## Symmetric firmware

Prefer one firmware image with runtime configuration:

### Mobile profile

- advertise identity continuously and scan within the power budget
- participate in opportunistic peer ranging
- optimize aggressively for coin-cell power
- log peer encounters locally
- potentially communicate with anchors

### Anchor profile

- same fundamental ranging capabilities
- known/fixed position
- potentially higher radio duty cycle
- potentially more generous encounter / ranging budget
- potentially act as a data collection point

The difference between mobile and anchor devices should primarily be **policy**, not hardware abstraction or protocol implementation.

## Architectural stance: opportunistic encounter arbiter

Do **not** think of the system as a scheduler that assumes peers will be present at a planned slot.

The architecture should be **event-driven**. Its job is to prevent chaos when animals wander into and out of contact, not to schedule the ecosystem:

```text
ADVERTISEMENT / DISCOVERY
        ↓
NEW PEER OBSERVED
        ↓
cheap eligibility check
        ↓
attempt ranging
        ↓
log result
        ↓
cooldown / retry policy
```

Keep **coarse discovery** separate from **precision ranging**. BLE advertising and RSSI can cheaply indicate *there may be another Juxta here*. Channel Sounding answers *how far away is it?* Do not run Channel Sounding against every advertisement.

### Encounter principles

- **Discovery is continuous/opportunistic.** Every tag advertises its identity and periodically scans according to its power budget.
- **Discovery triggers ranging.** A newly observed peer becomes a candidate immediately rather than waiting for a scheduled slot.
- **Deterministic pair arbitration.** If A and B encounter one another, a simple rule such as `lower ID initiates` can prevent both from trying to start the same interaction.
- **Randomized retry/backoff.** Important when 10–30 animals cluster; otherwise simultaneous attempts can synchronize and repeatedly collide.
- **Per-peer cooldown.** After A ranges B, do not continuously range B unless enough time has elapsed or a relevant condition changes.
- **Global ranging budget.** A tag must not try to range 29 peers at once. Keep a small candidate queue and a limit on radio work.
- **Prioritize new encounters.** For social-contact detection, detecting B for the first time is more valuable than taking the 50th measurement of C.
- **Never assume persistence.** If a peer disappears at any point, abandon that interaction quickly and return to discovery.

Do **not** yet prescribe exact cooldowns, scan intervals, connection strategy, queue depth, or the arbitration algorithm. Those depend on what the current Nordic Channel Sounding implementation makes cheap versus expensive.

### Dense-group behavior

Thirty tags imply **435 possible pairs**. Naive pairwise Channel Sounding against every nearby peer is too expensive.

A reasonable high-level policy, to be tuned later, is:

```text
peer first detected
    → range ASAP

peer remains nearby
    → range occasionally

peer disappears/reappears
    → treat as new encounter

many peers present
    → service candidates opportunistically
       with backoff and rate limits
```

## Suggested software architecture

Keep these concerns separate:

Device Configuration
    |
    +-- unique device identity
    +-- mobile/anchor mode
    +-- encounter / ranging policy
    +-- power policy

Peer Discovery
    |
    +-- advertise identity
    +-- scan for Juxta peers
    +-- cheap coarse filters (identity, RSSI, recency)
    +-- emit newly observed / lost-peer events
    +-- do not initiate Channel Sounding

Encounter Manager
    |
    +-- opportunistic encounter arbiter (not a scheduler)
    +-- peer identity and encounter state
    +-- eligibility / qualify encounter
    +-- deterministic pair arbitration
    +-- candidate queue and global ranging budget
    +-- prioritize first detections over repeat measurements
    +-- per-peer cooldown and randomized retry/backoff
    +-- abandon quickly if a peer disappears
    +-- request Channel Sounding only for qualified candidates

Channel Sounding Layer
    |
    +-- Nordic SDK-specific implementation
    +-- support both peer roles
    +-- return distance + SDK-provided quality metadata
    +-- hide Nordic implementation details from higher layers

Sensor Layer
    |
    +-- BMI270
    +-- ADXL367
    +-- BME688

Encounter Logger
    |
    +-- MX25L3233F storage (4 MiB)
    +-- timestamp
    +-- peer ID
    +-- distance
    +-- quality/status
    +-- optional sensor context

Power Manager
    |
    +-- mobile power state
    +-- anchor power state
    +-- sensor/radio wake policy
    +-- discovery vs ranging radio budget

The application layer should not directly depend on Nordic Channel Sounding APIs. Put the SDK implementation behind a small internal interface so that Nordic changes can be isolated.

Pipeline:

```text
Peer Discovery
      ↓
Encounter Manager
      ↓
Channel Sounding
      ↓
Encounter Logger
```

## Pair arbitration

Because mobile devices are peers, two tags may discover each other simultaneously.

There should therefore be a deterministic mechanism ensuring that a pair does not unnecessarily start competing duplicate ranging operations.

A simple device-ID-based rule such as `lower ID initiates` is an acceptable starting point. The implementation agent should confirm whether the current Nordic SDK already provides a superior mechanism.

The desired property is:

    Pair(A,B) -> one coordinated ranging interaction

rather than:

    A independently ranges B
    B independently ranges A

unless bidirectional procedures are specifically required by the SDK or produce useful additional information.

When many tags cluster, arbitration alone is not enough: combine it with a small candidate queue, a global ranging budget, and randomized backoff so attempts do not synchronize.

## Initial unit / hardware-in-loop tests

### Hardware identity

- nRF54L15 boots successfully.
- unique device identity can be retrieved/persisted.
- firmware can identify its configured mobile/anchor role.

### External flash

Because MX25L3233F is assumed populated (4 MiB):

- flash responds correctly
- erase succeeds
- page write succeeds
- readback matches written data
- data survives reboot
- boundary conditions around erase/page regions are tested

### ADXL367

- device responds at expected TWI address 0x1D
- WHO_AM_I/device identification matches expected part
- acceleration samples can be read
- interrupt path can be exercised if used by production firmware

### BME688

- defer for now, may not exist in final hardware

### BMI270

- defer for now, may not exist in final hardware

### Basic radio

With two physical Juxta units:

- each device can discover the other
- peer identities remain stable
- connection/link establishment succeeds as required by the selected Nordic Channel Sounding implementation
- disconnect/reconnect recovery succeeds
- a disappeared peer is abandoned quickly and does not corrupt peer state

### Channel Sounding

With two physical tags:

- Tag A and Tag B can successfully complete a ranging operation
- A can operate in each required Channel Sounding role
- B can operate in each required Channel Sounding role
- a valid distance result is returned
- invalid/failed measurements are distinguishable from valid measurements
- SDK-provided quality/status information is retained rather than discarded

The test should use several known physical separations. Do not initially enforce aggressive absolute-accuracy thresholds; first characterize the hardware and Nordic implementation.

### Symmetric-peer behavior

Using two identical mobile configurations:

- simultaneous discovery does not cause a persistent role conflict
- one pair can reach a coordinated ranging interaction
- repeated encounters continue to work, including after a cooldown
- either physical tag can assume either required ranging role

### Multiple-peer behavior

Using at least three physical tags:

- A can discover B and C
- ranging one peer does not permanently prevent interaction with another
- the encounter manager does not deadlock
- a global ranging budget prevents attempting every peer at once
- new encounters are preferred over repeated measurements of a still-present peer
- peers can disappear without corrupting peer state
- a peer that disappears and reappears is treated as a new encounter and can be ranged again

Do not prescribe concurrency, queue depth, or exact backoff until the current Nordic SDK capabilities and radio costs are understood.

### Anchor interoperability

Configure one identical board as an anchor:

- mobile tag discovers anchor
- mobile/anchor ranging succeeds
- changing device profile does not require a different Channel Sounding implementation
- anchor identity and known position can be associated with its ranging records

### Logging

For each successful encounter, verify persistence of at least:

    timestamp
    local device ID
    peer device ID
    distance
    ranging status/quality

Verify records survive reset.

### Fault recovery

Test:

- peer disappears during an interaction
- ranging fails
- connection drops
- Channel Sounding operation times out
- sensor communication fails
- flash write fails or storage becomes unavailable

Failures should abandon the current interaction quickly and return the system to discovery so future peer interactions remain possible.

## Tests to defer

Do not prematurely encode assumptions about:

- maximum simultaneous peers
- candidate queue depth
- ranging frequency or per-peer cooldown
- optimal discovery / scan interval
- Channel Sounding procedure parameters
- antenna-selection strategy
- exact distance accuracy
- connection lifetime
- whether links should remain established between ranging events
- exact initiator/reflector arbitration algorithm
- exact power-management timing
- RSSI or other coarse-filter thresholds used to qualify an encounter

These should be determined experimentally and from the current Nordic SDK. In particular, do not prescribe what is cheap versus expensive until Nordic's Channel Sounding implementation is measured.

## Architectural principle

The most important abstraction is:

```text
DISCOVER
   ↓
QUALIFY ENCOUNTER
   ↓
ARBITRATE
   ↓
RANGE IF POSSIBLE
   ↓
LOG
```

rather than:

```text
DISCOVER PEER
   ↓
SCHEDULE INTERACTION
   ↓
RANGE PEER
   ↓
RECORD RESULT
```

and rather than making the application itself aware of Nordic radio procedures.

Discovery answers whether another Juxta may be present. Qualification and arbitration decide whether this tag should spend radio budget on that peer now. Channel Sounding, when attempted, answers distance.

The Channel Sounding subsystem should expose something conceptually similar to:

    range(peer) -> {
        distance,
        quality,
        status
    }

while internally owning whatever connection management, role assignment, antenna handling, timing, and Nordic SDK state machines are actually required.

If a peer vanishes at any stage, abandon the attempt and return to discovery. This is better suited to animals wandering unpredictably through an ecosystem than a conventional scheduler.

This makes it possible to evolve the Nordic implementation without rewriting the social-proximity application.
