
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(SCRIPT_DIR)
sys.path.insert(0, SCRIPT_DIR)

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from reportlab.lib import colors
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.platypus import (Image, KeepTogether, Paragraph, SimpleDocTemplate,
                                Spacer, Table, TableStyle)

from common import N_REQUESTS, QUANTUM
from metrics import CELLS, CLASSES, REFERENCE, fmt_count, load_all

AUTHORS = os.environ.get("REPORT_AUTHORS", "Mohit and teammate")
RESULTS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "results")
D = load_all(RESULTS)

POLICY_COLOR = {"fcfs": "#4c72b0", "sjf": "#dd8452", "rr": "#55a868", "drr": "#8172b3"}
LABEL = {"fcfs_ref": "fcfs", "sjf_ref": "sjf", "rr_ref": "rr", "drr_ref": "drr",
         "fcfs_st1": "fcfs", "rr_st1": "rr"}
THR = {"fcfs_ref": 4, "sjf_ref": 4, "rr_ref": 4, "drr_ref": 4, "fcfs_st1": 1, "rr_st1": 1}

def pol(cell):
    return cell.split("_")[0]

def m(cell):
    return D["cells"][cell]

def slow(cell, cls, key="slow_med"):
    return m(cell)["by_class"][cls][key]

def wait_avg(cell, cls):
    return m(cell)["by_class"][cls]["wait_avg_ns"] / 1000.0

plt.rcParams.update({"font.size": 7.5, "axes.titlesize": 8, "axes.labelsize": 7.5,
                     "legend.fontsize": 7, "figure.dpi": 200})

def fig1():
    fig, (a, b) = plt.subplots(1, 2, figsize=(7.4, 2.7))
    xs = list(range(len(CELLS)))
    w = 0.38
    for i, c in enumerate(CELLS):
        a.bar(i - w / 2, m(c)["wait_p50_ns"] / 1000, w, color=POLICY_COLOR[pol(c)])
        a.bar(i + w / 2, m(c)["wait_p99_ns"] / 1000, w, color=POLICY_COLOR[pol(c)], hatch="///",
              edgecolor="white", linewidth=0.5)
    a.set_yscale("log")
    a.set_xticks(xs)
    a.set_xticklabels([f"{LABEL[c]}\n{THR[c]} thr" for c in CELLS])
    a.set_ylabel("waiting time (us, log)")
    a.set_title("(a) waiting: solid = p50, hatched = p99")
    for i, c in enumerate(CELLS):
        b.bar(i, m(c)["throughput"], 0.7, color=POLICY_COLOR[pol(c)])
    b.set_xticks(xs)
    b.set_xticklabels([f"{LABEL[c]}\n{THR[c]} thr" for c in CELLS])
    b.set_ylabel("throughput (req/s)")
    b.set_title("(b) throughput")
    fig.tight_layout()
    fig.savefig(os.path.join(ROOT, "fig1_waiting_throughput.png"))
    plt.close(fig)

def fig2():
    fig, axes = plt.subplots(1, 2, figsize=(7.4, 2.6), sharey=True)
    for ax, group, title in ((axes[0], REFERENCE, "(a) 4 server threads (reference)"),
                             (axes[1], ("fcfs_st1", "rr_st1"), "(b) 1 server thread")):
        n = len(group)
        w = 0.8 / n
        for j, cell in enumerate(group):
            xs = [i + (j - (n - 1) / 2) * w for i in range(3)]
            vals = [slow(cell, cls) for cls in CLASSES]
            ax.bar(xs, vals, w, color=POLICY_COLOR[pol(cell)], label=LABEL[cell])
        ax.set_yscale("log")
        ax.set_xticks(range(3))
        ax.set_xticklabels([f"{c}\n({s})" for c, s in zip(CLASSES, ("~1 KB", "~30 KB", "~150 KB"))])
        ax.set_title(title)
    axes[0].set_ylabel("normalised slowdown (ns/byte, log)")
    axes[0].legend(ncol=4, loc="upper right")
    fig.tight_layout()
    fig.savefig(os.path.join(ROOT, "fig2_slowdown.png"))
    plt.close(fig)

