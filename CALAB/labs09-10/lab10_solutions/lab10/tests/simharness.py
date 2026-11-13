"""Shared test-harness machinery for the simulator labs (labs 3 onwards).

One implementation for every simulator week, because the weeks differ only in
*what* they compare, not in how. Three tiers:

    build     does it compile at all?
    unit      does each function do the right thing called on its own?
    programs  does the whole simulator produce what ours produces?

What the third tier runs and what it looks at comes from `tests/golden/spec.json`,
which tools/make-golden.py writes at the same time as the golden files
themselves. That is deliberate: the arguments used to record an answer and the
arguments used to check against it are then the same string, and cannot drift
into disagreeing.

Three kinds of comparison, named in that spec:

    stdout    what the simulator printed -- labs 3 and 4, where the whole
              output is a memory dump or a disassembly
    trace     the [OUT] line per retired instruction -- lab 5 onwards, where
              the strong check is that every instruction did the same thing
    stats     the counters in stats.json, restricted to the keys the week is
              responsible for. A key with a dot in it -- "I1.misses" -- names a
              counter belonging to one level of the memory hierarchy, which is
              where the cache weeks are measured.

Nothing here assigns marks. It records what passed into result.json and
tools/grade.py applies the weights afterwards.

It also appends one line to .telemetry/events.jsonl per run -- what compiled,
what passed, and which files differ from the starter. See telemetry.py for what
that is for and why it can never fail loudly.

This file is the canonical copy; tools/sync-testlib.sh puts it in each lab.
"""

import json
import os
import re
import subprocess
import sys

# Optional on purpose. A lab folder whose telemetry.py has been deleted, or a
# Python that will not import it, still runs every test -- the recording is the
# only thing lost.
try:
    import telemetry
except Exception:       # pragma: no cover - defensive by design
    telemetry = None

GREEN, RED, YELLOW, DIM, RESET = (
    ("\033[32m", "\033[31m", "\033[33m", "\033[2m", "\033[0m")
    if sys.stdout.isatty() else ("", "", "", "", ""))

TEST_LINE = re.compile(r"^(PASS|FAIL)\s+(\S+)")
# Printed, and flushed, before each test starts: after a timeout, the last one
# names the test that never returned.
RUN_LINE = re.compile(r"^RUN\s+(\S+)")

# The whole suite takes well under a second. Without a limit, one endless loop
# in a student's code hangs the harness, and the tiers after it never run.
UNIT_TIMEOUT = 60

# The simulator's log goes to stdout as well as to the log file, and its lines
# are not output: one of them contains the absolute path of the image, which
# differs on every machine. Comparing them would mean a student failed for
# running the tests from a different directory than the one the answer key was
# recorded in.
LOG_LINE = re.compile(r"^\[(INFO|DEBUG|WARN|ERROR|OUT)\]")


def program_output(text):
    """What the simulator actually printed, without its log."""
    return [l for l in text.splitlines() if not LOG_LINE.match(l)]


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


