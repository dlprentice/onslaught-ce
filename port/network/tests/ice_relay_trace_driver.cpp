// SPDX-License-Identifier: GPL-3.0-only
// The native relay exchange's private trace (ice_game.cpp GameExchange::Trace) under production CreateGameSession,
// driven the way the game drives it: the transport polls the attached exchange once a frame. The consumer fixture's
// authored provider, stage, exchange, credential and engine stand-ins are used, so the steps, signaling status and ICE
// progress are set one at a time; no socket, HTTP, libjuice, credential, record, game or service is used. What the
// exchange writes to stderr is captured and compared line by line, and every captured byte is searched for the
// authored grant, record, description and directory names.
#define main ConsumerRegressionMain
#include "signaling_consumer_fixture.inc"
#undef main
#include <unistd.h>
#include <vector>

namespace {
const char kPrefix[] = "onslaught-ce: relay exchange: ";
const char kSetupTimedOut[] = "Relay setup timed out. Return to the menu and prepare a fresh approved setup.";
const char kBadRecord[] = "Relay signaling returned an invalid peer record. Return to the menu and check the setup.";
const char kNoCredentials[] = "Relay signaling could not deliver this player's credentials. Return to the menu and check the setup.";
const char kRefusedRemote[] = "relay peer description was refused; begin a fresh network session";
const char kExpiredAccess[] = "relay access or local clock is invalid; relaunch both games with fresh private lobby and game folders and repeat the private code and file exchange";
const char kFallback[] = "relay exchange failed";

// Everything the process writes to stderr while body runs.
template <class F> std::string Captured(F&& body)
{
    std::fflush(stderr);
    std::FILE* file = std::tmpfile();
    const int saved = dup(2);
    Check(file && saved >= 0 && dup2(fileno(file), 2) >= 0, "stderr is captured");
    body();
    std::fflush(stderr);
    dup2(saved, 2); close(saved);
    std::string text; char block[512]; size_t count;
    std::rewind(file);
    while ((count = std::fread(block, 1, sizeof block, file)) > 0) text.append(block, count);
    std::fclose(file);
    return text;
}

// The captured lines that belong to the trace.
std::vector<std::string> Trace(const std::string& text)
{
    std::vector<std::string> lines;
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.compare(0, sizeof kPrefix - 1, kPrefix) == 0) lines.push_back(line);
    }
    return lines;
}

std::string Progress(const char* role, unsigned beat, unsigned step, unsigned signaling, unsigned gathered, unsigned relay,
                     unsigned state, unsigned after)
{
    char text[256];
    const std::string label = beat ? " beat=" + std::to_string(beat) : std::string();
    std::snprintf(text, sizeof text, "%s%s%s step=%02x signaling=%u gathered=%u relay=%u state=%u after_ms=%u", kPrefix, role,
                  label.c_str(), step, signaling, gathered, relay, state, after);
    return text;
}
std::string Stopped(const char* role, const char* sentence) { return std::string(kPrefix) + role + " stopped: " + sentence; }
unsigned Index(CeIceSignalStatus status) { return static_cast<unsigned>(status); }

void Same(const std::vector<std::string>& got, const std::vector<std::string>& want, const char* label)
{
    Check(got == want, label);
    if (got == want) return;
    std::printf("--- %s\n", label);
    for (const auto& line : got) std::printf("got:  %s\n", line.c_str());
    for (const auto& line : want) std::printf("want: %s\n", line.c_str());
}

// The transport's frame: each poll is 1001 ms after the last, and the exchange was created at the fixture's start.
struct Frames {
    CE_NetSession* session;
    explicit Frames(CE_NetSession* s) : session(s) {}
    std::string Run(unsigned count) { return Captured([&] { for (unsigned i = 0; i < count; ++i) Poll(session); }); }
};

unsigned Count(const std::vector<std::string>& lines, const char* needle)
{
    unsigned count = 0;
    for (const auto& line : lines) count += line.find(needle) != std::string::npos;
    return count;
}
}

