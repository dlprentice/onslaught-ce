// SPDX-License-Identifier: GPL-3.0-only
#if defined(CE_NETWORK_ICE)
#include "ice_bridge.h"
#include <array>
#include <cstring>
#include <limits>
#include <sstream>

namespace {
constexpr size_t kMaxQueuedDatagrams = 64;
constexpr size_t kMaxDatagram = ENET_PROTOCOL_MAXIMUM_MTU;
void CountLoss(uint64_t& counter) {
    if (counter != std::numeric_limits<uint64_t>::max()) ++counter;
}
bool RelayCandidate(const std::string& line) {
    std::istringstream words(line);
    std::string previous, word;
    while (words >> word) {
        if (previous == "typ" && word == "relay") return true;
        previous = word;
    }
    return false;
}
}

std::unique_ptr<CeIceBridge> CeIceBridge::Create(ENetHost* host,
                                               const juice_config_t& input,
                                               bool require_relay) {
    if (!host || host->peerCount != 1) return nullptr;
    std::unique_ptr<CeIceBridge> bridge(new CeIceBridge);
    if (enet_socket_get_address(host->socket, &bridge->ce_address_) != 0 ||
        !bridge->ce_address_.port) return nullptr;
    ENetAddress loopback{};
    enet_address_set_host_ip(&loopback, "127.0.0.1");
    if (bridge->ce_address_.host != ENET_HOST_ANY &&
        bridge->ce_address_.host != loopback.host) return nullptr;
    // Never inherit an arbitrary destination from the caller or from a packet.
    bridge->ce_address_.host = loopback.host;
    bridge->bridge_address_ = bridge->ce_address_;
    bridge->bridge_address_.port = 0;
    bridge->socket_ = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (bridge->socket_ == ENET_SOCKET_NULL ||
        enet_socket_bind(bridge->socket_, &bridge->bridge_address_) != 0 ||
        enet_socket_set_option(bridge->socket_, ENET_SOCKOPT_NONBLOCK, 1) != 0 ||
        enet_socket_get_address(bridge->socket_, &bridge->bridge_address_) != 0)
        return nullptr;
    bridge->require_relay_ = require_relay;
    juice_config_t config = input;
    config.cb_state_changed = StateChanged;
    config.cb_candidate = nullptr;
    config.cb_gathering_done = GatheringDone;
    config.cb_recv = Received;
    config.user_ptr = bridge.get();
    // Dependency warnings may include temporary ICE credentials on auth errors.
    juice_set_log_level(JUICE_LOG_LEVEL_NONE);
    bridge->agent_ = juice_create(&config);
    if (!bridge->agent_ ||
        juice_set_relay_only(bridge->agent_, require_relay) != JUICE_ERR_SUCCESS ||
        juice_gather_candidates(bridge->agent_) != JUICE_ERR_SUCCESS)
        return nullptr;
    return bridge;
}

CeIceBridge::~CeIceBridge() {
    // libjuice joins its callback activity before the mutex/socket disappear.
    if (agent_) juice_destroy(agent_);
    if (socket_ != ENET_SOCKET_NULL) enet_socket_destroy(socket_);
}

void CeIceBridge::StateChanged(juice_agent_t*, juice_state_t state, void* opaque) {
    static_cast<CeIceBridge*>(opaque)->state_.store(state);
}
void CeIceBridge::GatheringDone(juice_agent_t*, void* opaque) {
    static_cast<CeIceBridge*>(opaque)->gathering_done_.store(true);
}
void CeIceBridge::Received(juice_agent_t*, const char* data, size_t size, void* opaque) {
    auto* bridge = static_cast<CeIceBridge*>(opaque);
    if (!size || size > kMaxDatagram) return;
    std::lock_guard<std::mutex> lock(bridge->receive_mutex_);
    if (bridge->received_.size() < kMaxQueuedDatagrams)
        bridge->received_.emplace_back(data, data + size);
    else
        CountLoss(bridge->losses_.queue_overflow);
}

CeIceBridge::Losses CeIceBridge::LossSnapshot() const {
    std::lock_guard<std::mutex> lock(receive_mutex_);
    return losses_;
}

