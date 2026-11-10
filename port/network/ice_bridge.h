// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_ICE_BRIDGE_H
#define CE_ICE_BRIDGE_H
#if defined(CE_NETWORK_ICE)

#include <enet/enet.h>
#include <juice/juice.h>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// One optional ICE path for one CE-owned ENet host. No remote UDP destination is
// supplied: inbound ICE data can reach only that host's own loopback socket.
// Destroy the bridge before destroying its ENet host. Pump on the ENet thread.
// Descriptions contain ICE session credentials; callers must not log them.
class CeIceBridge {
public:
    struct Losses {
        uint64_t queue_overflow = 0;
        uint64_t ice_send = 0;
        uint64_t local_delivery = 0;
    };
    static std::unique_ptr<CeIceBridge> Create(ENetHost* ce_host,
                                             const juice_config_t& config,
                                             bool require_relay = true);
    ~CeIceBridge();
    CeIceBridge(const CeIceBridge&) = delete;
    CeIceBridge& operator=(const CeIceBridge&) = delete;

    bool LocalDescription(std::string& description) const;
    bool SetRemoteDescription(const std::string& description);
    bool Ready() const;
    bool Failed() const;
    bool UsesRelay() const;
    bool Pump();
    const ENetAddress& LocalAddress() const { return bridge_address_; }
    uint64_t SentDatagrams() const { return sent_; }
    uint64_t DeliveredDatagrams() const { return delivered_; }
    uint64_t RejectedLocalDatagrams() const { return rejected_local_; }
    // Saturating local-loss counts only; no packet data or dependency details.
    Losses LossSnapshot() const;
    // Read-only progress for the exchange's private trace: whether gathering has finished, how many relay candidates
    // the local description holds so far, and the agent's state index (juice_state_t). Counts and indexes only: no
    // description text, address or credential leaves the bridge.
    bool GatheringDone() const { return gathering_done_.load(); }
    uint32_t RelayCandidateCount() const;
    uint32_t StateIndex() const { return static_cast<uint32_t>(state_.load()); }

private:
    CeIceBridge() = default;
    static void StateChanged(juice_agent_t*, juice_state_t, void*);
    static void GatheringDone(juice_agent_t*, void*);
    static void Received(juice_agent_t*, const char*, size_t, void*);
    static bool FilterDescription(const std::string&, bool, std::string&);

    juice_agent_t* agent_ = nullptr;
    ENetSocket socket_ = ENET_SOCKET_NULL;
    ENetAddress ce_address_{};
    ENetAddress bridge_address_{};
    bool require_relay_ = true;
    bool remote_set_ = false;
    std::atomic<bool> gathering_done_{false};
    std::atomic<bool> failed_{false};
    std::atomic<juice_state_t> state_{JUICE_STATE_DISCONNECTED};
    mutable std::mutex receive_mutex_;
    std::deque<std::vector<char>> received_;
    uint64_t sent_ = 0;
    uint64_t delivered_ = 0;
    uint64_t rejected_local_ = 0;
    // Snapshot reads and all writers hold receive_mutex_.
    Losses losses_{};
};
#endif
#endif
