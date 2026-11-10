// SPDX-License-Identifier: GPL-3.0-only
#include "../text_chat_owner.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace {
using namespace CeTextChat;
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
const std::uint8_t* Bytes(const char* text) {
    return reinterpret_cast<const std::uint8_t*>(text);
}
Binding Bind(std::uint64_t generation = 1, std::uint32_t epoch = 1,
             std::uint32_t slot = 0) { return {generation, epoch, slot, Schema}; }
template <typename T> bool Zero(const T& value) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    for (std::size_t i = 0; i != sizeof(value); ++i)
        if (bytes[i] != 0) return false;
    return true;
}
Result Draft(Owner& owner, const char* text) {
    Result result = owner.BeginCompose();
    return result == Result::Ok ? owner.Append(Bytes(text), std::strlen(text)) : result;
}
Result Send(Owner& owner, const char* text, std::uint64_t now, bool commit = true) {
    Result result = Draft(owner, text);
    if (result == Result::Ok) result = owner.SubmitDraft(now);
    if (result != Result::Ok || !commit) return result;
    Outgoing outgoing{};
    result = owner.PeekOutgoing(outgoing);
    if (result == Result::Ok)
        result = owner.CommitOutgoing(outgoing.binding, outgoing.sequence);
    Clear(outgoing);
    return result;
}
Result Remote(Owner& owner, std::uint64_t sequence, std::uint64_t now,
              const char* text = "remote", std::uint64_t generation = 1,
              std::uint32_t epoch = 1) {
    Frame frame{};
    Result result = Encode(Bytes(text), std::strlen(text), sequence, frame);
    if (result == Result::Ok)
        result = owner.Receive(generation, epoch, frame.bytes, frame.size, now);
    Clear(frame);
    return result;
}

void ClosedAndOutputs() {
    Owner owner;
    const auto state = owner.State();
    Check(!state.open && !state.composing && !state.disconnect_required &&
          state.terminal_result == Result::NotOpen, "closed-status");
    Check(owner.BeginCompose() == Result::NotOpen, "closed-composer");
    Decoded draft;
    std::memset(&draft, 1, sizeof(draft));
    Check(owner.CopyDraft(draft) == Result::NotOpen && Zero(draft), "closed-draft-zero");
    Outgoing outgoing;
    std::memset(&outgoing, 1, sizeof(outgoing));
    Check(owner.PeekOutgoing(outgoing) == Result::NotOpen && Zero(outgoing),
          "closed-outgoing-zero");
    HistoryEntry entry;
    std::memset(&entry, 1, sizeof(entry));
    Check(owner.HistoryAt(0, entry) == Result::Empty && Zero(entry), "empty-history-zero");
    Check(Remote(owner, 1, 0) == Result::NotOpen && !owner.State().disconnect_required,
          "closed-receive-no-false-fault");
    Binding invalid = Bind(); invalid.local_slot = 2;
    Check(owner.Open(invalid, 0) == Result::InvalidBinding, "invalid-slot");
    invalid = Bind(); invalid.text_schema = 0;
    Check(owner.Open(invalid, 0) == Result::Disabled, "disabled-profile");
    invalid.text_schema = Schema + 1;
    Check(owner.Open(invalid, 0) == Result::UpdateRequired, "unsupported-profile");
    owner.Close(); owner.Close();
    Check(!owner.State().open, "close-idempotent");
}

void Composition() {
    Owner owner;
    Check(owner.Open(Bind(), 0) == Result::Ok, "composition-open");
    Check(Draft(owner, u8"Aé🙂") == Result::Ok, "unicode-compose");
    Decoded draft{};
    Check(owner.CopyDraft(draft) == Result::Ok && draft.size == 7 && draft.scalars == 3,
          "unicode-lengths");
    Check(owner.Backspace() == Result::Ok && owner.CopyDraft(draft) == Result::Ok &&
          draft.size == 3 && draft.scalars == 2, "backspace-four-byte-scalar");
    Check(owner.Backspace() == Result::Ok && owner.CopyDraft(draft) == Result::Ok &&
          draft.size == 1 && draft.utf8[0] == 'A', "backspace-two-byte-scalar");
    const std::uint8_t overlong[] = {192, 175};
    Check(owner.Append(overlong, sizeof(overlong)) == Result::InvalidText, "overlong-refused");
    Check(owner.Append(Bytes("\n"), 1) == Result::InvalidText, "control-refused");
    Check(owner.Append(nullptr, 1) == Result::InvalidText, "null-input-refused");
    Check(owner.Append(Bytes("x"), std::numeric_limits<std::size_t>::max()) ==
          Result::InvalidText, "overflow-size-refused");
    Check(owner.CopyDraft(draft) == Result::Ok && draft.size == 1 && draft.utf8[0] == 'A',
          "invalid-edit-is-atomic");
    owner.CancelCompose();
    Check(owner.CopyDraft(draft) == Result::NotOpen && Zero(draft), "cancel-wipes-output");
    const std::string ascii(MaxScalars, 'x');
    Check(Draft(owner, ascii.c_str()) == Result::Ok, "scalar-limit-accepted");
    Check(owner.Append(Bytes("x"), 1) == Result::InvalidText, "scalar-overflow-refused");
    Check(owner.CopyDraft(draft) == Result::Ok && draft.size == MaxScalars,
          "scalar-refusal-preserves-draft");
    owner.CancelCompose();
    std::string multibyte;
    for (std::size_t i = 0; i != MaxScalars; ++i) multibyte += u8"é";
    Check(Draft(owner, multibyte.c_str()) == Result::Ok, "byte-limit-accepted");
    Check(owner.Append(Bytes("x"), 1) == Result::InvalidText, "byte-overflow-refused");
    Check(owner.CopyDraft(draft) == Result::Ok && draft.size == MaxTextBytes &&
          draft.utf8[MaxTextBytes] == 0, "byte-bound-and-terminator");
    owner.CancelCompose();
    Check(Draft(owner, "%s <tag>") == Result::Ok, "plain-text-punctuation");
    Check(owner.CopyDraft(draft) == Result::Ok &&
          std::memcmp(draft.utf8, "%s <tag>", 8) == 0, "plain-text-unchanged");
    Clear(draft);
}

