#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read a frame-feel log (port/platform/common/feel.h) and report how the game paced, ticked, moved its camera and answered input.

    python3 port/tools/feel_report.py LOG [--from-ms A] [--to-ms B] [--after-arm] [--json OUT] [--quiet]
    python3 port/tools/feel_report.py --compare BEFORE.json AFTER.json

A run's loop is a sequence of passes: the window layer's pump, zero or more game updates (ticks), the scene's draws, then Present.
The report is built from those parts:

- pacing: the interval between successive Presents returning, its distribution and its hitches;
- ticks: updates per Present, the interval between updates, and how the game's own render fraction advances between them;
- camera: the view matrix the world was drawn with in each frame (the transform with the most draws behind it, a perspective
  projection, not a mirror), its movement per Present, and whether that movement is steady or steps with the ticks;
- input: for a scripted mouse stream or key, the time from the input to the first frame that shows it and to most of its effect;
- cost: the main thread's CPU time per frame beside wall time, draw counts, and the GPU timestamps when the log has them.

Times are the log's own: nanoseconds on the real clock. Nothing here reads the game or its data. The numbers describe the game up
to Present; a real display adds its own pipeline after that, the same for any variant compared."""
import argparse
import json
import math
import sys
from collections import defaultdict
from statistics import mean, median

NS = 1e-6      # nanoseconds to milliseconds


# ---------------------------------------------------------------------------------------------------------------- reading --

class Log:
    def __init__(self):
        self.header = []
        self.presents = []      # dicts, in order
        self.ticks = []         # (t, n, level, state, paused, evt)
        self.pumps = []
        self.events = []        # (t, type, age_ns)
        self.mouse = []         # (t, dx, dy, buttons)
        self.keys = []          # (t, count, hash)
        self.begins = []
        self.ends = []          # (t, draws, tris, scene)
        self.views = []         # (t, draws_so_far, tris_so_far, matrix)
        self.projs = []
        self.games = []         # (t, fraction, fraction2, level, state, paused)
        self.gpu = {}           # frame -> (start_ns, end_ns)
        self.notes = []         # (t, text)
        self.flushes = []       # (t, duration_ns, written)
        self.threads = []       # (t, tid, cpu_ns, sys_ns, name)


def load(path):
    log = Log()
    with open(path, errors="replace") as f:
        for line in f:
            if not line.strip():
                continue
            if line.startswith("#"):
                log.header.append(line.rstrip("\n"))
                continue
            kind = line[0]
            parts = line.split()
            try:
                if kind == "P":
                    log.presents.append(dict(frame=int(parts[1]), begin=int(parts[2]), real_end=int(parts[3]), end=int(parts[4]),
                                             hr=int(parts[5], 16), cpu_begin=int(parts[6]), cpu_end=int(parts[7]),
                                             minflt=float(parts[8]), csw=float(parts[9]), heap_kb=float(parts[10])))
                elif kind == "T":
                    log.ticks.append((int(parts[1]), int(parts[2]), int(parts[3]), int(parts[4]), int(float(parts[5])), float(parts[6])))
                elif kind == "U":
                    log.pumps.append(int(parts[1]))
                elif kind == "N":
                    log.events.append((int(parts[1]), int(parts[2]), int(parts[3])))
                elif kind == "M":
                    log.mouse.append((int(parts[1]), float(parts[2]), float(parts[3]), int(parts[4])))
                elif kind == "K":
                    log.keys.append((int(parts[1]), int(parts[2]), int(parts[3], 16)))
                elif kind == "B":
                    log.begins.append(int(parts[1]))
                elif kind == "E":
                    log.ends.append((int(parts[1]), int(parts[2]), int(parts[3]), int(parts[4])))
                elif kind in "VJ":
                    rec = (int(parts[1]), int(parts[2]), int(parts[3]), tuple(float(x) for x in parts[4:20]))
                    (log.views if kind == "V" else log.projs).append(rec)
                elif kind == "F":
                    row = (int(parts[1]), float(parts[2]), float(parts[3]), int(parts[4]), int(parts[5]), int(parts[6]))
                    if len(parts) >= 19:
                        row += tuple(float(x) for x in parts[7:19])      # the driven unit's displayed and simulated pose
                        if len(parts) > 19 and parts[19] == "m":
                            row += ("m",)                                # or, tagged, the paused map's window (corner x, corner y, span, then zeros)
                    log.games.append(row)
                elif kind == "G":
                    log.gpu[int(parts[2])] = (int(parts[3]), int(parts[4]))
                elif kind == "Q":
                    log.notes.append((int(parts[1]), line.split(None, 2)[2].rstrip("\n") if len(parts) > 2 else ""))
                elif kind == "X":
                    log.flushes.append((int(parts[1]), int(parts[2]), int(parts[3])))
                elif kind == "H":
                    log.threads.append((int(parts[1]), int(parts[2]), int(parts[3]), int(parts[4]), parts[5] if len(parts) > 5 else ""))
            except (ValueError, IndexError):
                continue        # a truncated last line of a log that was cut short
    log.presents.sort(key=lambda p: p["frame"])
    # Events are written in the order they happened, except a Present, which is stamped with the time it began.
    log.ticks.sort()
    log.pumps.sort()
    log.mouse.sort()
    log.begins.sort()
    log.ends.sort()
    log.views.sort(key=lambda v: v[0])
    log.projs.sort(key=lambda v: v[0])
    log.games.sort()
    log.notes.sort(key=lambda n: n[0])
    return log


# --------------------------------------------------------------------------------------------------------------- statistics --

def pct(values, q):
    if not values:
        return float("nan")
    s = sorted(values)
    k = (len(s) - 1) * q
    lo, hi = math.floor(k), math.ceil(k)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def dist(values, unit=1.0):
    v = [x * unit for x in values]
    if not v:
        return dict(n=0)
    m = mean(v)
    sd = math.sqrt(sum((x - m) ** 2 for x in v) / len(v))
    return dict(n=len(v), mean=m, sd=sd, min=min(v), p1=pct(v, .01), p50=pct(v, .5), p90=pct(v, .9), p95=pct(v, .95),
                p99=pct(v, .99), p999=pct(v, .999), max=max(v))


# --------------------------------------------------------------------------------------------------------------- frame model --

def bucket(sorted_times, lo, hi):
    """Indices of the sorted times in [lo, hi)."""
    import bisect
    return bisect.bisect_left(sorted_times, lo), bisect.bisect_left(sorted_times, hi)


def det3(m):
    return (m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]))


def is_perspective(p):
    return abs(p[15]) < 1e-6 and abs(p[11] - 1.0) < 1e-3 or abs(p[15]) < 1e-6 and abs(p[11]) > 0.5


def pose(v):
    """Eye position and forward axis from a Direct3D view matrix (row vectors; rows 0..2 hold the camera axes in their columns)."""
    right = (v[0], v[4], v[8])
    up = (v[1], v[5], v[9])
    fwd = (v[2], v[6], v[10])
    t = (v[12], v[13], v[14])
    eye = tuple(-(t[0] * right[i] + t[1] * up[i] + t[2] * fwd[i]) for i in range(3))
    return eye, fwd


def rotation_angle(a, b):
    """Angle of the rotation taking view a's orientation to view b's (radians). From the sine and cosine of the angle, so that a
    camera that has not turned reads as zero: the arccosine of the trace alone cannot see an angle below a milliradian in a float matrix."""
    ra = [[a[0], a[1], a[2]], [a[4], a[5], a[6]], [a[8], a[9], a[10]]]
    rb = [[b[0], b[1], b[2]], [b[4], b[5], b[6]], [b[8], b[9], b[10]]]
    # relative rotation r = ra * rb^T
    r = [[sum(ra[i][k] * rb[j][k] for k in range(3)) for j in range(3)] for i in range(3)]
    sx, sy, sz = r[2][1] - r[1][2], r[0][2] - r[2][0], r[1][0] - r[0][1]
    sine = math.sqrt(sx * sx + sy * sy + sz * sz) / 2.0
    cosine = (r[0][0] + r[1][1] + r[2][2] - 1.0) / 2.0
    return math.atan2(sine, cosine)


def build_frames(log, t_from=None, t_to=None):
    """One record per Present, with what happened in its pass. Pass i starts when Present i-1 returned (after any display model)."""
    frames = []
    prev_end = 0
    ticks_t = [t[0] for t in log.ticks]
    views_t = [v[0] for v in log.views]
    projs_t = [v[0] for v in log.projs]
    mouse_t = [m[0] for m in log.mouse]
    begins_t = [b for b in log.begins]
    ends_t = [e[0] for e in log.ends]
    games_t = [g[0] for g in log.games]
    pumps_t = log.pumps
    for p in log.presents:
        start, stop = prev_end, p["begin"]
        f = dict(p)
        f["pass_start"] = start
        i, j = bucket(ticks_t, start, stop)
        f["ticks"] = log.ticks[i:j]
        i, j = bucket(pumps_t, start, stop)
        f["pump"] = pumps_t[i] if i < j else None
        i, j = bucket(begins_t, start, stop)
        f["first_begin"] = begins_t[i] if i < j else None
        i, j = bucket(ends_t, start, stop)
        f["last_end"] = ends_t[j - 1] if i < j else None
        f["scenes"] = log.ends[i:j]
        f["draws"] = sum(e[1] for e in f["scenes"])
        f["tris"] = sum(e[2] for e in f["scenes"])
        i, j = bucket(mouse_t, start, stop)
        f["mouse"] = log.mouse[i:j]
        # the game's own sample is taken just after Present returns, so it belongs to the pass it ends
        i, j = bucket(games_t, p["begin"], p["end"] + 1)
        f["game"] = log.games[i] if i < len(log.games) and games_t[i] < p["end"] + 1 and games_t[i] >= p["begin"] else None
        # views and projections of this pass
        vi, vj = bucket(views_t, start, stop)
        pi, pj = bucket(projs_t, start, stop)
        events = sorted([(v[0], "V", v) for v in log.views[vi:vj]] + [(q[0], "J", q) for q in log.projs[pi:pj]], key=lambda e: e[0])
        world = None
        cur_v, cur_p = None, None
        for k, (t, kind, rec) in enumerate(events):
            if kind == "V":
                cur_v = rec
            else:
                cur_p = rec
            nxt = events[k + 1][2][1] if k + 1 < len(events) else f["draws"]
            draws = nxt - rec[1]
            if cur_v is not None and cur_p is not None and is_perspective(cur_p[3]) and det3(cur_v[3]) > 0:
                if world is None or draws > world["draws"]:
                    world = dict(t=cur_v[0], draws=draws, view=cur_v[3], proj=cur_p[3])
        f["world"] = world
        f["views_set"] = len(log.views[vi:vj])
        frames.append(f)
        prev_end = p["end"]
    if t_from is not None or t_to is not None:
        lo = t_from if t_from is not None else -1
        hi = t_to if t_to is not None else float("inf")
        frames = [f for f in frames if lo <= f["end"] <= hi]
    return frames


def flush_spans(log):
    return [(t - d, t) for t, d, _ in log.flushes]


def clean(frames, log):
    """Drop the frames a log write or a screenshot readback landed in: they measure the measuring. A frame written out by the input
    script's shots verb (or the shots switch) shows as a Present call of milliseconds where an ordinary one takes a fraction of one."""
    spans = flush_spans(log)
    shots = any(x.startswith("script shots ") for _, x in log.notes)
    keep, dropped = [], 0
    for f in frames:
        if any(not (f["end"] < a or f["pass_start"] > b) for a, b in spans):
            dropped += 1
            continue
        if shots and f["real_end"] - f["begin"] > 2_500_000:
            dropped += 1
            continue
        keep.append(f)
    return keep, dropped


