// SPDX-License-Identifier: GPL-3.0-only
// Zero-account local Ed25519 player identity, profile persistence, and challenge signing.
#ifndef CE_ONLINE_PROFILE_H
#define CE_ONLINE_PROFILE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

struct CeOnlineProfileSettings {
    std::string callsign;
    uint32_t matches_played = 0;
    uint32_t matches_won = 0;
    std::string preferred_side = "forseti"; // "forseti" or "muspell"
    std::string lobby_url;                  // optional explicit lobby origin override
};

// Owns a local RFC 8032 SHA-512 Ed25519 keypair stored in <user-dir>/online-profile.key
// (owner-only 0600 on Linux, CeWinAcl owner-only DACL on Windows) and settings/stats in
// <user-dir>/online-profile.ini. Secret key material is wiped with crypto_wipe on destruction.
class CeOnlineProfile {
public:
    static constexpr size_t kSeedBytes = 32;
    static constexpr size_t kPublicKeyBytes = 32;
    static constexpr size_t kSecretKeyBytes = 64;
    static constexpr size_t kSignatureBytes = 64;
    static constexpr size_t kMinCallsignLen = 3;
    static constexpr size_t kMaxCallsignLen = 20;

    CeOnlineProfile() = default;
    ~CeOnlineProfile();
    CeOnlineProfile(const CeOnlineProfile&) = delete;
    CeOnlineProfile& operator=(const CeOnlineProfile&) = delete;
    CeOnlineProfile(CeOnlineProfile&& other) noexcept;
    CeOnlineProfile& operator=(CeOnlineProfile&& other) noexcept;

    // Loads an existing profile from user_dir or generates a new Ed25519 keypair if absent.
    // Fails closed if online-profile.key exists with insecure permissions, wrong size, or symlink/reparse point.
    bool LoadOrCreate(const char* user_dir, std::string* error_out = nullptr);

    // Deterministic in-memory initialization from a 32-byte seed (for tests / RFC vectors; never touches disk).
    bool InitFromSeed(const uint8_t seed[kSeedBytes], const std::string& callsign = "");

    // Persists current settings/stats to <user-dir>/online-profile.ini atomically.
    bool SaveSettings(std::string* error_out = nullptr) const;

    // Updates callsign (sanitized to 3..20 [A-Za-z0-9 _-]). Returns false if sanitized result is < 3 chars.
    bool SetCallsign(const std::string& raw, bool save = true, std::string* error_out = nullptr);

    // Records a completed online match and persists stats when save is true.
    bool RecordMatchResult(bool won, bool save = true, std::string* error_out = nullptr);

    // Sets preferred_side ("forseti" or "muspell").
    bool SetPreferredSide(const std::string& side, bool save = true, std::string* error_out = nullptr);

    // Sets optional lobby_url override (empty or printable non-whitespace ASCII <= 512 bytes).
    bool SetLobbyUrl(const std::string& url, bool save = true, std::string* error_out = nullptr);

    // Signs canonical auth challenge message using RFC 8032 SHA-512 Ed25519:
    //   "onslaught-ce-lobby-auth-v1\n<nonce>\n<issued_at>\n<pubkey_hex>\n<callsign>"
    // Outputs 128 lowercase hex characters into out_signature_hex.
    bool SignAuthChallenge(const std::string& nonce_hex, uint64_t issued_at,
                           const std::string& callsign, std::string& out_signature_hex) const;

    // Verifies an auth challenge signature against an arbitrary 64-hex Ed25519 public key.
    static bool VerifyAuthChallenge(const std::string& pubkey_hex, const std::string& nonce_hex,
                                    uint64_t issued_at, const std::string& callsign,
                                    const std::string& signature_hex);

    static std::string BuildAuthMessage(const std::string& nonce_hex, uint64_t issued_at,
                                        const std::string& pubkey_hex, const std::string& callsign);

    static bool IsValidCallsign(const std::string& text);
    static std::string SanitizeCallsign(const std::string& raw, const std::string& fallback = "");
    static std::string EncodeCrockfordBase32Lower(const uint8_t* data, size_t len);
    static std::string DerivePlayerIdFromPubkey(const uint8_t pubkey[kPublicKeyBytes]);
    static std::string DeriveTagFromPubkey(const uint8_t pubkey[kPublicKeyBytes]);
    static bool IsValidNonceHex(const std::string& nonce_hex);

    bool Loaded() const noexcept { return loaded_; }
    const std::string& PubkeyHex() const noexcept { return pubkey_hex_; }
    const std::string& PlayerId() const noexcept { return player_id_; }
    const std::string& Tag() const noexcept { return tag_; }
    std::string DisplayName() const { return settings_.callsign + "#" + tag_; }
    const CeOnlineProfileSettings& Settings() const noexcept { return settings_; }
    const std::string& Callsign() const noexcept { return settings_.callsign; }
    uint32_t MatchesPlayed() const noexcept { return settings_.matches_played; }
    uint32_t MatchesWon() const noexcept { return settings_.matches_won; }
    const std::string& PreferredSide() const noexcept { return settings_.preferred_side; }
    const std::string& LobbyUrl() const noexcept { return settings_.lobby_url; }
    const std::string& UserDir() const noexcept { return user_dir_; }
    const std::array<uint8_t, kPublicKeyBytes>& PublicKeyBytes() const noexcept { return public_key_; }

    void Clear() noexcept;

private:
    void DeriveIdentityFromKeypair();
    bool LoadSettingsFile(std::string* error_out);

    bool loaded_ = false;
    std::string user_dir_;
    std::array<uint8_t, kSecretKeyBytes> secret_key_{};
    std::array<uint8_t, kPublicKeyBytes> public_key_{};
    std::string pubkey_hex_;
    std::string player_id_;
    std::string tag_;
    CeOnlineProfileSettings settings_;
};

#endif // CE_ONLINE_PROFILE_H
