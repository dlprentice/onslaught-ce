#include "network.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
int main(int argc, char **argv) {
 if (argc != 3) return 10;
 CE_NetConfig config{};
 config.role = CE_NET_GUEST;
 config.ipv4 = "127.0.0.1";
 config.port = static_cast<uint16_t>(std::strtoul(argv[1], nullptr, 10));
 std::memset(config.secret, 0x5a, sizeof config.secret);
 std::memset(config.compatibility, 0x3c, sizeof config.compatibility);
 std::snprintf(config.version, sizeof config.version, "authored-fixture");
 CE_NetSession *session = CE_NetCreate(&config);
 if (!session) return 11;
 const bool poll = std::strcmp(argv[2], "poll") == 0;
 const uint64_t start = CE_NetNowMs();
 CE_NetStatus status{};
 while (CE_NetNowMs() - start < 7000) {
  if (poll) CE_NetPoll(session, CE_NetNowMs(), 64);
  CE_NetGetStatus(session, &status);
  if (status.phase == CE_NET_FAILED || status.phase == CE_NET_CLOSED) break;
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }
 const uint64_t elapsed = CE_NetNowMs() - start;
 const bool failed = status.phase == CE_NET_FAILED;
 const bool auth = status.error == CE_NET_AUTH;
 std::printf("{\"failed\":%s,\"auth_class\":%s,\"elapsed_ms\":%llu}\n", failed ? "true" : "false", auth ? "true" : "false", static_cast<unsigned long long>(elapsed));
 CE_NetDestroy(session);
 return 0;
}