# ------------------------------------------------------------------------------------------------------------------ metrics --

def pairs(frames):
    """Successive frames only: a frame dropped from the sequence (a log write landed in it) breaks the chain."""
    return [(a, b) for a, b in zip(frames, frames[1:]) if b["frame"] == a["frame"] + 1]


def pacing(frames):
    iv = [b["end"] - a["end"] for a, b in pairs(frames)]
    if not iv:
        return dict(n=0)
    med = median(iv)
    out = dict(interval_ms=dist(iv, NS), fps_mean=1e9 / mean(iv), fps_p1_low=1e9 / pct(iv, .99))
    out["over_1_5x_median"] = sum(1 for x in iv if x > 1.5 * med)
    out["over_2x_median"] = sum(1 for x in iv if x > 2.0 * med)
    out["over_33ms"] = sum(1 for x in iv if x > 33.4e6)
    out["over_50ms"] = sum(1 for x in iv if x > 50e6)
    out["over_100ms"] = sum(1 for x in iv if x > 100e6)
    out["time_in_hitches_pct"] = 100.0 * sum(x for x in iv if x > 1.5 * med) / sum(iv)
    return out


REFRESH_RATES = (60, 75, 90, 120, 144, 165, 240)


def refresh_budget(frames):
    """What the measured work per frame would do on displays of various refresh rates, if each frame waited for the next vertical sync
    after it was done (a model: it takes the work from the log and assumes the display adds nothing). The work of a frame is the time from
    the start of its pass to the return of Present, less the time the display model itself held it, so a run with the model on gives the
    same answer as one without."""
    work = [(f["real_end"] - f["pass_start"]) for f in frames if f["pass_start"]]
    if len(work) < 10:
        return {}
    out = {}
    for hz in REFRESH_RATES:
        period = 1e9 / hz
        flips = [max(1, math.ceil(w / period)) for w in work]
        out[str(hz)] = dict(over_budget_pct=round(100.0 * sum(1 for w in work if w > period) / len(work), 2),
                            predicted_fps=round(hz / mean(flips), 1),
                            frames_two_or_more_syncs_pct=round(100.0 * sum(1 for f in flips if f >= 2) / len(flips), 2))
    out["work_ms"] = dist(work, NS)
    return out


