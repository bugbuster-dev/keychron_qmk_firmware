#!/usr/bin/env bash
# Validate release prerequisites without creating tags or publishing assets.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_FIRMWARE_REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

VERSION="${1:-}"
FIRMWARE_REPO="${FIRMWARE_REPO:-$DEFAULT_FIRMWARE_REPO}"
QMK_TOOLS_REPO="${QMK_TOOLS_REPO:-$(dirname "$FIRMWARE_REPO")/qmk-tools}"

fail() {
    echo "ERROR: $*" >&2
    exit 1
}

require_git_repo() {
    local repo="$1"
    local label="$2"
    [[ -d "$repo" ]] || fail "$label repo not found: $repo"
    git -C "$repo" rev-parse --is-inside-work-tree >/dev/null 2>&1 || fail "$label is not a git repo: $repo"
}

require_no_tracked_changes() {
    local repo="$1"
    local label="$2"
    git -C "$repo" diff --quiet || fail "$label has uncommitted tracked changes"
    git -C "$repo" diff --cached --quiet || fail "$label has staged but uncommitted changes"
}

require_synced_upstream() {
    local repo="$1"
    local label="$2"
    local upstream status

    upstream="$(git -C "$repo" rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null)" || fail "$label branch has no upstream"
    status="$(git -C "$repo" status -sb)"
    [[ "$status" != *"[ahead"* ]] || fail "$label branch is ahead of $upstream; push commits before release"
    [[ "$status" != *"[behind"* ]] || fail "$label branch is behind $upstream; pull before release"
}

local_tag_exists() {
    local repo="$1"
    local version="$2"
    git -C "$repo" rev-parse -q --verify "refs/tags/$version" >/dev/null
}

remote_tag_exists() {
    local repo="$1"
    local version="$2"
    [[ -n "$(git -C "$repo" ls-remote --tags origin "refs/tags/$version" "refs/tags/$version^{}")" ]]
}

remote_tag_commit() {
    local repo="$1"
    local version="$2"
    local peeled direct

    peeled="$(git -C "$repo" ls-remote --tags origin "refs/tags/$version^{}" | awk 'NR == 1 {print $1}')"
    if [[ -n "$peeled" ]]; then
        echo "$peeled"
        return
    fi

    direct="$(git -C "$repo" ls-remote --tags origin "refs/tags/$version" | awk 'NR == 1 {print $1}')"
    echo "$direct"
}

require_firmware_tag_absent() {
    local version="$1"
    if local_tag_exists "$FIRMWARE_REPO" "$version" || remote_tag_exists "$FIRMWARE_REPO" "$version"; then
        fail "firmware tag already exists: $version"
    fi
}

require_qmk_tools_tag_compatible() {
    local version="$1"
    local head local_commit remote_commit

    head="$(git -C "$QMK_TOOLS_REPO" rev-parse HEAD)"

    if local_tag_exists "$QMK_TOOLS_REPO" "$version"; then
        local_commit="$(git -C "$QMK_TOOLS_REPO" rev-list -n 1 "$version")"
        [[ "$local_commit" == "$head" ]] || fail "qmk-tools tag $version exists but does not point at HEAD"
    fi

    if remote_tag_exists "$QMK_TOOLS_REPO" "$version"; then
        remote_commit="$(remote_tag_commit "$QMK_TOOLS_REPO" "$version")"
        [[ "$remote_commit" == "$head" ]] || fail "remote qmk-tools tag $version exists but does not point at HEAD"
    fi
}

[[ -n "$VERSION" ]] || fail "usage: scripts/release-preflight.sh vX.Y.Z"
[[ "$VERSION" =~ ^v[0-9]+(\.[0-9]+)+$ ]] || fail "version must look like v0.1.6: $VERSION"

command -v git >/dev/null || fail "git is required"

require_git_repo "$FIRMWARE_REPO" "firmware"
require_git_repo "$QMK_TOOLS_REPO" "qmk-tools"

require_no_tracked_changes "$FIRMWARE_REPO" "firmware"
require_no_tracked_changes "$QMK_TOOLS_REPO" "qmk-tools"

require_synced_upstream "$FIRMWARE_REPO" "firmware"
require_synced_upstream "$QMK_TOOLS_REPO" "qmk-tools"

require_firmware_tag_absent "$VERSION"
require_qmk_tools_tag_compatible "$VERSION"

echo "release preflight passed for $VERSION"
