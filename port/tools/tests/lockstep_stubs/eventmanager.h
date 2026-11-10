#pragma once
struct MockEventManager { float GetTime(){return 0.f;} unsigned GetFrameCount(){return 0;} unsigned GetCurrentEventProcessNum(){return 0;} int TotalEvents(){return 0;} };
extern MockEventManager EVENT_MANAGER;
