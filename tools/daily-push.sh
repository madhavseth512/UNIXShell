#!/usr/bin/env bash
#
# Publish one prepared commit per day from `staging` onto `main`.
#
# The six milestone commits are prepared and verified in advance on
# `staging`. This script replays the next one onto `main` in a dedicated
# git worktree, dates it the day it actually lands, and pushes. The
# worktree exists so that your own checkout is never touched: you keep
# working on `staging` while `main` catches up behind you.
#
#   daily-push.sh            publish today's commit, at most one
#   daily-push.sh --status   show progress, change nothing
#   daily-push.sh --force    publish even if one already went out today
#   daily-push.sh --dry-run  show what would be published, change nothing
#
set -uo pipefail

REPO="${MSH_REPO:-$HOME/projects/msh}"
WORKTREE="${MSH_RELEASE_WORKTREE:-$HOME/projects/msh-release}"
STAGING="${MSH_STAGING_BRANCH:-staging}"
STATE_DIR="${MSH_STATE_DIR:-$HOME/.msh-release}"
WIN_GIT="${MSH_WIN_GIT:-/mnt/c/Program Files/Git/cmd/git.exe}"

LOG="$STATE_DIR/push.log"
STAMP="$STATE_DIR/last-push-date"
TODAY="$(date +%F)"
MODE="${1:-}"

mkdir -p "$STATE_DIR"

log()  { printf '%s  %s\n' "$(date '+%F %T')" "$*" >>"$LOG"; printf '%s\n' "$*"; }
die()  { log "ERROR: $*"; exit 1; }
r()    { git -C "$REPO" "$@"; }
g()    { git -C "$WORKTREE" "$@"; }

# The same repository as Windows sees it, for the fallback below.
win_repo() {
    printf '\\\\wsl.localhost\\%s%s' "${WSL_DISTRO_NAME:-Ubuntu}" \
        "$(printf '%s' "$REPO" | tr '/' '\\')"
}

#
# Any git operation that needs the network.
#
# WSL's NAT occasionally stops passing TLS to github.com while the Windows
# host is still perfectly able to reach it -- the handshake opens and then
# stalls on the certificate. Rather than lose a day to that, fall back to
# the Windows git binary operating on this same repository over \\wsl.localhost.
# Refs are shared, so a push from either side updates the same branch.
#
net_git() {
    if git -C "$REPO" "$@" >>"$LOG" 2>&1; then
        return 0
    fi
    if [ -x "$WIN_GIT" ]; then
        log "WSL could not reach origin; retrying through Windows git"
        if "$WIN_GIT" -C "$(win_repo)" -c safe.directory='*' "$@" >>"$LOG" 2>&1; then
            return 0
        fi
    fi
    return 1
}

[ -d "$REPO/.git" ] || die "no git repository at $REPO"
r rev-parse --verify --quiet "$STAGING" >/dev/null \
    || die "branch '$STAGING' does not exist in $REPO"

# ---------------------------------------------------------------- setup
# main lives in its own worktree so this script never disturbs whatever
# you have checked out and half-edited in the main directory.
if [ ! -d "$WORKTREE/.git" ] && [ ! -f "$WORKTREE/.git" ]; then
    log "creating release worktree at $WORKTREE"
    r worktree add "$WORKTREE" main >>"$LOG" 2>&1 \
        || die "could not create worktree (is main checked out elsewhere?)"
fi

# ------------------------------------------------------------- progress
# The queue is everything on staging that main has not reached yet. Deriving
# the base with merge-base rather than pinning a sha means a history rewrite
# does not silently strip the queue down to nothing: cherry-picked commits
# get new hashes and are never ancestors of staging, so the merge-base stays
# put at the branch point as main advances.
BASE="${MSH_BASE:-$(r merge-base main "$STAGING" 2>/dev/null)}"
[ -n "$BASE" ] || die "cannot find a merge-base between main and $STAGING"

queue=()
while IFS= read -r sha; do queue+=("$sha"); done < <(r rev-list --reverse "$BASE..$STAGING")
total=${#queue[@]}
[ "$total" -gt 0 ] || die "no prepared commits in $BASE..$STAGING"

done_count=$(g rev-list --count "$BASE..main" 2>/dev/null) || die "cannot read main"

if [ "$MODE" = "--status" ]; then
    printf 'published %d of %d\n\n' "$done_count" "$total"
    i=0
    for sha in "${queue[@]}"; do
        if [ "$i" -lt "$done_count" ]; then mark="[done]   "; else mark="[pending]"; fi
        printf '%s %s\n' "$mark" "$(r log -1 --format='%s' "$sha")"
        i=$((i+1))
    done
    [ -f "$STAMP" ] && printf '\nlast published: %s\n' "$(cat "$STAMP")"
    exit 0
fi

# ----------------------------------------------------------- once a day
if [ "$MODE" != "--force" ] && [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$TODAY" ]; then
    log "already published today ($TODAY); nothing to do"
    exit 0
fi

net_git fetch --quiet origin || die "could not reach origin"

# A commit made yesterday whose push failed must go out before a new one
# is created, or main would silently run two commits ahead of the remote.
ahead=$(g rev-list --count origin/main..main 2>/dev/null || echo 0)
if [ "$ahead" -gt 0 ]; then
    log "main is $ahead commit(s) ahead of origin; retrying the push"
    [ "$MODE" = "--dry-run" ] && { log "(dry run) would push"; exit 0; }
    net_git push origin main || die "push failed; will retry next run"
    date +%F >"$STAMP"
    log "published: $(g log -1 --format='%h %s' main)"
    exit 0
fi

if [ "$done_count" -ge "$total" ]; then
    log "all $total commits published; nothing left to do"
    exit 0
fi

# ------------------------------------------------------------- publish
next="${queue[$done_count]}"
subject="$(r log -1 --format=%s "$next")"

if [ "$MODE" = "--dry-run" ]; then
    log "(dry run) would publish $((done_count+1))/$total: $subject"
    exit 0
fi

log "publishing $((done_count+1))/$total: $subject"

now="$(date -R)"
export GIT_COMMITTER_DATE="$now"

g cherry-pick "$next" >>"$LOG" 2>&1 || {
    g cherry-pick --abort >/dev/null 2>&1
    die "cherry-pick of ${next:0:8} failed; nothing was pushed"
}

# cherry-pick keeps the original author date; this makes the commit carry
# the day it actually landed, which is the whole point of the exercise.
g commit --amend --no-edit --date="$now" >>"$LOG" 2>&1 \
    || die "could not redate the commit"

net_git push origin main \
    || die "commit created but push failed; the next run will retry it"

date +%F >"$STAMP"
log "published: $(g log -1 --format='%h %s' main)"
remaining=$((total - done_count - 1))
if [ "$remaining" -gt 0 ]; then
    log "$remaining commit(s) remaining"
else
    log "that was the last one -- main and $STAGING now match"
fi
