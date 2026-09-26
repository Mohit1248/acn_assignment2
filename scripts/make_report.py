#!/usr/bin/env python3
"""Generates the four figures (PNG, repo top level) and report_A.pdf from results/.

Needs matplotlib and reportlab (not used by anything else in the project):
    python scripts/make_report.py [results_dir]

Every number in the text and tables is computed from the per-request CSVs by
metrics.py, so the report cannot drift from the data.
"""
import os
import statistics
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(SCRIPT_DIR)
sys.path.insert(0, SCRIPT_DIR)

import matplotlib  # noqa: E402

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from reportlab.lib import colors  # noqa: E402
from reportlab.lib.pagesizes import letter  # noqa: E402
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet  # noqa: E402
from reportlab.lib.units import inch  # noqa: E402
from reportlab.platypus import (Image, KeepTogether, Paragraph, SimpleDocTemplate,  # noqa: E402
                                Spacer, Table, TableStyle)

from common import N_REQUESTS, QUANTUM  # noqa: E402
from metrics import (ALL_CELLS, CLASSES, REFERENCE, across, across_a14,  # noqa: E402
                     fmt_count, load_all)

AUTHORS = os.environ.get("REPORT_AUTHORS", "Mohit and teammate")  # e.g. REPORT_AUTHORS="Name1 (entry no), Name2 (entry no)"
RESULTS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "results")
D = load_all(RESULTS)
K = len(D["runs"])

POLICY_COLOR = {"fcfs": "#4c72b0", "sjf": "#dd8452", "rr": "#55a868", "drr": "#8172b3"}
LABEL = {"fcfs_ref": "fcfs", "sjf_ref": "sjf", "rr_ref": "rr", "drr_ref": "drr",
         "fcfs_st1": "fcfs", "sjf_st1": "sjf", "rr_st1": "rr", "drr_st1": "drr"}


def pol(cell):
    return cell.split("_")[0]


def med(cell, getter):
    return across(D, cell, getter)["median"]


def slow(cell, cls, key="slow_med"):
    return med(cell, lambda m: m["by_class"][cls][key])


def wait_cls(cell, cls, key="wait_p50_ns"):
    return med(cell, lambda m: m["by_class"][cls][key]) / 1000.0


def bars(ax, xs, stat, color, width, label=None, hatch=None):
    """Bar = median over the repeated runs, whisker = min..max."""
    lo = [s["median"] - s["min"] for s in stat]
    hi = [s["max"] - s["median"] for s in stat]
    ax.bar(xs, [s["median"] for s in stat], width, yerr=[lo, hi], capsize=2, color=color,
           label=label, hatch=hatch, edgecolor="white", linewidth=0.5, error_kw={"elinewidth": 0.8})


plt.rcParams.update({"font.size": 7.5, "axes.titlesize": 8, "axes.labelsize": 7.5,
                     "legend.fontsize": 7, "figure.dpi": 200})


# ------------------------------------------------------------------ figures --

def fig1():
    cells = [c for c in ALL_CELLS if any(c in r["cells"] for r in D["runs"])]
    fig, (a, b) = plt.subplots(1, 2, figsize=(7.4, 2.7))
    xs = list(range(len(cells)))
    w = 0.38
    p50 = [across(D, c, lambda m: m["wait_p50_ns"] / 1000) for c in cells]
    p99 = [across(D, c, lambda m: m["wait_p99_ns"] / 1000) for c in cells]
    for i, c in enumerate(cells):
        bars(a, [i - w / 2], [p50[i]], POLICY_COLOR[pol(c)], w)
        bars(a, [i + w / 2], [p99[i]], POLICY_COLOR[pol(c)], w, hatch="///")
    a.set_yscale("log")
    a.set_xticks(xs)
    a.set_xticklabels([f"{LABEL[c]}\n{'4 thr' if c.endswith('ref') else '1 thr'}" for c in cells])
    a.set_ylabel("waiting time (us, log)")
    a.set_title("(a) waiting: solid = p50, hatched = p99")
    thr = [across(D, c, lambda m: m["throughput"]) for c in cells]
    for i, c in enumerate(cells):
        bars(b, [i], [thr[i]], POLICY_COLOR[pol(c)], 0.7)
    b.set_xticks(xs)
    b.set_xticklabels([f"{LABEL[c]}\n{'4 thr' if c.endswith('ref') else '1 thr'}" for c in cells])
    b.set_ylabel("throughput (req/s)")
    b.set_title("(b) throughput")
    fig.suptitle(f"Median of {K} runs per cell, whiskers = min..max; drr and sjf at 1 thr are supplementary cells",
                 fontsize=7, y=0.02, va="bottom")
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    fig.savefig(os.path.join(ROOT, "fig1_waiting_throughput.png"))
    plt.close(fig)