def cost(frames, log):
    out = {}
    cpu, wall, busy = [], [], []
    for a, b in pairs(frames):
        w = b["end"] - a["end"]
        c = b["cpu_end"] - a["cpu_end"]
        if w > 0 and c >= 0 and a["cpu_end"] and b["cpu_end"]:
            wall.append(w)
            cpu.append(c)
            busy.append(100.0 * c / w)
    if cpu:
        out["main_thread_cpu_ms"] = dist(cpu, NS)
        out["main_thread_busy_pct"] = dist(busy)
    pre, render, present, wait = [], [], [], []
    for f in frames:
        if f["first_begin"] is not None:
            pre.append(f["first_begin"] - f["pass_start"])
        if f["first_begin"] is not None and f["last_end"] is not None:
            render.append(f["last_end"] - f["first_begin"])
        present.append(f["real_end"] - f["begin"])
        wait.append(f["end"] - f["real_end"])
    out["pass_start_to_first_scene_ms"] = dist(pre, NS)
    out["first_scene_to_last_endscene_ms"] = dist(render, NS)
    out["present_call_ms"] = dist(present, NS)
    out["display_model_wait_ms"] = dist(wait, NS)
    out["draw_calls_per_frame"] = dist([f["draws"] for f in frames])
    out["triangles_per_frame"] = dist([f["tris"] for f in frames])
    out["scenes_per_frame"] = dist([len(f["scenes"]) for f in frames])
    # Updates: from the start of one to whichever of the next update or the first scene comes first.
    durations = []
    for f in frames:
        ts = [t[0] for t in f["ticks"]]
        for k, t in enumerate(ts):
            stop = ts[k + 1] if k + 1 < len(ts) else (f["first_begin"] or f["begin"])
            durations.append(stop - t)
    out["update_and_prework_ms"] = dist(durations, NS)
    # GPU: the interval between a frame's start and end timestamps, and the gap between successive frame ends.
    g = log.gpu
    if g:
        spans = [e - s for s, e in g.values() if e >= s]
        order = sorted(g)
        cad = [g[b][1] - g[a][1] for a, b in zip(order, order[1:]) if b == a + 1]
        out["gpu_start_to_end_ms"] = dist(spans, NS)
        out["gpu_frame_to_frame_ms"] = dist(cad, NS)
    if frames and frames[0]["minflt"]:
        flts = [b["minflt"] - a["minflt"] for a, b in pairs(frames)]
        out["minor_faults_per_frame"] = dist(flts)
        out["minor_faults_over_100_frames"] = sum(1 for x in flts if x > 100)
    threads = thread_cpu(log)
    if threads:
        out["thread_cpu_seconds"] = threads
    return out


def thread_cpu(log):
    """CPU seconds each thread used between the first and last census (Linux), summed by thread name. A census is the run of thread
    records written together: records less than 50 ms apart belong to one."""
    records = sorted(log.threads)
    groups, last = [], None
    for rec in records:
        if last is None or rec[0] - last > 50_000_000:
            groups.append([])
        groups[-1].append(rec)
        last = rec[0]
    if len(groups) < 2:
        return {}
    first = {tid: cpu for _, tid, cpu, _, _ in groups[0]}
    out = defaultdict(float)
    for _, tid, cpu, _, name in groups[-1]:
        out[name] += (cpu - first.get(tid, 0)) / 1e9
    return {k: round(v, 3) for k, v in sorted(out.items(), key=lambda kv: -kv[1]) if v >= 0.01}


def ticks_report(frames, log):
    per = [len(f["ticks"]) for f in frames]
    hist = defaultdict(int)
    for n in per:
        hist[n] += 1
    all_ticks = [t[0] for f in frames for t in f["ticks"]]
    iv = [b - a for a, b in zip(all_ticks, all_ticks[1:])]
    out = dict(ticks_per_frame={str(k): hist[k] for k in sorted(hist)}, tick_interval_ms=dist(iv, NS))
    # the render fraction against the time since the last update
    last_tick = None
    ticks_sorted = sorted(t[0] for t in log.ticks)
    import bisect
    resid, back, flat = [], 0, 0
    prev = None
    for f in frames:
        g = f["game"]
        if not g:
            continue
        k = bisect.bisect_right(ticks_sorted, f["begin"]) - 1
        if k < 0:
            continue
        since = f["begin"] - ticks_sorted[k]
        fr = g[1]
        if iv:
            tick_len = median(iv)
            resid.append(fr - since / tick_len)
        if prev is not None and fr < prev[0] and not f["ticks"]:
            back += 1
        if prev is not None and abs(fr - prev[0]) < 1e-9 and not f["ticks"]:
            flat += 1
        prev = (fr,)
    if resid:
        out["fraction_minus_elapsed_over_tick"] = dist(resid)
    out["fraction_backwards_without_tick"] = back
    out["fraction_unchanged_without_tick"] = flat
    fr_all = [f["game"][1] for f in frames if f["game"]]
    if fr_all:
        out["fraction_values"] = dist(fr_all)
    return out


