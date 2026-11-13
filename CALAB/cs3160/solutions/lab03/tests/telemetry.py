"""What the student did, as opposed to what they scored.

The mid-semester exam produced a result the score data cannot explain: about
half the class landed at or below what the untouched starter scores. A mark of
16 is the same number whether it came from three hours of work that did not
come together, an hour spent on the wrong part, or a folder nobody opened. Those
are three different problems with three different answers, and telling them
apart needs behaviour rather than outcomes.

So this records, per lab folder:

    build      one per invocation of `make`, successful or not -- the
               debugging loop, which is the measurement the diagnostic exists
               to take
    testrun    one per `make test`, carrying what passed and which files
               differ from the starter as issued

Both go to `.telemetry/events.jsonl` inside the lab folder, appended and never
rewritten, because the folder is what gets collected at the end of a session.
Records carry the roll number as written, so the log holds personal data and is
handled the way the marks are.

**Nothing here may ever break a student's build or test run.** Every entry
point swallows its own exceptions and returns; a machine with a corrupt
stubs.json, a read-only disk or a Python this file does not like still
compiles and still runs its tests. `make` goes through the wrapper below, so
that rule is not a nicety -- a raised exception here would be a student unable
to build. Where that trade-off costs information, it costs information.

This file is the canonical copy; tools/sync-testlib.sh puts it in each lab.
"""

import datetime
import difflib
import hashlib
import json
import os
import re
import subprocess
import sys

# One folder rather than one file, so that the derived summary can be written
# beside the raw log later. Hidden, and named in the lab's .gitignore, so that a
# student who runs `git init` sees their own work in `git status` and not this.
# Deliberately not removed by `make clean`: a student who cleans should not
# erase the session.
LOG_DIR  = ".telemetry"
LOG_FILE = "events.jsonl"

# Fingerprints of every file as issued, written by tools/generate.py when the
# starter is built. See stubs_path() for why it is fingerprints rather than the
# files themselves.
STUBS = os.path.join("tests", "stubs.json")

# When the session began. In order:
#
#   CS3160_SESSION_START    an explicit ISO-8601 timestamp, for a session sat
#                           at an unusual time or replayed afterwards
#   details.txt's mtime     the normal answer. It is the first thing a student
#                           is told to do and the folder is unpacked at the
#                           start of the session, so either the moment they
#                           filled it in or the moment they were given the
#                           folder -- both of which are when they started
#   Monday 14:00            last resort, for a folder with no details.txt at
#                           all, which is a broken folder anyway
SESSION_WEEKDAY = 0     # Monday
SESSION_HOUR    = 14

# A compiler diagnostic, for counting. g++ writes "file:line:col: error: ...".
ERROR_LINE = re.compile(r"\berror:")


# ------------------------------------------------------------------ the clock

def session_start(lab_dir=None, now=None):
    """When this session began, as a timezone-aware datetime.

    A student who fills details.txt in at the *end* rather than the start moves
    this forward, which would make the events recorded before that point look
    as though they happened before the session did. Nothing is done about it
    here -- every record also carries its own absolute `timestamp_iso`, and
    tools/telemetry-report.py works the elapsed times out from those against
    one session start per student, so a moved details.txt cannot make one
    student's timeline run backwards.
    """
    override = os.environ.get("CS3160_SESSION_START")
    if override:
        try:
            started = datetime.datetime.fromisoformat(override)
            if started.tzinfo is None:
                started = started.astimezone()
            return started
        except ValueError:
            pass  # a malformed override falls through to details.txt

    if lab_dir is not None:
        try:
            mtime = os.path.getmtime(os.path.join(lab_dir, "details.txt"))
            return datetime.datetime.fromtimestamp(mtime).astimezone()
        except OSError:
            pass  # no details.txt: fall through to the weekly default

    now = now or datetime.datetime.now().astimezone()
    start = now.replace(hour=SESSION_HOUR, minute=0, second=0, microsecond=0)
    # Back up to the most recent Monday 14:00 that has already happened. A run
    # at Monday 13:00 belongs to the previous week's session, not to one that
    # has not started.
    days = (now.weekday() - SESSION_WEEKDAY) % 7
    start -= datetime.timedelta(days=days)
    if start > now:
        start -= datetime.timedelta(days=7)
    return start


