#!/usr/bin/env python3
"""Prove that a foreground pipeline runs in its own process group, and that
every stage of the pipeline shares that one group."""
import os, pty, select, time

MSH = os.path.expanduser("~/projects/msh/msh")

def drain(fd, seconds):
    out = b""
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                c = os.read(fd, 4096)
            except OSError:
                break
            if not c:
                break
            out += c
    return out.decode(errors="replace")

pid, fd = pty.fork()
if pid == 0:
    os.execv(MSH, [MSH])

shell_pid = pid
drain(fd, 0.4)
os.write(fd, b"ps -o pid=,pgid=,comm= | cat\n")
out = drain(fd, 1.2)
os.write(fd, b"exit\n")
drain(fd, 0.4)
os.close(fd)
os.waitpid(pid, 0)

print("msh pid = %d (its pgid should equal this)" % shell_pid)
print(out)

rows = []
for line in out.splitlines():
    parts = line.split()
    if len(parts) == 3 and parts[0].isdigit() and parts[1].isdigit():
        rows.append((int(parts[0]), int(parts[1]), parts[2]))

stages = [r for r in rows if r[2] in ("ps", "cat")]
ok = True
if len(stages) != 2:
    print("FAIL could not see both pipeline stages: %r" % rows)
    ok = False
else:
    pgids = {r[1] for r in stages}
    if len(pgids) != 1:
        print("FAIL pipeline stages are in different groups: %r" % stages)
        ok = False
    else:
        pgid = pgids.pop()
        print("ok   both stages share pgid %d" % pgid)
        if pgid == shell_pid:
            print("FAIL pipeline is in the SHELL's group (%d)" % shell_pid)
            ok = False
        else:
            print("ok   pipeline group %d differs from shell group %d"
                  % (pgid, shell_pid))
        leader = min(r[0] for r in stages)
        if pgid == leader:
            print("ok   group leader is the first stage (pid %d)" % leader)
        else:
            print("FAIL pgid %d is not the first stage pid %d" % (pgid, leader))
            ok = False

raise SystemExit(0 if ok else 1)
