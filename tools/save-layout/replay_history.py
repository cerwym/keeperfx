#!/usr/bin/env python3
"""Run the save-format check over the history of a branch, one commit at a time.
Used to prove the check gives the expected answers before relying on it.

    replay_history.py extract <dir> [--ref upstream/master] [--since 2026-03-21]
        For every first-parent commit since the date, export the headers and
        extract the saved layout. Commits with identical headers share one
        layout. Needs git, plus the tools extract.sh needs, and SDL_INCLUDES.
        Commits from before SDL3 (#5085) also need SDL2 headers in SDL_INCLUDES.

    replay_history.py check <dir> [--expect identical=N,compatible=N,refused=N,silent=N]
        Classify each commit against its first parent (the previous alpha
        build) for saved games, print the counts, and fail if they differ
        from --expect.

Reference result, 2026-03-27 .. 2026-09-21 on upstream master (490 commits):
    identical=447,compatible=2,refused=36,silent=5
"""
import argparse
import collections
import hashlib
import json
import os
import subprocess
import sys
import tarfile
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import compare  # noqa: E402


def git(*args):
    return subprocess.run(["git"] + list(args), capture_output=True, text=True, check=True).stdout


def extract(a):
    os.makedirs(os.path.join(a.dir, "out"), exist_ok=True)
    commits = git("log", a.ref, "--since=" + a.since, "--first-parent", "--reverse",
                  "--format=%H\t%h\t%cd\t%s", "--date=short").splitlines()
    first = commits[0].split("\t")[0]
    base = git("rev-parse", first + "^1").strip()
    rows = [(base, base[:9], git("show", "-s", "--format=%cd", "--date=short", base).strip(), "(baseline)")]
    rows += [tuple(c.split("\t", 3)) for c in commits]
    meta, done = [], set()
    for sha, short, date, subject in rows:
        listing = git("ls-tree", "-r", sha, "src")
        headers = "\n".join(l for l in listing.splitlines() if l.endswith((".h", ".hpp")))
        key = hashlib.sha1(headers.encode()).hexdigest()[:16]
        meta.append({"sha": sha, "short": short, "date": date, "subject": subject, "hdrkey": key})
        out = os.path.join(a.dir, "out", key + ".tsv")
        if key in done or os.path.exists(out):
            done.add(key)
            continue
        done.add(key)
        with tempfile.TemporaryDirectory() as tmp:
            tar = os.path.join(tmp, "src.tar")
            subprocess.run(["git", "archive", "--format=tar", "-o", tar, sha, "--",
                            ":(glob)src/**/*.h", ":(glob)src/**/*.hpp", ":(glob)src/*.h",
                            ":(glob)deps/centitoml/*.h"], check=True)
            with tarfile.open(tar) as t:
                t.extractall(tmp)
            r = subprocess.run(["bash", os.path.join(HERE, "extract.sh"), tmp, out], capture_output=True, text=True)
            print("%s %s %s" % (short, key, "ok" if r.returncode == 0 else "FAILED"), flush=True)
            if r.returncode != 0:
                sys.stderr.write(r.stderr[-2000:])
                if os.path.exists(out):
                    os.remove(out)
    json.dump(meta, open(os.path.join(a.dir, "commits.json"), "w"), indent=1)


def check(a):
    roots = compare.load_roots(os.path.join(HERE, "roots.txt"))
    meta = json.load(open(os.path.join(a.dir, "commits.json")))
    cache = {}

    def layout(key):
        if key not in cache:
            cache[key] = compare.load_layout(os.path.join(a.dir, "out", key + ".tsv"))
        return cache[key]

    counts = collections.Counter()
    for prev, cur in zip(meta, meta[1:]):
        if prev["hdrkey"] == cur["hdrkey"]:
            state = "identical"
        else:
            state = compare.analyse(roots, layout(prev["hdrkey"]), layout(cur["hdrkey"]))["files"]["savegame"]
            if state != "identical":
                print("%-10s %s %-10s %s" % (cur["short"], cur["date"], state, cur["subject"][:70]))
        counts[state] += 1
    got = ",".join("%s=%d" % (k, counts[k]) for k in compare.STATE_ORDER)
    print("commits=%d %s" % (len(meta) - 1, got))
    if a.expect and a.expect != got:
        print("MISMATCH: expected %s" % a.expect)
        return 1
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("extract")
    e.add_argument("dir")
    e.add_argument("--ref", default="upstream/master")
    e.add_argument("--since", default="2026-03-21")
    c = sub.add_parser("check")
    c.add_argument("dir")
    c.add_argument("--expect")
    a = ap.parse_args()
    return extract(a) if a.cmd == "extract" else check(a)


if __name__ == "__main__":
    sys.exit(main() or 0)
