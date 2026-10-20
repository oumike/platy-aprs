#!/bin/bash
# Cuts a platy-aprs release, or just builds the release images into dist/.
# Modelled on camillia-mt's scripts/release.sh, minus what platy-aprs has no
# use for (OTA signing, PR mode, AI-written notes, the P4 toolchain).
#
# Usage: ./scripts/release.sh [-y|--yes] [--alpha] [--version V] [--no-clean]
#                             [--build-local]
#        ./scripts/release.sh --dist-only [--only ENVS] [--version V] [--no-clean]
#        ./scripts/release.sh --notes-only
#        ./scripts/release.sh --check-targets
#
# Run with --help for what each one does.
#
# Bash 3.2 (macOS's stock bash): no associative arrays, no mapfile.
set -euo pipefail

RELEASE_ENVS=(
    m9
    wio-tracker-l2
    tdeck-plus
    mesh-deck
)

# Every release image is merged with this esptool. esptool 5 renamed merge_bin
# and its flags, so an unpinned upgrade could quietly change a factory image's
# layout. The release workflow pins the same version.
ESPTOOL_VERSION="4.11.0"

cd "$(git rev-parse --show-toplevel)"

env_label() {
    case "$1" in
        m9)             echo "Elecrow ThinkNode M9" ;;
        wio-tracker-l2) echo "Seeed Wio Tracker L2" ;;
        tdeck-plus)     echo "LilyGo T-Deck Plus" ;;
        mesh-deck)      echo "Attaky Mesh Deck" ;;
        *)              echo "$1" ;;
    esac
}

# Every board here is an ESP32-S3 with 16 MB of flash. Kept as functions so a
# board that differs only has to add a case.
env_out_name()   { echo "$1"; }
env_chip()       { echo "esp32s3"; }
env_flash_size() { echo "16MB"; }
# DIO in the image header boots on every QIO-capable part, and is what the
# camillia-mt images for the same boards ship with.
env_flash_mode() { echo "dio"; }

has_env() { grep -q "^\[env:$1\]" platformio.ini; }

validate_release_targets() {
    local env_name out_name seen="|" failed=false
    echo "Release target contract:"
    for env_name in "${RELEASE_ENVS[@]}"; do
        if ! has_env "$env_name"; then
            echo "  ERROR: release environment '$env_name' is missing from platformio.ini" >&2
            failed=true
            continue
        fi
        out_name="$(env_out_name "$env_name")"
        case "$seen" in
            *"|$out_name|"*)
                echo "  ERROR: duplicate release artifact slug '$out_name'" >&2
                failed=true
                ;;
            *) seen="$seen$out_name|" ;;
        esac
        if ! sed -n "/^\[env:$env_name\]/,/^\[/p" platformio.ini | grep -q 'set_app_version.py'; then
            echo "  ERROR: [env:$env_name] does not run tools/set_app_version.py" >&2
            failed=true
        fi
        printf '  %-16s -> platy-aprs-%s-<tag>.bin\n' "$env_name" "$out_name"
    done
    # Every firmware env should be released; a new board that is not is a slip.
    local e
    for e in $(sed -n 's/^\[env:\(.*\)\]$/\1/p' platformio.ini | grep -v '^native$'); do
        case " ${RELEASE_ENVS[*]} " in
            *" $e "*) ;;
            *) echo "  ERROR: [env:$e] is not in RELEASE_ENVS (scripts/release.sh)" >&2; failed=true ;;
        esac
    done
    [[ "$failed" == false ]]
}

