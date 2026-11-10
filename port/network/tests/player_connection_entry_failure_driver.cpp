// SPDX-License-Identifier: GPL-3.0-only
// Status updates only: no menu actions, keyboard, pad, SDL, sockets or game data.
#define main ConnectionEntryUnusedFixtureMain
#include "player_connection_entry_driver.cpp"
#undef main

static void FailureStatus(decltype(fixture_status.state) state, bool host,
                          const char *notice)
{
    fixture_status = {};
    fixture_status.state = state;
    fixture_status.host = host;
    snprintf(fixture_status.notice, sizeof(fixture_status.notice), "%s", notice);
    fixture_leave_calls = fixture_host_calls = fixture_join_calls = 0;
}

static void HostFailureUpdates()
{
    const char *cause = "Authored relay failure. Use fresh private setup.";
    for (int in_lobby = 0; in_lobby != 2; ++in_lobby)
    {
        sMenu->Show(in_lobby ? NP_LOBBY : NP_HOST);
        FailureStatus(CE_LOBBY_FAILED, true, cause);
        sMenu->Update();
        Check(sMenu->mPage == NP_MENU, "terminal host returns to the menu");
        Check(std::string(sMenu->mNotice) == cause, "host preserves the exact backend notice");
        Check(fixture_leave_calls == 1, "host deliberately retires the failed session once");
        Check(fixture_status.state == CE_LOBBY_OFF && fixture_status.notice[0] == 0,
              "provider teardown clears its own status and notice");
        Check(fixture_host_calls == 0 && fixture_join_calls == 0,
              "host failure does not create another connection");
        fixture_drawn.clear();
        sMenu->DrawText();
        Check(DrawnContains(cause), "the retained host cause reaches the draw boundary");
        sMenu->Update();
        Check(sMenu->mPage == NP_MENU && std::string(sMenu->mNotice) == cause,
              "later idle update retains the failure notice");
        Check(fixture_leave_calls == 1, "later idle update does not leave twice");
    }
}

static void GuestTerminalUpdates()
{
    const char *cause = "Authored guest terminal notice.";
    const decltype(fixture_status.state) states[] = {CE_LOBBY_FAILED, CE_LOBBY_LEFT};
    for (int in_lobby = 0; in_lobby != 2; ++in_lobby)
        for (const auto state : states)
        {
            sMenu->Show(in_lobby ? NP_LOBBY : NP_JOIN);
            FailureStatus(state, false, cause);
            sMenu->Update();
            Check(sMenu->mPage == NP_JOIN, "guest terminal status retains join recovery");
            Check(std::string(sMenu->mNotice) == cause, "guest preserves the exact backend notice");
            Check(fixture_leave_calls == 1, "guest terminal status retires its session");
            Check(fixture_host_calls == 0 && fixture_join_calls == 0,
                  "guest recovery does not automatically reconnect");
        }
}

static void HostNonterminalUpdates()
{
    sMenu->Show(NP_HOST);
    FailureStatus(CE_LOBBY_READY, true, "");
    sMenu->Update();
    Check(sMenu->mPage == NP_LOBBY, "ready host reaches the existing lobby");
    Check(fixture_leave_calls == 0, "ready host retains its session");

    const decltype(fixture_status.state) states[] = {CE_LOBBY_HOST_WAITING, CE_LOBBY_HOST_CONNECTING};
    for (const auto state : states)
    {
        sMenu->Show(NP_LOBBY);
        FailureStatus(state, true, "");
        sMenu->Update();
        Check(sMenu->mPage == NP_HOST, "waiting host retains the existing host page");
        Check(fixture_leave_calls == 0, "waiting host retains its session");
    }

    const char *cause = "Authored host setup refusal.";
    sMenu->Show(NP_HOST);
    FailureStatus(CE_LOBBY_OFF, true, cause);
    sMenu->Update();
    Check(sMenu->mPage == NP_MENU, "off host retains its existing menu return");
    Check(std::string(sMenu->mNotice) == cause, "off host retains its existing notice");
    Check(fixture_leave_calls == 0, "off host does not retire a second time");
}

int main()
{
    fixture_clear_notice_on_leave = true;
    PortCeNetOpen();
    HostFailureUpdates();
    GuestTerminalUpdates();
    HostNonterminalUpdates();
    PortCeNetShutdown();
    printf("{\"checks\":%d,\"failures\":%d,\"status_updates_only\":true}\n", checks, failures);
    return failures ? 1 : 0;
}
