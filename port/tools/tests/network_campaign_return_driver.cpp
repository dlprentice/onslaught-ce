#include "ce_network_campaign_return.h"
#include "ce_netcode.h"
#include "ce_netlobby.h"
#include "../../network/compatibility.h"
#include "../../network/network.h"
#include <cstdio>
#include <cstring>
#include <string>

extern "C" int CE_NetGenerateSecret(uint8_t out[CE_NET_SECRET_BYTES]) {
    for (unsigned i = 0; i < CE_NET_SECRET_BYTES; ++i) out[i] = (uint8_t)(i + 1);
    return 1;
}

static int failures;
static void check(bool ok, const char *label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static CeNetworkCampaign agreed(bool win) {
    CeNetworkCampaign campaign = {};
    campaign.identity = {37, 49, 101};
    campaign.phase = win ? CE_CAMPAIGN_AGREED_WIN : CE_CAMPAIGN_AGREED_LOSS;
    campaign.host_outcome = campaign.guest_outcome =
        win ? CE_CAMPAIGN_OUTCOME_WIN : CE_CAMPAIGN_OUTCOME_LOSS;
    return campaign;
}

extern bool read_campaign_return_in_other_unit(CeCampaignReturn *out);

static void serialization() {
    const uint32_t mission = CE_LOBBY_DEFAULT_LEVEL;
    CE_Handoff record = {};
    record.kind = CE_HANDOFF_OUTCOME;
    record.created = CE_HandoffNow();
    record.good = 1;
    std::snprintf(record.text, sizeof(record.text), "%s", "Authored campaign result");
    char bytes[CE_HANDOFF_MAX_BYTES + 1] = {};
    size_t plainSize = CE_HandoffFormat(&record, bytes, sizeof(bytes));
    std::string plain(bytes, plainSize);
    CE_Handoff parsed = {};
    const char *error = NULL;
    check(plainSize && CE_HandoffParse(bytes, plainSize, record.created, &parsed, &error),
          "ordinary outcome remains readable");
    check(!parsed.campaign.version, "ordinary outcome has no continuation identity");
    for (int win = 0; win != 2; ++win) {
        record.good = win;
        record.campaign = {1, 37, 49, mission};
        size_t size = CE_HandoffFormat(&record, bytes, sizeof(bytes));
        check(size && CE_HandoffParse(bytes, size, record.created, &parsed, &error),
              "typed campaign outcome round trips");
        check(parsed.good == win && parsed.campaign.version == 1 &&
              parsed.campaign.session == 37 && parsed.campaign.attempt == 49 &&
              parsed.campaign.mission == mission, "return wire retains identity and agreed result");
        check(!CE_HandoffCampaignValid(&parsed), "return context is not a launch admission");
    }
    std::string canonical(bytes, CE_HandoffFormat(&record, bytes, sizeof(bytes)));
    const char *marker = "\ncampaign_return_version=";
    size_t split = canonical.find(marker);
    check(split != std::string::npos, "return context uses its own versioned suffix");
    if (split == std::string::npos) return;
    std::string variants[] = {
        canonical.substr(0, canonical.size() - 1), canonical + "extra\n",
        canonical + canonical.substr(split),
        canonical.substr(0, split) + "\ncampaign_return_version=2\ncampaign_session=37\ncampaign_attempt=49\ncampaign_mission=" + std::to_string(mission) + "\n",
        canonical.substr(0, split) + "\ncampaign_return_version=1\ncampaign_session=037\ncampaign_attempt=49\ncampaign_mission=" + std::to_string(mission) + "\n",
        canonical.substr(0, split) + "\ncampaign_return_version=1\ncampaign_session=0\ncampaign_attempt=49\ncampaign_mission=" + std::to_string(mission) + "\n",
        canonical.substr(0, split) + "\ncampaign_return_version=1\ncampaign_session=18446744073709551616\ncampaign_attempt=49\ncampaign_mission=" + std::to_string(mission) + "\n",
        canonical.substr(0, split) + "\ncampaign_return_version=1\ncampaign_session=37\ncampaign_attempt=49\ncampaign_mission=999\n"
    };
    for (const std::string &variant : variants) {
        std::memset(&parsed, 0x7f, sizeof(parsed));
        check(!CE_HandoffParse(variant.data(), variant.size(), record.created, &parsed, &error),
              "invalid return context is refused");
        CE_Handoff zero = {};
        check(std::memcmp(&parsed, &zero, sizeof(parsed)) == 0,
              "refused return clears every output field");
    }
    record.campaign = {};
    record.good = 1;
    size_t size = CE_HandoffFormat(&record, bytes, sizeof(bytes));
    check(std::string(bytes, size) == plain, "plain outcome bytes stay unchanged");

    CE_Handoff launch = {};
    launch.kind = CE_HANDOFF_LAUNCH;
    launch.role = CE_NET_HOST;
    launch.level = mission;
    launch.created = record.created;
    std::snprintf(launch.endpoint, sizeof(launch.endpoint), "%s", "127.0.0.1:27652");
    std::snprintf(launch.args, sizeof(launch.args), "%s", "-skipfmv");
    for (unsigned i = 0; i < sizeof(launch.secret); ++i) launch.secret[i] = (uint8_t)(i + 1);
    for (unsigned i = 0; i < sizeof(launch.nonce); ++i) launch.nonce[i] = (uint8_t)(i + 1);
    check(CE_HandoffCampaignSet(&launch), "authored launch retains its nonce-bound identity");
    size = CE_HandoffFormat(&launch, bytes, sizeof(bytes));
    check(size && CE_HandoffParse(bytes, size, record.created, &parsed, &error) &&
          CE_HandoffCampaignValid(&parsed), "campaign launch still round trips");
}

int main() {
    std::setvbuf(stdout, NULL, _IONBF, 0);
    CeCampaignReturn result = {};
    ce_campaign_return_clear();
    check(!ce_campaign_return_get(&result), "new process has no return context");
    check(!ce_campaign_return_get(NULL), "null destination is refused");
    for (int win = 0; win != 2; ++win) {
        CeNetworkCampaign campaign = agreed(win != 0);
        check(ce_campaign_return_capture(&campaign, true, false),
              "agreed terminal result is captured");
        campaign = {};
        check(ce_campaign_return_get(&result), "context survives clearing its runtime owner");
        check(result.identity.session == 37 && result.identity.attempt == 49 &&
              result.identity.mission == 101, "return keeps the exact completed identity");
        check(result.outcome == (win ? CE_CAMPAIGN_OUTCOME_WIN : CE_CAMPAIGN_OUTCOME_LOSS),
              "return distinguishes agreed win and loss");
        CeCampaignReturn other = {};
        check(read_campaign_return_in_other_unit(&other) &&
              other.identity.session == result.identity.session &&
              other.outcome == result.outcome, "translation units share one return owner");
    }
    CeNetworkCampaign campaign = agreed(true);
    check(!ce_campaign_return_capture(&campaign, false, false),
          "unconfirmed exchange cannot return continuation");
    check(!ce_campaign_return_get(&result), "refused capture clears previous context");
    check(!ce_campaign_return_capture(&campaign, true, true),
          "runtime failure cannot return continuation");
    check(!ce_campaign_return_capture(NULL, true, false), "missing owner is refused");
    const CeCampaignPhase refused[] = {CE_CAMPAIGN_IDLE, CE_CAMPAIGN_COLLECTING,
        CE_CAMPAIGN_CONFLICT, CE_CAMPAIGN_CANCELLED, CE_CAMPAIGN_DISCONNECTED,
        CE_CAMPAIGN_FAILED};
    for (CeCampaignPhase phase : refused) {
        campaign = agreed(true);
        campaign.phase = phase;
        check(!ce_campaign_return_capture(&campaign, true, false),
              "non-agreed phase cannot return continuation");
    }
    campaign = agreed(true);
    campaign.guest_outcome = CE_CAMPAIGN_OUTCOME_LOSS;
    check(!ce_campaign_return_capture(&campaign, true, false),
          "conflicting participant results are refused");
    campaign = agreed(true);
    campaign.identity.attempt = 0;
    check(!ce_campaign_return_capture(&campaign, true, false), "missing attempt is refused");
    campaign = agreed(true);
    ce_campaign_return_capture(&campaign, true, false);
    ce_campaign_return_clear();
    check(!ce_campaign_return_get(&result), "new mission clears earlier return context");
    serialization();
    return failures ? 1 : 0;
}