find_pio() {
    if command -v pio >/dev/null 2>&1; then command -v pio
    elif [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then echo "$HOME/.platformio/penv/bin/pio"
    else return 1; fi
}

# Points ESPTOOL at esptool $ESPTOOL_VERSION: the one on PATH when it is that
# version (CI pins it), else a venv of its own, installed on first use.
setup_esptool() {
    if python3 -c "import esptool,sys; sys.exit(esptool.__version__ != '$ESPTOOL_VERSION')" 2>/dev/null; then
        ESPTOOL=( python3 -m esptool )
        return 0
    fi
    local venv="${XDG_CACHE_HOME:-$HOME/.cache}/platy-aprs/esptool-$ESPTOOL_VERSION"
    local have=""
    if [[ -x "$venv/bin/python" ]]; then
        have="$("$venv/bin/python" -c 'import esptool; print(esptool.__version__)' 2>/dev/null || true)"
    fi
    if [[ "$have" != "$ESPTOOL_VERSION" ]]; then
        echo "Installing esptool $ESPTOOL_VERSION into $venv"
        rm -rf "$venv"
        mkdir -p "$(dirname "$venv")"
        if ! python3 -m venv "$venv" \
           || ! "$venv/bin/python" -m pip install --quiet --disable-pip-version-check \
                "esptool==$ESPTOOL_VERSION"; then
            rm -rf "$venv"
            echo "Error: could not install esptool $ESPTOOL_VERSION (a one-time download from PyPI)." >&2
            exit 1
        fi
    fi
    ESPTOOL=( "$venv/bin/python" -m esptool )
}

# ── Build, merge and verify ──────────────────────────────────────────────────
# Shared by the release and --dist-only, so a dist/ built without releasing
# comes out exactly as a release's would.

build_release_envs() {
    local env_name boot_app0
    # The clean is so a release image can never contain a stale object file.
    # --no-clean trades that away for speed; fine on a fresh checkout (CI) or
    # right after building the same sources.
    if [[ "$NO_CLEAN" == true ]]; then
        echo "Skipping full clean (--no-clean)."
    else
        for env_name in "$@"; do
            "$PIO" run -e "$env_name" -t fullclean >/dev/null
        done
    fi
    # --disable-auto-clean: PlatformIO otherwise wipes every env's build output
    # when the project checksum moves between two `pio run`s, and the merge
    # below would find nothing to merge.
    for env_name in "$@"; do
        echo "==> $(env_label "$env_name") [$env_name]"
        "$PIO" run -e "$env_name" --disable-auto-clean
        boot_app0="$(find "${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-arduinoespressif32/tools/partitions" \
            -name boot_app0.bin 2>/dev/null | head -1)"
        if [[ -z "$boot_app0" ]]; then
            echo "Error: boot_app0.bin missing after building $env_name." >&2
            exit 1
        fi
        cp "$boot_app0" ".pio/build/$env_name/boot_app0.bin"
    done
}

# dist/platy-aprs-<board>-<tag>.bin      factory image, flash at 0x0
# dist/platy-aprs-<board>-<tag>-ota.bin  app only, flash at 0x10000 (keeps settings)
# dist/platy-aprs-<board>-<tag>.elf      symbols for crash decoding (not published)
merge_assets() {
    local tag="$1" env_name d out
    shift
    for env_name in "$@"; do
        d=".pio/build/$env_name"
        out="dist/platy-aprs-$(env_out_name "$env_name")-$tag"
        echo "  $env_name -> $out.bin"
        "${ESPTOOL[@]}" --chip "$(env_chip "$env_name")" merge_bin -o "$out.bin" \
            -fm "$(env_flash_mode "$env_name")" -ff 80m -fs "$(env_flash_size "$env_name")" \
            0x0     "$d/bootloader.bin" \
            0x8000  "$d/partitions.bin" \
            0xe000  "$d/boot_app0.bin" \
            0x10000 "$d/firmware.bin" >/dev/null
        cp "$d/firmware.bin" "$out-ota.bin"
        cp "$d/firmware.elf" "$out.elf"
    done
    install -m 0755 scripts/flash.sh dist/flash.sh
}

verify_assets() {
    local tag="$1" env_name out asset
    shift
    echo "Verifying release assets..."
    for env_name in "$@"; do
        out="dist/platy-aprs-$(env_out_name "$env_name")-$tag"
        for asset in "$out.bin" "$out-ota.bin"; do
            if [[ ! -s "$asset" ]]; then
                echo "Error: missing or empty release asset: $asset" >&2
                return 1
            fi
        done
        if ! cmp -s ".pio/build/$env_name/firmware.bin" "$out-ota.bin"; then
            echo "Error: $out-ota.bin does not match $env_name/firmware.bin" >&2
            return 1
        fi
        echo "  OK $env_name: $(basename "$out.bin"), $(basename "$out-ota.bin")"
    done
    [[ -x dist/flash.sh ]] || { echo "Error: dist/flash.sh missing" >&2; return 1; }
}

# BUILD-INFO.txt says which build someone was given; SHA256SUMS is for
# wherever the images end up.
write_build_info() {
    local version="$1" dirty="$2"
    shift 2
    {
        echo "platy-aprs $version"
        echo "Built:   $(date -u '+%Y-%m-%d %H:%M UTC')"
        echo "Commit:  $(git rev-parse HEAD) ($(git rev-parse --abbrev-ref HEAD))"
        echo "Boards:  $*"
        if [[ -n "$dirty" ]]; then
            echo ""
            echo "Built with uncommitted changes:"
            printf '%s\n' "$dirty" | sed 's/^/  /'
        fi
    } > dist/BUILD-INFO.txt
    if command -v shasum >/dev/null 2>&1; then
        (cd dist && shasum -a 256 -- *.bin) > dist/SHA256SUMS
    else
        (cd dist && sha256sum -- *.bin) > dist/SHA256SUMS
    fi
}

# ── Flags ────────────────────────────────────────────────────────────────────
ASSUME_YES=false
ALPHA=false
VERSION_ARG=""
NO_CLEAN=false
BUILD_LOCAL=false
DIST_ONLY=false
ONLY_ARG=""
NOTES_ONLY=false
CHECK_TARGETS_ONLY=false

usage() {
    cat <<'EOF'
Usage: ./scripts/release.sh [-y|--yes] [--alpha] [--version V] [--no-clean] [--build-local]
       ./scripts/release.sh --dist-only [--only ENVS] [--version V] [--no-clean]
       ./scripts/release.sh --notes-only
       ./scripts/release.sh --check-targets

  With no mode flag this releases main as committed: it checks the tree is
  clean and in sync with origin, picks the version, and dispatches the Release
  workflow, which builds every board, commits VERSION, tags and publishes.
  Nothing is compiled on this machine.

  --build-local  Build, commit, tag and publish from this machine instead
                 (needs PlatformIO and an authenticated gh). The workflow's
                 own run works this way.
  --dist-only    Build the release images into dist/ and release nothing: no
                 commit, tag, push or GitHub release. Builds the working tree
                 as it stands, uncommitted changes included. The images carry
                 VERSION on the tagged release commit, else VERSION+<commit>
                 (plus .custom with uncommitted changes); --version names them
                 instead (e.g. 0.2.0+test). File names drop the +metadata.
  --only ENVS    With --dist-only: just these boards, comma-separated
                 (e.g. --only m9,mesh-deck).
  --notes-only   Draft RELEASE_NOTES.md from the commits since the last tag
                 and open it in $EDITOR. Commit it before releasing; a release
                 publishes it verbatim (or GitHub's generated notes if the file
                 is missing or empty).
  --check-targets
                 Validate the release environments, then exit.

  --version V    Release exactly this version (a leading 'v' is fine).
  -y, --yes      Bump the patch of the latest tag without prompting
                 (v0.1.3 -> v0.1.4; VERSION when there is no tag yet).
  --alpha        Tag v<version>-alpha.<n> and publish it as a GitHub
                 prerelease. With -y the alpha counter is bumped; from a
                 stable tag it starts a new series on the next patch.
  --no-clean     Skip the full clean and build on the existing .pio output.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        -y|--yes) ASSUME_YES=true ;;
        --alpha) ALPHA=true ;;
        --no-clean) NO_CLEAN=true ;;
        --build-local) BUILD_LOCAL=true ;;
        --dist-only) DIST_ONLY=true ;;
        --notes-only) NOTES_ONLY=true ;;
        --check-targets) CHECK_TARGETS_ONLY=true ;;
        --only)
            [[ -n "${2:-}" ]] || { echo "--only requires environments (e.g. --only m9,mesh-deck)" >&2; exit 1; }
            ONLY_ARG="${ONLY_ARG:+$ONLY_ARG,}$2"
            shift
            ;;
        --only=*) ONLY_ARG="${ONLY_ARG:+$ONLY_ARG,}${1#*=}" ;;
        --version)
            [[ -n "${2:-}" ]] || { echo "--version requires a value (e.g. --version 0.2.0)" >&2; exit 1; }
            VERSION_ARG="$2"
            shift
            ;;
        --version=*) VERSION_ARG="${1#*=}" ;;
        *) echo "Unknown argument: $1 (see --help)" >&2; exit 1 ;;
    esac
    shift
