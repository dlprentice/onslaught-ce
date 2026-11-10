// SPDX-License-Identifier: GPL-3.0-only
#include "../text_chat_owner.h"

#include <cstring>

namespace CeTextChat {
namespace {
void Wipe(void* value, std::size_t size) {
    volatile std::uint8_t* bytes = static_cast<volatile std::uint8_t*>(value);
    while (size != 0) {
        *bytes++ = 0;
        --size;
    }
}

bool SameBinding(const Binding& left, const Binding& right) {
    return left.generation == right.generation && left.epoch == right.epoch &&
           left.local_slot == right.local_slot &&
           left.text_schema == right.text_schema;
}
} // namespace

void Clear(HistoryEntry& value) { Wipe(&value, sizeof(value)); }

Owner::Owner()
    : binding_{}, highest_generation_(0), draft_{}, draft_size_(0), history_{},
      history_start_(0), history_count_(0), composing_(false), muted_(false),
      suspended_(false), disconnect_required_(false),
      terminal_result_(Result::NotOpen) {}

Owner::~Owner() { Close(); }

Result Owner::Open(const Binding& binding, std::uint64_t now) {
    // Validate before touching the live queue. Local bad requests must not
    // discard an existing session, draft, quota or queued outgoing frame.
    if (binding.generation == 0 || binding.epoch == 0 || binding.local_slot > 1)
        return Result::InvalidBinding;
    if (binding.text_schema == 0) return Result::Disabled;
    if (binding.text_schema != Schema) return Result::UpdateRequired;
    if (queue_.IsOpen() && binding.generation == binding_.generation) {
        if (binding.local_slot != binding_.local_slot ||
            binding.text_schema != binding_.text_schema)
            return Result::InvalidBinding;
        return BeginEpoch(binding.generation, binding.epoch, now);
    }
    if (binding.generation <= highest_generation_) return Result::Stale;
    const Result result = queue_.Open(binding, now);
    if (result != Result::Ok) return result;
    ClearPresentation();
    binding_ = binding;
    highest_generation_ = binding.generation;
    suspended_ = false;
    disconnect_required_ = false;
    terminal_result_ = Result::NotOpen;
    return Result::Ok;
}

Result Owner::BeginEpoch(std::uint64_t generation, std::uint32_t epoch,
                         std::uint64_t now) {
    if (!queue_.IsOpen()) return Result::NotOpen;
    if (generation != binding_.generation || epoch < binding_.epoch)
        return Result::Stale;
    if (epoch == binding_.epoch) return queue_.Open(binding_, now);
    const Result result = queue_.BeginEpoch(generation, epoch, now);
    if (result == Result::Ok) {
        ClearPresentation();
        binding_.epoch = epoch;
    }
    return result;
}

void Owner::Close() {
    queue_.Close();
    ClearPresentation();
    Wipe(&binding_, sizeof(binding_));
    suspended_ = false;
    disconnect_required_ = false;
    terminal_result_ = Result::NotOpen;
    // highest_generation_ and the local mute preference intentionally survive.
}

void Owner::SetSuspended(bool suspended) {
    suspended_ = suspended;
    if (suspended) CancelCompose();
}

void Owner::SetMuted(bool muted) {
    muted_ = muted;
    if (muted) ClearHistory();
}

void Owner::ClearHistory() {
    Wipe(history_, sizeof(history_));
    history_start_ = 0;
    history_count_ = 0;
}

void Owner::ClearPresentation() {
    CancelCompose();
    ClearHistory();
}

OwnerState Owner::State() const {
    return {queue_.IsOpen(), composing_, muted_, suspended_,
            disconnect_required_, terminal_result_, queue_.OutgoingSize(),
            history_count_};
}

Result Owner::BeginCompose() {
    if (!queue_.IsOpen()) return Result::NotOpen;
    if (suspended_) return Result::Backpressure;
    composing_ = true;
    return Result::Ok;
}

void Owner::CancelCompose() {
    Wipe(draft_, sizeof(draft_));
    draft_size_ = 0;
    composing_ = false;
}

Result Owner::Append(const std::uint8_t* text, std::size_t size) {
    if (!queue_.IsOpen() || !composing_) return Result::NotOpen;
    if (suspended_) return Result::Backpressure;
    if (size == 0) return Result::Ok;
    if (text == nullptr || size > MaxTextBytes - draft_size_)
        return Result::InvalidText;
    std::uint8_t candidate[MaxTextBytes + 1]{};
    std::memcpy(candidate, draft_, draft_size_);
    std::memcpy(candidate + draft_size_, text, size);
    std::uint32_t scalars = 0;
    const Result result = ValidateText(candidate, draft_size_ + size, scalars);
    if (result == Result::Ok) {
        draft_size_ += size;
        std::memcpy(draft_, candidate, draft_size_ + 1);
    }
    Wipe(candidate, sizeof(candidate));
    return result;
}

Result Owner::Backspace() {
    if (!queue_.IsOpen() || !composing_) return Result::NotOpen;
    if (suspended_) return Result::Backpressure;
    if (draft_size_ == 0) return Result::Ok;
    std::size_t first = draft_size_ - 1;
    while (first != 0 && (draft_[first] & 0xc0) == 0x80) --first;
    Wipe(draft_ + first, draft_size_ - first + 1);
    draft_size_ = first;
    return Result::Ok;
}

Result Owner::CopyDraft(Decoded& output) const {
    Clear(output);
    if (!queue_.IsOpen() || !composing_) return Result::NotOpen;
    output.size = static_cast<std::uint32_t>(draft_size_);
    if (draft_size_ != 0) {
        std::uint32_t scalars = 0;
        const Result result = ValidateText(draft_, draft_size_, scalars);
        if (result != Result::Ok) { Clear(output); return result; }
        output.scalars = scalars;
        std::memcpy(output.utf8, draft_, draft_size_);
    }
    return Result::Ok;
}

Result Owner::SubmitDraft(std::uint64_t now) {
    if (!queue_.IsOpen() || !composing_) return Result::NotOpen;
    if (suspended_) return Result::Backpressure;
    const Result result = queue_.Submit(draft_, draft_size_, now);
    if (result == Result::Ok) CancelCompose();
    return result;
}

Result Owner::PeekOutgoing(Outgoing& output) const {
    Clear(output);
    if (!queue_.IsOpen()) return Result::NotOpen;
    if (suspended_) return Result::Backpressure;
    return queue_.PeekOutgoing(output);
}

void Owner::AddHistory(const Message& message, Origin origin) {
    std::size_t index = (history_start_ + history_count_) % HistoryCapacity;
    if (history_count_ == HistoryCapacity) {
        index = history_start_;
        history_start_ = (history_start_ + 1) % HistoryCapacity;
    } else {
        ++history_count_;
    }
    Clear(history_[index]);
    history_[index].origin = origin;
    history_[index].message = message;
}

Result Owner::CommitOutgoing(const Binding& binding, std::uint64_t sequence) {
    if (!queue_.IsOpen()) return Result::NotOpen;
    if (!SameBinding(binding_, binding)) return Result::Stale;
    Outgoing outgoing{};
    Result result = queue_.PeekOutgoing(outgoing);
    if (result != Result::Ok) { Clear(outgoing); return result; }
    if (!SameBinding(outgoing.binding, binding) || outgoing.sequence != sequence) {
        Clear(outgoing);
        return Result::Stale;
    }
    Message message{};
    message.binding = binding;
    message.sender_slot = binding.local_slot;
    result = Decode(outgoing.frame.bytes, outgoing.frame.size, message.text);
    Clear(outgoing);
    if (result != Result::Ok) {
        Clear(message);
        Terminal(result);
        return result;
    }
    result = queue_.CommitOutgoing(binding, sequence);
    if (result == Result::Ok) AddHistory(message, Origin::LocalTransportAccepted);
    Clear(message);
    return result;
}

void Owner::Terminal(Result result) {
    queue_.Close();
    ClearPresentation();
    disconnect_required_ = true;
    terminal_result_ = result;
}

Result Owner::Receive(std::uint64_t generation, std::uint32_t epoch,
                      const std::uint8_t* bytes, std::size_t size,
                      std::uint64_t now) {
    const bool was_open = queue_.IsOpen();
    const Result result = queue_.Receive(generation, epoch, bytes, size, now);
    if (was_open && !queue_.IsOpen()) {
        Terminal(result);
        return result;
    }
    if (result != Result::Ok) return result;
    Message message{};
    while (queue_.Pop(message) == Result::Ok) {
        if (!muted_ && !suspended_) AddHistory(message, Origin::Remote);
        Clear(message);
    }
    Clear(message);
    return result;
}

Result Owner::HistoryAt(std::size_t index, HistoryEntry& output) const {
    Clear(output);
    if (index >= history_count_) return Result::Empty;
    output = history_[(history_start_ + index) % HistoryCapacity];
    return Result::Ok;
}

} // namespace CeTextChat
