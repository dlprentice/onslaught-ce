// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_TEXT_CHAT_H
#define CE_TEXT_CHAT_H

#include <cstddef>
#include <cstdint>

// Offline session-owner handoff. This component does not authenticate a peer,
// negotiate a profile, send packets, display text, or collect telemetry.
namespace CeTextChat {

constexpr std::uint32_t Schema = 1;
constexpr std::size_t MaxTextBytes = 512;
constexpr std::size_t MaxScalars = 256;
constexpr std::size_t HeaderBytes = 16;
constexpr std::size_t MaxFrameBytes = HeaderBytes + MaxTextBytes;
constexpr std::size_t OutgoingCapacity = 8;
constexpr std::size_t IncomingCapacity = 16;
constexpr std::size_t BurstMessages = 4;
constexpr std::size_t WindowMessages = 12;
constexpr std::uint64_t BurstMilliseconds = 2000;
constexpr std::uint64_t WindowMilliseconds = 10000;

enum class Result {
    Ok,
    Disabled,
    NotOpen,
    UpdateRequired,
    InvalidBinding,
    Stale,
    InvalidText,
    InvalidFrame,
    Duplicate,
    RateLimited,
    Backpressure,
    ClockRegression,
    Empty,
    SequenceExhausted
};

struct Frame {
    std::uint32_t size;
    std::uint8_t bytes[MaxFrameBytes];
};

struct Decoded {
    std::uint64_t sequence;
    std::uint32_t size;
    std::uint32_t scalars;
    // One trailing zero is supplied for a future plain-text display adapter.
    // size remains authoritative; no format-string or markup interpretation.
    std::uint8_t utf8[MaxTextBytes + 1];
};

struct Binding {
    // The trusted owner must assign a fresh nonzero generation per CE session.
    std::uint64_t generation;
    std::uint32_t epoch;
    std::uint32_t local_slot; // 0 or 1, from admission, never from the wire.
    std::uint32_t text_schema; // 0 disables; 1 requires a matched profile.
};

struct Outgoing {
    Binding binding;
    std::uint64_t sequence;
    Frame frame;
};

struct Message {
    Binding binding;
    std::uint32_t sender_slot; // The admitted remote slot (local_slot xor 1).
    Decoded text;
};

void Clear(Frame& value);
void Clear(Decoded& value);
void Clear(Outgoing& value);
void Clear(Message& value);

// Canonical UTF-8 only. Empty text, C0/C1 controls and Unicode line/paragraph
// separators are refused. Limits count bytes and Unicode scalars, not glyphs.
Result ValidateText(const std::uint8_t* text, std::size_t size,
                    std::uint32_t& scalar_count);
Result Encode(const std::uint8_t* text, std::size_t size,
              std::uint64_t sequence, Frame& output);
Result Decode(const std::uint8_t* bytes, std::size_t size, Decoded& output);

// Single-owner, fixed-storage queue. Open is a caller assertion, not proof of
// authentication: a future CE adapter must call it only after ACTIVE and a
// mutually accepted text profile, and Close on every departure/failure.
class Queue {
public:
    Queue();
    ~Queue();
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    // An identical repeated Open preserves queues and quotas. Changing a
    // binding in the same generation is refused; use BeginEpoch for an epoch.
    Result Open(const Binding& binding, std::uint64_t monotonic_ms);
    Result BeginEpoch(std::uint64_t generation, std::uint32_t epoch,
                      std::uint64_t monotonic_ms);
    void Close();
    bool IsOpen() const { return open_; }

    Result Submit(const std::uint8_t* text, std::size_t size,
                  std::uint64_t monotonic_ms);
    // A failed transport send leaves this exact head available. Commit only
    // after the real transport accepts it; Ok never means peer receipt/read.
    Result PeekOutgoing(Outgoing& output) const;
    Result CommitOutgoing(const Binding& binding, std::uint64_t sequence);

    // Outer generation/epoch and remote slot come from the trusted session.
    // InvalidFrame/UpdateRequired close and clear this queue; the real owner
    // must perform the corresponding safe session disconnect. RateLimited and
    // Backpressure drop the newest frame while preserving existing messages.
    Result Receive(std::uint64_t generation, std::uint32_t epoch,
                   const std::uint8_t* bytes, std::size_t size,
                   std::uint64_t monotonic_ms);
    Result Pop(Message& output);

    std::size_t OutgoingSize() const { return outgoing_count_; }
    std::size_t IncomingSize() const { return incoming_count_; }

private:
    struct Window {
        std::uint64_t accepted[WindowMessages];
        std::size_t count;
        bool Allows(std::uint64_t now);
        void Add(std::uint64_t now);
    };

    bool CheckClock(std::uint64_t now);
    void ClearQueues();

    bool open_;
    Binding binding_;
    std::uint64_t last_now_;
    std::uint64_t next_sequence_;
    std::uint64_t highest_received_;
    Frame outgoing_[OutgoingCapacity];
    Message incoming_[IncomingCapacity];
    std::size_t outgoing_head_;
    std::size_t outgoing_count_;
    std::size_t incoming_head_;
    std::size_t incoming_count_;
    Window outgoing_window_;
    Window incoming_window_;
};

} // namespace CeTextChat
#endif
