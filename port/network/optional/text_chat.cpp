// SPDX-License-Identifier: GPL-3.0-only
#include "../text_chat.h"

#include <cstring>
#include <limits>

namespace CeTextChat {
namespace {

void Wipe(void* address, std::size_t size) {
    volatile std::uint8_t* bytes = static_cast<volatile std::uint8_t*>(address);
    while (size--) *bytes++ = 0;
}

bool SameBinding(const Binding& left, const Binding& right) {
    return left.generation == right.generation && left.epoch == right.epoch &&
           left.local_slot == right.local_slot &&
           left.text_schema == right.text_schema;
}

std::uint64_t ReadSequence(const std::uint8_t* bytes) {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i)
        result |= static_cast<std::uint64_t>(bytes[8 + i]) << (i * 8);
    return result;
}

Result Reject(Decoded& output) {
    Clear(output);
    return Result::InvalidFrame;
}

} // namespace

void Clear(Frame& value) { Wipe(&value, sizeof(value)); }
void Clear(Decoded& value) { Wipe(&value, sizeof(value)); }
void Clear(Outgoing& value) { Wipe(&value, sizeof(value)); }
void Clear(Message& value) { Wipe(&value, sizeof(value)); }

Result ValidateText(const std::uint8_t* text, std::size_t size,
                    std::uint32_t& scalar_count) {
    scalar_count = 0;
    if (!text || size == 0 || size > MaxTextBytes) return Result::InvalidText;
    std::uint32_t count = 0;
    for (std::size_t offset = 0; offset < size;) {
        const std::uint8_t first = text[offset++];
        std::uint32_t scalar = 0;
        std::uint32_t minimum = 0;
        unsigned following = 0;
        if (first <= 127) scalar = first;
        else if (first >= 194 && first <= 223) {
            scalar = first & 31;
            minimum = 128;
            following = 1;
        } else if (first >= 224 && first <= 239) {
            scalar = first & 15;
            minimum = 2048;
            following = 2;
        } else if (first >= 240 && first <= 244) {
            scalar = first & 7;
            minimum = 65536;
            following = 3;
        } else return Result::InvalidText;
        if (size - offset < following) return Result::InvalidText;
        for (unsigned i = 0; i < following; ++i) {
            const std::uint8_t byte = text[offset++];
            if (byte < 128 || byte > 191) return Result::InvalidText;
            scalar = (scalar << 6) | (byte & 63);
        }
        if (scalar < minimum || scalar > 1114111 ||
            (scalar >= 55296 && scalar <= 57343) || scalar < 32 ||
            (scalar >= 127 && scalar <= 159) ||
            scalar == 8232 || scalar == 8233 || ++count > MaxScalars)
            return Result::InvalidText;
    }
    scalar_count = count;
    return Result::Ok;
}

Result Encode(const std::uint8_t* text, std::size_t size,
              std::uint64_t sequence, Frame& output) {
    std::uint32_t count = 0;
    if (ValidateText(text, size, count) != Result::Ok) {
        Clear(output);
        return Result::InvalidText;
    }
    if (!sequence) {
        Clear(output);
        return Result::InvalidFrame;
    }
    Frame temporary = {};
    temporary.bytes[0] = 'C';
    temporary.bytes[1] = 'E';
    temporary.bytes[2] = 'T';
    temporary.bytes[3] = 'X';
    temporary.bytes[4] = static_cast<std::uint8_t>(Schema);
    temporary.bytes[6] = static_cast<std::uint8_t>(size);
    temporary.bytes[7] = static_cast<std::uint8_t>(size >> 8);
    for (unsigned i = 0; i < 8; ++i)
        temporary.bytes[8 + i] = static_cast<std::uint8_t>(sequence >> (i * 8));
    std::memcpy(temporary.bytes + HeaderBytes, text, size);
    temporary.size = static_cast<std::uint32_t>(HeaderBytes + size);
    Clear(output);
    output = temporary;
    Clear(temporary);
    return Result::Ok;
}

Result Decode(const std::uint8_t* bytes, std::size_t size, Decoded& output) {
    if (!bytes || size <= HeaderBytes || size > MaxFrameBytes)
        return Reject(output);
    if (bytes[0] != 'C' || bytes[1] != 'E' || bytes[2] != 'T' || bytes[3] != 'X')
        return Reject(output);
    if (bytes[4] != Schema) {
        Clear(output);
        return Result::UpdateRequired;
    }
    const std::size_t text_size = static_cast<std::size_t>(bytes[6]) |
                                  (static_cast<std::size_t>(bytes[7]) << 8);
    if (bytes[5] || text_size != size - HeaderBytes || !ReadSequence(bytes))
        return Reject(output);
    Decoded temporary = {};
    if (ValidateText(bytes + HeaderBytes, text_size, temporary.scalars) != Result::Ok)
        return Reject(output);
    temporary.sequence = ReadSequence(bytes);
    temporary.size = static_cast<std::uint32_t>(text_size);
    std::memcpy(temporary.utf8, bytes + HeaderBytes, text_size);
    Clear(output);
    output = temporary;
    Clear(temporary);
    return Result::Ok;
}

Queue::Queue() { Close(); }
Queue::~Queue() { Close(); }

void Queue::ClearQueues() {
    for (Frame& frame : outgoing_) Clear(frame);
    for (Message& message : incoming_) Clear(message);
    outgoing_head_ = outgoing_count_ = incoming_head_ = incoming_count_ = 0;
    next_sequence_ = 1;
    highest_received_ = 0;
}

void Queue::Close() {
    ClearQueues();
    Wipe(&binding_, sizeof(binding_));
    Wipe(&outgoing_window_, sizeof(outgoing_window_));
    Wipe(&incoming_window_, sizeof(incoming_window_));
    open_ = false;
    last_now_ = 0;
}

