// SPDX-License-Identifier: GPL-3.0-only
// Authored private-filesystem fixtures. No unlink, rename, game, or network use.
#include "../../platform/common/ce_netcode.h"
#include "../../network/network.h"
#include "../../network/compatibility.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

extern "C" int CE_NetGenerateSecret(uint8_t out[CE_NET_SECRET_BYTES]) {
    for (size_t i = 0; i < CE_NET_SECRET_BYTES; ++i)
        out[i] = (uint8_t)(i + 1);
    return 1;
}

static unsigned checks = 0, failures = 0, clear_calls = 0;
static bool protect_clear = false, fail_clear = false, fail_write = false, fail_close = false;

extern "C" int __real_ftruncate(int, off_t);
extern "C" ssize_t __real_write(int, const void *, size_t);
extern "C" int __real_close(int);
extern "C" int __wrap_ftruncate(int fd, off_t size) {
    ++clear_calls;
    if (protect_clear || fail_clear) { errno = EIO; return -1; }
    return __real_ftruncate(fd, size);
}
extern "C" ssize_t __wrap_write(int fd, const void *data, size_t size) {
    if (fail_write) {
        fail_write = false;
        return __real_write(fd, data, size / 2);
    }
    return __real_write(fd, data, size);
}
extern "C" int __wrap_close(int fd) {
    int result = __real_close(fd);
    if (fail_close) { fail_close = false; errno = EIO; return -1; }
    return result;
}

static void check(bool ok, const char *name) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", name); }
}
static bool zero(const void *p, size_t n) {
    const unsigned char *bytes = static_cast<const unsigned char *>(p);
    for (size_t i = 0; i != n; ++i) if (bytes[i]) return false;
    return true;
}
static bool exists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}
static std::string read_file(const std::string &path) {
    int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return std::string();
    std::string result;
    char buffer[512];
    ssize_t count;
    while ((count = read(fd, buffer, sizeof buffer)) > 0) result.append(buffer, size_t(count));
    __real_close(fd);
    return result;
}
static bool write_new(const std::string &path, const std::string &text) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool ok = __real_write(fd, text.data(), text.size()) == ssize_t(text.size());
    return __real_close(fd) == 0 && ok;
}
static CE_Handoff fixture(bool campaign = false) {
    CE_Handoff h = {};
    h.kind = CE_HANDOFF_LAUNCH;
    h.role = CE_NET_HOST;
    std::snprintf(h.endpoint, sizeof h.endpoint, "127.0.0.1:39000");
    for (size_t i = 0; i != sizeof h.secret; ++i) h.secret[i] = uint8_t(i + 17);
    for (size_t i = 0; i != sizeof h.nonce; ++i) h.nonce[i] = uint8_t(i + 1);
    for (uint32_t level = 1; level != 8192; ++level)
        if (CE_CompatibilityLockstepLevel(level)) { h.level = level; break; }
    h.created = CE_HandoffNow();
    std::snprintf(h.args, sizeof h.args, "--authored-fixture");
    check(h.level != 0, "a supported fixture mission exists");
    if (campaign) check(CE_HandoffCampaignSet(&h) != 0, "typed campaign fixture is derived");
    return h;
}
static CE_HandoffOwner stage(const std::string &path, const CE_Handoff &h) {
    CE_HandoffOwner owner = {};
    const char *error = 0;
    check(CE_HandoffOwnedWrite(path.c_str(), &h, &owner, &error) != 0 && !error,
          "fresh owned record stages");
    return owner;
}
static void refused(const std::string &path, const CE_HandoffOwner *owner,
                    const std::string &expected, const char *label) {
    CE_Handoff out;
    std::memset(&out, 0x5a, sizeof out);
    const char *error = 0;
    clear_calls = 0;
    protect_clear = true;
    int result = CE_HandoffOwnedRead(path.c_str(), owner, CE_HandoffNow(), &out, &error);
    protect_clear = false;
    check(!result && error && zero(&out, sizeof out), label);
    check(clear_calls == 0, "unknown record never reaches cleanup");
    check(exists(path) && read_file(path) == expected, "unknown record is preserved");
}

