// SPDX-License-Identifier: GPL-3.0-only
#include "text_chat.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
using namespace CeTextChat;
unsigned checks = 0;
unsigned failures = 0;

void Check(bool condition, const char* fixed_label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "failed: %s\n", fixed_label);
    }
}

template <class T> bool EmptyBytes(const T& value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    for (std::size_t i = 0; i < sizeof(value); ++i)
        if (bytes[i]) return false;
    return true;
}

const std::uint8_t text[] = {'r', 'e', 'a', 'd', 'y'};
Binding Bound(std::uint64_t generation = 17, std::uint32_t epoch = 4,
              std::uint32_t slot = 0) {
    return {generation, epoch, slot, Schema};
}

Frame Packet(std::uint64_t sequence) {
    Frame frame = {};
    Check(Encode(text, sizeof(text), sequence, frame) == Result::Ok, "fixture_encode");
    return frame;
}

Result Receive(Queue& queue, std::uint64_t sequence, std::uint64_t now,
               const Binding& binding = Bound()) {
    Frame frame = Packet(sequence);
    const Result result = queue.Receive(binding.generation, binding.epoch,
                                        frame.bytes, frame.size, now);
    Clear(frame);
    return result;
}

void SubmitAndCommit(Queue& queue, std::uint64_t now) {
    Check(queue.Submit(text, sizeof(text), now) == Result::Ok, "submit_before_commit");
    Outgoing outgoing = {};
    Check(queue.PeekOutgoing(outgoing) == Result::Ok, "peek_before_commit");
    Check(queue.CommitOutgoing(outgoing.binding, outgoing.sequence) == Result::Ok,
          "transport_accept_commit");
    Clear(outgoing);
}