def camera(frames, window=None):
    """Per-Present movement of the world view and how it relates to the update cadence."""
    series = [f for f in frames if f["world"]]
    if window:
        series = [f for f in series if window[0] <= f["end"] <= window[1]]
    steps = []
    for a, b in pairs(series):
        dt = b["end"] - a["end"]
        if dt <= 0:
            continue
        ea, _ = pose(a["world"]["view"])
        eb, _ = pose(b["world"]["view"])
        dpos = math.dist(ea, eb)
        drot = rotation_angle(a["world"]["view"], b["world"]["view"])
        steps.append(dict(t=b["end"], dt=dt, dpos=dpos, drot=drot, ticks=len(b["ticks"]), f=b))
    out = dict(frames_with_world_view=len(series), views_set_per_frame=dist([f["views_set"] for f in frames]))
    if not steps:
        return out
    speed = [s["dpos"] / (s["dt"] * 1e-9) for s in steps]
    omega = [s["drot"] / (s["dt"] * 1e-9) for s in steps]
    out["position_speed_units_per_s"] = dist(speed)
    out["rotation_rate_rad_per_s"] = dist(omega)
    for name, vals, floor in (("position", speed, 1e-3), ("rotation", omega, 1e-5)):
        moving = [v for v in vals if v > floor]
        if len(moving) >= 20:
            m = median(moving)
            out["%s_moving_frames" % name] = len(moving)
            out["%s_cv_while_moving" % name] = (math.sqrt(sum((v - mean(moving)) ** 2 for v in moving) / len(moving)) / mean(moving))
            # the same over the whole stretch from the first moving frame to the last, standing-still frames included: a camera that
            # moves only on some frames has an even speed when it moves and a large spread over the stretch
            first = next(i for i, v in enumerate(vals) if v > floor)
            last = len(vals) - 1 - next(i for i, v in enumerate(reversed(vals)) if v > floor)
            span = vals[first:last + 1]
            out["%s_cv_over_span" % name] = math.sqrt(sum((v - mean(span)) ** 2 for v in span) / len(span)) / mean(span)
            out["%s_frames_standing_still_in_motion" % name] = sum(1 for v in span if v <= floor) if m > 100 * floor else 0
    # Phase profile: mean movement per frame by where the frame falls in the update period.
    ticks_sorted = sorted(t[0] for t in (x for f in frames for x in f["ticks"]))
    import bisect
    ivs = [b - a for a, b in zip(ticks_sorted, ticks_sorted[1:])]
    if ivs:
        tick_len = median(ivs)
        bins = defaultdict(list)
        for s, v_pos, v_rot in zip(steps, speed, omega):
            k = bisect.bisect_right(ticks_sorted, s["t"]) - 1
            if k < 0:
                continue
            ph = min(0.999, (s["t"] - ticks_sorted[k]) / tick_len)
            bins[int(ph * 5)].append((v_pos, v_rot))
        out["speed_by_update_phase"] = {str(b): dict(pos=mean(x[0] for x in v), rot=mean(x[1] for x in v), n=len(v)) for b, v in sorted(bins.items())}
    return out


def tick_motion(frames):
    """The motion each game update produced, as the displayed camera shows it: the pose in the first frame after each update,
    compared with the one after the update before (the interpolation is at the same point of the period in both)."""
    firsts = [f for f in frames if f["ticks"] and f["world"]]
    rows = []
    for a, b in zip(firsts, firsts[1:]):
        ta, tb = a["ticks"][-1], b["ticks"][0]
        if tb[1] != ta[1] + 1:
            continue
        dt = (tb[0] - ta[0]) * 1e-9
        if dt <= 0:
            continue
        ea, _ = pose(a["world"]["view"])
        eb, _ = pose(b["world"]["view"])
        rows.append((math.dist(ea, eb) / dt, rotation_angle(a["world"]["view"], b["world"]["view"]) / dt))
    out = {}
    if len(rows) < 8:
        return out
    speeds = [r[0] for r in rows]
    rots = [r[1] for r in rows]
    out["per_update_speed_units_per_s"] = dist(speeds)
    out["per_update_rotation_rate_rad_per_s"] = dist(rots)
    for name, vals, floor in (("speed", speeds, 0.05), ("rotation", rots, 0.005)):
        moving = [v for v in vals if v > floor]
        if len(moving) >= 8:
            m = mean(moving)
            changes = [abs(b - a) / m for a, b in zip(vals, vals[1:]) if a > floor and b > floor]
            if changes:
                out["per_update_%s_change_over_mean" % name] = dist(changes)
            n = len(moving)
            var = sum((v - m) ** 2 for v in moving) / n
            if var > 0 and n > 24:
                ac = {}
                for lag in (1, 2, 3, 6, 12):
                    ac[str(lag)] = round(sum((moving[i] - m) * (moving[i + lag] - m) for i in range(n - lag)) / ((n - lag) * var), 3)
                out["per_update_%s_autocorrelation_by_lag" % name] = ac
    return out


def driven_unit(frames):
    """While a unit is driven (the frame samples carry its pose): where the camera sits relative to it, and how smoothly it moves.
    The offset is the eye's position from the unit's displayed position, split along the unit's displayed heading, across it and up."""
    rows = []
    for f in frames:
        g = f["game"]
        if not f["world"] or not g or len(g) < 18 or (len(g) > 18 and g[18] == "m"):
            continue
        eye, _ = pose(f["world"]["view"])
        rp, sp, rh, sh = g[6:9], g[9:12], g[12:15], g[15:18]
        norm = math.hypot(rh[0], rh[1])
        if norm < 1e-6:
            continue
        hx, hy = rh[0] / norm, rh[1] / norm
        d = [eye[i] - rp[i] for i in range(3)]
        rows.append(dict(frame=f["frame"], end=f["end"], forward=d[0] * hx + d[1] * hy, side=-d[0] * hy + d[1] * hx, up=d[2],
                         pos=rp, sim=sp, heading=math.atan2(rh[1], rh[0]), sim_heading=math.atan2(sh[1], sh[0])))
    out = {}
    if len(rows) < 20:
        return out
    out["frames_driving"] = len(rows)
    for key in ("forward", "side", "up"):
        vals = [r[key] for r in rows]
        out["camera_offset_%s" % key] = dist(vals)
    steps = defaultdict(list)
    prev = None
    for r in rows:
        if prev and r["frame"] == prev["frame"] + 1:
            for key in ("forward", "side", "up"):
                steps[key].append(abs(r[key] - prev[key]))
        prev = r
    for key, vals in steps.items():
        out["camera_offset_%s_change_per_frame" % key] = dist(vals)
    # the unit's own displayed motion
    speeds, turns, sim_gap = [], [], []
    prev = None
    for r in rows:
        if prev and r["frame"] == prev["frame"] + 1:
            dt = (r["end"] - prev["end"]) * 1e-9
            if dt > 0:
                speeds.append(math.dist(r["pos"], prev["pos"]) / dt)
                dh = (r["heading"] - prev["heading"] + math.pi) % (2 * math.pi) - math.pi
                turns.append(abs(dh) / dt)
        sim_gap.append(math.dist(r["pos"], r["sim"]))
        prev = r
    out["unit_displayed_speed_units_per_s"] = dist(speeds)
    out["unit_displayed_turn_rad_per_s"] = dist(turns)
    out["displayed_minus_simulated_position"] = dist(sim_gap)
    moving = [v for v in speeds if v > 0.05]
    if len(moving) >= 20:
        out["unit_displayed_speed_cv_while_moving"] = math.sqrt(sum((v - mean(moving)) ** 2 for v in moving) / len(moving)) / mean(moving)
    return out


def view_angles(v):
    """Heading, pitch and roll of a world view matrix, in radians. The game's z axis points down: pitch is positive looking down, roll positive when the camera's
    right side is the lower; the heading is the forward axis' angle from the x axis. (A Direct3D view has the camera's right, up and forward axes down its columns.)"""
    fx, fy, fz = v[2], v[6], v[10]
    pitch = math.asin(max(-1.0, min(1.0, fz)))
    level = math.hypot(fx, fy)
    roll = math.asin(max(-1.0, min(1.0, v[8] / level))) if level > 1e-3 else 0.0
    return math.atan2(fy, fx), pitch, roll