def fig3():
    fig, axes = plt.subplots(1, 3, figsize=(7.4, 2.5))
    a, b, c = axes
    w = 0.35
    for j, cell in enumerate(("rr_ref", "drr_ref")):
        per_get = [m(cell)["by_class"][cls]["forfeited"] / max(1, m(cell)["by_class"][cls]["n_get"]) for cls in CLASSES]
        a.bar([i + (j - 0.5) * w for i in range(3)], per_get, w, color=POLICY_COLOR[pol(cell)], label=LABEL[cell])
        rounds = [m(cell)["by_class"][cls]["rounds_mean_get"] for cls in CLASSES]
        b.bar([i + (j - 0.5) * w for i in range(3)], rounds, w, color=POLICY_COLOR[pol(cell)], label=LABEL[cell])
    a.set_yscale("symlog", linthresh=10)
    a.set_title("(a) forfeited bytes per GET")
    b.set_title("(b) rounds per GET")
    for ax in (a, b):
        ax.set_xticks(range(3))
        ax.set_xticklabels(CLASSES)
    a.legend()
    for j, cell in enumerate(("rr_ref", "drr_ref")):
        for k, (key, hatch) in enumerate((("slow_med", None), ("slow_p99", "///"))):
            x = (j - 0.5) * 0.36 + (k - 0.5) * 0.17
            c.bar(x, slow(cell, "large", key), 0.17, color=POLICY_COLOR[pol(cell)], hatch=hatch,
                  edgecolor="white", linewidth=0.5)
    c.set_xticks([0])
    c.set_xticklabels(["4 thr"])
    c.set_yscale("log")
    c.set_title("(c) large.txt slowdown (ns/byte)\nsolid = median, hatched = p99")
    fig.tight_layout()
    fig.savefig(os.path.join(ROOT, "fig3_rr_vs_drr.png"))
    plt.close(fig)

def fig4():
    fig, axes = plt.subplots(1, 2, figsize=(7.4, 2.5), sharey=True)
    for ax, group, title in ((axes[0], REFERENCE, "(a) 4 server threads"),
                             (axes[1], ("fcfs_st1", "rr_st1"), "(b) 1 server thread")):
        n = len(group)
        w = 0.8 / n
        for j, cell in enumerate(group):
            xs = [i + (j - (n - 1) / 2) * w for i in range(3)]
            vals = [wait_avg(cell, cls) for cls in CLASSES]
            ax.bar(xs, vals, w, color=POLICY_COLOR[pol(cell)], label=LABEL[cell])
        ax.set_yscale("log")
        ax.set_xticks(range(3))
        ax.set_xticklabels(CLASSES)
        ax.set_title(title)
    axes[0].set_ylabel("average waiting time (us, log)")
    axes[0].legend(ncol=4, loc="upper left")
    fig.tight_layout()
    fig.savefig(os.path.join(ROOT, "fig4_sjf_starvation.png"))
    plt.close(fig)

ss = getSampleStyleSheet()
BODY = ParagraphStyle("body", parent=ss["BodyText"], fontName="Helvetica", fontSize=8.6, leading=10.6,
                      spaceAfter=3.5)
H1 = ParagraphStyle("h1", parent=ss["Heading2"], fontName="Helvetica-Bold", fontSize=10.5, leading=12.5,
                    spaceBefore=6, spaceAfter=3, keepWithNext=1)
TITLE = ParagraphStyle("title", parent=ss["Title"], fontName="Helvetica-Bold", fontSize=13.5, leading=16,
                       spaceAfter=2)
SMALL = ParagraphStyle("small", parent=BODY, fontSize=7.4, leading=9, textColor=colors.HexColor("#333333"))

def P(text, style=BODY):
    return Paragraph(text, style)

def table(rows, col_widths, header_rows=1, font=7.4):
    t = Table(rows, colWidths=col_widths, repeatRows=header_rows)
    t.setStyle(TableStyle([
        ("FONT", (0, 0), (-1, -1), "Helvetica", font),
        ("FONT", (0, 0), (-1, header_rows - 1), "Helvetica-Bold", font),
        ("BACKGROUND", (0, 0), (-1, header_rows - 1), colors.HexColor("#e8e8ee")),
        ("GRID", (0, 0), (-1, -1), 0.3, colors.HexColor("#999999")),
        ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ("TOPPADDING", (0, 0), (-1, -1), 1.8), ("BOTTOMPADDING", (0, 0), (-1, -1), 1.8),
        ("LEFTPADDING", (0, 0), (-1, -1), 3), ("RIGHTPADDING", (0, 0), (-1, -1), 3),
    ]))
    return t