void Codec() {
    Frame frame = Packet(1);
    Decoded decoded = {};
    Check(frame.size == HeaderBytes + sizeof(text), "exact_frame_length");
    Check(frame.bytes[0] == 'C' && frame.bytes[3] == 'X' && frame.bytes[4] == Schema &&
          frame.bytes[5] == 0 && frame.bytes[6] == sizeof(text) && frame.bytes[7] == 0 &&
          frame.bytes[8] == 1 && frame.bytes[15] == 0, "fixed_header_layout");
    Check(Decode(frame.bytes, frame.size, decoded) == Result::Ok, "ascii_decode");
    Check(decoded.sequence == 1 && decoded.size == sizeof(text) &&
          decoded.scalars == sizeof(text) && !decoded.utf8[decoded.size] &&
          !std::memcmp(decoded.utf8, text, sizeof(text)), "owned_plain_text");

    const std::uint8_t unicode[] = {194, 162, 226, 130, 172, 240, 159, 154, 128};
    Check(Encode(unicode, sizeof(unicode), std::numeric_limits<std::uint64_t>::max(),
                 frame) == Result::Ok, "unicode_encode");
    Check(Decode(frame.bytes, frame.size, decoded) == Result::Ok && decoded.scalars == 3 &&
          decoded.sequence == std::numeric_limits<std::uint64_t>::max(),
          "unicode_and_sequence_boundaries");
    const std::uint8_t literal[] = {'%', 's', '%', 'n', '<', 'b', '>'};
    Check(Encode(literal, sizeof(literal), 2, frame) == Result::Ok &&
          Decode(frame.bytes, frame.size, decoded) == Result::Ok &&
          !std::memcmp(decoded.utf8, literal, sizeof(literal)), "formatting_is_literal_data");

    std::vector<std::uint8_t> maximum;
    for (unsigned i = 0; i < MaxScalars; ++i) {
        maximum.push_back(194);
        maximum.push_back(162);
    }
    Check(maximum.size() == MaxTextBytes &&
          Encode(maximum.data(), maximum.size(), 3, frame) == Result::Ok,
          "maximum_bytes_and_scalars");
    Check(frame.size == MaxFrameBytes && Decode(frame.bytes, frame.size, decoded) == Result::Ok &&
          decoded.size == MaxTextBytes && decoded.scalars == MaxScalars &&
          !decoded.utf8[MaxTextBytes], "maximum_round_trip_no_truncation");
    maximum.push_back('a');
    Check(Encode(maximum.data(), maximum.size(), 4, frame) == Result::InvalidText &&
          EmptyBytes(frame), "over_byte_limit_refused_empty");
    std::vector<std::uint8_t> too_many(MaxScalars + 1, 'a');
    Check(Encode(too_many.data(), too_many.size(), 4, frame) == Result::InvalidText,
          "over_scalar_limit_refused");

    const std::vector<std::vector<std::uint8_t>> invalid = {
        {}, {128}, {191}, {192, 175}, {193, 191}, {194}, {194, 32},
        {224, 128, 175}, {226, 130}, {237, 160, 128}, {240, 128, 128, 175},
        {244, 144, 128, 128}, {245, 128, 128, 128}, {254}, {255},
        {226, 128, 168}, {226, 128, 169}, {'a', 0, 'b'}
    };
    for (const auto& bytes : invalid) {
        std::uint32_t scalars = 99;
        Check(ValidateText(bytes.data(), bytes.size(), scalars) == Result::InvalidText &&
              scalars == 0, "invalid_utf8_or_line_separator");
    }
    for (unsigned control = 0; control < 32; ++control) {
        const std::uint8_t byte = static_cast<std::uint8_t>(control);
        std::uint32_t scalars = 99;
        Check(ValidateText(&byte, 1, scalars) == Result::InvalidText && !scalars,
              "c0_controls_refused");
    }
    for (unsigned control = 127; control <= 159; ++control) {
        const std::uint8_t bytes[] = {194, static_cast<std::uint8_t>(control)};
        const std::uint8_t single = 127;
        std::uint32_t scalars = 99;
        Check(ValidateText(control == 127 ? &single : bytes, control == 127 ? 1 : 2,
                           scalars) == Result::InvalidText, "c1_controls_refused");
    }
    const std::vector<std::vector<std::uint8_t>> valid = {
        {' '}, {194, 160}, {237, 159, 191}, {238, 128, 128}, {244, 143, 191, 191},
        {226, 128, 141}
    };
    for (const auto& bytes : valid) {
        std::uint32_t scalars = 0;
        Check(ValidateText(bytes.data(), bytes.size(), scalars) == Result::Ok && scalars == 1,
              "valid_scalar_boundaries");
    }
    std::uint32_t scalars = 99;
    Check(ValidateText(nullptr, 1, scalars) == Result::InvalidText && !scalars,
          "null_text_refused");
    Check(Encode(text, sizeof(text), 0, frame) == Result::InvalidFrame && EmptyBytes(frame),
          "zero_sequence_refused");

    const Frame good = Packet(9);
    const std::vector<std::size_t> mutations = {0, 3, 5, 6, 7};
    for (std::size_t offset : mutations) {
        frame = good;
        frame.bytes[offset] ^= 1;
        std::memset(&decoded, 33, sizeof(decoded));
        Check(Decode(frame.bytes, frame.size, decoded) == Result::InvalidFrame &&
              EmptyBytes(decoded), "header_corruption_refused_empty");
    }
    frame = good;
    frame.bytes[4] = 2;
    Check(Decode(frame.bytes, frame.size, decoded) == Result::UpdateRequired &&
          EmptyBytes(decoded), "unknown_schema_requires_update");
    frame.bytes[4] = 0;
    Check(Decode(frame.bytes, frame.size, decoded) == Result::UpdateRequired,
          "disabled_schema_not_wire_schema");
    frame = good;
    std::memset(frame.bytes + 8, 0, 8);
    Check(Decode(frame.bytes, frame.size, decoded) == Result::InvalidFrame,
          "wire_zero_sequence_refused");
    for (std::size_t size = 0; size < good.size; ++size)
        Check(Decode(good.bytes, size, decoded) == Result::InvalidFrame && EmptyBytes(decoded),
              "every_truncation_refused");
    frame = good;
    ++frame.size;
    Check(Decode(frame.bytes, frame.size, decoded) == Result::InvalidFrame,
          "trailing_byte_refused");
    Check(Decode(nullptr, good.size, decoded) == Result::InvalidFrame && EmptyBytes(decoded),
          "null_frame_refused");
    Check(Decode(good.bytes, MaxFrameBytes + 1, decoded) == Result::InvalidFrame,
          "oversize_refused_before_read");
    frame = good;
    frame.bytes[HeaderBytes] = 0;
    Check(Decode(frame.bytes, frame.size, decoded) == Result::InvalidFrame && EmptyBytes(decoded),
          "wire_text_validated");
    Clear(frame);
    Clear(decoded);
    Check(EmptyBytes(frame) && EmptyBytes(decoded), "explicit_outputs_clear");
}