# --------------------------------------------------------------- who this is

FIELDS = {
    "name":    re.compile(r"^\s*Name\s*:(.*)$", re.I),
    "roll":    re.compile(r"^\s*Roll\s*(?:Number|No\.?)?\s*:(.*)$", re.I),
    "machine": re.compile(r"^\s*(?:Lab\s*)?Machine\s*(?:Number|No\.?)?\s*:(.*)$", re.I),
}


def read_details(lab_dir):
    """The three fields of details.txt, each None if the line is absent.

    Deliberately the same shapes tools/grade.py accepts, so that a file good
    enough to be graded is good enough to be logged.
    """
    found = {"name": None, "roll": None, "machine": None}
    try:
        with open(os.path.join(lab_dir, "details.txt"), encoding="utf-8",
                  errors="replace") as f:
            text = f.read()
    except OSError:
        return found
    for line in text.splitlines():
        for key, pattern in FIELDS.items():
            m = pattern.match(line)
            if m and found[key] is None:
                found[key] = m.group(1).strip().strip("\t").strip()
    return found


def student_id(details):
    """The roll number as written, or None if nothing identifies them.

    Recorded in the clear. That makes `.telemetry/events.jsonl` a file holding
    personal data, so it is treated the way the marks are: never committed --
    the repository's .gitignore covers `.telemetry/` -- and any report built
    from it belongs outside the repository, next to the marks and under the
    same care.
    """
    roll = (details.get("roll") or "").strip()
    return roll or None


# ------------------------------------------------- what the student has edited

def stubs_path(lab_dir):
    return os.path.join(lab_dir, STUBS)