static int scenarios(const std::string &root) {
    CE_Handoff ordinary = fixture(), campaign = fixture(true);
    char text[CE_HANDOFF_MAX_BYTES];
    size_t size = CE_HandoffFormat(&ordinary, text, sizeof text);
    std::string expected(text, size);
    check(write_new(root + "/authored-input.record", expected), "immutable authored input retained");
    std::string good_path = root + "/ordinary.record";
    CE_HandoffOwner good = stage(good_path, ordinary);
    check(read_file(good_path) == expected, "ordinary serialized bytes are unchanged");

    char token[256];
    size_t token_size = CE_HandoffOwnerFormat(&good, token, sizeof token);
    CE_HandoffOwner parsed = {};
    check(token_size && CE_HandoffOwnerParse(token, token_size, &parsed), "owner token round trip");
    char again[256];
    check(CE_HandoffOwnerFormat(&parsed, again, sizeof again) == token_size &&
          std::memcmp(token, again, token_size) == 0, "owner token is canonical");
    std::string noncanonical = "0" + std::string(token, token_size);
    check(!CE_HandoffOwnerParse(noncanonical.data(), noncanonical.size(), &parsed) &&
          zero(&parsed, sizeof parsed), "noncanonical owner is refused and cleared");
    std::string wrong_version(token, token_size); wrong_version[0] = '2';
    check(!CE_HandoffOwnerParse(wrong_version.data(), wrong_version.size(), &parsed),
          "mixed owner version is refused");
    check(!CE_HandoffOwnerParse(token, token_size - 1, &parsed), "truncated owner is refused");
    std::string extra = std::string(token, token_size) + ",extra";
    check(!CE_HandoffOwnerParse(extra.data(), extra.size(), &parsed), "extra owner data is refused");
    check(!CE_HandoffOwnerParse(0, 0, &parsed) && zero(&parsed, sizeof parsed),
          "absent owner is an explicit failure");

    std::string copy_path = root + "/separate-identical.record";
    check(write_new(copy_path, expected), "separate authored identity created");
    refused(copy_path, &good, expected, "identical contents do not grant file ownership");
    refused(good_path, 0, expected, "absent ownership cannot consume a record");
    CE_HandoffOwner wrong = good; wrong.digest[0] ^= 1;
    refused(good_path, &wrong, expected, "wrong content binding is refused");
    wrong = good; wrong.stamp ^= 1;
    refused(good_path, &wrong, expected, "changed file stamp is refused");
    wrong = good; wrong.version = 2;
    refused(good_path, &wrong, expected, "unsupported owner context is refused");

    CE_Handoff out = {};
    const char *error = 0;
    check(CE_HandoffOwnedRead(good_path.c_str(), &good, CE_HandoffNow(), &out, &error),
          "verified ordinary restart is admitted");
    check(out.role == ordinary.role && out.level == ordinary.level && !out.campaign.version,
          "ordinary role and mission survive");
    check(exists(good_path) && read_file(good_path).empty(), "consumption retains an empty entry");
    check(!CE_HandoffOwnedRead(good_path.c_str(), &good, CE_HandoffNow(), &out, &error) &&
          zero(&out, sizeof out), "owned record is consumed once");

    std::string campaign_path = root + "/campaign.record";
    CE_HandoffOwner campaign_owner = stage(campaign_path, campaign);
    check(CE_HandoffOwnedRead(campaign_path.c_str(), &campaign_owner, CE_HandoffNow(), &out, &error) &&
          out.campaign.session == campaign.campaign.session &&
          out.campaign.attempt == campaign.campaign.attempt && out.level == campaign.level,
          "typed campaign restart survives ownership admission");

    CE_Handoff stale = ordinary;
    stale.created -= int64_t(CE_HANDOFF_MAX_AGE) + 1;
    std::string stale_path = root + "/stale.record";
    CE_HandoffOwner stale_owner = stage(stale_path, stale);
    check(!CE_HandoffOwnedRead(stale_path.c_str(), &stale_owner, CE_HandoffNow(), &out, &error) &&
          zero(&out, sizeof out), "known stale launch is refused");
    check(exists(stale_path) && read_file(stale_path).empty(), "known stale contents are cleared");

    std::string cleanup_path = root + "/cleanup-failure.record";
    CE_HandoffOwner cleanup_owner = stage(cleanup_path, ordinary);
    fail_clear = true;
    int taken = CE_HandoffOwnedRead(cleanup_path.c_str(), &cleanup_owner, CE_HandoffNow(), &out, &error);
    fail_clear = false;
    check(!taken && error && zero(&out, sizeof out), "cleanup failure cannot report admission");
    check(read_file(cleanup_path) == expected, "failed cleanup retains its authored record");
    check(CE_HandoffOwnedClear(cleanup_path.c_str(), &cleanup_owner, &error) &&
          read_file(cleanup_path).empty(), "owned cancellation clears through its handle");
    protect_clear = true; clear_calls = 0;
    int canceled = CE_HandoffOwnedClear(copy_path.c_str(), &good, &error);
    protect_clear = false;
    check(!canceled && clear_calls == 0 && read_file(copy_path) == expected,
          "cancellation preserves an unowned identity");

    std::string failed_path = root + "/failed-write.record";
    CE_HandoffOwner failed_owner;
    std::memset(&failed_owner, 0x5a, sizeof failed_owner);
    fail_write = true; fail_clear = true;
    int written = CE_HandoffOwnedWrite(failed_path.c_str(), &ordinary, &failed_owner, &error);
    fail_clear = false;
    check(!written && error && zero(&failed_owner, sizeof failed_owner),
          "failed write publishes no ownership");
    std::string failed_bytes = read_file(failed_path);
    check(exists(failed_path) && !failed_bytes.empty(), "failed clearing retains partial authored data");
    std::string retry_path = root + "/fresh-retry.record";
    CE_HandoffOwner retry = stage(retry_path, ordinary);
    check(CE_HandoffOwnedRead(retry_path.c_str(), &retry, CE_HandoffNow(), &out, &error),
          "fresh attempt recovers after failed write");
    check(read_file(failed_path) == failed_bytes, "recovery preserves the failed artifact");

    std::string close_path = root + "/close-failure.record";
    fail_close = true;
    check(!CE_HandoffOwnedWrite(close_path.c_str(), &ordinary, &failed_owner, &error) &&
          zero(&failed_owner, sizeof failed_owner), "late close failure publishes no ownership");
    check(read_file(close_path) == expected, "late close failure retains completed private data");
    std::memset(&failed_owner, 0x5a, sizeof failed_owner);
    check(!CE_HandoffOwnedWrite(copy_path.c_str(), &ordinary, &failed_owner, &error) &&
          zero(&failed_owner, sizeof failed_owner) && read_file(copy_path) == expected,
          "occupied destination is preserved");
    check(!CE_HandoffOwnedClear(copy_path.c_str(), 0, &error), "clear without identity fails explicitly");
    return failures ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    int result = scenarios(argv[1]);
    std::printf("owned handoff: checks=%u failures=%u\n", checks, failures);
    return result;
}