void Sending() {
    Queue queue;
    Outgoing outgoing;
    std::memset(&outgoing, 33, sizeof(outgoing));
    Check(queue.PeekOutgoing(outgoing) == Result::NotOpen && EmptyBytes(outgoing),
          "closed_peek_empty");
    Check(queue.Submit(text, sizeof(text), 0) == Result::NotOpen, "closed_submit_refused");
    Binding binding = Bound();
    binding.text_schema = 0;
    Check(queue.Open(binding, 0) == Result::Disabled && !queue.IsOpen(), "disabled_profile");
    binding.text_schema = 2;
    Check(queue.Open(binding, 0) == Result::UpdateRequired && !queue.IsOpen(),
          "profile_mismatch_requires_update");
    binding = Bound(0);
    Check(queue.Open(binding, 0) == Result::InvalidBinding, "zero_generation_refused");
    binding = Bound(17, 4, 2);
    Check(queue.Open(binding, 0) == Result::InvalidBinding, "unadmitted_slot_refused");
    binding = Bound();
    Check(queue.Open(binding, 0) == Result::Ok, "open_trusted_binding");
    for (unsigned i = 0; i < BurstMessages; ++i)
        Check(queue.Submit(text, sizeof(text), 0) == Result::Ok, "outgoing_burst");
    Check(queue.Open(binding, 0) == Result::Ok && queue.OutgoingSize() == BurstMessages,
          "repeated_open_preserves_queue");
    Check(queue.Submit(text, sizeof(text), 0) == Result::RateLimited,
          "repeated_open_cannot_reset_rate");
    Check(queue.PeekOutgoing(outgoing) == Result::Ok && outgoing.sequence == 1,
          "first_head_available");
    Outgoing repeated = {};
    Check(queue.PeekOutgoing(repeated) == Result::Ok &&
          repeated.frame.size == outgoing.frame.size &&
          !std::memcmp(repeated.frame.bytes, outgoing.frame.bytes, outgoing.frame.size),
          "transport_backpressure_keeps_exact_head");
    Binding stale = binding;
    ++stale.generation;
    Check(queue.CommitOutgoing(stale, outgoing.sequence) == Result::Stale &&
          queue.OutgoingSize() == BurstMessages, "stale_send_commit_refused");
    Check(queue.CommitOutgoing(binding, outgoing.sequence + 1) == Result::Stale,
          "wrong_head_commit_refused");
    Check(queue.CommitOutgoing(binding, outgoing.sequence) == Result::Ok &&
          queue.OutgoingSize() == BurstMessages - 1, "accepted_send_removes_one");
    Check(queue.Submit(text, sizeof(text), 1999) == Result::RateLimited,
          "burst_boundary_before");
    Check(queue.Submit(text, sizeof(text), 2000) == Result::Ok, "burst_boundary_exact");
    Check(queue.Submit(text, sizeof(text), 1999) == Result::ClockRegression,
          "clock_rollback_does_not_reset_budget");

    Check(queue.Open(Bound(18), 0) == Result::Ok && queue.OutgoingSize() == 0,
          "fresh_generation_clears_old_queue");
    for (unsigned batch = 0; batch < 3; ++batch)
        for (unsigned i = 0; i < 4; ++i) SubmitAndCommit(queue, batch * 2000);
    Check(queue.Submit(text, sizeof(text), 6000) == Result::RateLimited,
          "sustained_rate_bound");
    Check(queue.Submit(text, sizeof(text), 9999) == Result::RateLimited,
          "sustained_boundary_before");
    Check(queue.Submit(text, sizeof(text), 10000) == Result::Ok,
          "sustained_boundary_exact");

    Check(queue.Open(Bound(19), 0) == Result::Ok, "new_capacity_fixture");
    for (unsigned i = 0; i < 8; ++i)
        Check(queue.Submit(text, sizeof(text), (i / 4) * 2000) == Result::Ok,
              "outgoing_capacity_fill");
    Check(queue.OutgoingSize() == OutgoingCapacity &&
          queue.Submit(text, sizeof(text), 4000) == Result::Backpressure,
          "outgoing_full_refuses_newest");
    Check(queue.PeekOutgoing(outgoing) == Result::Ok && outgoing.sequence == 1,
          "full_queue_preserves_oldest");
    Check(queue.CommitOutgoing(outgoing.binding, outgoing.sequence) == Result::Ok &&
          queue.Submit(text, sizeof(text), 4000) == Result::Ok,
          "retry_after_capacity_available");
    for (std::uint64_t sequence = 2; sequence <= 9; ++sequence) {
        Check(queue.PeekOutgoing(outgoing) == Result::Ok && outgoing.sequence == sequence,
              "no_sequence_consumed_on_refusal");
        Check(queue.CommitOutgoing(outgoing.binding, outgoing.sequence) == Result::Ok,
              "ordered_send_commit");
    }
    Check(queue.PeekOutgoing(outgoing) == Result::Empty && EmptyBytes(outgoing),
          "empty_outgoing_has_no_previous_text");
    Check(queue.Submit(nullptr, 1, 4000) == Result::InvalidText,
          "invalid_local_text_is_nonterminal");
    Check(queue.IsOpen(), "invalid_local_text_preserves_session");
    Clear(outgoing);
    Clear(repeated);
}

