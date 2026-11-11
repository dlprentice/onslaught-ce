// SPDX-License-Identifier: GPL-3.0-only
#include "ce_netcode.h"
#include "network.h"
#include "compatibility.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

static unsigned checks = 0, failures = 0;
static uint32_t supported_mission() {
    for (uint32_t value = 1; value < 100000; ++value)
        if (CE_CompatibilityLockstepLevel(value)) return value;
    return 0;
}
static const uint32_t mission = supported_mission();
extern "C" int CE_BootCampaignAdmit(const CE_Handoff *);
extern "C" int CE_BootCampaignGet(CE_CampaignLaunch *, int *);
extern "C" void CE_BootCampaignClear(void);
extern "C" int CE_NetGenerateSecret(uint8_t out[32]) {
    for (unsigned i = 0; i != 32; ++i) out[i] = uint8_t(i + 1);
    return 1;
}
static void check(bool ok, const char *label) {
    ++checks;
    if (!ok) { ++failures; fprintf(stderr, "FAIL %s\n", label); }
}
static CE_Handoff launch() {
    CE_Handoff result = {};
    result.kind = CE_HANDOFF_LAUNCH;
    result.role = CE_NET_HOST;
    result.level = mission;
    result.created = 1700000000;
    strcpy(result.endpoint, "127.0.0.1:27652");
    for (unsigned i = 0; i != sizeof(result.nonce); ++i) result.nonce[i] = uint8_t(i + 1);
    for (unsigned i = 0; i != sizeof(result.secret); ++i) result.secret[i] = uint8_t(i + 31);
    return result;
}
static std::string encode(const CE_Handoff &handoff) {
    char buffer[CE_HANDOFF_MAX_BYTES];
    size_t size = CE_HandoffFormat(&handoff, buffer, sizeof(buffer));
    check(size != 0, "format accepted fixture");
    return std::string(buffer, size);
}
static bool decode(const std::string &text, int64_t now, CE_Handoff &out) {
    const char *error = NULL;
    return CE_HandoffParse(text.data(), text.size(), now, &out, &error) != 0;
}
static void rejected(const std::string &text, const char *label) {
    CE_Handoff out = launch();
    CE_HandoffCampaignSet(&out);
    check(!decode(text, out.created, out), label);
    check(out.campaign.version == 0 && out.campaign.session == 0 &&
        out.campaign.attempt == 0 && out.campaign.mission == 0, "refusal clears descriptor");
}
int main(int argc, char **argv) {
    if (argc == 3) {
        CE_Handoff handoff = launch();
        handoff.created = CE_HandoffNow();
        check(CE_HandoffCampaignSet(&handoff), "process fixture descriptor derives");
        if (!strcmp(argv[1], "--write")) {
            check(CE_HandoffWrite(argv[2], &handoff), "first process writes owned launch");
        } else if (!strcmp(argv[1], "--read")) {
            CE_Handoff consumed = {};
            const char *error = NULL;
            check(CE_HandoffRead(argv[2], CE_HandoffNow(), &consumed, &error),
                  "restarted process consumes launch");
            check(CE_BootCampaignAdmit(&consumed), "restarted process admits launch");
            CE_CampaignLaunch admitted = {};
            int host = 0;
            check(CE_BootCampaignGet(&admitted, &host) && host &&
                  admitted.session == handoff.campaign.session &&
                  admitted.attempt == handoff.campaign.attempt &&
                  admitted.mission == handoff.campaign.mission,
                  "real process boundary preserves typed identity");
            check(!CE_HandoffRead(argv[2], CE_HandoffNow(), &consumed, &error),
                  "consumed descriptor cannot replay");
            CE_BootCampaignClear();
        } else {
            check(false, "unknown fixture mode");
        }
        printf("campaign process checks=%u failures=%u\n", checks, failures);
        return failures ? 1 : 0;
    }
    check(mission != 0, "supported mission exists");
    CE_Handoff ordinary = launch();
    std::string legacy = encode(ordinary);
    CE_Handoff decoded = {};
    check(decode(legacy, ordinary.created, decoded), "legacy parses");
    check(!decoded.campaign.version, "legacy has no campaign");
    CE_Handoff campaign = ordinary;
    check(CE_HandoffCampaignSet(&campaign), "nonce-bound descriptor derives");
    check(campaign.campaign.version == 1 && campaign.campaign.mission == mission &&
        campaign.campaign.session && campaign.campaign.attempt, "typed identity populated");
    std::string encoded = encode(campaign);
    check(encoded.compare(0, legacy.size(), legacy) == 0, "ordinary serialized prefix unchanged");
    check(decode(encoded, campaign.created, decoded), "campaign parses");
    check(decoded.campaign.session == campaign.campaign.session &&
        decoded.campaign.attempt == campaign.campaign.attempt &&
        decoded.campaign.mission == campaign.campaign.mission, "identity survives serialization");
    check(CE_BootCampaignAdmit(&decoded), "boot admits typed descriptor");
    CE_CampaignLaunch admitted = {};
    int host = 0;
    check(CE_BootCampaignGet(&admitted, &host) && host &&
        admitted.session == campaign.campaign.session, "boot exposes admitted identity");
    CE_Handoff bad = campaign;
    ++bad.campaign.mission;
    check(!CE_HandoffCampaignValid(&bad), "mission identity mismatch rejected");
    check(!CE_BootCampaignAdmit(&bad) && !CE_BootCampaignGet(&admitted, &host),
        "failed admission removes prior identity");
    check(CE_BootCampaignAdmit(&campaign) && CE_BootCampaignAdmit(&ordinary) &&
        !CE_BootCampaignGet(&admitted, &host), "ordinary start clears campaign");
    campaign.role = CE_NET_GUEST;
    check(CE_BootCampaignAdmit(&campaign) && CE_BootCampaignGet(&admitted, &host) && !host,
        "guest role survives admission");
    CE_BootCampaignClear();
    check(!CE_BootCampaignGet(&admitted, &host), "explicit stop clears admission");
    const char *keys[] = {"campaign_version=", "campaign_session=", "campaign_attempt=", "campaign_mission="};
    for (const char *key : keys) {
        std::string changed = encoded;
        size_t value = changed.find(key) + strlen(key);
        changed[value] = changed[value] == '9' ? '8' : '9';
        rejected(changed, "altered descriptor rejected");
    }
    rejected(encoded + "extra=1\n", "extra field rejected");
    rejected(encoded + "campaign_version=1\n", "duplicate descriptor rejected");
    for (size_t size = legacy.size() + 1; size < encoded.size(); ++size)
        rejected(encoded.substr(0, size), "truncated descriptor rejected");
    check(!decode(encoded, ordinary.created + CE_HANDOFF_MAX_AGE + 1, decoded),
        "expired campaign handoff rejected");
    check(!decode(encoded, ordinary.created - CE_HANDOFF_MAX_AGE - 1, decoded), "future handoff rejected");
    bad = campaign;
    bad.kind = CE_HANDOFF_OUTCOME;
    check(!CE_HandoffCampaignValid(&bad), "presentation outcome cannot carry launch identity");
    bad = campaign;
    memset(bad.nonce, 0, sizeof(bad.nonce));
    check(!CE_HandoffCampaignSet(&bad), "zero nonce cannot become identity");
    bad = campaign;
    bad.campaign.version = 2;
    check(!CE_HandoffCampaignValid(&bad), "unknown campaign version refused");
    char small[8];
    check(!CE_HandoffFormat(&campaign, small, sizeof(small)), "short output refuses");
    printf("campaign handoff checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
