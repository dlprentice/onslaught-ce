#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare two frames: two PNG files, or the same shot LABEL in two journey runs (port/journeys/README.md).

    python3 port/tools/shot_diff.py A.png B.png [--tolerance N] [--max-changed F] [--region X,Y,W,H] [--json OUT]
    python3 port/tools/shot_diff.py RUN_A/result.json RUN_B/result.json --label LABEL [...]

A pixel counts as changed when any channel differs by more than --tolerance (0 by default). The report gives the frame size,
the share of changed pixels, the largest channel change, the bounding box of the change, and the mean light of each frame
(the measured number beside the eye). --region limits all of it to a rectangle.
Exit 0 when the changed share is at most --max-changed (0 by default: every pixel equal within the tolerance), 1 otherwise,
2 when the frames cannot be compared (missing, or different sizes: NOT COMPARABLE, never a pass)."""
import argparse
import json
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageStat


def frame(arg, label):
    path = Path(arg)
    if path.suffix == ".json":
        if not label:
            sys.exit("--label names the shot to compare when the arguments are journey results")
        path = path.parent / f"{label}.png"
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--label")
    ap.add_argument("--tolerance", type=int, default=0)
    ap.add_argument("--max-changed", type=float, default=0.0)
    ap.add_argument("--region")
    ap.add_argument("--json")
    o = ap.parse_args()
    pa, pb = frame(o.a, o.label), frame(o.b, o.label)
    report = {"a": str(pa), "b": str(pb)}
    if not pa.is_file() or not pb.is_file():
        report["verdict"] = "NOT COMPARABLE: a frame is missing"
    else:
        ia, ib = Image.open(pa).convert("RGB"), Image.open(pb).convert("RGB")
        report["size"] = [list(ia.size), list(ib.size)]
        if ia.size != ib.size:
            report["verdict"] = "NOT COMPARABLE: the frames differ in size"
        else:
            if o.region:
                x, y, w, h = (int(v) for v in o.region.split(","))
                ia, ib = ia.crop((x, y, x + w, y + h)), ib.crop((x, y, x + w, y + h))
                report["region"] = [x, y, w, h]
            diff = ImageChops.difference(ia, ib)
            # the per-pixel maximum over the three channels
            r, g, b = diff.split()
            peak = ImageChops.lighter(ImageChops.lighter(r, g), b)
            changed_mask = peak.point(lambda v: 255 if v > o.tolerance else 0)
            changed = changed_mask.histogram()[255]
            total = ia.size[0] * ia.size[1]
            light = lambda im: round(ImageStat.Stat(im.convert("L")).mean[0], 3)
            report.update(changed_pixels=changed, changed_share=round(changed / total, 6), largest_change=peak.getextrema()[1],
                          changed_box=changed_mask.getbbox(), mean_light=[light(ia), light(ib)], tolerance=o.tolerance,
                          max_changed=o.max_changed)
            report["verdict"] = "within the limit" if changed / total <= o.max_changed else "differ"
    if o.json:
        Path(o.json).write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps(report))
    if report["verdict"].startswith("NOT COMPARABLE"):
        return 2
    return 0 if report["verdict"] == "within the limit" else 1


if __name__ == "__main__":
    sys.exit(main())