void Receiving() {
    Queue queue;
    Check(queue.Open(Bound(), 0) == Result::Ok, "receive_open");
    Check(queue.Receive(99, 4, nullptr, 99999, 0) == Result::Stale && queue.IsOpen(),
          "stale_generation_before_decode");
    Check(queue.Receive(17, 3, nullptr, 99999, 0) == Result::Stale && queue.IsOpen(),
          "stale_epoch_before_decode");
    Check(Receive(queue, 1, 0) == Result::Ok, "admitted_remote_message");
    Check(Receive(queue, 1, 0) == Result::Duplicate, "duplicate_suppressed");
    Message message = {};
    Check(queue.Pop(message) == Result::Ok && message.sender_slot == 1 &&
          message.binding.generation == 17 && message.binding.epoch == 4 &&
          message.text.sequence == 1, "remote_identity_from_binding");
    Check(queue.Pop(message) == Result::Empty && EmptyBytes(message),
          "empty_receive_has_no_previous_text");
    Check(Receive(queue, 100, 0) == Result::Ok &&
          Receive(queue, 99, 0) == Result::Duplicate, "old_sequence_after_gap_suppressed");
    Check(queue.Open(Bound(18, 4, 1), 0) == Result::Ok && queue.IncomingSize() == 0,
          "new_generation_retires_received_text");
    Check(Receive(queue, 1, 0, Bound(18, 4, 1)) == Result::Ok &&
          queue.Pop(message) == Result::Ok && message.sender_slot == 0,
          "opposite_admitted_remote_slot");

    Check(queue.Open(Bound(19), 0) == Result::Ok, "receive_rate_fixture");
    for (unsigned i = 1; i <= 4; ++i)
        Check(Receive(queue, i, 0, Bound(19)) == Result::Ok, "incoming_burst");
    Check(Receive(queue, 5, 0, Bound(19)) == Result::RateLimited && queue.IncomingSize() == 4,
          "incoming_rate_drops_newest");
    Check(Receive(queue, 5, 2000, Bound(19)) == Result::Duplicate,
          "rate_dropped_frame_cannot_replay");
    Check(Receive(queue, 6, 2000, Bound(19)) == Result::Ok, "receive_after_rate_window");
    Check(queue.Submit(text, sizeof(text), 2000) == Result::Ok,
          "direction_budgets_are_independent");

    Check(queue.Open(Bound(20), 0) == Result::Ok, "receive_capacity_fixture");
    for (unsigned i = 1; i <= 16; ++i) {
        const std::uint64_t now = i <= 12 ? ((i - 1) / 4) * 2500 : 10000;
        Check(Receive(queue, i, now, Bound(20)) == Result::Ok, "incoming_capacity_fill");
    }
    Check(queue.IncomingSize() == IncomingCapacity &&
          Receive(queue, 17, 12500, Bound(20)) == Result::Backpressure,
          "incoming_full_drops_newest");
    Check(queue.Pop(message) == Result::Ok && message.text.sequence == 1,
          "incoming_full_preserves_oldest");
    Check(Receive(queue, 17, 12500, Bound(20)) == Result::Duplicate,
          "capacity_dropped_frame_cannot_replay");
    Check(Receive(queue, 18, 12500, Bound(20)) == Result::Ok,
          "new_frame_after_capacity_available");
    for (unsigned sequence = 2; sequence <= 16; ++sequence)
        Check(queue.Pop(message) == Result::Ok && message.text.sequence == sequence,
              "incoming_order_preserved");
    Check(queue.Pop(message) == Result::Ok && message.text.sequence == 18 &&
          queue.Pop(message) == Result::Empty, "dropped_text_not_delivered_later");

    Check(queue.Open(Bound(21), 100) == Result::Ok, "receive_clock_fixture");
    Check(Receive(queue, 1, 99, Bound(21)) == Result::ClockRegression && queue.IsOpen(),
          "incoming_clock_rollback_refused");
    Check(Receive(queue, 1, 100, Bound(21)) == Result::Ok,
          "clock_refusal_did_not_consume_sequence");
    Clear(message);
}

