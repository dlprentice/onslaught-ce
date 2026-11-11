// Exercise the lobby with a synthetic interface list and the existing fake transport.
// This driver does not create a socket or inspect the machine's interfaces.
#define main notice_driver_main
#include "network_lobby_notice_driver.cpp"
#undef main

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

static int enumerate_count;
static int free_count;
static int interface_count = 2;

extern "C" int getifaddrs(struct ifaddrs **result) {
    static struct ifaddrs interfaces[2];
    static struct sockaddr_in addresses[2];
    static char first_name[] = "test_virtual";
    static char second_name[] = "test_lan";
    ++enumerate_count;
    memset(interfaces, 0, sizeof(interfaces));
    memset(addresses, 0, sizeof(addresses));
    const char *const values[] = {"10.111.0.2", "10.112.0.2"};
    char *const names[] = {first_name, second_name};
    for (int i = 0; i < interface_count; ++i) {
        addresses[i].sin_family = AF_INET;
        if (inet_pton(AF_INET, values[i], &addresses[i].sin_addr) != 1) return -1;
        interfaces[i].ifa_name = names[i];
        interfaces[i].ifa_flags = IFF_UP;
        interfaces[i].ifa_addr = reinterpret_cast<struct sockaddr *>(&addresses[i]);
        interfaces[i].ifa_next = i + 1 < interface_count ? &interfaces[i + 1] : NULL;
    }
    *result = interface_count ? &interfaces[0] : NULL;
    return 0;
}
extern "C" void freeifaddrs(struct ifaddrs *) { ++free_count; }

static int expect(bool result, int failure) { return result ? 0 : failure; }
static uint32_t parsed_address(const std::string &text) {
    struct in_addr address;
    if (inet_pton(AF_INET, text.c_str(), &address) != 1) return 0;
    return ntohl(address.s_addr);
}
static uint32_t hosted_address() {
    CE_LobbyStatus status;
    CE_LobbyStatusGet(&status);
    CE_JoinCode code;
    memset(&code, 0, sizeof(code));
    if (CE_CodeDecode(status.code, &code) != CE_CODE_OK) return 0;
    return code.ipv4;
}

int main() {
    unsetenv("BEA_NET_LOBBY_BIND");
    char choices[8][16];
    int failure = expect(CE_LobbyAddresses(choices, 8) == 2, 1);
    if (failure) return failure;
    const std::string first(choices[0]), second(choices[1]);
    if ((failure = expect(first != second, 2))) return failure;

    if ((failure = expect(CE_LobbyHost() != 0, 3))) return failure;
    const uint32_t automatic = hosted_address();
    CE_LobbyLeave();
    if ((failure = expect(automatic != 0, 4))) return failure;
    if ((failure = expect(automatic == parsed_address(first), 21))) return failure;

    if ((failure = expect(CE_LobbyHostAt(NULL) != 0, 5))) return failure;
    if ((failure = expect(hosted_address() == automatic, 6))) return failure;
    CE_LobbyLeave();
    if ((failure = expect(CE_LobbyHostAt(first.c_str()) != 0, 7))) return failure;
    if ((failure = expect(hosted_address() == automatic, 8))) return failure;
    CE_LobbyLeave();

    if ((failure = expect(CE_LobbyHostAt(second.c_str()) != 0, 9))) return failure;
    const uint32_t selected = hosted_address();
    if ((failure = expect(selected != 0 && selected != automatic, 10))) return failure;
    if ((failure = expect(selected == parsed_address(second), 22))) return failure;
    CE_LobbyStatus previous;
    CE_LobbyStatusGet(&previous);
    const int before_create = test_create_count;
    const int before_destroy = test_destroy_count;
    if ((failure = expect(CE_LobbyHostAt("not an address") == 0, 11))) return failure;
    if ((failure = expect(test_create_count == before_create, 12))) return failure;
    if ((failure = expect(test_destroy_count == before_destroy, 13))) return failure;
    CE_LobbyStatus status;
    CE_LobbyStatusGet(&status);
    if ((failure = expect(strstr(status.notice, "address") != NULL, 14))) return failure;
    if ((failure = expect(status.state == previous.state &&
                          strcmp(status.code, previous.code) == 0, 26))) return failure;
    if ((failure = expect(CE_LobbyHostAt("") == 0, 24))) return failure;
    if ((failure = expect(test_create_count == before_create, 25))) return failure;

    interface_count = 1;
    if ((failure = expect(CE_LobbyHostAt(second.c_str()) == 0, 15))) return failure;
    if ((failure = expect(test_create_count == before_create, 16))) return failure;
    CE_LobbyStatusGet(&status);
    if ((failure = expect(strstr(status.notice, "address") != NULL, 17))) return failure;

    interface_count = 0;
    if ((failure = expect(CE_LobbyHostAt(first.c_str()) == 0, 18))) return failure;
    if ((failure = expect(test_create_count == before_create, 19))) return failure;
    if ((failure = expect(enumerate_count == free_count, 20))) return failure;
    CE_LobbyLeave();
    if ((failure = expect(test_create_count == test_destroy_count, 23))) return failure;
    return 0;
}