def driven_view_angles(frames):
    """While a unit is driven: how smoothly the world view's heading, pitch and roll change from Present to Present. A camera hung from a unit on uneven ground
    changed its tilt in steps of the update (20 a second) where the unit's own displayed pose was smooth: the change per frame is large on the frames that
    hold an update and nothing on the others. For each axis: the change per frame, the second difference (how abruptly the rate of change itself changes: zero for a
    steady turn, large for steps), and the share of all the motion that happened on frames holding an update, against the share of frames that do (a smooth camera
    has the two alike)."""
    rows = []
    for f in frames:
        g = f["game"]
        if not f["world"] or not g or len(g) < 18 or (len(g) > 18 and g[18] == "m"):
            continue
        rows.append((f, view_angles(f["world"]["view"])))
    if len(rows) < 20:
        return {}
    out = {"frames": len(rows)}
    chains = []
    chain = [rows[0]]
    for row in rows[1:]:
        if row[0]["frame"] == chain[-1][0]["frame"] + 1:
            chain.append(row)
        else:
            chains.append(chain)
            chain = [row]
    chains.append(chain)
    deg = 180.0 / math.pi
    wrap = lambda a: (a + math.pi) % (2 * math.pi) - math.pi
    steps = {name: [] for name in ("heading", "pitch", "roll")}      # (|change| in degrees, on a frame that holds an update)
    seconds = {name: [] for name in steps}
    for chain in chains:
        for k in range(1, len(chain)):
            (fa, aa), (fb, ab) = chain[k - 1], chain[k]
            on_update = bool(fb["ticks"])
            for i, name in enumerate(steps):
                steps[name].append((abs(wrap(ab[i] - aa[i])) * deg, on_update))
        for k in range(2, len(chain)):
            for i, name in enumerate(seconds):
                a, b, c = chain[k - 2][1][i], chain[k - 1][1][i], chain[k][1][i]
                seconds[name].append(abs(wrap(wrap(c - b) - wrap(b - a))) * deg)
    n_pairs = len(steps["heading"])
    if n_pairs:
        out["frames_holding_an_update_pct"] = 100.0 * sum(1 for _, u in steps["heading"] if u) / n_pairs
    for name in steps:
        out["%s_change_per_frame_deg" % name] = dist([d for d, _ in steps[name]])
        out["%s_second_difference_deg" % name] = dist(seconds[name])
        total = sum(d for d, _ in steps[name])
        if total > 1e-6:
            out["%s_motion_on_update_frames_pct" % name] = 100.0 * sum(d for d, u in steps[name] if u) / total
    return out


# ----------------------------------------------------------------------------------------------------------------- input --

def script_of(log):
    """The authored input the run was given: the key steps, mouse segments and arm time, from the log's own notes."""
    arm_t = None
    keys, segs = [], []
    for t, text in log.notes:
        if text.startswith("arm ") and arm_t is None:
            arm_t = t
        elif text.startswith("script mouse "):
            p = text.split()
            segs.append(tuple(float(x) for x in p[2:6]))
        elif text.startswith("script key "):
            p = text.split()
            keys.append((float(p[2]), p[3], int(p[4])))
    return arm_t, keys, segs


def mouse_response(frames, log, from_ms=None):
    """For each scripted mouse segment: when the camera started to turn, how far behind the input it runs, how even the turn is,
    and how long it keeps turning after the input stops. The camera angle is measured from the last view before the segment."""
    arm_t, keys, segs = script_of(log)
    if arm_t is None or not segs:
        return {}
    series = [f for f in frames if f["world"]]
    results = []
    for a, b, vx, vy in segs:
        t_a, t_b = arm_t + a * 1e6, arm_t + b * 1e6
        later = [s[0] for s in segs if s[0] > a]
        t_stop = arm_t + (min(later) if later else b + 1500) * 1e6
        before = [f for f in series if f["end"] <= t_a]
        window = [f for f in series if t_a < f["end"] <= t_stop]
        total = math.hypot(vx, vy) * (b - a) / 1000.0
        if not before or len(window) < 6 or total <= 0:
            continue
        base = before[-1]["world"]["view"]
        track = [((f["end"] - t_a) * NS, rotation_angle(base, f["world"]["view"]), f) for f in window]
        final = track[-1][1]
        if final < 1e-4:
            results.append(dict(segment=[a, b, vx, vy], note="the camera did not turn"))
            continue
        seg = dict(segment=[a, b, vx, vy], gain_rad_per_count=final / total)
        onset = next((t for t, ang, _ in track if ang > max(0.03 * final, 1e-4)), None)
        seg["onset_ms"] = None if onset is None else round(onset, 2)
        # where the delay goes: the first poll that carried movement, the first update after it, then the first frame showing it
        polls = [m for m in log.mouse if m[0] >= t_a and (m[1] or m[2])]
        if polls:
            t_poll = polls[0][0]
            seg["first_nonzero_poll_ms"] = round((t_poll - t_a) * NS, 2)
            later_ticks = [t[0] for t in log.ticks if t[0] >= t_poll]
            if later_ticks:
                seg["first_update_after_poll_ms"] = round((later_ticks[0] - t_a) * NS, 2)
            polled = sum(1 for m in log.mouse if t_a <= m[0] <= t_b and (m[1] or m[2]))
            seg["polls_with_movement_during_stream"] = polled

        def reach(frac):
            return next((t for t, ang, _ in track if ang >= frac * final), None)
        lags = {}
        for frac in (0.1, 0.25, 0.5, 0.75, 0.9):
            tc = reach(frac)
            if tc is not None:
                lags[str(frac)] = round(tc - frac * (b - a), 2)
        seg["lag_ms_at_fraction"] = lags
        if lags:
            seg["lag_ms_median"] = median(lags.values())
        seg["tail_ms"] = None if reach(0.99) is None else round(reach(0.99) - (b - a), 2)
        # evenness: the turn rate of every Present inside the steady part of the segment
        inner = [(t, ang, f) for t, ang, f in track if 250.0 <= t <= (b - a) - 100.0]
        rates, prev = [], None
        for t, ang, f in inner:
            if prev is not None and f["frame"] == prev[2]["frame"] + 1:
                dt = (f["end"] - prev[2]["end"]) * 1e-9
                if dt > 0:
                    rates.append((ang - prev[1]) / dt)
            prev = (t, ang, f)
        if len(rates) >= 5 and mean(rates) > 0:
            seg["rate_cv"] = math.sqrt(sum((r - mean(rates)) ** 2 for r in rates) / len(rates)) / mean(rates)
            seg["rate_p10_over_p90"] = pct(rates, .1) / pct(rates, .9) if pct(rates, .9) > 0 else None
            seg["frames_not_turning"] = sum(1 for r in rates if r < 0.05 * mean(rates))
        results.append(seg)
    out = dict(mouse_segments=results)
    lagm = [r["lag_ms_median"] for r in results if "lag_ms_median" in r]
    if lagm:
        out["lag_ms_median"] = median(lagm)
    onsets = [r["onset_ms"] for r in results if r.get("onset_ms") is not None]
    if onsets:
        out["onset_ms_median"] = median(onsets)
        out["onset_ms_max"] = max(onsets)
    cvs = [r["rate_cv"] for r in results if "rate_cv" in r]
    if cvs:
        out["rate_cv_median"] = median(cvs)
    nots = [r["frames_not_turning"] for r in results if "frames_not_turning" in r]
    if nots:
        out["frames_not_turning_total"] = sum(nots)
    gains = [r["gain_rad_per_count"] for r in results if "gain_rad_per_count" in r]
    if gains:
        out["gain_rad_per_count_median"] = median(gains)
    return out