void Lifecycle() {
    Queue queue;
    Check(queue.Open(Bound(), 0) == Result::Ok, "lifecycle_open");
    for (unsigned i = 1; i <= 4; ++i) {
        Check(queue.Submit(text, sizeof(text), 0) == Result::Ok, "epoch_outgoing_fixture");
        Check(Receive(queue, i, 0) == Result::Ok, "epoch_incoming_fixture");
    }
    Check(queue.BeginEpoch(17, 5, 0) == Result::Ok && !queue.IncomingSize() &&
          !queue.OutgoingSize(), "epoch_transition_clears_both_queues");
    Check(queue.Submit(text, sizeof(text), 0) == Result::RateLimited &&
          Receive(queue, 1, 0, Bound(17, 5)) == Result::RateLimited,
          "epoch_transition_preserves_rate_budgets");
    Check(queue.BeginEpoch(17, 4, 0) == Result::Stale &&
          queue.BeginEpoch(99, 6, 0) == Result::Stale, "stale_epoch_transition_refused");
    Check(Receive(queue, 2, 2000, Bound(17, 5)) == Result::Ok,
          "new_epoch_sequence_space");
    Check(queue.BeginEpoch(17, 5, 2000) == Result::Ok && queue.IncomingSize() == 1,
          "same_epoch_is_idempotent");
    Check(queue.Open(Bound(17, 6), 2000) == Result::InvalidBinding && !queue.IsOpen(),
          "same_generation_rebinding_is_terminal");

    Check(queue.Open(Bound(18), 0) == Result::Ok, "malformed_frame_fixture");
    Check(queue.Submit(text, sizeof(text), 0) == Result::Ok &&
          Receive(queue, 1, 0, Bound(18)) == Result::Ok, "queued_before_protocol_failure");
    Frame bad = Packet(2);
    bad.bytes[5] = 1;
    Check(queue.Receive(18, 4, bad.bytes, bad.size, 0) == Result::InvalidFrame &&
          !queue.IsOpen() && !queue.IncomingSize() && !queue.OutgoingSize(),
          "malformed_remote_frame_closes_and_clears");
    Message message;
    std::memset(&message, 33, sizeof(message));
    Check(queue.Pop(message) == Result::NotOpen && EmptyBytes(message),
          "failed_session_cannot_deliver_text");
    Check(queue.Open(Bound(19), 0) == Result::Ok &&
          queue.Submit(text, sizeof(text), 0) == Result::Ok, "schema_failure_fixture");
    bad = Packet(1);
    bad.bytes[4] = 2;
    Check(queue.Receive(19, 4, bad.bytes, bad.size, 0) == Result::UpdateRequired &&
          !queue.IsOpen() && !queue.OutgoingSize(), "schema_failure_requires_safe_disconnect");
    Check(queue.Open(Bound(20), 0) == Result::Ok &&
          queue.Submit(text, sizeof(text), 0) == Result::Ok &&
          Receive(queue, 1, 0, Bound(20)) == Result::Ok, "disconnect_fixture");
    queue.Close();
    queue.Close();
    Check(!queue.IsOpen() && !queue.IncomingSize() && !queue.OutgoingSize(),
          "disconnect_idempotently_clears_both_queues");
    Check(Receive(queue, 2, 0, Bound(20)) == Result::NotOpen,
          "late_frame_after_disconnect_refused");
    Check(queue.Open(Bound(21), 0) == Result::Ok &&
          Receive(queue, 1, 0, Bound(21)) == Result::Ok, "fresh_session_after_disconnect");
    Clear(bad);
    Clear(message);
}

} // namespace

int main() {
    Codec();
    Sending();
    Receiving();
    Lifecycle();
    // Fixed labels/counts only; no message bodies, identities or packet dumps.
    std::printf("{\"checks\":%u,\"failures\":%u}\n", checks, failures);
    return failures ? 1 : 0;
}
