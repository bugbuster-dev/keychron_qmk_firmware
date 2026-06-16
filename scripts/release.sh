#!/usr/bin/env bash
# Build all firmware variants and SRAM modules for a GitHub release.
#
# Usage:
#   ./scripts/release.sh              # build + package only
#   ./scripts/release.sh --tag v0.2.0 # also create tag + push
#   ./scripts/release.sh --release v0.2.0  # also create GitHub release (needs gh)
#
# Env:
#   QMK_TOOLS_REPO=/path/to/qmk-tools  # defaults to ../qmk-tools
#
# Output: .release/keychron-q3-max-<date>.tar.gz

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

QMK_TOOLS_REPO="${QMK_TOOLS_REPO:-$(dirname "$REPO_ROOT")/qmk-tools}"

VERSION=""
DO_TAG=false
DO_RELEASE=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tag)     VERSION="$2"; DO_TAG=true; shift 2 ;;
        --release) VERSION="$2"; DO_TAG=true; DO_RELEASE=true; shift 2 ;;
        *) echo "unknown flag: $1"; exit 1 ;;
    esac
done

tag_qmk_tools() {
    local local_tag remote_tag

    echo "=== Tagging qmk-tools $VERSION ==="

    local_tag="$(git -C "$QMK_TOOLS_REPO" tag --list "$VERSION")"
    remote_tag="$(git -C "$QMK_TOOLS_REPO" ls-remote --tags origin "refs/tags/$VERSION" "refs/tags/$VERSION^{}")"

    if [[ -n "$remote_tag" ]]; then
        echo "qmk-tools tag $VERSION already exists on origin"
        return
    fi

    if [[ -z "$local_tag" ]]; then
        git -C "$QMK_TOOLS_REPO" tag -a "$VERSION" -m "Release $VERSION"
    fi

    git -C "$QMK_TOOLS_REPO" push origin "$VERSION"
}

if $DO_TAG; then
    echo "=== Release preflight $VERSION ==="
    FIRMWARE_REPO="$REPO_ROOT" QMK_TOOLS_REPO="$QMK_TOOLS_REPO" bash "$REPO_ROOT/scripts/release-preflight.sh" "$VERSION"
fi

OUTDIR=".release"
mkdir -p "$OUTDIR"

echo "=== Building firmware ==="
make keychron/q3_max/ansi_encoder:keychron
make keychron/q3_max/iso_encoder:keychron

echo "=== Building SRAM modules ==="
python3 emulator/scripts/build_sram_module.py --feature sticky_combo
python3 emulator/scripts/build_sram_module.py --feature dyad
python3 emulator/scripts/build_sram_module.py --feature autotext
python3 emulator/scripts/build_sram_module.py --feature holdseq
python3 emulator/scripts/build_sram_module.py --feature vim_modal

echo "=== Packaging ==="
STAMP="$(date +%Y%m%d)"

cp .build/keychron_q3_max_ansi_encoder_keychron.bin   "$OUTDIR/"
cp .build/keychron_q3_max_iso_encoder_keychron.bin    "$OUTDIR/"
cp .build/keychron_q3_max_ansi_encoder_keychron.map   "$OUTDIR/"
cp .build/keychron_q3_max_iso_encoder_keychron.map    "$OUTDIR/"
cp .build/kbsm_sticky_combo.bin                       "$OUTDIR/"
cp .build/kbsm_dyad.bin                               "$OUTDIR/"
cp .build/kbsm_autotext.bin                           "$OUTDIR/"
cp .build/kbsm_holdseq.bin                            "$OUTDIR/"
cp .build/kbsm_vim_modal.bin                          "$OUTDIR/"

ARCHIVE="keychron-q3-max-${STAMP}.tar.gz"
tar czf "$OUTDIR/$ARCHIVE" -C "$OUTDIR" \
    keychron_q3_max_ansi_encoder_keychron.bin \
    keychron_q3_max_iso_encoder_keychron.bin \
    keychron_q3_max_ansi_encoder_keychron.map \
    keychron_q3_max_iso_encoder_keychron.map \
    kbsm_sticky_combo.bin \
    kbsm_dyad.bin \
    kbsm_autotext.bin \
    kbsm_holdseq.bin \
    kbsm_vim_modal.bin

echo "=== Done: $OUTDIR/$ARCHIVE ==="
ls -la "$OUTDIR/$ARCHIVE"

echo
echo "Contents:"
tar tzf "$OUTDIR/$ARCHIVE"

if $DO_TAG; then
    tag_qmk_tools

    echo "=== Tagging firmware $VERSION ==="
    git tag -a "$VERSION" -m "Release $VERSION"
    git push origin "$VERSION"
fi

if $DO_RELEASE; then
    REPO="$(git remote get-url origin | sed 's|.*github.com[:\/]||; s|\.git$||')"
    if command -v gh &>/dev/null; then
        echo "=== Creating GitHub release $VERSION ==="
        gh release create "$VERSION" \
            --repo "$REPO" \
            "$OUTDIR"/*.bin \
            "$OUTDIR"/*.map \
            "$OUTDIR/$ARCHIVE" \
            --title "$VERSION" \
            --notes "Firmware + kbsm SRAM module examples (dyad, autotext, holdseq, vim_modal)"
    else
        echo "gh CLI not installed. Create the release manually at:"
        echo "  https://github.com/$REPO/releases/new?tag=$VERSION"
        echo "Attach: $OUTDIR/$ARCHIVE"
    fi
fi
