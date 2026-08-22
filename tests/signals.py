#!/usr/bin/env python3
"""Milestone 7 checks. These need a real controlling terminal, so msh is
driven through a pty and Ctrl-C is delivered as a literal 0x03 byte -- the
same path a human keypress takes, via the tty line discipline."""
import os, pty, select, signal, subprocess, sys, time

MSH = os.path.expanduser("~/projects/msh/msh")
passed = failed = 0

def report(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print("ok   " + name)
    else:
        failed += 1
        print("FAIL " + name + ("\n       " + detail if detail else ""))

def drain(fd, seconds):
    out = b""
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                chunk = os.read(fd, 4096)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
    return out.decode(errors="replace")

def session(steps, settle=0.4):
    """steps: list of byte strings to write, with a pause after each."""
    pid, fd = pty.fork()
    if pid == 0:
        os.execv(MSH, [MSH])
    out = drain(fd, settle)
    for s in steps:
        os.write(fd, s)
        out += drain(fd, settle)
    try:
        os.write(fd, b"exit\n")
        out += drain(fd, settle)
    except OSError:
        pass
    try:
        os.close(fd)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return out

# 1. Ctrl-C at an idle prompt: shell must survive and redraw.
out = session([b"\x03", b"echo alive\n"])
report("Ctrl-C at idle prompt does not kill the shell", "alive" in out, repr(out))
report("Ctrl-C at idle prompt redraws the prompt", out.count("msh>") >= 3, repr(out))

# 2. Ctrl-C during a foreground child: child dies, shell lives.
t0 = time.time()
out = session([b"sleep 30\n", b"\x03", b"echo alive\n"], settle=1.0)
elapsed = time.time() - t0
report("Ctrl-C interrupts a foreground child", elapsed < 10, "took %.1fs" % elapsed)
report("shell survives interrupting its child", "alive" in out, repr(out))

# 3. $? after an interrupted child is 130 (128 + SIGINT).
out = session([b"sleep 30\n", b"\x03", b"echo [$?]\n"], settle=1.0)
report("interrupted child sets $? to 130", "[130]" in out, repr(out))

# 4. Ctrl-C kills every stage of a pipeline, not just one.
t0 = time.time()
out = session([b"sleep 30 | cat\n", b"\x03", b"echo alive\n"], settle=1.0)
elapsed = time.time() - t0
report("Ctrl-C kills a whole pipeline", elapsed < 10 and "alive" in out,
       "took %.1fs %r" % (elapsed, out))

# (process-group isolation is checked structurally in pgroups.py)

# 5. Non-interactive mode prints no prompt.
proc = subprocess.run(["bash", "-c", "printf 'echo hi\\n' | %s" % MSH],
                      capture_output=True, text=True)
report("no prompt when stdin is not a tty",
       proc.stdout.strip() == "hi", repr(proc.stdout))

# 6. Interactive mode does print a prompt.
out = session([b"echo hi\n"])
report("prompt shown when stdin is a tty", "msh>" in out, repr(out))

# 7. EOF (Ctrl-D) at the prompt exits cleanly.
pid, fd = pty.fork()
if pid == 0:
    os.execv(MSH, [MSH])
drain(fd, 0.4)
os.write(fd, b"\x04")
drain(fd, 0.5)
try:
    os.close(fd)
except OSError:
    pass
_, status = os.waitpid(pid, 0)
report("Ctrl-D exits with status 0", os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0,
       "status=%d" % status)

print("\npassed: %d   failed: %d" % (passed, failed))
sys.exit(1 if failed else 0)