def fig_img(name, width_in=7.2):
    from PIL import Image as PILImage
    path = os.path.join(ROOT, name)
    w, h = PILImage.open(path).size
    return Image(path, width=width_in * inch, height=width_in * inch * h / w)

def close(a, b, tol=0.15):
    return abs(a - b) / max(a, b) <= tol

def build_pdf():
    w50 = {c: m(c)["wait_p50_ns"] / 1000 for c in CELLS}
    w99 = {c: m(c)["wait_p99_ns"] / 1000 for c in CELLS}
    r50 = {c: m(c)["resp_p50_ns"] / 1000 for c in CELLS}
    r99 = {c: m(c)["resp_p99_ns"] / 1000 for c in CELLS}
    thr = {c: m(c)["throughput"] for c in CELLS}

    a14_rr = D["a14"].get("rr_ref")
    forf_med_get = m("rr_ref")["by_class"]["medium"]["forfeited"] / max(1, m("rr_ref")["by_class"]["medium"]["n_get"])
    forf_large_get = m("rr_ref")["by_class"]["large"]["forfeited"] / max(1, m("rr_ref")["by_class"]["large"]["n_get"])
    rounds_rr = m("rr_ref")["by_class"]["large"]["rounds_mean_get"]
    rounds_drr = m("drr_ref")["by_class"]["large"]["rounds_mean_get"]
    n_get_large = m("rr_ref")["by_class"]["large"]["n_get"]

    large_close = close(slow("rr_ref", "large"), slow("drr_ref", "large"))
    thr_close = close(thr["fcfs_st1"], thr["rr_st1"])

    story = []
    story.append(P("COL724/7524 Assignment 2, Part A: Link Scheduling", TITLE))
    story.append(P(f"{AUTHORS}", SMALL))
    story.append(Spacer(1, 3))

    story.append(P("1. The four policies as implemented", H1))
    story.append(P(
        "<b>Queue and threads (A5).</b> An acceptor thread only calls accept(). Each connection goes to a short-lived "
        "admission thread that reads the header line under a 5 s total deadline, answers HEALTH immediately with the "
        "current queue depth, validates the file name (A25), and enqueues the request. For a GET it opens the file and "
        "takes the size from fstat, so the size is known, and fixed to one version of the file, when the request enters "
        "the queue. <i>server_threads</i> workers only loop on <i>next()</i>, then serve_slice() for one round, then "
        "either requeue the request (PREEMPTED) or finish it. Because admission never waits for a worker, requests "
        "really do pile up in the scheduler queue: a test that pins the only worker on a stalled PUT sees HEALTH report "
        "a depth of 6, and HEALTH still answers in about 1 ms while a silent client is connected. Clients that trickle "
        "bytes are cut off by total deadlines (5 s for the header, 10 s per 64 KB of a PUT body), not by a per-recv() "
        "timeout, so they cannot pin an admission thread or a worker either."))
    story.append(P(
        "<b>Policies.</b> <b>fcfs</b> serves in arrival order. <b>sjf</b> serves the queued request with the smallest "
        "declared byte count (file size for GET, the count in the request line for PUT: one key for both verbs), "
        "earliest arrival first on ties, with no aging. fcfs and sjf never preempt, so every request takes one round "
        f"(<i>rounds</i> = 1). <b>rr</b> gives each scheduled request an allowance of Q = {QUANTUM} bytes per round and "
        "requeues it at the tail. <b>drr</b> uses the same queue but each request carries a deficit counter (initially "
        "0): a round's allowance is deficit + Q, and whatever part of it goes unused is kept in the deficit instead of "
        "being forfeited; the deficit is discarded when the request finishes."))
    story.append(P(
        "<b>One round (serve_slice).</b> A GET is sent in whole lines (A6): the line length is found by scanning to "
        "the next '\\n' (a last line without one still counts), and lines are grouped --p per write() (A8) without "
        "changing which bytes go out or how the allowance is charged. A round ends as soon as the next line would "
        "exceed the remaining allowance (A13); under rr the unused remainder is added to <i>forfeited_bytes</i>, under "
        "drr it becomes the deficit. <b>A14</b> (rr only): if a line longer than Q starts a round, i.e. the round has "
        "transferred nothing yet, it is sent in full and the round ends, overrunning the allowance and logging one "
        "line. I read A14 this way because its stated purpose is to stop a request being requeued having transferred "
        "nothing, forever; a long line reached <i>mid-round</i> is handled by A13 instead (round ends, remainder "
        "forfeited) and goes out first in the next round. drr has no escape: the deficit grows by Q per round until it "
        "covers the line (A16), so a 20,000-byte line costs up to 3 rounds. A PUT round reads exactly "
        "min(allowance, remaining) bytes (A9, A13) into a temporary file that is renamed onto the destination when the "
        "last byte arrives, so a concurrent GET never sees a half-written file. The 'OK n' response line is protocol "
        "overhead and is not charged to the allowance; the terminating '\\n' of every line is (A7)."))
    story.append(P(
        "<b>Preemption state (A17).</b> Everything a later round needs lives in the Request: the byte offset; the file "
        "descriptor opened at admission (so every round reads the version that was sized); the bytes already read from "
        "the socket but not yet consumed (for a PUT, the body bytes that arrived with the header); the deficit; "
        "<i>rounds</i> and <i>forfeited_bytes</i>; and the temporary-file path. Correctness is checked by 72 unit "
        "assertions that drive serve_slice over a message-preserving socketpair against rounds, forfeiture and A14 "
        "counts worked out by hand (e.g. ten 10-byte lines with Q = 35: rr takes 4 rounds and forfeits 15 bytes, drr "
        "takes 3 and forfeits 0), including a GET that keeps serving the old file after a PUT has replaced it, and by "
        "an integration suite that runs all four policies end to end."))

    story.append(P("2. Experimental setup", H1))
    story.append(P(
        "<b>Machine and configuration.</b> Server and client run on one laptop under WSL2 (Ubuntu 24.04, 8 logical "
        "CPUs) over loopback TCP, so absolute times are not network-representative; only comparisons between policies "
        "are meaningful. The reference configuration is 4 server threads and 8 client threads; the second is 1 server "
        f"thread. Q = {QUANTUM} bytes. Each run makes {N_REQUESTS} counted requests from a closed loop of 8 client "
        "threads, after 3 uncounted seeding PUTs (dropped by sorting the CSV by arrival and removing the first 3 "
        "rows). Each request picks one of the workload files uniformly and does a GET or a PUT with probability 1/2. "
        "Percentiles are nearest-rank. TCP_NODELAY is set so every write() is a separate segment."))
    story.append(P(
        "<b>Workload (A27), both axes in one directory.</b> <i>Size:</i> small.txt 1,044 B, medium.txt 30,774 B, "
        "large.txt 153,613 B. <i>Line length:</i> small and medium contain only ordinary 60-79 byte lines; large.txt "
        f"has the same ordinary lines plus 6 lines of 20,000 bytes (2.4 x Q) spread evenly through it, so it is both "
        f"the large file and the long-line file. With Q = {QUANTUM} the large file needs 19 rounds under a byte-exact "
        "accounting."))
    story.append(P(
        "<b>Repetition (A28).</b> One run per cell, as the spec's default ('No repetition is required'). Two pairs of "
        "numbers below are close enough that a single run cannot support a claim of direction (the rr/drr slowdown of "
        "the long-line file, and the fcfs/rr throughput with one server thread); per A28's rule I state them as not "
        "separable rather than rerun for a direction I do not need. Raw per-request CSVs of all six runs are in "
        "results/."))

    story.append(P("3. Per-run results", H1))
    rows = [["cell", "waiting p50 (us)", "waiting p99 (us)", "response p50 (us)", "response p99 (us)", "throughput (req/s)"]]
    for c in CELLS:
        rows.append([f"{LABEL[c]}, {THR[c]} thr", f"{w50[c]:.0f}", f"{w99[c]:.0f}", f"{r50[c]:.0f}", f"{r99[c]:.0f}", f"{thr[c]:.1f}"])
    story.append(KeepTogether([
        P("<b>Table 1.</b> Waiting time (start - arrival), response time (finish - arrival) and throughput, one run "
          "per cell (A19, A20).", SMALL),
        table(rows, [1.15 * inch, 1.27 * inch, 1.3 * inch, 1.27 * inch, 1.3 * inch, 1.05 * inch]),
    ]))
    story.append(Spacer(1, 3))
    story.append(fig_img("fig1_waiting_throughput.png"))

    rows = [["cell", "small med", "small p99", "medium med", "medium p99", "large med", "large p99"]]
    for c in REFERENCE:
        rows.append([f"{LABEL[c]}, 4 thr"] + [f"{slow(c, cls, k):.1f}" for cls in CLASSES for k in ("slow_med", "slow_p99")])
    story.append(KeepTogether([
        P("<b>Table 2.</b> Normalised slowdown (response / bytes, ns per byte) by size class, reference config only "
          "(A21).", SMALL),
        table(rows, [1.4 * inch, 0.95 * inch, 0.95 * inch, 1.0 * inch, 1.0 * inch, 0.95 * inch, 0.95 * inch]),
    ]))
    story.append(Spacer(1, 3))
    story.append(fig_img("fig2_slowdown.png"))

    story.append(P(
        f"<b>Reading the numbers.</b> Waiting falls sharply under the preemptive policies: median waiting is "
        f"{w50['fcfs_ref']:.0f} us under fcfs, {w50['sjf_ref']:.0f} us under sjf, and {w50['rr_ref']:.0f} / "
        f"{w50['drr_ref']:.0f} us under rr / drr (about {w50['fcfs_ref'] / w50['rr_ref']:.1f}x lower than fcfs), and "
        f"with one server thread from {w50['fcfs_st1'] / 1000:.1f} ms to {w50['rr_st1'] / 1000:.1f} ms under rr - no "
        f"request holds a worker for its whole transfer. The slowdown table (A21) shows who pays for the shorter "
        f"queue: the small class gains most, from {slow('fcfs_ref', 'small'):.0f} (fcfs) to {slow('sjf_ref', 'small'):.0f} "
        f"(sjf) to {slow('rr_ref', 'small'):.0f} / {slow('drr_ref', 'small'):.0f} ns/byte (rr / drr); the large class "
        f"pays, from {slow('fcfs_ref', 'large'):.0f} (fcfs) up to {slow('rr_ref', 'large'):.0f} / "
        f"{slow('drr_ref', 'large'):.0f} (rr / drr). Medium files move the least at the reference configuration. "
        f"Throughput is lower under rr/drr ({thr['rr_ref']:.0f} / {thr['drr_ref']:.0f} req/s vs {thr['fcfs_ref']:.0f} "
        f"for fcfs): every extra round costs a queue trip and extra system calls. " +
        (f"With one server thread the throughputs ({thr['fcfs_st1']:.0f} fcfs vs {thr['rr_st1']:.0f} rr) are close "
         f"enough that a single run cannot separate them: the one worker is the bottleneck either way."
         if thr_close else
         f"With one server thread the throughputs differ ({thr['fcfs_st1']:.0f} fcfs vs {thr['rr_st1']:.0f} rr).")))
    story.append(P(
        f"<b>Waiting versus response (A19).</b> The two tell different stories. At the reference configuration rr / "
        f"drr cut median waiting {w50['fcfs_ref'] / w50['rr_ref']:.1f}x, yet median response is "
        f"{'higher' if min(r50['rr_ref'], r50['drr_ref']) > r50['fcfs_ref'] else 'not lower'}: {r50['rr_ref']:.0f} / "
        f"{r50['drr_ref']:.0f} us against {r50['fcfs_ref']:.0f} us for fcfs. Response also contains the request's own "
        f"service time, and a preempted request's service is spread over several rounds interleaved with other "
        f"requests, so the earlier start is spent on the way to the finish rather than shortening it. With one server "
        f"thread the queue is long enough for the gain to survive: median response falls from "
        f"{r50['fcfs_st1'] / 1000:.1f} ms (fcfs) to {r50['rr_st1'] / 1000:.1f} ms (rr), but the tail moves the other "
        f"way: p99 response {r99['fcfs_st1'] / 1000:.0f} ms (fcfs) vs {r99['rr_st1'] / 1000:.0f} ms (rr), because the "
        f"large requests now wait through many rounds. Response time alone would have hidden the waiting-time gain."))

    story.append(P("4. rr versus drr (A29)", H1))
    rows = [["", "forfeited bytes", "", "", "rounds / GET", "", "A14 fires", "large.txt slowdown"],
            ["cell", "small", "medium", "large", "medium", "large", "per run", "median / p99"]]
    for c in ("rr_ref", "drr_ref"):
        rows.append([f"{LABEL[c]}, 4 thr",
                     *(f"{m(c)['by_class'][cls]['forfeited']:,.0f}" for cls in CLASSES),
                     f"{m(c)['by_class']['medium']['rounds_mean_get']:.1f}",
                     f"{m(c)['by_class']['large']['rounds_mean_get']:.1f}",
                     fmt_count(D["a14"].get(c)),
                     f"{slow(c, 'large'):.1f} / {slow(c, 'large', 'slow_p99'):.1f}"])
    t = table(rows, [1.3 * inch, 0.7 * inch, 0.85 * inch, 0.95 * inch, 0.75 * inch, 0.6 * inch, 0.75 * inch, 1.4 * inch], header_rows=2)
    t.setStyle(TableStyle([("SPAN", (1, 0), (3, 0)), ("SPAN", (4, 0), (5, 0))]))
    story.append(KeepTogether([
        P(f"<b>Table 3.</b> Long-line comparison, one run each. A14 fired {fmt_count(a14_rr)} times under rr, exactly "
          f"the 6 long lines of every large-file GET ({n_get_large} GETs this run), and never under drr.", SMALL),
        t]))
    story.append(Spacer(1, 3))
    story.append(fig_img("fig3_rr_vs_drr.png"))
    story.append(P(
        f"<b>Why long lines get less than their full allowance under rr.</b> A round can only end on a line boundary. "
        f"With ordinary 60-79 byte lines the leftover is tiny: a medium GET forfeits about {forf_med_get:.0f} bytes in "
        f"total over its 3 preempted rounds (well under 1% of Q per round). A 20,000-byte line changes that: about "
        f"4.8 KB of ordinary lines separate the long lines, so when the next long line is reached mid-round it cannot "
        f"fit in what is left, the round ends early and the remainder is forfeited - the round transfers only part of "
        f"its Q. That happens once per long line: the large file forfeits {forf_large_get:,.0f} bytes per GET "
        f"({m('rr_ref')['by_class']['large']['forfeited']:,.0f} this run), about "
        f"{forf_large_get / 6 / 1000:.1f} KB, or {100 * forf_large_get / 6 / QUANTUM:.0f}% of a quantum, at each of "
        f"its six long lines. The next round then starts with the long line and A14 sends it whole, overrunning Q. "
        f"rr's accounting is therefore uneven: it under-serves the long-line file before each long line and "
        f"over-serves it on the A14 round, finishing the file in {rounds_rr:.0f} rounds. <b>drr recovers exactly what "
        f"rr forfeits</b>: the unused part of each allowance becomes deficit, so every round's allowance is Q plus "
        f"what was left over and the file's cumulative service tracks Q per round. It never forfeits, never fires "
        f"A14, and needs {rounds_drr:.0f} rounds (= ceil(153,613 / {QUANTUM})), of which the deficit-building rounds "
        f"ahead of each long line transfer nothing. What drr buys is exact, predictable accounting; what it costs is "
        f"more rounds (and queue trips) for the long-line file."))
    story.append(P(
        f"<b>Effect on response time.</b> The mechanism differences above are large and deterministic; their effect "
        f"on the large file's response time is small at this load: its slowdown is {slow('rr_ref', 'large'):.1f} "
        f"(rr) and {slow('drr_ref', 'large'):.1f} (drr) ns/byte, " +
        ("close enough that one run cannot separate them (fig. 3c)." if large_close else
         "with rr and drr clearly separated in this run (fig. 3c).") +
        f" The reason is load: with 8 closed-loop clients there are never more than 4 requests waiting at the "
        f"reference configuration, so an extra round, or a zero-progress deficit round, is soon followed by another "
        f"turn."))

    story.append(P("5. Trade-offs and the SJF starvation", H1))
    story.append(fig_img("fig4_sjf_starvation.png"))
    story.append(P(
        f"<b>Who each policy penalises.</b> Ranking the four policies' slowdown within each size class (best to worst) "
        f"gives a different order every time: small is rr ({slow('rr_ref', 'small'):.0f}), drr "
        f"({slow('drr_ref', 'small'):.0f}), sjf ({slow('sjf_ref', 'small'):.0f}), fcfs ({slow('fcfs_ref', 'small'):.0f} "
        f"ns/byte); medium is sjf ({slow('sjf_ref', 'medium'):.0f}), fcfs ({slow('fcfs_ref', 'medium'):.0f}), drr "
        f"({slow('drr_ref', 'medium'):.0f}), rr ({slow('rr_ref', 'medium'):.0f}); large is fcfs "
        f"({slow('fcfs_ref', 'large'):.0f}), sjf ({slow('sjf_ref', 'large'):.0f}), drr ({slow('drr_ref', 'large'):.0f}), "
        f"rr ({slow('rr_ref', 'large'):.0f} ns/byte). <b>fcfs</b> is fair in waiting (all classes wait about the same: "
        f"{wait_avg('fcfs_ref', 'small'):.0f} / {wait_avg('fcfs_ref', 'medium'):.0f} / "
        f"{wait_avg('fcfs_ref', 'large'):.0f} us) but is worst for small files, which queue behind long transfers, "
        f"and best for large ones, which run to completion with no interruption overhead once picked. <b>rr and drr</b> "
        f"are the opposite: best for small (frequent turns) and worst for large, because a large request is now "
        f"interleaved with everyone else instead of running to completion - {slow('fcfs_ref', 'small') / slow('rr_ref', 'small'):.1f}x "
        f"better than fcfs for small, {slow('rr_ref', 'large') / slow('fcfs_ref', 'large'):.1f}x worse for large. "
        f"<b>sjf</b> does not simply reverse fcfs: it is best for medium and, because it never preempts, second-best "
        f"for large ({slow('sjf_ref', 'large'):.0f} ns/byte, beaten only by fcfs's {slow('fcfs_ref', 'large'):.0f}) "
        f"despite making large requests wait the longest of any policy (next section) - once picked, a large request "
        f"runs to completion just as under fcfs, so a long wait does not translate into a long slowdown the way it "
        f"does under fcfs. Its cost falls on small files instead ({slow('sjf_ref', 'small'):.0f} ns/byte: much better "
        f"than fcfs, but worse than rr/drr, which never rank by size at all)."))
    story.append(P(
        f"<b>The SJF starvation (A11).</b> Under sjf the wait depends on size: the average wait is "
        f"{wait_avg('sjf_ref', 'small'):.0f} us for small, {wait_avg('sjf_ref', 'medium'):.0f} for medium and "
        f"{wait_avg('sjf_ref', 'large'):.0f} us for large requests - large waits "
        f"{wait_avg('sjf_ref', 'large') / wait_avg('sjf_ref', 'small'):.1f}x longer than small, while under fcfs, rr "
        f"and drr the three classes wait about the same. Starvation here is bounded, because each of the 8 clients "
        f"has at most one request outstanding, so a large request is overtaken only by requests issued while it "
        f"waits; with an open arrival stream of small requests it would be unbounded, which is why real systems add "
        f"aging."))

    story.append(P("6. Caveats", H1))
    p1 = D["aside"].get("aside_p1")
    p10 = D["aside"].get("aside_p10")
    if p1 and p10 and p1["send_calls"] is not None and p10["send_calls"] is not None:
        p_text = (f"--p 10 vs --p 1 on the fcfs reference cell (one run each) cut the number of send() calls from "
                  f"{p1['send_calls']:,} to {p10['send_calls']:,} ({p1['send_calls'] / p10['send_calls']:.1f}x fewer) "
                  f"without changing the scheduling columns (rounds = 1, forfeited_bytes = 0 in both) - it changes "
                  f"system-call count, not scheduling, as the spec says.")
    else:
        p_text = ("its send()-count effect is described in README.md (the submission zip does not ship server logs, "
                  "which the send() count comes from).")
    story.append(P(
        "One machine, loopback, three workload files and a closed loop of 8 clients: the queue is short, which "
        "limits how far the policies can separate, and a single run per cell (A28's default) means the two close "
        "pairs above are reported as not separable rather than as a claimed direction. The A14 reading in section 1 "
        "is my interpretation of the spec; the alternative (apply it to a long line reached anywhere in a round, not "
        "just at the start) would leave rr with almost no forfeiture on this workload and remove the effect described "
        "in section 4. <i>--p</i> (A30) is not part of any required experiment; briefly, " + p_text))

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