def load_stubs(lab_dir):
    """The starter's fingerprints: {relative path: {sha256, lines: [...]}}.

    Fingerprints rather than the starter's text, for one reason: shipping a
    pristine copy of every file inside the folder would hand a student the
    restore that R2 exists to detect. `lines` is one short hash per line, which
    is enough for difflib to produce exact added/removed counts and is not
    enough to reconstruct the file.
    """
    try:
        with open(stubs_path(lab_dir), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def line_hashes(text):
    """One short hash per line, trailing whitespace ignored.

    Ignored because editors add and remove it on their own, and a student who
    saved a file twice has not written forty lines.
    """
    return [hashlib.sha1(l.rstrip().encode("utf-8", "replace")).hexdigest()[:10]
            for l in text.splitlines()]


def fingerprint(text):
    return {"sha256": hashlib.sha256(text.encode("utf-8", "replace")).hexdigest(),
            "lines": line_hashes(text)}


def file_reports(lab_dir, stubs=None):
    """Per file, what has happened to it since it was issued.

    Reported per file rather than aggregated, because "edited csr.cpp and never
    opened processor.cpp" and "edited both a little" are different states and
    the second column of the diagnostic is which one a student is in.
    """
    stubs = load_stubs(lab_dir) if stubs is None else stubs
    out = {}
    for rel, want in sorted(stubs.items()):
        entry = {"differs_from_stub": None, "lines_added": None,
                 "lines_removed": None, "sha256": None, "present": False}
        try:
            with open(os.path.join(lab_dir, rel), encoding="utf-8",
                      errors="replace") as f:
                text = f.read()
        except OSError:
            # Absent is not unmodified: a deleted file is a thing that happened.
            out[rel] = entry
            continue

        entry["present"] = True
        got = fingerprint(text)
        entry["sha256"] = got["sha256"]
        entry["differs_from_stub"] = got["sha256"] != want.get("sha256")

        added = removed = 0
        sm = difflib.SequenceMatcher(None, want.get("lines", []), got["lines"],
                                     autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag in ("replace", "delete"):
                removed += i2 - i1
            if tag in ("replace", "insert"):
                added += j2 - j1
        entry["lines_added"]   = added
        entry["lines_removed"] = removed
        out[rel] = entry
    return out


# ------------------------------------------------------------------- the log

def log_path(lab_dir):
    return os.path.join(lab_dir, LOG_DIR, LOG_FILE)


def append(lab_dir, record):
    """One JSON object, one line, appended. Never raises.

    Appended under O_APPEND as a single write, which is what keeps two runs
    started at once from interleaving halves of a line.

    CS3160_TELEMETRY=off turns the whole thing into a no-op. tools/grade.py
    sets it: grading rebuilds a student's work in a throwaway workspace and
    runs the harness over it days after the fact, and those runs must not
    appear in the log as though the student had made them.
    """
    if os.environ.get("CS3160_TELEMETRY") == "off":
        return
    try:
        path = log_path(lab_dir)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        line = json.dumps(record, sort_keys=True) + "\n"
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644)
        try:
            os.write(fd, line.encode("utf-8", "replace"))
        finally:
            os.close(fd)
    except Exception:
        pass  # instrumentation never costs a student a test run


def base_record(lab_dir, kind):
    now     = datetime.datetime.now().astimezone()
    started = session_start(lab_dir, now)
    details = read_details(lab_dir)
    return {
        "kind":          kind,
        "student_id":    student_id(details),
        "has_details":   {k: bool(v) for k, v in details.items()},
        "machine":       details.get("machine") or None,
        "timestamp_iso": now.isoformat(timespec="seconds"),
        "elapsed_s":     int((now - started).total_seconds()),
        "session_start": started.isoformat(timespec="seconds"),
    }


def record_build(lab_dir, returncode, stderr_text):
    """One `make`, successful or not. The debugging loop, counted."""
    try:
        errors = [l for l in stderr_text.splitlines() if ERROR_LINE.search(l)]
        rec = base_record(lab_dir, "build")
        rec.update({
            "compiled":            returncode == 0,
            "exit_status":         returncode,
            "compiler_error_count": len(errors),
            "first_error_line":    (errors[0].strip()[:200] if errors else None),
        })
        append(lab_dir, rec)
    except Exception:
        pass


def record_testrun(lab_dir, **fields):
    """One `make test`: what compiled, what passed, and what was edited."""
    try:
        rec = base_record(lab_dir, "testrun")
        rec.update(fields)
        rec["files_modified"] = file_reports(lab_dir)
        append(lab_dir, rec)
    except Exception:
        pass


# --------------------------------------------------- the `make` wrapper (R3)

def wrap_build(lab_dir, cmd):
    """Run a build, record that it happened, and pass its status on.

    stderr is read a line at a time and echoed as it arrives, so the student
    sees compiler errors exactly when they would have seen them otherwise. That
    matters more than it sounds: a wrapper that buffered the errors until the
    end would change the thing being measured.
    """
    captured = []
    try:
        p = subprocess.Popen(cmd, cwd=lab_dir, stderr=subprocess.PIPE,
                             text=True, bufsize=1)
    except Exception:
        # Could not even start it under a pipe. Try it unwatched -- a wrapper
        # that cannot watch must still not stop the build -- and if that fails
        # too, hand back a status rather than a traceback. A student staring at
        # a Python stack trace where a compiler error should be is the exact
        # failure this whole file is written to avoid.
        try:
            return subprocess.call(cmd, cwd=lab_dir)
        except Exception as e:
            print(f"telemetry: could not run {' '.join(cmd)}: {e}",
                  file=sys.stderr)
            return 127

    try:
        for line in p.stderr:
            captured.append(line)
            sys.stderr.write(line)
            sys.stderr.flush()
    except Exception:
        pass
    finally:
        p.stderr.close()
    rc = p.wait()

    record_build(lab_dir, rc, "".join(captured))
    return rc


USAGE = """usage: telemetry.py build -- <command>...

Runs the command from the lab folder, records whether it compiled, and exits
with its status. The Makefile's `all` target goes through this so that a failed
build leaves a record as well as a successful one."""


def main(argv):
    if len(argv) < 2 or argv[1] != "build" or "--" not in argv:
        print(USAGE, file=sys.stderr)
        return 2
    cmd = argv[argv.index("--") + 1:]
    if not cmd:
        print(USAGE, file=sys.stderr)
        return 2
    lab_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return wrap_build(lab_dir, cmd)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