def key_response(frames, log):
    """For scripted key steps: the time from a key going down to the first frame whose camera differs from the one before it."""
    arm_t, keys, _ = script_of(log)
    if arm_t is None:
        return {}
    downs = [(arm_t + ms * 1e6) for ms, kind, sc in keys if kind == "d"]
    out = []
    series = [f for f in frames if f["world"]]
    for t0 in downs:
        before = [f for f in series if f["end"] <= t0][-3:]
        after = [f for f in series if f["end"] > t0]
        if not before or not after:
            continue
        ref = before[-1]["world"]["view"]
        hit = None
        for f in after[:600]:
            e1, _ = pose(ref)
            e2, _ = pose(f["world"]["view"])
            if math.dist(e1, e2) > 1e-3 or rotation_angle(ref, f["world"]["view"]) > 1e-4:
                hit = f
                break
        out.append(None if hit is None else round((hit["end"] - t0) * NS, 2))
    return dict(key_down_to_first_camera_change_ms=out) if out else {}


POS_FLOOR = 0.05        # units a second: below this a camera is not moving (float noise in a view matrix is a thousandth of it)
ROT_FLOOR = 0.002       # radians a second


def view_steps(frames):
    """The world view's speed (units a second) and turn rate (radians a second) over each pair of successive Presents, and whether an
    update ran in the pass that made the later one."""
    series = [f for f in frames if f["world"]]
    out = []
    for a, b in pairs(series):
        dt = (b["end"] - a["end"]) * 1e-9
        if dt <= 0:
            continue
        ea, _ = pose(a["world"]["view"])
        eb, _ = pose(b["world"]["view"])
        out.append(dict(t=b["end"], dt=dt, pos=math.dist(ea, eb) / dt, rot=rotation_angle(a["world"]["view"], b["world"]["view"]) / dt,
                        tick=bool(b["ticks"])))
    return out


def map_view_steps(frames):
    """The paused map's pan, in the shape view_steps gives: `pos` is the travel of the map's corner in map units a second, `rot` the zoom
    (the relative change of the span a second), from the frame samples the game tags as the map's window."""
    rows = [(f, f["game"]) for f in frames if f["game"] and len(f["game"]) > 18 and f["game"][18] == "m"]
    out = []
    for (fa, ga), (fb, gb) in zip(rows, rows[1:]):
        if fb["frame"] != fa["frame"] + 1:
            continue
        dt = (fb["end"] - fa["end"]) * 1e-9
        if dt <= 0:
            continue
        zoom = abs(math.log(gb[8] / ga[8])) / dt if ga[8] > 0 and gb[8] > 0 else 0.0
        out.append(dict(t=fb["end"], dt=dt, pos=math.hypot(gb[6] - ga[6], gb[7] - ga[7]) / dt, rot=zoom, tick=bool(fb["ticks"])))
    return out


def held_motion(steps, t_lo, t_hi):
    """The camera's motion while an input is held, from the pairs of Presents that end inside [t_lo, t_hi], taken from the first pair that
    moved to the last (a camera that reaches the end of its range stops there, and the pairs after that are not an answer to the input):
    for the position and for the rotation, its mean speed, the spread of the speed (cv), the share of pairs in which the camera did not
    move, and how the pairs that moved relate to the update cadence. A camera that moves with each update has a large spread, and moves
    in the pairs that hold an update and in few others. A quantity whose speed stays under the noise floor is left out."""
    sel = [s for s in steps if t_lo <= s["t"] <= t_hi]
    out = dict(pairs=len(sel))
    if len(sel) < 8:
        return out
    for name, floor in (("pos", POS_FLOOR), ("rot", ROT_FLOOR)):
        peak = pct([s[name] for s in sel], .9)
        if peak < floor:
            continue
        live = [i for i, s in enumerate(sel) if s[name] > 0.1 * peak]
        if len(live) < 4:
            continue
        span = sel[live[0]:live[-1] + 1]
        vals = [s[name] for s in span]
        m = mean(vals)
        moved = [v > 0.05 * m for v in vals]
        d = dict(pairs=len(span), active_ms=(span[-1]["t"] - span[0]["t"]) * NS, mean=m,
                 cv=math.sqrt(sum((v - m) ** 2 for v in vals) / len(vals)) / m,
                 p10_over_p90=(pct(vals, .1) / pct(vals, .9)) if pct(vals, .9) > 0 else None,
                 still_pct=100.0 * sum(1 for x in moved if not x) / len(moved))
        with_tick = [x for x, s in zip(moved, span) if s["tick"]]
        without = [x for x, s in zip(moved, span) if not s["tick"]]
        if len(with_tick) >= 4 and len(without) >= 4:
            d["moved_pct_when_an_update_ran"] = 100.0 * sum(with_tick) / len(with_tick)
            d["moved_pct_when_none_ran"] = 100.0 * sum(without) / len(without)
        out[name] = d
    return out