void CommitAndSuspension() {
    Owner owner;
    Check(owner.Open(Bind(), 0) == Result::Ok, "commit-open");
    Check(Send(owner, "queued", 0, false) == Result::Ok, "submit-queues");
    Check(owner.State().outgoing == 1 && owner.State().history == 0 &&
          !owner.State().composing, "no-local-delivery-claim-at-submit");
    Outgoing first{}, second{};
    Check(owner.PeekOutgoing(first) == Result::Ok && owner.PeekOutgoing(second) == Result::Ok &&
          first.sequence == second.sequence && first.frame.size == second.frame.size &&
          std::memcmp(first.frame.bytes, second.frame.bytes, first.frame.size) == 0,
          "failed-transport-retains-exact-head");
    Binding stale = Bind(); ++stale.epoch;
    Check(owner.CommitOutgoing(stale, first.sequence) == Result::Stale &&
          owner.State().outgoing == 1, "wrong-binding-cannot-commit");
    Check(owner.CommitOutgoing(first.binding, first.sequence + 1) == Result::Stale &&
          owner.State().history == 0, "wrong-sequence-cannot-commit");
    Check(Draft(owner, "unfinished") == Result::Ok, "pending-composer");
    owner.SetSuspended(true);
    Check(!owner.State().composing && owner.State().suspended, "suspend-cancels-focus");
    Check(owner.BeginCompose() == Result::Backpressure, "suspend-blocks-compose");
    Check(owner.PeekOutgoing(second) == Result::Backpressure && Zero(second),
          "suspend-defers-outgoing");
    owner.SetSuspended(false);
    Check(owner.PeekOutgoing(second) == Result::Ok && second.sequence == first.sequence,
          "resume-retains-head");
    Check(owner.CommitOutgoing(first.binding, first.sequence) == Result::Ok,
          "accepted-transport-commit");
    HistoryEntry entry{};
    Check(owner.HistoryAt(0, entry) == Result::Ok &&
          entry.origin == Origin::LocalTransportAccepted && entry.message.sender_slot == 0 &&
          entry.message.text.size == 6, "local-history-has-limited-origin");
    Check(owner.CommitOutgoing(first.binding, first.sequence) == Result::Empty &&
          owner.State().history == 1, "commit-does-not-duplicate-history");
    Clear(first); Clear(second); Clear(entry);
}