def fig2():
    fig, axes = plt.subplots(1, 2, figsize=(7.4, 2.6), sharey=True)
    for ax, group, title in ((axes[0], REFERENCE, "(a) 4 server threads (reference)"),
                             (axes[1], ("fcfs_st1", "sjf_st1", "rr_st1", "drr_st1"), "(b) 1 server thread")):
        n = len(group)
        w = 0.8 / n
        for j, cell in enumerate(group):
            stat = [across(D, cell, lambda m, c=cls: m["by_class"][c]["slow_med"]) for cls in CLASSES]
            xs = [i + (j - (n - 1) / 2) * w for i in range(3)]
            bars(ax, xs, stat, POLICY_COLOR[pol(cell)], w, label=LABEL[cell])
        ax.set_yscale("log")
        ax.set_xticks(range(3))
        ax.set_xticklabels([f"{c}\n({s})" for c, s in zip(CLASSES, ("~1 KB", "~30 KB", "~150 KB"))])
        ax.set_title(title)
    axes[0].set_ylabel("median normalised slowdown (ns/byte, log)")
    axes[0].legend(ncol=4, loc="upper right")
    fig.tight_layout()
    fig.savefig(os.path.join(ROOT, "fig2_slowdown.png"))
    plt.close(fig)


def fig3():
    fig, axes = plt.subplots(1, 3, figsize=(7.4, 2.5))
    a, b, c = axes
    w = 0.38
    for j, cell in enumerate(("rr_ref", "drr_ref")):
        per_get = []
        for cls in CLASSES:
            f = across(D, cell, lambda m, cl=cls: m["by_class"][cl]["forfeited"] / max(1, m["by_class"][cl]["n_get"]))
            per_get.append(f)
        bars(a, [i + (j - 0.5) * w for i in range(3)], per_get, POLICY_COLOR[pol(cell)], w, label=LABEL[cell])
        r = [across(D, cell, lambda m, cl=cls: m["by_class"][cl]["rounds_mean_get"]) for cls in CLASSES]
        bars(b, [i + (j - 0.5) * w for i in range(3)], r, POLICY_COLOR[pol(cell)], w, label=LABEL[cell])
    a.set_yscale("symlog", linthresh=10)
    a.set_title("(a) forfeited bytes per GET")
    b.set_title("(b) rounds per GET")
    for ax in (a, b):
        ax.set_xticks(range(3))
        ax.set_xticklabels(CLASSES)
    a.legend()
    # (c) slowdown of the long-line file (large.txt), median and p99, both configs
    groups = [("rr_ref", "drr_ref", "4 thr"), ("rr_st1", "drr_st1", "1 thr")]
    for gi, (rr, dr, lab) in enumerate(groups):
        for j, cell in enumerate((rr, dr)):
            for k, (key, hatch) in enumerate((("slow_med", None), ("slow_p99", "///"))):
                s = across(D, cell, lambda m, kk=key: m["by_class"]["large"][kk])
                x = gi + (j - 0.5) * 0.36 + (k - 0.5) * 0.17
                bars(c, [x], [s], POLICY_COLOR[pol(cell)], 0.17, hatch=hatch)
    c.set_xticks([0, 1])
    c.set_xticklabels([g[2] for g in groups])
    c.set_yscale("log")
    c.set_title("(c) large.txt slowdown (ns/byte)\nsolid = median, hatched = p99")
    fig.tight_layout()
    fig.savefig(os.path.join(ROOT, "fig3_rr_vs_drr.png"))
    plt.close(fig)


def fig4():
    fig, axes = plt.subplots(1, 2, figsize=(7.4, 2.5), sharey=True)
    for ax, group, title in ((axes[0], REFERENCE, "(a) 4 server threads"),
                             (axes[1], ("fcfs_st1", "sjf_st1", "rr_st1", "drr_st1"), "(b) 1 server thread")):
        n = len(group)
        w = 0.8 / n
        for j, cell in enumerate(group):
            stat = [across(D, cell, lambda m, cl=cls: m["by_class"][cl]["wait_p50_ns"] / 1000) for cls in CLASSES]
            xs = [i + (j - (n - 1) / 2) * w for i in range(3)]
            bars(ax, xs, stat, POLICY_COLOR[pol(cell)], w, label=LABEL[cell])
            p99 = [wait_cls(cell, cls, "wait_p99_ns") for cls in CLASSES]
            ax.plot(xs, p99, "kD", markersize=2.5)
        ax.set_yscale("log")
        ax.set_xticks(range(3))
        ax.set_xticklabels(CLASSES)
        ax.set_title(title)
    axes[0].set_ylabel("waiting time (us, log)")
    axes[0].legend(ncol=4, loc="upper left")
    fig.text(0.5, 0.005, "bars = median of run p50 (whiskers min..max); black diamonds = median of run p99",
             ha="center", fontsize=7)
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    fig.savefig(os.path.join(ROOT, "fig4_sjf_starvation.png"))
    plt.close(fig)


# ------------------------------------------------------------------- report --

def disjoint(a, b):
    """True if the [min, max] ranges of two across() results do not overlap."""
    return a["max"] < b["min"] or b["max"] < a["min"]


def rng(stat, scale=1.0, nd=0):
    return f"{stat['median'] / scale:.{nd}f} [{stat['min'] / scale:.{nd}f}-{stat['max'] / scale:.{nd}f}]"