uint32_t CeIceBridge::RelayCandidateCount() const {
    // The description is read into a local buffer and only a count leaves this function. Reading it formats the
    // agent's description under the dependency's lock and changes nothing else: the agent took its ICE role when
    // Create started gathering, so this read can never be the call that fixes it.
    std::array<char, JUICE_MAX_SDP_STRING_LEN> buffer{};
    if (!agent_ || juice_get_local_description(agent_, buffer.data(), buffer.size()) != JUICE_ERR_SUCCESS) return 0;
    std::istringstream stream(buffer.data());
    std::string line;
    uint32_t count = 0;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if ((line.compare(0, 12, "a=candidate:") == 0 || line.compare(0, 10, "candidate:") == 0) &&
            RelayCandidate(line) && count != std::numeric_limits<uint32_t>::max()) ++count;
    }
    return count;
}

bool CeIceBridge::FilterDescription(const std::string& input, bool relay,
                                    std::string& output) {
    output.clear();
    if (input.empty() || input.size() >= JUICE_MAX_SDP_STRING_LEN ||
        input.find('\0') != std::string::npos) return false;
    std::istringstream stream(input);
    std::string line;
    bool candidate = false;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, 12, "a=candidate:") == 0 ||
            line.compare(0, 10, "candidate:") == 0) {
            if (relay && !RelayCandidate(line)) continue;
            candidate = true;
        }
        output += line + "\r\n";
    }
    if (!candidate || output.size() >= JUICE_MAX_SDP_STRING_LEN) {
        output.clear();
        return false;
    }
    return true;
}

bool CeIceBridge::LocalDescription(std::string& description) const {
    description.clear();
    if (!gathering_done_.load() || Failed()) return false;
    std::array<char, JUICE_MAX_SDP_STRING_LEN> buffer{};
    return juice_get_local_description(agent_, buffer.data(), buffer.size()) == JUICE_ERR_SUCCESS &&
           FilterDescription(buffer.data(), require_relay_, description);
}

bool CeIceBridge::SetRemoteDescription(const std::string& description) {
    if (remote_set_ || Failed()) return false;
    std::string filtered;
    if (!FilterDescription(description, require_relay_, filtered) ||
        juice_set_remote_description(agent_, filtered.c_str()) != JUICE_ERR_SUCCESS)
        return false;
    remote_set_ = true;
    return juice_set_remote_gathering_done(agent_) == JUICE_ERR_SUCCESS;
}

bool CeIceBridge::UsesRelay() const {
    std::array<char, JUICE_MAX_CANDIDATE_SDP_STRING_LEN> local{}, remote{};
    return juice_get_selected_candidates(agent_, local.data(), local.size(),
                                          remote.data(), remote.size()) == JUICE_ERR_SUCCESS &&
           (RelayCandidate(local.data()) || RelayCandidate(remote.data()));
}
bool CeIceBridge::Failed() const {
    return failed_.load() || state_.load() == JUICE_STATE_FAILED;
}
bool CeIceBridge::Ready() const {
    const auto state = state_.load();
    return !Failed() && (state == JUICE_STATE_CONNECTED || state == JUICE_STATE_COMPLETED) &&
           (!require_relay_ || UsesRelay());
}

bool CeIceBridge::Pump() {
    if (Failed()) return false;
    if (!Ready()) return true;
    std::array<char, kMaxDatagram + 1> data{};
    for (size_t i = 0; i != kMaxQueuedDatagrams; ++i) {
        ENetAddress sender{};
        ENetBuffer buffer{};
        buffer.data = data.data();
        buffer.dataLength = data.size();
        int size = enet_socket_receive(socket_, &sender, &buffer, 1);
        if (size == 0) break;
        // ENet uses -2 for interrupted or truncated datagrams, not a dead socket.
        if (size == -2) continue;
        if (size < 0) { failed_.store(true); return false; }
        if (sender.host != ce_address_.host || sender.port != ce_address_.port ||
            static_cast<size_t>(size) > kMaxDatagram) {
            ++rejected_local_;
            continue;
        }
        if (juice_send(agent_, data.data(), static_cast<size_t>(size)) == JUICE_ERR_SUCCESS)
            ++sent_;
        else {
            std::lock_guard<std::mutex> lock(receive_mutex_);
            CountLoss(losses_.ice_send);
        }
    }
    std::deque<std::vector<char>> ready;
    {
        std::lock_guard<std::mutex> lock(receive_mutex_);
        ready.swap(received_);
    }
    for (auto& packet : ready) {
        ENetBuffer buffer{};
        buffer.data = packet.data();
        buffer.dataLength = packet.size();
        if (enet_socket_send(socket_, &ce_address_, &buffer, 1) == static_cast<int>(packet.size()))
            ++delivered_;
        else {
            std::lock_guard<std::mutex> lock(receive_mutex_);
            CountLoss(losses_.local_delivery);
        }
    }
    return true;
}
#endif