void QuotasAndEpochs() {
    Owner owner;
    Check(owner.Open(Bind(10), 100) == Result::Ok, "quota-open");
    for (unsigned i = 0; i != BurstMessages; ++i)
        Check(Send(owner, "burst", 100) == Result::Ok, "burst-accepted");
    Check(Send(owner, "retained-draft", 100) == Result::RateLimited &&
          owner.State().composing, "rate-refusal-retains-composer");
    Check(owner.Open(Bind(10), 100) == Result::Ok && owner.State().composing,
          "same-open-does-not-clear-draft");
    Check(owner.SubmitDraft(100) == Result::RateLimited, "same-open-no-budget-refill");
    Check(owner.BeginEpoch(10, 2, 101) == Result::Ok && !owner.State().composing &&
          owner.State().history == 0 && owner.State().outgoing == 0,
          "epoch-clears-presentation-and-queued-text");
    Check(Send(owner, "next-epoch", 101) == Result::RateLimited,
          "epoch-keeps-session-rate-budget");
    Check(owner.BeginEpoch(10, 1, 102) == Result::Stale, "old-epoch-refused");
    Check(owner.BeginEpoch(9, 3, 102) == Result::Stale, "wrong-generation-epoch-refused");
    Binding changed = Bind(10, 2, 1);
    Check(owner.Open(changed, 102) == Result::InvalidBinding && owner.State().composing,
          "same-generation-slot-change-preserves-session");
    owner.CancelCompose();
    for (unsigned batch = 1; batch != 3; ++batch)
        for (unsigned i = 0; i != BurstMessages; ++i)
            Check(Send(owner, "window", 100 + batch * 2100) == Result::Ok,
                  "window-message-accepted");
    Check(Send(owner, "window-full", 6500) == Result::RateLimited, "long-window-rate-refused");
    Check(owner.SubmitDraft(6400) == Result::ClockRegression && owner.State().composing,
          "backward-clock-retains-draft");
    Check(owner.SubmitDraft(10201) == Result::Ok, "budget-expires-normally");
    owner.Close();
    Check(owner.Open(Bind(10), 10202) == Result::Stale, "retired-generation-no-refill");
    Check(owner.Open(Bind(9), 10202) == Result::Stale, "older-generation-refused");
    Check(owner.Open(Bind(11), 10202) == Result::Ok, "fresh-generation-accepted");
}

void QueueCapacity() {
    Owner owner;
    Check(owner.Open(Bind(), 0) == Result::Ok, "capacity-open");
    for (unsigned batch = 0; batch != 2; ++batch)
        for (unsigned i = 0; i != BurstMessages; ++i)
            Check(Send(owner, "pending", batch * 2100, false) == Result::Ok,
                  "bounded-pending-accepted");
    Check(owner.State().outgoing == OutgoingCapacity, "pending-capacity");
    Check(Send(owner, "overflow", 4200, false) == Result::Backpressure &&
          owner.State().outgoing == OutgoingCapacity && owner.State().composing,
          "pending-full-preserves-draft-and-old-heads");
    Outgoing outgoing{};
    Check(owner.PeekOutgoing(outgoing) == Result::Ok && outgoing.sequence == 1,
          "oldest-pending-retained");
    Check(owner.CommitOutgoing(outgoing.binding, outgoing.sequence) == Result::Ok &&
          owner.SubmitDraft(4200) == Result::Ok, "space-retry-accepts-same-draft");
    Clear(outgoing);
}

void PendingLifecycleAndInvalidRequests() {
    Owner owner;
    Check(owner.Open(Bind(), 100) == Result::Ok, "lifecycle-open");
    Check(owner.BeginCompose() == Result::Ok &&
          owner.SubmitDraft(100) == Result::InvalidText && owner.State().composing,
          "empty-submit-retains-focus");
    Check(Send(owner, "old-pending", 100, false) == Result::Ok &&
          Draft(owner, "old-draft") == Result::Ok, "lifecycle-pending-and-draft");
    Binding bad = Bind(); bad.local_slot = 3;
    Check(owner.Open(bad, 101) == Result::InvalidBinding &&
          owner.State().outgoing == 1 && owner.State().composing,
          "invalid-local-open-preserves-pending");
    Check(owner.Open(Bind(), 99) == Result::ClockRegression &&
          owner.State().outgoing == 1 && owner.State().composing,
          "same-open-clock-refusal-preserves-pending");
    Check(owner.Open(Bind(1, 2), 102) == Result::Ok &&
          owner.State().outgoing == 0 && !owner.State().composing,
          "open-new-epoch-clears-old-outgoing");
    Outgoing outgoing{};
    Check(owner.PeekOutgoing(outgoing) == Result::Empty && Zero(outgoing),
          "epoch-has-no-stale-head");
    Check(Remote(owner, 1, 102) == Result::Stale && !owner.State().disconnect_required,
          "old-epoch-frame-nonterminal");
    Check(Send(owner, "replacement-pending", 102, false) == Result::Ok,
          "new-epoch-queue");
    owner.SetMuted(true);
    Check(owner.Open(Bind(2, 1, 1), 103) == Result::Ok &&
          owner.State().outgoing == 0 && owner.State().history == 0 && owner.State().muted,
          "replacement-clears-text-preserves-mute-preference");
    Clear(outgoing);
}