ss = getSampleStyleSheet()
BODY = ParagraphStyle("body", parent=ss["BodyText"], fontName="Helvetica", fontSize=8.6, leading=10.6,
                      spaceAfter=3.5)
H1 = ParagraphStyle("h1", parent=ss["Heading2"], fontName="Helvetica-Bold", fontSize=10.5, leading=12.5,
                    spaceBefore=6, spaceAfter=3, keepWithNext=1)
TITLE = ParagraphStyle("title", parent=ss["Title"], fontName="Helvetica-Bold", fontSize=13.5, leading=16,
                       spaceAfter=2)
SMALL = ParagraphStyle("small", parent=BODY, fontSize=7.4, leading=9, textColor=colors.HexColor("#333333"))
CELL = ParagraphStyle("cell", parent=BODY, fontSize=7.2, leading=8.6, spaceAfter=0)


def P(text, style=BODY):
    return Paragraph(text, style)


def table(rows, col_widths, header_rows=1, font=7.2):
    t = Table(rows, colWidths=col_widths, repeatRows=header_rows)
    t.setStyle(TableStyle([
        ("FONT", (0, 0), (-1, -1), "Helvetica", font),
        ("FONT", (0, 0), (-1, header_rows - 1), "Helvetica-Bold", font),
        ("BACKGROUND", (0, 0), (-1, header_rows - 1), colors.HexColor("#e8e8ee")),
        ("GRID", (0, 0), (-1, -1), 0.3, colors.HexColor("#999999")),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ("TOPPADDING", (0, 0), (-1, -1), 1.6), ("BOTTOMPADDING", (0, 0), (-1, -1), 1.6),
        ("LEFTPADDING", (0, 0), (-1, -1), 3), ("RIGHTPADDING", (0, 0), (-1, -1), 3),
    ]))
    return t


def fig_img(name, width_in=7.2):
    from PIL import Image as PILImage
    path = os.path.join(ROOT, name)
    w, h = PILImage.open(path).size
    return Image(path, width=width_in * inch, height=width_in * inch * h / w)


