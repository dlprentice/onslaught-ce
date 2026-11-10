// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/feel_script.h: the parts of the frame-feel module (feel.h) that are plain arithmetic, kept free of the
// game, SDL and Direct3D so that a host test can run them: the authored input script, the mouse stream's integral and the
// display model's grid.
#ifndef PORT_FEEL_SCRIPT_H
#define PORT_FEEL_SCRIPT_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace feel
{
// The display model: with a grid of `period` nanoseconds anchored at `grid`, a Present that returns at `now` is held until
// the next grid time at or after `now` (a vertical sync); one that returns before the anchor, or exactly on a grid time, is not held.
inline uint64_t PaceTarget(uint64_t now, uint64_t grid, uint64_t period)
{
	if (!period || now <= grid) return now;
	uint64_t periods = (now - grid + period - 1) / period;
	return grid + periods * period;
}

struct KeyStep { double ms; unsigned sc, raw; bool down, done; };
struct MouseSeg { double from, until, vx, vy; };

enum ArmMode { ARM_NONE, ARM_PRESENT, ARM_STATE };

// A script is lines of text (feel.h lists them). Parsing keeps the accepted lines, as text, so that the log can carry the input a
// run was given.
class Script
{
public:
	std::vector<KeyStep> keys;
	std::vector<MouseSeg> mouse;
	std::vector<std::string> accepted, rejected;
	ArmMode armMode = ARM_NONE;
	unsigned armPresent = 0, armState = 0, armTicks = 20;
	double quitMs = -1;
	double shotsMs = -1;			// `shots MS COUNT DIR`: COUNT consecutive frames from the first Present at or after MS
	unsigned shotsCount = 0;
	std::string shotsDir;

	bool Empty() const { return armMode == ARM_NONE; }

	// Reads the whole text; returns true when it has an arm line.
	bool Parse(const char *text)
	{
		std::string all(text ? text : "");
		size_t at = 0;
		while (at <= all.size())
		{
			size_t eol = all.find('\n', at);
			std::string line = all.substr(at, eol == std::string::npos ? std::string::npos : eol - at);
			at = eol == std::string::npos ? all.size() + 1 : eol + 1;
			size_t hash = line.find('#');
			if (hash != std::string::npos) line.erase(hash);
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
			size_t first = line.find_first_not_of(" \t");
			if (first == std::string::npos) continue;
			line.erase(0, first);
			if (!ParseLine(line)) rejected.push_back(line);
			else accepted.push_back(line);
		}
		for (size_t i = 1; i < keys.size(); ++i)
			for (size_t j = i; j > 0 && keys[j].ms < keys[j - 1].ms; --j)
			{
				KeyStep t = keys[j];
				keys[j] = keys[j - 1];
				keys[j - 1] = t;
			}
		return !Empty();
	}

	// ---- the timeline, once armed ----
	void Arm(uint64_t nowNs)
	{
		if (armed) return;
		armed = true;
		armNs = nowNs;
		lastTakeMs = 0;
	}
	bool Armed() const { return armed; }
	double Ms(uint64_t nowNs) const { return armed ? (double)(nowNs - armNs) / 1e6 : -1.0; }

	// One tick of game phase `state` with `paused`: arms the state mode after armTicks consecutive unpaused ticks.
	bool TickArm(int state, int paused, uint64_t nowNs)
	{
		if (armed || armMode != ARM_STATE) return false;
		if (state == (int)armState && !paused)
		{
			if (++run >= armTicks)
			{
				Arm(nowNs);
				return true;
			}
		}
		else run = 0;
		return false;
	}
	bool PresentArm(unsigned presents, uint64_t nowNs)
	{
		if (armed || armMode != ARM_PRESENT || presents < armPresent) return false;
		Arm(nowNs);
		return true;
	}

	// The mouse counts due since the last call: the integral of the velocity of every segment over the interval, carrying
	// the fraction that is not yet a whole count, so the total delivered never differs from the integral by a count.
	void Take(uint64_t nowNs, long *dx, long *dy)
	{
		if (!armed || mouse.empty()) return;
		double nowMs = Ms(nowNs), from = lastTakeMs, fx = remX, fy = remY;
		if (nowMs < from) return;
		lastTakeMs = nowMs;
		for (const MouseSeg &s : mouse)
		{
			double lo = from > s.from ? from : s.from, hi = nowMs < s.until ? nowMs : s.until;
			if (hi > lo)
			{
				fx += s.vx * (hi - lo) / 1000.0;
				fy += s.vy * (hi - lo) / 1000.0;
			}
		}
		double ix = trunc(fx), iy = trunc(fy);
		remX = fx - ix;
		remY = fy - iy;
		*dx += (long)ix;
		*dy += (long)iy;
	}

	// The earliest key step that is due and not yet taken.
	bool KeyDue(uint64_t nowNs, KeyStep *out)
	{
		if (!armed) return false;
		double nowMs = Ms(nowNs);
		for (KeyStep &k : keys)
			if (!k.done && k.ms <= nowMs)
			{
				k.done = true;
				*out = k;
				return true;
			}
		return false;
	}

	// True for each Present from the shots time until COUNT have been asked for; `dir` is where they go.
	bool ShotDue(uint64_t nowNs, std::string *dir)
	{
		if (!armed || shotsMs < 0 || shotsTaken >= shotsCount || Ms(nowNs) < shotsMs) return false;
		++shotsTaken;
		*dir = shotsDir;
		return true;
	}

	// True once, when the quit time has come.
	bool QuitDue(uint64_t nowNs)
	{
		if (!armed || quitMs < 0 || quitSent || Ms(nowNs) < quitMs) return false;
		quitSent = true;
		return true;
	}

private:
	bool armed = false, quitSent = false;
	unsigned shotsTaken = 0;
	uint64_t armNs = 0;
	double lastTakeMs = 0, remX = 0, remY = 0;
	unsigned run = 0;

	bool ParseLine(const std::string &line)
	{
		const char *s = line.c_str();
		unsigned n = 0, k = 0, sc = 0, raw = 0;
		double ms = 0, until = 0, vx = 0, vy = 0;
		char ud = 0, tail = 0;
		if (sscanf(s, "arm present %u %c", &n, &tail) == 1)
		{
			armMode = ARM_PRESENT;
			armPresent = n;
			return true;
		}
		if (strncmp(s, "arm state ", 10) == 0)
		{
			int got = sscanf(s + 10, "%u %u %c", &n, &k, &tail);
			if (got != 1 && got != 2) return false;
			armMode = ARM_STATE;
			armState = n;
			armTicks = got == 2 && k ? k : 20;
			return true;
		}
		if (sscanf(s, "key %lf %c %u %x %c", &ms, &ud, &sc, &raw, &tail) == 4 && (ud == 'd' || ud == 'u') && ms >= 0)
		{
			keys.push_back({ms, sc, raw, ud == 'd', false});
			return true;
		}
		if (sscanf(s, "mouse %lf %lf %lf %lf %c", &ms, &until, &vx, &vy, &tail) == 4 && until >= ms && ms >= 0)
		{
			mouse.push_back({ms, until, vx, vy});
			return true;
		}
		if (sscanf(s, "quit %lf %c", &ms, &tail) == 1 && ms >= 0)
		{
			quitMs = ms;
			return true;
		}
		int used = 0;
		if (sscanf(s, "shots %lf %u %n", &ms, &n, &used) == 2 && used > 0 && s[used] && ms >= 0 && n > 0 && strlen(s + used) < 560)
		{
			shotsMs = ms;
			shotsCount = n;
			shotsDir = s + used;
			return true;
		}
		return false;
	}
};
}

#endif