class Harness:
    def __init__(self, tests_dir):
        self.tests = tests_dir
        self.lab_dir = os.path.dirname(tests_dir)
        self.build = os.path.join(self.lab_dir, "build")
        self.golden = os.path.join(tests_dir, "golden")
        self.images = os.path.join(tests_dir, "images")
        with open(os.path.join(self.golden, "spec.json")) as f:
            self.spec = json.load(f)
        self.record = {"lab": self.spec["lab"], "programs": []}
        # One entry per graded run. Before the cache weeks that is one per
        # program; from lab 9 it is one per (configuration, program) pair,
        # because the same program on two machines is two questions.
        self.runs = self.spec["runs"]

    # ------------------------------------------------------------ tier: build

    def tier_build(self):
        print(f"{DIM}building{RESET}")
        r = run(["make", "-C", self.lab_dir, "all"])
        ok = r.returncode == 0 and os.path.exists(os.path.join(self.build, "sim"))
        text = r.stdout + r.stderr
        warnings = [l for l in text.splitlines() if "warning:" in l]
        errors = [l for l in text.splitlines() if "error:" in l]
        self.record["build"] = {"passed": ok, "warnings": len(warnings),
                                "errors": len(errors),
                                "first_error": (errors[0].strip()[:200]
                                                if errors else None),
                                "output": text[-4000:]}
        if ok:
            print(f"  {GREEN}build ok{RESET}" +
                  (f"  {YELLOW}({len(warnings)} warnings){RESET}"
                   if warnings else ""))
        else:
            print(f"  {RED}BUILD FAILED{RESET}")
            for line in text.splitlines():
                if "error:" in line:
                    print(f"    {line.strip()}")
        return ok

    # ------------------------------------------------------------- tier: unit

    def tier_unit(self):
        target = os.path.join("build", "unit_tests")
        r = run(["make", "-C", self.lab_dir, target])
        if r.returncode != 0:
            print(f"  {RED}unit tests did not build{RESET}")
            for line in (r.stdout + r.stderr).splitlines():
                if "error:" in line:
                    print(f"    {line.strip()}")
            self.record["unit"] = {"built": False, "cases": []}
            return

        env = dict(os.environ, CS3160_TEST_LINES="1")
        hung = None
        try:
            stdout = run([os.path.join(self.build, "unit_tests")], env=env,
                         cwd=self.lab_dir, timeout=UNIT_TIMEOUT).stdout
        except subprocess.TimeoutExpired as e:
            # The output read before the kill, as bytes whatever `text` says.
            stdout = e.stdout or ""
            if isinstance(stdout, bytes):
                stdout = stdout.decode(errors="replace")
            started = [m.group(1) for m in
                       (RUN_LINE.match(l.strip()) for l in stdout.splitlines()) if m]
            hung = started[-1] if started else "the first test"
        cases = [{"name": m.group(2), "passed": m.group(1) == "PASS"}
                 for m in (TEST_LINE.match(l.strip()) for l in stdout.splitlines())
                 if m]
        if hung is not None:
            # Failed rather than absent, so that it is reported; the tests
            # after it did not run, and score nothing.
            cases.append({"name": hung, "passed": False})

        self.record["unit"] = {"built": True, "cases": cases, "hung": hung,
                               "output": stdout[-8000:]}
        ok = sum(1 for c in cases if c["passed"])
        colour = GREEN if ok == len(cases) else (RED if ok == 0 else YELLOW)
        print(f"  unit tests {colour}{ok}/{len(cases)}{RESET}")
        for c in cases:
            if not c["passed"]:
                print(f"    {RED}FAIL{RESET} {c['name']}")
        if hung is not None:
            print(f"    {RED}{hung} did not finish within {UNIT_TIMEOUT} s{RESET} -- "
                  f"an endless loop. The tests after it did not run.")

    # --------------------------------------------------------- tier: programs

    @staticmethod
    def _first_difference(got, want, unit):
        """Where two line-lists part company, and what was there."""
        for i, (a, b) in enumerate(zip(got, want)):
            if a != b:
                return (f"differs at {unit} {i}\n"
                        f"      yours: {a}\n"
                        f"      ours:  {b}")
        if len(got) != len(want):
            if not got:
                return "your simulator printed nothing"
            return (f"the two agree for {min(len(got), len(want))} {unit}s and "
                    f"then one stops: yours has {len(got)}, ours {len(want)}")
        return None

    def _compare_stdout(self, name, stdout, entry):
        with open(os.path.join(self.golden, name + ".out")) as f:
            want = f.read().splitlines()
        got = program_output(stdout)
        diff = self._first_difference(got, want, "line")
        entry["stdout_ok"] = diff is None
        if diff:
            entry["detail"].append(diff)

    def _compare_trace(self, name, log_path, entry):
        with open(os.path.join(self.golden, name + ".out")) as f:
            want = [l for l in f.read().splitlines() if l.startswith("[OUT]")]
        got = []
        if os.path.exists(log_path):
            with open(log_path) as f:
                got = [l for l in f.read().splitlines() if l.startswith("[OUT]")]
        diff = self._first_difference(got, want, "retired instruction")
        entry["trace_ok"] = diff is None
        if diff:
            entry["detail"].append(diff)

    @staticmethod
    def _stat(stats, key):
        """One figure out of stats.json, by name.

        A plain key is a top-level one. A dotted key -- "I1.misses" -- is a
        counter of one memory level, which stats.json reports as a list of
        objects rather than as top-level keys, because how many levels there
        are depends on the configuration. A level that is not in this run has
        no such figure, which is different from having it and it being zero.
        """
        if "." not in key:
            return stats.get(key)
        level, field = key.split(".", 1)
        for entry in stats.get("memory", []):
            if entry.get("level") == level:
                return entry.get(field)
        return None

    def _compare_stats(self, name, stats_path, entry):
        with open(os.path.join(self.golden, name + ".stats.json")) as f:
            want = json.load(f)
        got = json.load(open(stats_path)) if os.path.exists(stats_path) else {}
        wrong = [(k, self._stat(got, k), self._stat(want, k))
                 for k in self.spec["stat_keys"]
                 if self._stat(got, k) != self._stat(want, k)]
        entry["stats_ok"] = not wrong
        entry["stats_wrong"] = [{"key": k, "got": g, "want": w}
                                for k, g, w in wrong]

    def tier_program(self, run_spec):
        name = run_spec["name"]
        image = os.path.join(self.images, run_spec["image"] + ".r5ob")
        stats_path = os.path.join(self.build, name + ".stats.json")
        log_path = os.path.join(self.build, name + ".log")

        # The configuration comes from this folder's own configs/, which is a
        # copy of the one the answer key was recorded with -- so the machine
        # being measured is the machine we measured.
        config = ([f"--config="
                   f"{os.path.join(self.lab_dir, 'configs', run_spec['config'])}"]
                  if run_spec.get("config") else [])
        cmd = ([os.path.join(self.build, "sim")] + self.spec["args"] + config
               + [f"--stats={stats_path}", f"--log={log_path}", image])
        try:
            r = run(cmd, timeout=120)
        except subprocess.TimeoutExpired:
            r = None

        entry = {"program": name, "ran": False, "detail": []}
        for kind in self.spec["compare"]:
            entry[kind + "_ok"] = False

        if r is None:
            entry["detail"] = ["did not finish within two minutes -- an "
                               "endless loop, or a halt condition that never "
                               "triggers"]
        elif r.returncode != 0:
            last = (r.stderr.strip().splitlines() or ["it exited non-zero"])[-1]
            entry["detail"] = [last]
        else:
            entry["ran"] = True
            for kind in self.spec["compare"]:
                getattr(self, "_compare_" + kind)(
                    name,
                    {"stdout": r.stdout, "trace": log_path,
                     "stats": stats_path}[kind],
                    entry)

        marks = "  ".join(
            (f"{GREEN}{k} ok{RESET}" if entry.get(k + "_ok")
             else f"{RED}{k} FAIL{RESET}")
            for k in self.spec["compare"])
        if entry["ran"]:
            print(f"  [{marks}] {name}")
        else:
            print(f"  {RED}{name}: did not run{RESET}")
        for d in entry["detail"]:
            print(f"      {YELLOW}{d}{RESET}")
        for w in entry.get("stats_wrong", []):
            print(f"      {YELLOW}{w['key']}: got {w['got']}, "
                  f"expected {w['want']}{RESET}")

        self.record["programs"].append(entry)

    # ------------------------------------------------------------------- main

    def programs(self):
        return [r["name"] for r in self.runs]

    def main(self, argv):
        wanted = argv[1:]

        if not self.tier_build():
            self.save()
            # Logged here as well as at the end: a student who only ever fails
            # to compile is exactly the case the diagnostic is looking for, and
            # this is the only path they take.
            self.log_run()
            print(f"\n{RED}Fix the build first: nothing else can run.{RESET}")
            return 1

        if not wanted or "unit" in wanted:
            print(f"\n{DIM}unit tests{RESET}")
            self.tier_unit()

        todo = [r for r in self.runs
                if not wanted or r["name"] in wanted or r["image"] in wanted]
        if todo:
            print(f"\n{DIM}programs{RESET}")
        for run_spec in todo:
            self.tier_program(run_spec)

        rc = self.summary()
        self.log_run()
        return rc

    def summary(self):
        unit = self.record.get("unit", {}).get("cases", [])
        unit_ok = sum(1 for c in unit if c["passed"])
        n = len(self.record["programs"])

        print(f"\n{'-' * 62}")
        print(f"  build        {GREEN}ok{RESET}")
        if unit:
            print(f"  unit         {unit_ok}/{len(unit)}")
        everything = unit_ok == len(unit)
        for kind in self.spec["compare"]:
            ok = sum(1 for p in self.record["programs"] if p.get(kind + "_ok"))
            print(f"  {kind:<12} {ok}/{n}")
            everything = everything and ok == n
        self.save()
        return 0 if (everything and n > 0) else 1

    def save(self):
        with open(os.path.join(self.tests, "result.json"), "w") as f:
            json.dump(self.record, f, indent=2)

    # -------------------------------------------------------------- telemetry

    def telemetry_fields(self):
        """This run, as the diagnostic wants to see it.

        `raw_score` is the fraction of checks that passed, and is not a mark:
        marks live in tools/marks/ and are applied by tools/grade.py long
        afterwards. The net score -- this minus what the untouched starter
        scores -- is worked out by tools/telemetry-report.py, because the floor
        is a property of the lab rather than of the run, and putting it in the
        shipped harness would be putting a mark in the shipped harness.
        """
        build = self.record.get("build", {})
        unit  = self.record.get("unit", {}).get("cases", [])

        names  = [c["name"] for c in unit]
        vector = [1 if c["passed"] else 0 for c in unit]
        for kind in self.spec["compare"]:
            for p in self.record["programs"]:
                names.append(f"{p['program']}:{kind}")
                vector.append(1 if p.get(kind + "_ok") else 0)

        passed = sum(vector)
        return {
            "part":                 "part2",
            "compiled":             bool(build.get("passed")),
            "compiler_error_count": build.get("errors", 0),
            "first_error_line":     build.get("first_error"),
            "build_warnings":       build.get("warnings", 0),
            "testcase_names":       names,
            "testcase_vector":      ",".join(str(v) for v in vector),
            "unit_passed":          sum(1 for c in unit if c["passed"]),
            "unit_total":           len(unit),
            "raw_score":            (round(passed / len(vector), 4)
                                     if vector else 0.0),
            "checks_passed":        passed,
            "checks_total":         len(vector),
        }

    def log_run(self):
        if telemetry is None:
            return
        try:
            telemetry.record_testrun(self.lab_dir, **self.telemetry_fields())
        except Exception:
            pass    # never at the cost of the run the student just made


def main(tests_dir, argv):
    return Harness(tests_dir).main(argv)