def key_onsets(frames, log, steps=None, settle_ms=250.0, tail_ms=100.0, window_ms=60.0):
    """For each scripted key held long enough to measure: how the camera answers it. `onset_ms` is the time from the key going down to
    the first pair of Presents whose speed (position) or turn rate (rotation) differs from the camera's own motion before the key by
    a twentieth of the change the held key makes; `half_ms` is the time at which the speed, averaged over a window of `window_ms`, has
    covered half of that change; `stop_ms` is the time from the key going up to the last pair that still moved. Each is looked for within
    1.5 s of the key event or until the next key event of the script, and is None when it did not happen then. The camera's motion before the key is taken from the 300 ms before it, so a camera already moving is not
    mistaken for an answer."""
    arm_t, keys, _ = script_of(log)
    if arm_t is None:
        return []
    steps = view_steps(frames) if steps is None else steps
    times = sorted(ms for ms, _, _ in keys)

    def window_end(ms):
        """The end of the stretch in which a camera's motion is the answer to the key event at `ms`: 1.5 s, or the next key event."""
        later = [t for t in times if t > ms]
        return arm_t + min(ms + 1500.0, later[0] if later else ms + 1500.0) * 1e6
    downs = {}
    out = []
    for ms, kind, sc in keys:
        if kind == "d":
            downs[sc] = ms
        elif sc in downs:
            down, up = downs.pop(sc), ms
            if up - down < settle_ms + tail_ms + 100:
                continue
            t0, t1 = arm_t + down * 1e6, arm_t + up * 1e6
            entry = dict(scancode=sc, down_ms=down, held_ms=up - down)
            for name, floor in (("pos", POS_FLOOR), ("rot", ROT_FLOOR)):
                before = [s[name] for s in steps if t0 - 300e6 <= s["t"] <= t0]
                held = [s[name] for s in steps if t0 + settle_ms * 1e6 <= s["t"] <= t1 - tail_ms * 1e6]
                if len(before) < 3 or len(held) < 5:
                    continue
                # the speed the key makes, over the stretch in which the camera moved (it may stop short of the key's release at the end of its range)
                peak = pct(held, .9)
                live = [i for i, v in enumerate(held) if v > 0.1 * peak]
                if len(live) < 3:
                    continue
                base, hold = median(before), mean(held[live[0]:live[-1] + 1])
                change = hold - base
                if abs(change) < 2 * floor:
                    continue
                threshold = 0.05 * abs(change)
                sign = 1.0 if change > 0 else -1.0
                end_down, end_up = window_end(down), window_end(up)
                onset = next((s for s in steps if t0 < s["t"] <= end_down and sign * (s[name] - base) > threshold), None)
                half = None
                for s in steps:
                    if t0 < s["t"] <= end_down:
                        near = [x[name] for x in steps if s["t"] - window_ms * 1e6 < x["t"] <= s["t"]]
                        if near and sign * (mean(near) - base) >= 0.5 * abs(change):
                            half = s
                            break
                after = [s for s in steps if t1 < s["t"] <= end_up and sign * (s[name] - base) > 0.1 * abs(change)]
                entry[name] = dict(speed_before=base, speed_held=hold,
                                   onset_ms=None if onset is None else round((onset["t"] - t0) * NS, 2),
                                   half_ms=None if half is None else round((half["t"] - t0) * NS, 2),
                                   stop_ms=None if not after else round((after[-1]["t"] - t1) * NS, 2))
            if "pos" in entry or "rot" in entry:
                out.append(entry)
    return out


# ----------------------------------------------------------------------------------------------------------------- report --

def note_time(log, text, last=False):
    hits = [t for t, x in log.notes if text in x and not x.startswith("script ")]
    return None if not hits else (hits[-1] if last else hits[0])


def report(path, t_from_ms=None, t_to_ms=None, after_arm=False, from_note=None, to_note=None):
    log = load(path)
    arm_t, _, _ = script_of(log)
    lo = None if t_from_ms is None else t_from_ms * 1e6
    hi = None if t_to_ms is None else t_to_ms * 1e6
    if after_arm and arm_t is not None:
        lo = arm_t if lo is None else max(lo, arm_t)
    if from_note:
        t = note_time(log, from_note)
        if t is not None:
            lo = t if lo is None else max(lo, t)
    if to_note:
        t = note_time(log, to_note, last=False)
        if t is not None:
            hi = t if hi is None else min(hi, t)
    frames = build_frames(log, lo, hi)
    frames, dropped = clean(frames, log)
    out = dict(log=str(path), header=log.header[:6], frames=len(frames), dropped_for_log_writes=dropped, presents_in_log=len(log.presents))
    if len(frames) < 3:
        return out
    out["duration_s"] = (frames[-1]["end"] - frames[0]["end"]) * 1e-9
    out["notes"] = [(round(t * NS, 1), x) for t, x in log.notes if not x.startswith("script ")][:40]
    out["pacing"] = pacing(frames)
    out["ticks"] = ticks_report(frames, log)
    out["camera"] = camera(frames)
    out["camera"].update(tick_motion(frames))
    out["driven_unit"] = driven_unit(frames)
    out["driven_view_angles"] = driven_view_angles(frames)
    out["cost"] = cost(frames, log)
    out["refresh_budget"] = refresh_budget(frames)
    out["input"] = {}
    out["input"].update(mouse_response(frames, log))
    out["input"].update(key_response(frames, log))
    onsets = key_onsets(frames, log)
    if onsets:
        out["input"]["keys_held"] = onsets
    map_steps = map_view_steps(frames)
    if len(map_steps) >= 8:
        map_onsets = key_onsets(frames, log, steps=map_steps)
        if map_onsets:
            out["input"]["keys_held_map_view"] = map_onsets
    ages = [a for _, ty, a in log.events if a]
    if ages:
        out["input"]["sdl_event_age_ms"] = dist(ages, NS)
    polls = [b - a for (a, *_), (b, *_) in zip(log.mouse, log.mouse[1:])]
    if polls:
        out["input"]["mouse_poll_interval_ms"] = dist(polls, NS)
    return out


def fmt_dist(d, prec=2):
    if not d or d.get("n", 0) == 0:
        return "-"
    keys = ("mean", "p50", "p95", "p99", "max")
    return "n=%d " % d["n"] + " ".join("%s=%.*f" % (k, prec, d[k]) for k in keys if k in d)


