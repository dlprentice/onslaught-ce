// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_TEXT_CHAT_OWNER_H
#define CE_TEXT_CHAT_OWNER_H

#include "text_chat.h"

namespace CeTextChat {

constexpr std::size_t HistoryCapacity = 16;

enum class Origin {
    Remote,
    LocalTransportAccepted
};

struct HistoryEntry {
    Origin origin;
    Message message;
};

void Clear(HistoryEntry& value);

struct OwnerState {
    bool open;
    bool composing;
    bool muted;
    bool suspended;
    bool disconnect_required;
    Result terminal_result;
    std::size_t outgoing;
    std::size_t history;
};

// One owner, on the session thread. It has no I/O, clock, input or renderer.
// The caller supplies an authenticated binding and monotonic milliseconds.
// Generations are nonzero, strictly increasing local session-owner counters.
// Keep this owner alive across departure/rejoin; a retired generation cannot
// be reopened to refill rate budgets. Epochs increase within a generation.
class Owner {
public:
    Owner();
    ~Owner();
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;

    Result Open(const Binding& binding, std::uint64_t monotonic_ms);
    Result BeginEpoch(std::uint64_t generation, std::uint32_t epoch,
                      std::uint64_t monotonic_ms);
    void Close();

    // Sync/congestion does not reopen the queue or replenish its budgets.
    // Suspension cancels composition; an accepted outgoing head is retained.
    void SetSuspended(bool suspended);
    void SetMuted(bool muted);
    void ClearHistory();
    OwnerState State() const;

    Result BeginCompose();
    void CancelCompose();
    Result Append(const std::uint8_t* utf8, std::size_t size);
    Result Backspace(); // Removes one Unicode scalar, not a grapheme cluster.
    Result CopyDraft(Decoded& output) const;
    Result SubmitDraft(std::uint64_t monotonic_ms);

    // Peek is read-only. Commit only after the real transport accepted this
    // exact binding/sequence. Its history origin does not claim peer delivery.
    Result PeekOutgoing(Outgoing& output) const;
    Result CommitOutgoing(const Binding& binding, std::uint64_t sequence);

    // The outer transport authenticates sender/session/epoch before this call.
    // Authenticated malformed/schema-invalid frames require the caller to
    // disconnect the actual CE session. Inspect State().disconnect_required.
    // Muting/suspension discards display text without resetting replay/rate state.
    Result Receive(std::uint64_t generation, std::uint32_t epoch,
                   const std::uint8_t* bytes, std::size_t size,
                   std::uint64_t monotonic_ms);
    Result HistoryAt(std::size_t oldest_index, HistoryEntry& output) const;

private:
    void ClearPresentation();
    void AddHistory(const Message& message, Origin origin);
    void Terminal(Result result);

    Queue queue_;
    Binding binding_;
    std::uint64_t highest_generation_;
    std::uint8_t draft_[MaxTextBytes + 1];
    std::size_t draft_size_;
    HistoryEntry history_[HistoryCapacity];
    std::size_t history_start_;
    std::size_t history_count_;
    bool composing_;
    bool muted_;
    bool suspended_;
    bool disconnect_required_;
    Result terminal_result_;
};

} // namespace CeTextChat
#endif