done

if [[ -n "$ONLY_ARG" && "$DIST_ONLY" != true ]]; then
    echo "--only applies only to --dist-only." >&2
    exit 1
fi

# ── --check-targets ──────────────────────────────────────────────────────────
if [[ "$CHECK_TARGETS_ONLY" == true ]]; then
    validate_release_targets
    exit $?
fi

validate_release_targets >/dev/null || { validate_release_targets; exit 1; }

# ── --notes-only ─────────────────────────────────────────────────────────────
if [[ "$NOTES_ONLY" == true ]]; then
    prev="$(git describe --tags --abbrev=0 2>/dev/null || true)"
    range="${prev:+$prev..}HEAD"
    {
        echo "### Changes"
        git log --no-merges --format='- %s' "$range" | grep -v '\[skip ci\]' || true
    } > RELEASE_NOTES.md
    "${EDITOR:-vi}" RELEASE_NOTES.md
    echo "RELEASE_NOTES.md written (commits ${prev:-since the start}). Commit it, then release."
    exit 0
fi

# ── --dist-only ──────────────────────────────────────────────────────────────
if [[ "$DIST_ONLY" == true ]]; then
    if [[ "$ASSUME_YES" == true || "$ALPHA" == true || "$BUILD_LOCAL" == true ]]; then
        echo "--dist-only takes only --only, --version and --no-clean." >&2
        exit 1
    fi
    DIST_ENVS=()
    if [[ -z "$ONLY_ARG" ]]; then
        DIST_ENVS=( "${RELEASE_ENVS[@]}" )
    else
        IFS=',' read -r -a requested <<< "$ONLY_ARG"
        for want in "${requested[@]}"; do
            case " ${RELEASE_ENVS[*]} " in
                *" $want "*) ;;
                *) echo "--only: '$want' is not a release environment. Choose from: ${RELEASE_ENVS[*]}" >&2; exit 1 ;;
            esac
        done
        for env_name in "${RELEASE_ENVS[@]}"; do
            for want in "${requested[@]}"; do
                [[ "$want" == "$env_name" ]] && { DIST_ENVS+=( "$env_name" ); break; }
            done
        done
    fi

    # Read before VERSION is touched, so the run's own edit never counts.
    dirty="$(git status --porcelain)"
    if [[ -n "$VERSION_ARG" ]]; then
        DIST_TAG="v${VERSION_ARG#v}"
    else
        base="$(tr -d '[:space:]' < VERSION)"
        base="v${base#v}"
        base="${base%%+*}"
        tagged="$(git rev-parse -q --verify "refs/tags/$base^{commit}" 2>/dev/null || true)"
        if [[ -n "$dirty" ]]; then DIST_TAG="$base+$(git rev-parse --short=7 HEAD).custom"
        elif [[ "$tagged" == "$(git rev-parse HEAD)" ]]; then DIST_TAG="$base"
        else DIST_TAG="$base+$(git rev-parse --short=7 HEAD)"; fi
    fi
    if [[ ! "$DIST_TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?(\+[0-9A-Za-z.]+)?$ ]]; then
        echo "Error: '$DIST_TAG' is not a version to build (X.Y.Z[-pre][+build])." >&2
        exit 1
    fi
    FILE_TAG="${DIST_TAG%%+*}"

    PIO="$(find_pio)" || { echo "Error: platformio not found. Run: pip install platformio" >&2; exit 1; }
    setup_esptool

    echo ""
    echo "Dist-only build: nothing is committed, tagged or published."
    echo "  Version:  $DIST_TAG"
    echo "  Files:    platy-aprs-<board>-$FILE_TAG.bin"
    echo "  Commit:   $(git rev-parse --short HEAD) ($(git rev-parse --abbrev-ref HEAD))"
    [[ -n "$dirty" ]] && { echo "  Uncommitted changes, built in:"; printf '%s\n' "$dirty" | sed 's/^/    /'; }
    echo "  Boards:   ${DIST_ENVS[*]}"
    echo "  Output:   dist/, replaced"
    echo ""

    # VERSION is baked into the firmware, so it is rewritten for the build and
    # put back however the run ends.
    DIST_TMP="$(mktemp -d "${TMPDIR:-/tmp}/platy-dist.XXXXXX")"
    cp -p VERSION "$DIST_TMP/VERSION"
    trap 'cp -p "$DIST_TMP/VERSION" VERSION; rm -rf "$DIST_TMP"' EXIT
    echo "$DIST_TAG" > VERSION

    build_release_envs "${DIST_ENVS[@]}"
    echo ""
    echo "Merging factory images..."
    rm -rf dist
    mkdir -p dist
    merge_assets "$FILE_TAG" "${DIST_ENVS[@]}"
    verify_assets "$FILE_TAG" "${DIST_ENVS[@]}"
    write_build_info "$DIST_TAG" "$dirty" "${DIST_ENVS[@]}"
    echo ""
    ls -lh dist/
    echo ""
    echo "dist/ holds $DIST_TAG for ${#DIST_ENVS[@]} board(s). Nothing was committed, tagged or published."
    exit 0
fi

# ── Release ──────────────────────────────────────────────────────────────────
# Without --build-local this machine only checks and dispatches; the workflow
# (GITHUB_ACTIONS) is the run that builds.
REMOTE=true
if [[ "$BUILD_LOCAL" == true || "${GITHUB_ACTIONS:-}" == "true" ]]; then
    REMOTE=false
fi

command -v gh >/dev/null 2>&1 || { echo "Error: the GitHub CLI (gh) is required to release." >&2; exit 1; }

if [[ "${GITHUB_ACTIONS:-}" != "true" ]]; then
    # A release ships only committed work.
    dirty="$(git status --porcelain)"
    if [[ -n "$dirty" ]]; then
        echo "The working tree has uncommitted changes:" >&2
        printf '%s\n' "$dirty" | sed 's/^/  /' >&2
        echo "A release ships only committed work. Commit or stash it and rerun." >&2
        exit 1
    fi
    BRANCH="$(git rev-parse --abbrev-ref HEAD)"
    git fetch -q --tags origin
    if [[ "$(git rev-parse HEAD)" != "$(git rev-parse "origin/$BRANCH" 2>/dev/null || true)" ]]; then
        echo "$BRANCH is not in sync with origin/$BRANCH. Push or pull first." >&2
        exit 1
    fi
fi

# Newest well-formed tag matching a glob, in version order (sort -V puts
# -alpha.10 after -alpha.9).
latest_tag_matching() { git tag --list "$1" | grep -E "$2" | sort -V | tail -1 || true; }
version_ge() { [[ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" == "$1" ]]; }

PREV_STABLE="$(latest_tag_matching 'v*' '^v[0-9]+\.[0-9]+\.[0-9]+$')"
PREV_ALPHA="$(latest_tag_matching 'v*-alpha.*' '^v[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+$')"
CURRENT="$(tr -d '[:space:]' < VERSION)"
echo "VERSION:           $CURRENT"
echo "Latest stable tag: ${PREV_STABLE:-none}"
[[ "$ALPHA" == true ]] && echo "Latest alpha tag:  ${PREV_ALPHA:-none}"

NEXT_PATCH=""
if [[ "$PREV_STABLE" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    NEXT_PATCH="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.$(( BASH_REMATCH[3] + 1 ))"
fi

if [[ -n "$VERSION_ARG" ]]; then
    VERSION="${VERSION_ARG#v}"
    if [[ "$ALPHA" == true && "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then VERSION="$VERSION-alpha.1"; fi
elif [[ "$ALPHA" == true ]]; then
    if [[ "$ASSUME_YES" == true ]]; then
        # Continue the series only while it is still ahead of stable; an alpha
        # behind the latest release would be pointless.
        if [[ "$PREV_ALPHA" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)-alpha\.([0-9]+)$ ]] \
           && { [[ -z "$NEXT_PATCH" ]] || version_ge "${BASH_REMATCH[1]}" "$NEXT_PATCH"; }; then
            VERSION="${BASH_REMATCH[1]}-alpha.$(( BASH_REMATCH[2] + 1 ))"
        elif [[ -n "$NEXT_PATCH" ]]; then
            VERSION="$NEXT_PATCH-alpha.1"
        else
            base="${CURRENT#v}"
            VERSION="${base%%[-+]*}-alpha.1"
        fi
    else
        read -rp "New alpha version (e.g. 0.2.0-alpha.1, or 0.2.0 for -alpha.1): " VERSION
        if [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then VERSION="$VERSION-alpha.1"; fi
    fi
elif [[ "$ASSUME_YES" == true ]]; then
    if [[ -n "$NEXT_PATCH" ]]; then
        VERSION="$NEXT_PATCH"
    else
        base="${CURRENT#v}"
        VERSION="${base%%[-+]*}"   # first release: VERSION as it reads
    fi
else
    read -rp "New version (e.g. 0.2.0): " VERSION
fi
VERSION="${VERSION#v}"
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]]; then
    echo "Version must look like 0.2.0 or 0.2.0-alpha.1 (got '$VERSION')." >&2
    exit 1
fi
if [[ "$ALPHA" == true && ! "$VERSION" =~ -alpha\.[0-9]+$ ]]; then
    echo "An alpha version must end in -alpha.N (got '$VERSION')." >&2
    exit 1
fi
TAG="v$VERSION"
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    echo "Tag $TAG already exists." >&2
    exit 1
fi
echo "Releasing $TAG"

if [[ "$REMOTE" == true ]]; then
    args=( -f channel="$([[ "$ALPHA" == true ]] && echo alpha || echo stable)" -f version="$VERSION" )
    gh workflow run release.yml --ref "$BRANCH" "${args[@]}"
    echo "Release workflow dispatched for $TAG. Follow it with: gh run watch"
    exit 0
fi

PIO="$(find_pio)" || { echo "Error: platformio not found." >&2; exit 1; }
setup_esptool

echo "$TAG" > VERSION
build_release_envs "${RELEASE_ENVS[@]}"

echo ""
echo "Merging factory images..."
rm -rf dist
mkdir -p dist
merge_assets "$TAG" "${RELEASE_ENVS[@]}"
verify_assets "$TAG" "${RELEASE_ENVS[@]}"
write_build_info "$TAG" "" "${RELEASE_ENVS[@]}"

# [skip ci]: this tree was just built, so build.yml building it again on the
# push would tell us nothing.
git add VERSION
if git diff --cached --quiet; then
    echo "VERSION already reads $TAG."
else
    git commit -q -m "Release $TAG [skip ci]"
fi
git push -q origin HEAD
git tag "$TAG"
git push -q origin "$TAG"
echo "Tag $TAG pushed."

NOTES_ARGS=( --generate-notes )
if [[ -s RELEASE_NOTES.md ]]; then NOTES_ARGS=( --notes-file RELEASE_NOTES.md ); fi
RELEASE_FLAGS=()
[[ "$ALPHA" == true ]] && RELEASE_FLAGS+=( --prerelease )

# The .elf files stay out of the release; the workflow archives them.
gh release create "$TAG" --title "platy-aprs $TAG" \
    ${RELEASE_FLAGS[@]+"${RELEASE_FLAGS[@]}"} "${NOTES_ARGS[@]}" \
    dist/*.bin dist/SHA256SUMS dist/flash.sh
echo "Release $TAG published."
gh release view "$TAG" --json url -q .url