void ReceiveMuteHistoryAndFaults() {
    Owner owner;
    Check(owner.Open(Bind(1, 1, 1), 0) == Result::Ok, "receive-open");
    Check(Remote(owner, 1, 0) == Result::Ok, "remote-receive");
    HistoryEntry entry{};
    Check(owner.HistoryAt(0, entry) == Result::Ok && entry.origin == Origin::Remote &&
          entry.message.sender_slot == 0, "sender-derived-from-binding");
    Check(Remote(owner, 1, 0) == Result::Duplicate && owner.State().history == 1,
          "duplicate-not-displayed");
    owner.SetMuted(true);
    Check(owner.State().history == 0, "mute-clears-visible-history");
    Check(Remote(owner, 2, 0) == Result::Ok && owner.State().history == 0,
          "muted-receive-not-displayed");
    owner.SetMuted(false);
    Check(Remote(owner, 2, 0) == Result::Duplicate && owner.State().history == 0,
          "unmute-cannot-replay-muted-text");
    owner.SetSuspended(true);
    Check(Remote(owner, 3, 0) == Result::Ok && owner.State().history == 0,
          "sync-receive-drained-without-display");
    owner.SetSuspended(false);
    Check(Remote(owner, 4, 0) == Result::Ok && owner.State().history == 1,
          "remote-after-unmute");
    owner.ClearHistory();
    Check(Remote(owner, 5, 0) == Result::RateLimited && owner.State().history == 0,
          "mute-history-sync-do-not-refill-receive-quota");
    Check(Remote(owner, 5, 2100) == Result::Duplicate && owner.State().history == 0,
          "rate-dropped-text-cannot-replay-later");
    Check(owner.Receive(99, 1, nullptr, 1, 0) == Result::Stale &&
          owner.State().open && !owner.State().disconnect_required,
          "stale-binding-does-not-parse-or-disconnect");
    Check(Draft(owner, "private-draft") == Result::Ok, "draft-before-terminal");
    Check(owner.Receive(1, 1, nullptr, 1, 2101) == Result::InvalidFrame &&
          owner.State().disconnect_required && !owner.State().open &&
          !owner.State().composing && owner.State().history == 0 &&
          owner.State().terminal_result == Result::InvalidFrame,
          "authenticated-malformation-requires-real-disconnect");
    Check(owner.Open(Bind(1, 1, 1), 2101) == Result::Stale, "terminal-generation-cannot-reopen");
    Check(owner.HistoryAt(0, entry) == Result::Empty && Zero(entry),
          "terminal-history-copy-cleared");
    Check(owner.Open(Bind(2, 1, 1), 2102) == Result::Ok && !owner.State().disconnect_required,
          "new-session-clears-terminal-latch");
    Frame frame{};
    Check(Encode(Bytes("schema"), 6, 1, frame) == Result::Ok, "schema-frame-control");
    frame.bytes[4] = static_cast<std::uint8_t>(Schema + 1);
    Check(owner.Receive(2, 1, frame.bytes, frame.size, 2102) == Result::UpdateRequired &&
          owner.State().disconnect_required, "authenticated-schema-requires-disconnect");
    Clear(frame); Clear(entry);
}

void HistoryBoundsAndDeparture() {
    Owner owner;
    Check(owner.Open(Bind(), 0) == Result::Ok, "history-open");
    for (std::uint64_t sequence = 1; sequence != HistoryCapacity + 6; ++sequence)
        Check(Remote(owner, sequence, sequence * 3000, "unique-retained-text") == Result::Ok,
              "paced-history-receive");
    Check(owner.State().history == HistoryCapacity, "history-bounded");
    HistoryEntry entry{};
    Check(owner.HistoryAt(0, entry) == Result::Ok && entry.message.text.sequence == 6,
          "history-evicts-oldest");
    Check(owner.HistoryAt(HistoryCapacity - 1, entry) == Result::Ok &&
          entry.message.text.sequence == HistoryCapacity + 5, "history-newest-retained");
    Check(owner.HistoryAt(HistoryCapacity, entry) == Result::Empty && Zero(entry),
          "history-index-bound-clears-output");
    Check(Draft(owner, "unique-retained-text") == Result::Ok, "departure-draft");
    owner.Close();
    const char* marker = "unique-retained-text";
    const auto* memory = reinterpret_cast<const unsigned char*>(&owner);
    bool found = false;
    for (std::size_t i = 0; i + std::strlen(marker) <= sizeof(owner); ++i)
        found = found || std::memcmp(memory + i, marker, std::strlen(marker)) == 0;
    Check(!found && owner.State().history == 0 && owner.State().outgoing == 0,
          "departure-wipes-owned-text");
    Check(owner.Open(Bind(2), 70000) == Result::Ok &&
          Remote(owner, 1, 70000, "fresh", 2) == Result::Ok, "new-generation-fresh-sequence");
    Check(owner.BeginEpoch(2, 2, 70001) == Result::Ok && owner.State().history == 0,
          "epoch-clears-remote-history");
    Clear(entry);
}
} // namespace

int main() {
    ClosedAndOutputs(); Composition(); CommitAndSuspension(); QuotasAndEpochs();
    QueueCapacity(); PendingLifecycleAndInvalidRequests();
    ReceiveMuteHistoryAndFaults(); HistoryBoundsAndDeparture();
    std::printf("checks=%u failures=%u\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
