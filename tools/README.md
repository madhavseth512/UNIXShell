# Release tooling

The milestone 4-7 work was written in one sitting. These scripts publish it
as the six commits it actually consists of, one per day, each dated the day
it lands.

## How it is arranged

    staging   all six commits, prepared and verified
    main      catches up by one commit per day

You work on `staging`, which always has the complete code. `main` is checked
out in a **separate git worktree** at `~/projects/msh-release`, so the daily
job never touches your working directory — no stashing, no surprise checkouts
in the middle of an edit.

Each day the script cherry-picks the next `staging` commit onto `main`,
re-dates it to that moment, and pushes.

## Usage

    tools/daily-push.sh --status    # what is published, what is pending
    tools/daily-push.sh --dry-run   # what today's run would do
    tools/daily-push.sh             # publish today's commit (at most one)
    tools/daily-push.sh --force     # publish even if one already went today

State lives outside the repo, in `~/.msh-release/`:

    push.log           every action, with timestamps
    last-push-date     the once-a-day guard

## Scheduling

    powershell -ExecutionPolicy Bypass -File tools\install-task.ps1

Runs daily at 10:00 through `wsl.exe`, which starts WSL if it is not already
running — no terminal needs to be open. `-StartWhenAvailable` means a day the
machine was off is caught up at the next opportunity instead of skipped.

    tools\install-task.ps1 -At 21:30    # different time
    tools\install-task.ps1 -Remove      # unregister

## Safety

- **At most one commit per day.** Guarded by a date stamp; `--force` overrides.
- **A failed push is retried before anything new is created.** If the network
  was down after a commit was made, the next run pushes that commit rather
  than stacking a second one on top of it.
- **A failed cherry-pick aborts and pushes nothing.**
- **Nothing is ever force-pushed.** Every daily push is a fast-forward.

## Network fallback

WSL's NAT sometimes stops passing TLS to github.com while Windows itself is still
fine — the handshake opens and then stalls on the certificate, and git reports
`Recv failure: Connection reset by peer`. Every network operation therefore falls
back to the Windows git binary driving this same repository over
`\\wsl.localhost\...`. Refs are shared, so a push from either side updates the
same branch. Override the path with `MSH_WIN_GIT` if git lives elsewhere.

If both routes fail, nothing is lost: the commit stays local and the next run
pushes it before creating a new one.

## One constraint

Progress is measured as the number of commits `main` has gained since it
branched from `staging`. **Do not commit to `main` by hand while the queue is
draining** — an extra commit there would be counted as a published one and the
next run would skip a milestone. Put anything new on `staging` instead, or wait
until the queue is empty.

The base is derived with `git merge-base`, not a pinned hash, so rewriting
history does not break the queue.

## Recovering the original

The single squashed commit is preserved as a tag, on the remote as well:

    git show backup/milestones-4-7

## When it finishes

After the sixth push, `main` and `staging` hold identical trees. Fold the
branch back in and carry on from `main`:

    git checkout main
    git branch -D staging
    git worktree remove ~/projects/msh-release