bool Queue::CheckClock(std::uint64_t now) {
    if (now < last_now_) return false;
    last_now_ = now;
    return true;
}

bool Queue::Window::Allows(std::uint64_t now) {
    std::size_t kept = 0;
    std::size_t recent = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (now - accepted[i] >= WindowMilliseconds) continue;
        accepted[kept++] = accepted[i];
        if (now - accepted[i] < BurstMilliseconds) ++recent;
    }
    count = kept;
    return count < WindowMessages && recent < BurstMessages;
}

void Queue::Window::Add(std::uint64_t now) { accepted[count++] = now; }

Result Queue::Open(const Binding& binding, std::uint64_t monotonic_ms) {
    if (binding.text_schema == 0) {
        Close();
        return Result::Disabled;
    }
    if (binding.text_schema != Schema) {
        Close();
        return Result::UpdateRequired;
    }
    if (!binding.generation || binding.local_slot > 1) {
        Close();
        return Result::InvalidBinding;
    }
    if (open_ && binding.generation == binding_.generation) {
        if (!SameBinding(binding, binding_)) {
            Close();
            return Result::InvalidBinding;
        }
        return CheckClock(monotonic_ms) ? Result::Ok : Result::ClockRegression;
    }
    Close();
    binding_ = binding;
    last_now_ = monotonic_ms;
    open_ = true;
    return Result::Ok;
}

Result Queue::BeginEpoch(std::uint64_t generation, std::uint32_t epoch,
                         std::uint64_t monotonic_ms) {
    if (!open_) return Result::NotOpen;
    if (generation != binding_.generation || epoch < binding_.epoch)
        return Result::Stale;
    if (!CheckClock(monotonic_ms)) return Result::ClockRegression;
    if (epoch != binding_.epoch) {
        ClearQueues();
        binding_.epoch = epoch;
    }
    return Result::Ok;
}

Result Queue::Submit(const std::uint8_t* text, std::size_t size,
                     std::uint64_t monotonic_ms) {
    if (!open_) return Result::NotOpen;
    if (!CheckClock(monotonic_ms)) return Result::ClockRegression;
    if (!next_sequence_) return Result::SequenceExhausted;
    Frame candidate = {};
    const Result encoded = Encode(text, size, next_sequence_, candidate);
    if (encoded != Result::Ok) return encoded;
    Result result = Result::Ok;
    if (outgoing_count_ == OutgoingCapacity) result = Result::Backpressure;
    else if (!outgoing_window_.Allows(monotonic_ms)) result = Result::RateLimited;
    else {
        outgoing_[(outgoing_head_ + outgoing_count_) % OutgoingCapacity] = candidate;
        ++outgoing_count_;
        outgoing_window_.Add(monotonic_ms);
        next_sequence_ = next_sequence_ == std::numeric_limits<std::uint64_t>::max()
                             ? 0 : next_sequence_ + 1;
    }
    Clear(candidate);
    return result;
}

Result Queue::PeekOutgoing(Outgoing& output) const {
    Clear(output);
    if (!open_) return Result::NotOpen;
    if (!outgoing_count_) return Result::Empty;
    output.binding = binding_;
    output.frame = outgoing_[outgoing_head_];
    output.sequence = ReadSequence(output.frame.bytes);
    return Result::Ok;
}

Result Queue::CommitOutgoing(const Binding& binding, std::uint64_t sequence) {
    if (!open_) return Result::NotOpen;
    if (!SameBinding(binding, binding_)) return Result::Stale;
    if (!outgoing_count_) return Result::Empty;
    if (sequence != ReadSequence(outgoing_[outgoing_head_].bytes))
        return Result::Stale;
    Clear(outgoing_[outgoing_head_]);
    outgoing_head_ = (outgoing_head_ + 1) % OutgoingCapacity;
    --outgoing_count_;
    return Result::Ok;
}

Result Queue::Receive(std::uint64_t generation, std::uint32_t epoch,
                      const std::uint8_t* bytes, std::size_t size,
                      std::uint64_t monotonic_ms) {
    if (!open_) return Result::NotOpen;
    if (generation != binding_.generation || epoch != binding_.epoch)
        return Result::Stale;
    if (!CheckClock(monotonic_ms)) return Result::ClockRegression;
    Decoded decoded = {};
    const Result result = Decode(bytes, size, decoded);
    if (result != Result::Ok) {
        Close();
        return result;
    }
    if (decoded.sequence <= highest_received_) {
        Clear(decoded);
        return Result::Duplicate;
    }
    // Even a dropped reliable frame has been consumed. Do not let a replay of
    // it become new text once capacity or a rate window becomes available.
    highest_received_ = decoded.sequence;
    Result delivery = Result::Ok;
    if (!incoming_window_.Allows(monotonic_ms)) delivery = Result::RateLimited;
    else {
        incoming_window_.Add(monotonic_ms);
        if (incoming_count_ == IncomingCapacity) delivery = Result::Backpressure;
        else {
            Message& message = incoming_[(incoming_head_ + incoming_count_) % IncomingCapacity];
            message.binding = binding_;
            message.sender_slot = binding_.local_slot ^ 1U;
            message.text = decoded;
            ++incoming_count_;
        }
    }
    Clear(decoded);
    return delivery;
}

Result Queue::Pop(Message& output) {
    Clear(output);
    if (!open_) return Result::NotOpen;
    if (!incoming_count_) return Result::Empty;
    output = incoming_[incoming_head_];
    Clear(incoming_[incoming_head_]);
    incoming_head_ = (incoming_head_ + 1) % IncomingCapacity;
    --incoming_count_;
    return Result::Ok;
}

} // namespace CeTextChat
