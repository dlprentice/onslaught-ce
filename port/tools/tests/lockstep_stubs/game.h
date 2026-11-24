#pragma once
enum { QT_QUIT_TO_SYSTEM = 3, QT_RESTART_LEVEL = 4, QT_QUIT_TO_FRONTEND = 5 };
enum EGameState {
    GAME_STATE_PLAYING = 1,
    GAME_STATE_LEVEL_WON = 2,
    GAME_STATE_LEVEL_LOST = 3,
    GAME_STATE_PLAYER_1_WON = 4,
    GAME_STATE_PLAYER_2_WON = 5,
    GAME_STATE_GAME_DRAWN = 6,
    GAME_STATE_QUIT = 7
};
enum { MAX_CAREER_SLOTS = 32 };
struct MockGame {
    int state=GAME_STATE_PLAYING;
    unsigned slot_words[MAX_CAREER_SLOTS] = {};
    int GetGameState(){return state;}
    int GetNPlayers(){return 2;}
    float GetFrameRenderFraction(){return 1.0f;}
    unsigned Random(){return 0;}
    void *GetPlayer(int){return this;}
    int GetSlot(int slot){return slot>=0 && slot<MAX_CAREER_SLOTS*8 && (slot_words[slot>>5] & (1u<<(slot&31)))!=0;}
};
extern MockGame GAME;