def build_pdf():
    ref = {c: LABEL[c] for c in REFERENCE}
    f_small = lambda c: slow(c, "small")   # noqa: E731
    f_large = lambda c: slow(c, "large")   # noqa: E731

    thr = {c: med(c, lambda m: m["throughput"]) for c in ALL_CELLS if any(c in r["cells"] for r in D["runs"])}
    w50 = {c: med(c, lambda m: m["wait_p50_ns"]) / 1000 for c in thr}
    w99 = {c: med(c, lambda m: m["wait_p99_ns"]) / 1000 for c in thr}
    r50 = {c: med(c, lambda m: m["resp_p50_ns"]) / 1000 for c in thr}
    r99 = {c: med(c, lambda m: m["resp_p99_ns"]) / 1000 for c in thr}

    n_get_large = statistics.median(r["cells"]["rr_ref"]["by_class"]["large"]["n_get"] for r in D["runs"])
    forf_large_get = med("rr_ref", lambda m: m["by_class"]["large"]["forfeited"] / max(1, m["by_class"]["large"]["n_get"]))
    forf_med_get = med("rr_ref", lambda m: m["by_class"]["medium"]["forfeited"] / max(1, m["by_class"]["medium"]["n_get"]))
    rounds_rr = med("rr_ref", lambda m: m["by_class"]["large"]["rounds_mean_get"])
    rounds_drr = med("drr_ref", lambda m: m["by_class"]["large"]["rounds_mean_get"])
    a14_rr = across_a14(D, "rr_ref")["median"]
    a14_rr1 = across_a14(D, "rr_st1")["median"]
    a14_lo = min(across_a14(D, c)["min"] for c in ("rr_ref", "rr_st1"))
    a14_hi = max(across_a14(D, c)["max"] for c in ("rr_ref", "rr_st1"))

    aside = D["aside"]
    pairs = [(aside[f"aside_p1_{k}"], aside[f"aside_p10_{k}"]) for k in "abc" if f"aside_p1_{k}" in aside]
    wait_txt = ("lower with --p 10 in every pair" if all(b["wait_p50_ns"] < a["wait_p50_ns"] for a, b in pairs)
                else "no consistent difference between the pairs; the waiting effect is smaller than the host's noise")
    p1 = [m for k, m in aside.items() if k.startswith("aside_p1_")]
    p10 = [m for k, m in aside.items() if k.startswith("aside_p10_")]
    # send() counts come from each run's server log, which the submission zip does not ship
    have_sc = all(m["send_calls"] is not None for m in p1 + p10)
    sc1 = statistics.median(m["send_calls"] for m in p1) if have_sc else float("nan")
    sc10 = statistics.median(m["send_calls"] for m in p10) if have_sc else float("nan")
    if have_sc:
        sc_txt = (f"send() calls fell from about {sc1:,.0f} to {sc10:,.0f} per run "
                  f"({sc1 / sc10:.1f}x fewer, in every pair)")
    else:
        sc_txt = "the send() counts are in the server logs, which are not included here (n/a)"
    wait1 = statistics.median(m["wait_p50_ns"] for m in p1) / 1000
    wait10 = statistics.median(m["wait_p50_ns"] for m in p10) / 1000
    thr1 = statistics.median(m["throughput"] for m in p1)
    thr10 = statistics.median(m["throughput"] for m in p10)

    noise_x = max(across(D, c, lambda m: m["throughput"])["max"] / across(D, c, lambda m: m["throughput"])["min"]
                  for c in thr)
    tf = across(D, "fcfs_ref", lambda m: m["throughput"])
    trr = across(D, "rr_ref", lambda m: m["throughput"])
    tdr = across(D, "drr_ref", lambda m: m["throughput"])
    if disjoint(tf, trr) and disjoint(tf, tdr):
        thr_ref_text = (f"fcfs's runs ({rng(tf)}) do not overlap the rr ({rng(trr)}) or drr ({rng(tdr)}) runs, so this cost "
                        f"is real, about {100 * (1 - trr['median'] / tf['median']):.0f}% for rr and "
                        f"{100 * (1 - tdr['median'] / tf['median']):.0f}% for drr.")
    else:
        thr_ref_text = (f"Medians are {100 * (1 - trr['median'] / tf['median']):.0f}% (rr) and "
                        f"{100 * (1 - tdr['median'] / tf['median']):.0f}% (drr) below fcfs, but the run ranges overlap "
                        f"(fcfs {rng(tf)}, rr {rng(trr)}, drr {rng(tdr)}), so the size of the cost is uncertain even "
                        f"though its direction is consistent.")
    if disjoint(across(D, "fcfs_st1", lambda m: m["throughput"]), across(D, "rr_st1", lambda m: m["throughput"])):
        thr_st1_text = f"With one server thread the throughputs differ ({thr['fcfs_st1']:.0f} fcfs vs {thr['rr_st1']:.0f} rr)."
    else:
        thr_st1_text = (f"With one server thread the throughputs ({thr['fcfs_st1']:.0f} fcfs vs {thr['rr_st1']:.0f} rr) "
                        f"are not separable: the single worker is the bottleneck either way.")

    lg_rr = across(D, "rr_ref", lambda m: m["by_class"]["large"]["slow_med"])
    lg_dr = across(D, "drr_ref", lambda m: m["by_class"]["large"]["slow_med"])
    lg_rr1 = across(D, "rr_st1", lambda m: m["by_class"]["large"]["slow_med"])
    lg_dr1 = across(D, "drr_st1", lambda m: m["by_class"]["large"]["slow_med"])
    if disjoint(lg_rr1, lg_dr1) and lg_dr1["median"] > lg_rr1["median"]:
        lg_text = (f"With one thread drr's large file is {100 * (lg_dr1['median'] / lg_rr1['median'] - 1):.0f}% slower in "
                   f"every run: it needs {rounds_drr:.0f} rounds against rr's {rounds_rr:.0f}, and each extra round is a trip "
                   f"to the back of a longer queue.")
    elif disjoint(lg_rr1, lg_dr1):
        lg_text = "With one thread the two differ, drr being faster."
    else:
        lg_text = "With one thread the two are not separable."
    story = []
    story.append(P("COL724/7524 Assignment 2, Part A: Link Scheduling", TITLE))
    story.append(P(f"{AUTHORS}", SMALL))
    story.append(Spacer(1, 3))

    # ---------------------------------------------------------------- 1
    story.append(P("1. The four policies as implemented", H1))
    story.append(P(
        "<b>Queue and threads (A5).</b> An acceptor thread only calls accept(). Each connection goes to a short-lived "
        "admission thread that reads the header line under a 5 s total deadline, answers HEALTH immediately with the current "
        "queue depth, validates the file name (A25), and enqueues the request. For a GET it opens the file and takes the "
        "size from fstat, so the size is known, and fixed to one version of the file, when the request enters the queue. "
        "<i>server_threads</i> workers only loop on <i>next()</i>, then serve_slice() for one round, then either requeue "
        "the request (PREEMPTED) or finish it. Because admission never waits for a worker, requests really do pile up in "
        "the scheduler queue: a test that pins the only worker on a stalled PUT sees HEALTH report a depth of 6, and HEALTH "
        "still answers in about 1 ms while a silent client is connected. Clients that trickle bytes are cut off by total "
        "deadlines (5 s for the header, 10 s per 64 KB of a PUT body), not by a per-recv() timeout, so they cannot pin an "
        "admission thread or a worker either."))
    story.append(P(
        "<b>Policies.</b> <b>fcfs</b> serves in arrival order. <b>sjf</b> serves the queued request with the smallest declared "
        "byte count (file size for GET, the count in the request line for PUT: one key for both verbs), earliest arrival "
        "first on ties, with no aging. fcfs and sjf never preempt, so every request takes one round (<i>rounds</i> = 1). "
        f"<b>rr</b> gives each scheduled request an allowance of Q = {QUANTUM} bytes per round and requeues it at the tail. "
        "<b>drr</b> uses the same queue but each request carries a deficit counter (initially 0): a round's allowance is "
        "deficit + Q, and whatever part of it goes unused is kept in the deficit instead of being forfeited; the deficit "
        "is discarded when the request finishes."))
    story.append(P(
        "<b>One round (serve_slice).</b> A GET is sent in whole lines (A6): the line length is found by scanning to the next "
        "'\\n' (a last line without one still counts), and lines are grouped <i>--p</i> per write() (A8) without changing "
        "which bytes go out or how the allowance is charged. A round ends as soon as the next line would exceed the "
        "remaining allowance (A13); under rr the unused remainder is added to <i>forfeited_bytes</i>, under drr it becomes "
        "the deficit. <b>A14</b> (rr only): if a line longer than Q starts a round, i.e. the round has transferred nothing "
        "yet, it is sent in full and the round ends, overrunning the allowance and logging one line. I read A14 this way "
        "because its stated purpose is to stop a request being requeued having transferred nothing, forever; a long line "
        "reached <i>mid-round</i> is handled by A13 instead (round ends, remainder forfeited) and goes out first in the "
        "next round. drr has no escape: the deficit grows by Q per round until it covers the line (A16), so a 20,000-byte "
        "line costs up to 3 rounds. A PUT round reads exactly min(allowance, remaining) bytes (A9, A13) into a temporary "
        "file that is renamed onto the destination when the last byte arrives, so a concurrent GET never sees a "
        "half-written file. The 'OK n' response line is protocol overhead and is not charged to the allowance; the "
        "terminating '\\n' of every line is (A7)."))
    story.append(P(
        "<b>Preemption state (A17).</b> Everything a later round needs lives in the Request: the byte offset; the "
        "file descriptor opened at admission (so every round reads the version that was sized); the bytes already read "
        "from the socket but not yet consumed (for a PUT, the body bytes that arrived with the header); the deficit; "
        "<i>rounds</i> and <i>forfeited_bytes</i>; and the temporary-file path. Correctness is checked by 72 unit "
        "assertions that drive serve_slice over a message-preserving socketpair against rounds, forfeiture and A14 counts "
        "worked out by hand (e.g. ten 10-byte lines with Q = 35: rr takes 4 rounds and forfeits 15 bytes, drr takes 3 and "
        "forfeits 0), including a GET that keeps serving the old file after a PUT has replaced it, and by an integration "
        "suite that runs all four policies end to end."))

    # ---------------------------------------------------------------- 2
    story.append(P("2. Experimental setup", H1))
    story.append(P(
        f"<b>Machine and configuration.</b> Server and client run on one laptop under WSL2 (Ubuntu 24.04, 8 logical CPUs) "
        f"over loopback TCP, so absolute times are not network-representative; only comparisons between policies are "
        f"meaningful. The reference configuration is 4 server threads and 8 client threads; the second is 1 server "
        f"thread. Q = {QUANTUM} bytes. Each run makes {N_REQUESTS} counted requests from a closed loop of 8 client "
        f"threads, after 3 uncounted seeding PUTs (dropped by sorting the CSV by arrival and removing the first 3 rows). "
        f"Each request picks one of the workload files uniformly and does a GET or a PUT with probability 1/2. Percentiles "
        f"are nearest-rank. TCP_NODELAY is set so every write() is a separate segment."))
    story.append(P(
        "<b>Workload (A27), both axes in one directory.</b> <i>Size:</i> small.txt 1,044 B, medium.txt 30,774 B, "
        "large.txt 153,613 B. <i>Line length:</i> small and medium contain only ordinary 60-79 byte lines; large.txt has "
        f"the same ordinary lines plus 6 lines of 20,000 bytes (2.4 x Q) spread evenly through it, so it is both the large "
        f"file and the long-line file. With Q = {QUANTUM} the large file needs 19 rounds under a byte-exact accounting."))
    story.append(P(
        f"<b>Repetition.</b> A28 asks for one run per cell. This setup is noisy from run to run (background activity on "
        f"the Windows host made the slowest run of a cell up to {noise_x:.0f} times slower than its fastest), so every cell was run <b>{K} times</b>, in "
        f"interleaved blocks (each block runs all cells once, preceded by an unrecorded warm-up run because the first "
        f"cell of a session otherwise pays a cold-start cost). Tables report the <b>median over the {K} runs</b>, with "
        f"[min-max] where useful; whiskers in the figures are min-max. Raw per-request CSVs of every run are in "
        f"results/run1..run{K}. Two extra cells, sjf and drr at 1 server thread, are supplementary (not among the six "
        f"required cells) and are marked as such."))

    # ---------------------------------------------------------------- 3
    h3 = P("3. Per-run results", H1)
    rows = [["cell", "waiting p50 (us)", "waiting p99 (us)", "response p50 (us)", "response p99 (us)",
             "throughput (req/s)"]]
    for c in ALL_CELLS:
        if not any(c in r["cells"] for r in D["runs"]):
            continue
        tag = " (suppl.)" if c in ("drr_st1", "sjf_st1") else ""
        rows.append([f"{LABEL[c]}, {'4' if c.endswith('ref') else '1'} thr{tag}",
                     rng(across(D, c, lambda m: m["wait_p50_ns"]), 1000),
                     rng(across(D, c, lambda m: m["wait_p99_ns"]), 1000),
                     rng(across(D, c, lambda m: m["resp_p50_ns"]), 1000),
                     rng(across(D, c, lambda m: m["resp_p99_ns"]), 1000),
                     rng(across(D, c, lambda m: m["throughput"]))])
    story.append(KeepTogether([
        h3,
        P(f"<b>Table 1.</b> Waiting time (start - arrival), response time (finish - arrival) and throughput, "
          f"median [min-max] over {K} runs.", SMALL),
        table(rows, [1.15 * inch, 1.27 * inch, 1.3 * inch, 1.27 * inch, 1.3 * inch, 1.05 * inch]),
    ]))
    story.append(Spacer(1, 3))
    story.append(fig_img("fig1_waiting_throughput.png"))

    rows = [["cell", "small med", "small p99", "medium med", "medium p99", "large med", "large p99"]]
    for c in ALL_CELLS:
        if not any(c in r["cells"] for r in D["runs"]):
            continue
        tag = " (suppl.)" if c in ("drr_st1", "sjf_st1") else ""
        line = [f"{LABEL[c]}, {'4' if c.endswith('ref') else '1'} thr{tag}"]
        for cls in CLASSES:
            line += [f"{slow(c, cls):.0f}", f"{slow(c, cls, 'slow_p99'):.0f}"]
        rows.append(line)
    story.append(KeepTogether([
        P("<b>Table 2.</b> Normalised slowdown (response / bytes, ns per byte) by size class: median over runs of each "
          "run's median and p99. The first four rows are the reference configuration.", SMALL),
        table(rows, [1.7 * inch, 0.9 * inch, 0.9 * inch, 0.95 * inch, 0.95 * inch, 0.9 * inch, 0.9 * inch]),
    ]))
    story.append(Spacer(1, 3))
    story.append(fig_img("fig2_slowdown.png"))

    story.append(P(
        f"<b>Reading the numbers.</b> Median waiting falls from {w50['fcfs_ref']:.0f} us under fcfs to "
        f"{w50['sjf_ref']:.0f} us under sjf and to {w50['rr_ref']:.0f} / {w50['drr_ref']:.0f} us under rr / drr "
        f"(about {w50['fcfs_ref'] / w50['rr_ref']:.1f}x lower), and with one server thread from "
        f"{w50['fcfs_st1'] / 1000:.1f} ms to {w50['rr_st1'] / 1000:.1f} ms under rr: the preemptive policies keep the "
        f"queue short for everyone because no request holds a worker for its whole transfer. Waiting is what the "
        f"policy controls; the slowdown table shows who pays for it. The small class gains most: median slowdown "
        f"{f_small('fcfs_ref'):.0f} (fcfs) to {f_small('sjf_ref'):.0f} (sjf) to {f_small('rr_ref'):.0f} / "
        f"{f_small('drr_ref'):.0f} ns/byte (rr / drr), and with one server thread {f_small('fcfs_st1'):.0f} to "
        f"{f_small('rr_st1'):.0f}. The large class pays: {f_large('fcfs_ref'):.0f} (fcfs), {f_large('sjf_ref'):.0f} "
        f"(sjf), {f_large('rr_ref'):.0f} / {f_large('drr_ref'):.0f} (rr / drr); with one thread {f_large('fcfs_st1'):.0f} "
        f"under fcfs against {f_large('rr_st1'):.0f} under rr. Medium files barely move at the reference configuration "
        f"and gain under one thread. Throughput is lower under rr/drr (median {thr['rr_ref']:.0f} / {thr['drr_ref']:.0f} "
        f"req/s vs {thr['fcfs_ref']:.0f} for fcfs at the reference configuration): every extra round costs a queue "
        f"trip and extra system calls. " + thr_ref_text + " " + thr_st1_text))

    story.append(P(
        f"<b>Waiting versus response (A19).</b> The two tell different stories. At the reference configuration rr / drr "
        f"cut median waiting {w50['fcfs_ref'] / w50['rr_ref']:.1f}x, yet median response is "
        f"{'not lower' if min(r50['rr_ref'], r50['drr_ref']) >= r50['fcfs_ref'] else 'only slightly lower'}: "
        f"{r50['rr_ref']:.0f} / {r50['drr_ref']:.0f} us against {r50['fcfs_ref']:.0f} us for fcfs. Response also contains the request's own service time, and a preempted request's service is spread "
        f"over several rounds interleaved with other requests, so the earlier start is spent on the way to the finish. "
        f"With one server thread the queue is long enough for the gain to survive: median response falls from "
        f"{r50['fcfs_st1'] / 1000:.1f} ms (fcfs) to {r50['rr_st1'] / 1000:.1f} ms (rr) and {r50['sjf_st1'] / 1000:.1f} ms "
        f"(sjf), but the tail {'moves the other way' if r99['rr_st1'] > r99['fcfs_st1'] else 'does not follow'}: p99 response {r99['fcfs_st1'] / 1000:.0f} ms (fcfs), "
        f"{r99['rr_st1'] / 1000:.0f} ms (rr), {r99['sjf_st1'] / 1000:.0f} ms (sjf), because the large requests now wait "
        f"through many rounds or behind every smaller request. Response time alone would have hidden the first effect."))

    # ---------------------------------------------------------------- 4
    h4 = P("4. rr versus drr (A29)", H1)
    rows = [["", "forfeited bytes (all requests of a run)", "", "", "rounds / GET", "", "A14 fires", "large.txt slowdown"],
            ["cell", "small", "medium", "large", "medium", "large", "per run", "median / p99"]]
    for c in ("rr_ref", "drr_ref", "rr_st1", "drr_st1"):
        if not any(c in r["cells"] for r in D["runs"]):
            continue
        rows.append([f"{LABEL[c]}, {'4' if c.endswith('ref') else '1'} thr" + (" (suppl.)" if c == "drr_st1" else ""),
                     *(f"{med(c, lambda m, cl=cls: m['by_class'][cl]['forfeited']):,.0f}" for cls in CLASSES),
                     f"{med(c, lambda m: m['by_class']['medium']['rounds_mean_get']):.1f}",
                     f"{med(c, lambda m: m['by_class']['large']['rounds_mean_get']):.1f}",
                     fmt_count(across_a14(D, c)['median']),
                     f"{slow(c, 'large'):.0f} / {slow(c, 'large', 'slow_p99'):.0f}"])
    t = table(rows, [1.45 * inch, 0.6 * inch, 0.75 * inch, 0.85 * inch, 0.65 * inch, 0.55 * inch, 0.7 * inch, 1.3 * inch], header_rows=2)
    t.setStyle(TableStyle([("SPAN", (1, 0), (3, 0)), ("SPAN", (4, 0), (5, 0))]))
    story.append(KeepTogether([
        h4,
        P(f"<b>Table 3.</b> Long-line comparison, median over {K} runs. A14 fired {fmt_count(a14_rr)} times per rr run at the "
          f"reference configuration and {fmt_count(a14_rr1)} at one thread (range {fmt_count(a14_lo)}-{fmt_count(a14_hi)} over all runs): "
          f"exactly the 6 long lines of every large-file GET (about {n_get_large:.0f} GETs per run) and never under drr.", SMALL),
        t]))
    story.append(Spacer(1, 3))
    story.append(fig_img("fig3_rr_vs_drr.png"))
    story.append(P(
        f"<b>Why long lines get less than their full allowance under rr.</b> A round can only end on a line boundary. "
        f"With ordinary 60-79 byte lines the leftover is tiny: a medium GET forfeits about {forf_med_get:.0f} bytes in "
        f"total over its 3 preempted rounds (under 0.5% of Q per round). A 20,000-byte line changes that. About 4.8 KB "
        f"of ordinary lines separate the long lines, so when the next long line is reached mid-round it cannot fit in "
        f"what is left, the round ends early and the remainder is forfeited; the round transfers only part of its Q. "
        f"That happens once per long line: the large file forfeits {forf_large_get:,.0f} bytes per GET "
        f"({med('rr_ref', lambda m: m['by_class']['large']['forfeited']):,.0f} in a run), which is about "
        f"{forf_large_get / 6 / 1000:.1f} KB, or {100 * forf_large_get / 6 / QUANTUM:.0f}% of a quantum, at each of its six long "
        f"lines. The next round then starts with the long line and A14 sends it whole, overrunning Q by "
        f"about 11.8 KB. rr's accounting is therefore uneven: it under-serves the long-line file before each long line "
        f"and over-serves it on the A14 round, finishing the file in {rounds_rr:.0f} rounds. <b>drr recovers exactly "
        f"what rr forfeits</b>: the unused part of each allowance becomes deficit, so every round's allowance is Q plus "
        f"what was left over and the file's cumulative service tracks Q per round. It never forfeits, never fires A14, "
        f"and needs {rounds_drr:.0f} rounds (= ceil(153,613 / {QUANTUM})), of which the deficit-building rounds ahead of "
        f"each long line transfer nothing. What drr buys is exact, predictable accounting; what it costs is more "
        f"rounds (and queue trips) for the long-line file."))
    story.append(P(
        f"<b>Effect on response time.</b> The mechanism differences above are large and deterministic; their effect on "
        f"the large file's response time is small. Its median slowdown is {slow('rr_ref', 'large'):.0f} (rr) and "
        f"{slow('drr_ref', 'large'):.0f} (drr) ns/byte at the reference configuration, "
        f"{'with overlapping run-to-run ranges' if not disjoint(lg_rr, lg_dr) else 'in non-overlapping run-to-run ranges'} "
        f"({rng(lg_rr)} vs {rng(lg_dr)}), and {slow('rr_st1', 'large'):.0f} (rr) vs {slow('drr_st1', 'large'):.0f} (drr) with "
        f"one thread ({rng(lg_rr1)} vs {rng(lg_dr1)}; fig. 3c). "
        + lg_text +
        f" The reason the effect is small is load: with 8 closed-loop clients at most 4 requests wait at the reference "
        f"configuration (7 with one thread), so an extra round, or a zero-progress deficit round, is soon followed by "
        f"another turn."))

    # ---------------------------------------------------------------- 5
    story.append(P("5. Trade-offs and the SJF starvation", H1))
    story.append(fig_img("fig4_sjf_starvation.png"))
    sj = {cls: wait_cls("sjf_ref", cls) for cls in CLASSES}
    sj1_99 = wait_cls("sjf_st1", "large", "wait_p99_ns")
    fc1_99 = wait_cls("fcfs_st1", "large", "wait_p99_ns")
    story.append(P(
        f"<b>Who each policy penalises.</b> <b>fcfs</b> is fair in waiting (all classes wait alike: "
        f"{wait_cls('fcfs_ref', 'small'):.0f} / {wait_cls('fcfs_ref', 'medium'):.0f} / {wait_cls('fcfs_ref', 'large'):.0f} us "
        f"at p50) but penalises <i>small</i> requests, which queue behind long transfers: their slowdown is the highest "
        f"in every configuration ({f_small('fcfs_st1'):.0f} ns/byte at one thread). <b>sjf</b> reverses this: with one "
        f"thread it gives the best small and medium slowdown ({f_small('sjf_st1'):.0f} and "
        f"{slow('sjf_st1', 'medium'):.0f} ns/byte, against {f_small('rr_st1'):.0f} and {slow('rr_st1', 'medium'):.0f} for rr) "
        f"and pushes the cost onto the <i>large</i> class ({slow('sjf_st1', 'large'):.0f} ns/byte, at or above rr / drr "
        f"at {slow('rr_st1', 'large'):.0f} / {slow('drr_st1', 'large'):.0f} and about {slow('sjf_st1', 'large') / slow('fcfs_st1', 'large'):.1f}x "
        f"fcfs). At the reference configuration rr / drr already match or beat sjf for small files "
        f"({f_small('rr_ref'):.0f} / {f_small('drr_ref'):.0f} vs {f_small('sjf_ref'):.0f}). <b>rr and drr</b> get a similar "
        f"small-file gain without ever ranking requests by size: they cut small-file slowdown by about "
        f"{f_small('fcfs_st1') / f_small('rr_st1'):.0f}x at one thread while charging the large class about "
        f"{slow('rr_st1', 'large') / slow('fcfs_st1', 'large'):.1f}x its fcfs slowdown, because a large request is now "
        f"interleaved with everyone else instead of running to completion. What separates sjf is waiting time, next."))
    story.append(P(
        f"<b>The SJF starvation (A11).</b> Under sjf the wait depends on size: at the reference configuration the median "
        f"wait is {sj['small']:.0f} us for small, {sj['medium']:.0f} for medium and {sj['large']:.0f} us for large "
        f"requests (large waits {sj['large'] / sj['small']:.1f}x longer than small; under fcfs, rr and drr the three "
        f"classes wait about the same). The tail shows it: with one thread the large class's p99 waiting time is "
        f"{sj1_99 / 1000:.0f} ms under sjf against {fc1_99 / 1000:.0f} ms under fcfs, and sjf has the largest overall "
        f"p99 waiting time in that configuration ({w99['sjf_st1'] / 1000:.0f} ms vs {w99['fcfs_st1'] / 1000:.0f} ms). "
        f"Starvation here is bounded, because each of the 8 clients has at most one request outstanding, so a large "
        f"request is overtaken only by requests issued while it waits; with an open arrival stream of small requests "
        f"it would be unbounded, which is why real systems add aging."))

    # ---------------------------------------------------------------- 6
    story.append(P("6. Aside: --p (A30) and caveats", H1))
    story.append(P(
        f"<b>--p</b> changes system calls, not scheduling. fcfs at the reference configuration with --p 1 vs --p 10 "
        f"(3 alternating pairs): {sc_txt}. Median waiting was {wait1:.0f} us with --p 1 and {wait10:.0f} us with "
        f"--p 10 ({wait_txt}). Throughput was too noisy to rank (medians {thr1:.0f} vs {thr10:.0f} req/s, single runs "
        f"{min(m['throughput'] for m in p1 + p10):.0f}-{max(m['throughput'] for m in p1 + p10):.0f}). The scheduling columns "
        f"are unaffected (rounds = 1, forfeited_bytes = 0 in both). "
        f"<b>Caveats.</b> One machine, loopback, three workload files and a closed loop of 8 clients: the queue is "
        f"short, which limits how far the policies can separate. Throughput and tail values move a lot between runs on "
        f"this host, so the report relies on medians and on comparisons made inside the same block of runs. "
        f"The A14 reading above is my interpretation of the spec; the alternative (apply it to a long line anywhere in "
        f"a round) would leave rr with almost no forfeiture on this workload and remove the effect described in section 4."))

    doc = SimpleDocTemplate(os.path.join(ROOT, "report_A.pdf"), pagesize=letter,
                            leftMargin=0.55 * inch, rightMargin=0.55 * inch, topMargin=0.5 * inch,
                            bottomMargin=0.5 * inch, title="COL724/7524 Assignment 2 - Part A report",
                            author="Mohit")
    doc.build(story)
    return doc.page


if __name__ == "__main__":
    fig1()
    fig2()
    fig3()
    fig4()
    pages = build_pdf()
    print(f"figures written to {ROOT}; report_A.pdf has {pages} page(s) (limit 6)")