def print_report(r, out=sys.stdout):
    w = lambda *a: print(*a, file=out)
    w("log %s" % r["log"])
    w("frames %d (dropped %d for log writes), %.1f s" % (r["frames"], r["dropped_for_log_writes"], r.get("duration_s", 0)))
    if "pacing" not in r:
        return
    p = r["pacing"]
    w("\n== pacing (interval between Presents returning) ==")
    w("  interval ms  %s" % fmt_dist(p["interval_ms"]))
    w("  fps mean %.1f, 1%% low %.1f; over 1.5x median %d, over 2x %d, over 33 ms %d, over 50 ms %d, over 100 ms %d; %.1f%% of time in hitches" % (
        p["fps_mean"], p["fps_p1_low"], p["over_1_5x_median"], p["over_2x_median"], p["over_33ms"], p["over_50ms"], p["over_100ms"], p["time_in_hitches_pct"]))
    t = r["ticks"]
    w("\n== updates ==")
    w("  updates per Present  %s" % t["ticks_per_frame"])
    w("  interval between updates ms  %s" % fmt_dist(t["tick_interval_ms"]))
    if "fraction_minus_elapsed_over_tick" in t:
        w("  render fraction minus elapsed/update-length  %s" % fmt_dist(t["fraction_minus_elapsed_over_tick"], 3))
    w("  fraction went backwards with no update: %d; unchanged with no update: %d" % (t["fraction_backwards_without_tick"], t["fraction_unchanged_without_tick"]))
    c = r["camera"]
    w("\n== camera (world view per Present) ==")
    w("  frames with a world view %d; view sets per frame %s" % (c["frames_with_world_view"], fmt_dist(c.get("views_set_per_frame"))))
    for k in ("position_speed_units_per_s", "rotation_rate_rad_per_s"):
        if k in c:
            w("  %s  %s" % (k, fmt_dist(c[k], 4)))
    for k in ("position_cv_while_moving", "rotation_cv_while_moving", "position_cv_over_span", "rotation_cv_over_span",
              "position_frames_standing_still_in_motion", "rotation_frames_standing_still_in_motion"):
        if k in c:
            w("  %s  %.3f" % (k, c[k]))
    for k in ("per_update_speed_units_per_s", "per_update_rotation_rate_rad_per_s", "per_update_speed_change_over_mean", "per_update_rotation_change_over_mean"):
        if k in c:
            w("  %s  %s" % (k, fmt_dist(c[k], 4)))
    for k in ("per_update_speed_autocorrelation_by_lag", "per_update_rotation_autocorrelation_by_lag"):
        if k in c:
            w("  %s  %s" % (k, c[k]))
    if "speed_by_update_phase" in c:
        w("  speed by phase of the update period (fifths): " + "; ".join(
            "%s: pos %.3f rot %.5f (n=%d)" % (b, v["pos"], v["rot"], v["n"]) for b, v in c["speed_by_update_phase"].items()))
    i = r["input"]
    if i:
        w("\n== input ==")
        for k, v in i.items():
            if k in ("keys_held", "keys_held_map_view"):
                for e in v:
                    for name in ("pos", "rot"):
                        if name in e:
                            q = e[name]
                            w("  %s %s held %.0f ms, %s: before %.4g, held %.4g; onset %s ms, half %s ms, stop %s ms" % (
                                "key" if k == "keys_held" else "map view, key", e["scancode"], e["held_ms"], name, q["speed_before"], q["speed_held"],
                                q["onset_ms"], q["half_ms"], q["stop_ms"]))
                continue
            w("  %s  %s" % (k, fmt_dist(v) if isinstance(v, dict) and "n" in v else v))
    rb = r.get("refresh_budget")
    if rb:
        w("\n== refresh-rate budget (model: each frame waits for the next vertical sync once its work is done) ==")
        w("  work per frame ms  %s" % fmt_dist(rb["work_ms"]))
        for hz in REFRESH_RATES:
            e = rb.get(str(hz))
            if e:
                w("  %3d Hz: %5.1f%% of frames over the %.2f ms budget, %.1f fps predicted, %.1f%% of frames take two or more syncs" % (
                    hz, e["over_budget_pct"], 1000.0 / hz, e["predicted_fps"], e["frames_two_or_more_syncs_pct"]))
    du = r.get("driven_unit")
    if du:
        w("\n== driven unit (camera against the unit's displayed pose) ==")
        w("  frames driving %d" % du["frames_driving"])
        for key in ("camera_offset_forward", "camera_offset_side", "camera_offset_up", "camera_offset_forward_change_per_frame",
                    "camera_offset_side_change_per_frame", "camera_offset_up_change_per_frame", "unit_displayed_speed_units_per_s",
                    "unit_displayed_turn_rad_per_s", "displayed_minus_simulated_position"):
            if key in du:
                w("  %s  %s" % (key, fmt_dist(du[key], 4)))
        if "unit_displayed_speed_cv_while_moving" in du:
            w("  unit_displayed_speed_cv_while_moving  %.4f" % du["unit_displayed_speed_cv_while_moving"])
    dv = r.get("driven_view_angles")
    if dv:
        w("\n== driven unit's view angles (heading, pitch and roll of the world view, degrees) ==")
        w("  frames %d, of which %.1f%% hold an update" % (dv["frames"], dv.get("frames_holding_an_update_pct", 0.0)))
        for name in ("heading", "pitch", "roll"):
            w("  %s change per frame  %s" % (name, fmt_dist(dv["%s_change_per_frame_deg" % name], 4)))
            w("  %s second difference  %s" % (name, fmt_dist(dv["%s_second_difference_deg" % name], 4)))
            if "%s_motion_on_update_frames_pct" % name in dv:
                w("  %s motion on frames that hold an update  %.1f%%" % (name, dv["%s_motion_on_update_frames_pct" % name]))
    k = r["cost"]
    w("\n== cost ==")
    for name in ("main_thread_cpu_ms", "main_thread_busy_pct", "pass_start_to_first_scene_ms", "update_and_prework_ms",
                 "first_scene_to_last_endscene_ms", "present_call_ms", "display_model_wait_ms", "draw_calls_per_frame",
                 "triangles_per_frame", "scenes_per_frame", "gpu_start_to_end_ms", "gpu_frame_to_frame_ms", "minor_faults_per_frame"):
        if name in k:
            w("  %s  %s" % (name, fmt_dist(k[name])))
    if "thread_cpu_seconds" in k:
        w("  thread CPU seconds  %s" % k["thread_cpu_seconds"])


def flatten(d, prefix=""):
    out = {}
    for k, v in d.items():
        if isinstance(v, dict):
            out.update(flatten(v, prefix + k + "."))
        elif isinstance(v, (int, float)):
            out[prefix + k] = v
    return out


def compare(a_path, b_path, out=sys.stdout):
    with open(a_path) as fa, open(b_path) as fb:
        a, b = flatten(json.load(fa)), flatten(json.load(fb))
    print("%-62s %14s %14s %9s" % ("metric", "before", "after", "change"), file=out)
    for k in sorted(set(a) | set(b)):
        if any(s in k for s in ("header", "log")) and k.startswith("log"):
            continue
        va, vb = a.get(k), b.get(k)
        if va is None or vb is None or not (k.endswith((".p50", ".p95", ".p99", ".max", ".mean", ".sd")) or "_cv" in k or k.endswith(("hitches_pct", "fps_mean", "fps_p1_low")) or "over_" in k or "still" in k or "lag" in k or "response" in k):
            continue
        ch = "" if not va else "%+.0f%%" % (100.0 * (vb - va) / abs(va))
        print("%-62s %14.4g %14.4g %9s" % (k, va, vb, ch), file=out)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("log", nargs="?")
    ap.add_argument("--from-ms", type=float)
    ap.add_argument("--to-ms", type=float)
    ap.add_argument("--after-arm", action="store_true", help="only frames after the input script's arm")
    ap.add_argument("--from-note", help="only frames after the first note containing this text")
    ap.add_argument("--to-note", help="only frames before the first note containing this text")
    ap.add_argument("--json")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--compare", nargs=2, metavar=("BEFORE", "AFTER"))
    a = ap.parse_args(argv)
    if a.compare:
        compare(*a.compare)
        return 0
    if not a.log:
        ap.error("a log is required")
    r = report(a.log, a.from_ms, a.to_ms, a.after_arm, a.from_note, a.to_note)
    if a.json:
        with open(a.json, "w") as f:
            json.dump(r, f, indent=1, default=lambda o: None)
    if not a.quiet:
        print_report(r)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