int main(int argc, char** argv)
{
    if (argc != 2 || argv[1][0] != '/') return 2;
    lobbyDirectory = std::string(argv[1]) + "/lobby";
    gameDirectory = std::string(argv[1]) + "/game";
    const unsigned admitted = Index(CeIceSignalStatus::Admitted);
    std::string everything;

    {   // A guest waits for the host's record, imports it, takes its grant, starts ICE, gathers, then publishes.
        Reset(); CE_NetSession* session = Native(CE_NET_GUEST);
        Check(session != nullptr, "the guest's lobby session is created");
        signalFixture.grantAvailable = false;
        Frames frames(session);
        std::string text = frames.Run(6);
        Same(Trace(text), {Progress("guest", 0, 0x00, admitted, 0, 0, 0, 1001), Progress("guest", 1, 0x00, admitted, 0, 0, 0, 6006)},
             "a waiting guest prints its first line, one beat after five quiet seconds and nothing between");
        everything += text;
        signalFixture.peerAvailable = true; signalFixture.importPending = 1;
        text = frames.Run(1);
        Same(Trace(text), {}, "a pending import changes no field and prints nothing");
        everything += text;
        text = frames.Run(1);
        Same(Trace(text), {Progress("guest", 0, 0x01, admitted, 0, 0, 0, 8008)}, "the imported record is the first step");
        everything += text;
        signalFixture.grantAvailable = true; signalFixture.noLocalDescription = true; signalFixture.iceState = 1;
        text = frames.Run(1);
        Same(Trace(text), {Progress("guest", 0, 0x0f, admitted, 0, 0, 1, 9009)},
             "the grant, the started agent and the accepted peer description are the next steps, and the agent is gathering");
        everything += text;
        signalFixture.iceGathered = 1;
        text = frames.Run(1);
        Same(Trace(text), {Progress("guest", 0, 0x0f, admitted, 1, 0, 1, 10010)},
             "gathering that finished with no relay candidate is its own line");
        everything += text;
        signalFixture.iceRelays = 2; signalFixture.iceState = 2; signalFixture.noLocalDescription = false;
        text = frames.Run(1);
        Same(Trace(text), {Progress("guest", 0, 0x1f, admitted, 1, 2, 2, 11011)}, "relay candidates and the published local description");
        everything += text;
        text = frames.Run(1);
        Same(Trace(text), {Progress("guest", 0, 0x3f, admitted, 1, 2, 2, 12012)}, "the local record handed to the signaling client is the last step");
        everything += text;
        signalFixture.status = CeIceSignalStatus::ServiceDeliveryComplete;
        text = frames.Run(1);
        Same(Trace(text), {Progress("guest", 0, 0x3f, Index(CeIceSignalStatus::ServiceDeliveryComplete), 1, 2, 2, 13013)},
             "a signaling status change is a line");
        everything += text;
        CE_NetDestroy(session);
    }

    {   // A host takes its grant first, publishes, and imports the guest's record last.
        Reset(); CE_NetSession* session = Native(CE_NET_HOST);
        Frames frames(session);
        std::string text = frames.Run(2);
        Same(Trace(text), {Progress("host", 0, 0x16, admitted, 0, 0, 0, 1001), Progress("host", 0, 0x36, admitted, 0, 0, 0, 2002)},
             "a host takes its grant, starts ICE, publishes its description and then its record");
        everything += text;
        signalFixture.peerAvailable = true;
        text = frames.Run(1);
        Same(Trace(text), {Progress("host", 0, 0x3f, admitted, 0, 0, 0, 3003)}, "the imported record and the accepted description finish the host");
        everything += text;
        text = frames.Run(30);
        const auto quiet = Trace(text);
        Check(Count(quiet, "step=3f") == quiet.size() && Count(quiet, " beat=") == quiet.size() && quiet.size() >= 5 && quiet.size() <= 6,
              "a finished exchange only beats, about every five seconds");
        everything += text;
        CE_NetDestroy(session);
    }

    {   // A progress line starts a new quiet period: the next beat comes five quiet seconds after it, not at the old deadline.
        Reset(); CE_NetSession* session = Native(CE_NET_GUEST);
        signalFixture.grantAvailable = false;
        Frames frames(session);
        // The first line is at 1001 ms, so the first beat is due from 6001 ms; polls at 2002 to 4004 ms are quiet.
        std::string text = frames.Run(4);
        signalFixture.status = CeIceSignalStatus::Exchanging;
        text += frames.Run(1);      // 5005 ms: a changed status is a line, and five quiet seconds are counted from here
        text += frames.Run(4);      // 6006 to 9009 ms: past the first deadline, but not five seconds after the line at 5005 ms
        text += frames.Run(1);      // 10010 ms: the beat
        const unsigned exchanging = Index(CeIceSignalStatus::Exchanging);
        Same(Trace(text), {Progress("guest", 0, 0x00, admitted, 0, 0, 0, 1001), Progress("guest", 0, 0x00, exchanging, 0, 0, 0, 5005),
                           Progress("guest", 1, 0x00, exchanging, 0, 0, 0, 10010)},
             "a progress line starts a new quiet period, so the next beat is five seconds after it and not at the earlier deadline");
        everything += text;
        CE_NetDestroy(session);
    }

    // Every stop prints the exchange's own fixed sentence once, after the line that shows how far it had come.
    auto stops = [&](const char* label, CE_NetRole role, const char* sentence, unsigned step, auto&& arrange) {
        Reset(); arrange();
        CE_NetSession* session = Native(role);
        const char* who = role == CE_NET_HOST ? "host" : "guest";
        Frames frames(session);
        const std::string text = frames.Run(1);
        Same(Trace(text), {Progress(who, 0, step, admitted, 0, 0, 0, 1001), Stopped(who, sentence)}, label);
        Check(session && session->status.error != CE_NET_OK, "the failing poll fails the session");
        const std::string later = frames.Run(2);
        Same(Trace(later), {}, "a stopped exchange prints its sentence once");
        everything += text + later;
        CE_NetDestroy(session);
    };
    stops("a refused peer record stops the guest with the exchange's sentence", CE_NET_GUEST, "Authored record refusal.", 0x00,
          [] { signalFixture.peerAvailable = true; signalFixture.invalidPeer = true; });
    stops("an oversize peer record stops the guest with the provider sentence", CE_NET_GUEST, kBadRecord, 0x00,
          [] { signalFixture.peerAvailable = true; signalFixture.peerSize = CE_ICE_SIGNAL_RECORD_MAX + 1; });
    stops("a refused grant stops the host with the credential sentence", CE_NET_HOST, "Authored credential refusal.", 0x00,
          [] { signalFixture.invalidGrant = true; });
    stops("an empty grant stops the host with the delivery sentence", CE_NET_HOST, kNoCredentials, 0x00,
          [] { signalFixture.grantSize = 0; });
    stops("a refused peer description stops the host after the agent started", CE_NET_HOST, kRefusedRemote, 0x07,
          [] { signalFixture.refuseRemote = true; signalFixture.peerAvailable = true; });
    for (CeIceSignalStatus status : {CeIceSignalStatus::SetupExpired, CeIceSignalStatus::Closed}) {
        Reset(); CE_NetSession* session = Native(CE_NET_HOST);
        Frames frames(session);
        std::string text = frames.Run(1);
        signalFixture.status = status;
        text += frames.Run(1);
        const char* sentence = status == CeIceSignalStatus::SetupExpired ? kSetupTimedOut :
            "The approved pair ended. Return to the menu and obtain a fresh approved setup.";
        Same(Trace(text), {Progress("host", 0, 0x16, admitted, 0, 0, 0, 1001), Progress("host", 0, 0x16, Index(status), 0, 0, 0, 2002),
                           Stopped("host", sentence)},
             "a signaling status that ends the setup stops a started host with that status's sentence");
        everything += text;
        CE_NetDestroy(session);
    }
    {   // The grant runs out while the session is up: a different error code, the same single line.
        Reset(); CE_NetSession* session = Native(CE_NET_HOST);
        Frames frames(session);
        std::string text = frames.Run(1);
        signalFixture.wall = signalFixture.request.grantExpiresAt + 1;
        text += frames.Run(1);
        Same(Trace(text), {Progress("host", 0, 0x16, admitted, 0, 0, 0, 1001), Progress("host", 0, 0x36, admitted, 0, 0, 0, 2002),
                           Stopped("host", kExpiredAccess)},
             "expired relay access stops the exchange with its sentence");
        Check(session->status.error == CE_NET_TIMEOUT, "expired relay access is a timeout");
        everything += text;
        CE_NetDestroy(session);
    }
    {   // A stop inside a sampling interval still reports the agent's progress at that moment.
        Reset(); CE_NetSession* session = Native(CE_NET_HOST);
        Frames frames(session);
        std::string text = frames.Run(2);
        signalFixture.iceGathered = 1; signalFixture.iceRelays = 1; signalFixture.iceState = 3;
        signalFixture.wall = signalFixture.request.grantExpiresAt + 1;
        text += Captured([&] {
            signalFixture.monotonic += 10;
            session->status.error = session->callbacks.poll(session->owner, session, signalFixture.monotonic);
        });
        Same(Trace(text), {Progress("host", 0, 0x16, admitted, 0, 0, 0, 1001), Progress("host", 0, 0x36, admitted, 0, 0, 0, 2002),
                           Progress("host", 0, 0x36, admitted, 1, 1, 3, 2012), Stopped("host", kExpiredAccess)},
             "a stop reads the agent's progress again before it reports");
        everything += text;
        CE_NetDestroy(session);
    }
    {   // A deferred game-stage setup that runs out of time fails without a sentence of its own: the transport's words.
        Reset(); Check(CE_NetSelectSignaling(std::make_shared<AuthoredProvider>()), "game provider selected");
        signalFixture.gameAuthorized = true; signalFixture.preflightPending = true;
        CeIceSignalGamePlan plan = GamePlan();
        Check(CE_NetRestoreSignalingGamePlan(plan), "the game plan is restored");
        CE_NetConfig config = Config();
        CE_NetSession* session = CE_NetCreateGame(&config);
        Check(session != nullptr, "the game session waits for its preflight");
        Frames frames(session);
        std::string text = frames.Run(1);
        signalFixture.monotonic += 60000;
        text += frames.Run(1);
        Same(Trace(text), {Progress("host", 0, 0x00, admitted, 0, 0, 0, 1001), Progress("host", 1, 0x00, admitted, 0, 0, 0, 62002),
                           Stopped("host", kFallback)},
             "a stop with no problem sentence prints the transport's fallback words");
        everything += text;
        CE_NetDestroy(session);
    }
    {   // The manual file route is not traced.
        Reset(); signalFixture.manualReady = true;
        CE_NetConfig config = Config();
        CE_NetSession* session = CE_NetCreateLobby(&config);
        Check(session != nullptr && signalFixture.manualReads == 1, "the unselected manual route creates its session");
        Frames frames(session);
        const std::string text = frames.Run(8);
        Same(Trace(text), {}, "the manual file route prints no trace");
        everything += text;
        CE_NetDestroy(session);
    }

    {   // A signaling status that keeps changing prints at most the line bound, then only the bounded beats.
        Reset(); CE_NetSession* session = Native(CE_NET_GUEST);
        Frames frames(session);
        std::string text;
        for (unsigned i = 0; i < 400; ++i) {
            signalFixture.status = (i & 1) ? CeIceSignalStatus::Exchanging : CeIceSignalStatus::AwaitingPeerRecord;
            text += frames.Run(1);
        }
        const auto lines = Trace(text);
        const unsigned beats = Count(lines, " beat="), progress = lines.size() - beats;
        Check(progress == 24 && beats == 24 && Count(lines, "stopped") == 0, "a changing status prints 24 lines, then 24 beats, then nothing");
        unsigned previous = 0; bool spaced = true;
        for (const auto& line : lines) {
            if (line.find(" beat=") == std::string::npos) continue;
            const unsigned after = static_cast<unsigned>(std::stoul(line.substr(line.find("after_ms=") + 9)));
            spaced = spaced && (!previous || after >= previous + 5000);
            previous = after;
        }
        Check(spaced, "beats are at least five seconds apart");
        everything += text;
        CE_NetDestroy(session);
    }
    {   // The agent's progress is read at most about ten times a second.
        Reset(); CE_NetSession* session = Native(CE_NET_HOST);
        Frames frames(session);
        everything += frames.Run(1);
        signalFixture.progressReads = 0;
        const std::string text = Captured([&] {
            for (int i = 0; i < 100; ++i) {
                signalFixture.monotonic += 10;
                session->callbacks.poll(session->owner, session, signalFixture.monotonic);
            }
        });
        Check(signalFixture.progressReads >= 5 && signalFixture.progressReads <= 11, "ICE progress is sampled about every 100 ms");
        everything += text;
        CE_NetDestroy(session);
    }

    // Nothing the exchange carries may appear in what it printed: the grant, both descriptions, the record or a directory.
    Check(everything.find("authored-grant") == std::string::npos && everything.find("authored-peer") == std::string::npos &&
          everything.find("authored-local") == std::string::npos && everything.find(lobbyDirectory) == std::string::npos &&
          everything.find(gameDirectory) == std::string::npos && everything.find(std::string(argv[1])) == std::string::npos,
          "the trace carries no grant, description, record or directory");
    bool plain = true;
    for (const auto& line : Trace(everything))
        for (unsigned char byte : line) plain = plain && byte >= 0x20 && byte < 0x7f;
    Check(plain, "the trace is printable ASCII");
    Check(!Trace(everything).empty(), "the trace printed lines at all");
    CE_NetSelectSignaling({});
    std::printf("%d assertions, %d failures\n", assertions, failures);
    return failures ? 1 : 0;
}
