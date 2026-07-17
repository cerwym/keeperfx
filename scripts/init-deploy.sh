#!/usr/bin/env bash
# init-deploy.sh
# Initializes local .deploy/ using two local-only Docker layers:
#   1) keeperfx/dk-originals:local   (legal files from user's original DK install)
#   2) keeperfx/runtime-assets:local (KeeperFX runtime assets from repo + generated pkg data)
#
# Then optionally overlays a KeeperFX release and/or alpha patch for binary assets.
#
# Usage:
#   bash scripts/init-deploy.sh --dk-path "/games/DungeonKeeper"
#   bash scripts/init-deploy.sh --use-alpha
#   bash scripts/init-deploy.sh --kfx-version 1.3.2
#   bash scripts/init-deploy.sh --refresh-runtime-layer
#
# Options:
#   --dk-path PATH           Path to original DK install. Cached in ~/.keeperfx-dev/ after first use.
#   --kfx-version VERSION    Pin a specific KFX release (e.g. 1.3.2). Default: auto-detect latest.
#   --use-alpha              Also overlay the latest alpha patch on top of the full release.
#   --skip-kfx-overlay       Skip the KFX release/alpha overlay entirely.
#   --kfx-release-path PATH  Use a locally extracted KFX release; skips auto-resolve.
#   --refresh-dk-layer       Force rebuild of the DK originals Docker image.
#   --refresh-runtime-layer  Force rebuild of the runtime assets Docker image.
#   --skip-pkg-build         Skip running make pkg-* before rebuilding the runtime image.
#   --workspace PATH         Workspace/repo root. Defaults to parent of this script's directory.

set -euo pipefail

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS="${SCRIPT_DIR%/scripts}"

DK_PATH=""
KFX_VERSION=""
KFX_RELEASE_PATH=""
USE_ALPHA=0
SKIP_KFX_OVERLAY=0
REFRESH_DK_LAYER=0
REFRESH_RUNTIME_LAYER=0
SKIP_PKG_BUILD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dk-path)            DK_PATH="$2";          shift 2 ;;
        --kfx-version)        KFX_VERSION="$2";       shift 2 ;;
        --kfx-release-path)   KFX_RELEASE_PATH="$2";  shift 2 ;;
        --use-alpha)          USE_ALPHA=1;             shift   ;;
        --skip-kfx-overlay)   SKIP_KFX_OVERLAY=1;     shift   ;;
        --refresh-dk-layer)   REFRESH_DK_LAYER=1;     shift   ;;
        --refresh-runtime-layer) REFRESH_RUNTIME_LAYER=1; shift ;;
        --skip-pkg-build)     SKIP_PKG_BUILD=1;        shift   ;;
        --workspace)          WS="$2";                shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

COMPOSE_FILE="$WS/build/docker/compose.yml"
DEPLOY_DIR="$WS/.deploy"
REQUIRED_DK_LIST="$WS/docs/files_required_from_original_dk.txt"
DK_DOCKERFILE="$WS/build/docker/dk-originals/Dockerfile"
RUNTIME_DOCKERFILE="$WS/build/docker/kfx-runtime-assets/Dockerfile"

DK_IMAGE="keeperfx/dk-originals:local"
RUNTIME_IMAGE="keeperfx/runtime-assets:local"

# Shared cache in user home — works across git worktrees
KFX_DEV_DIR="${HOME}/.keeperfx-dev"
KFX_CACHE_DIR="${KFX_DEV_DIR}/cache"
KFX_EXTRACT_DIR="${KFX_CACHE_DIR}/extracted"
DK_PATH_FILE="${KFX_DEV_DIR}/dk-install-path.txt"

# ---------------------------------------------------------------------------
# Tooling checks
# ---------------------------------------------------------------------------

check_docker() {
    if ! command -v docker &>/dev/null; then
        echo "ERROR: Docker is required but not found in PATH." >&2; exit 1
    fi
    if ! docker compose version &>/dev/null; then
        echo "ERROR: Docker Compose is required but not available." >&2; exit 1
    fi
}

check_7z() {
    if command -v 7z &>/dev/null; then
        SEVEN_ZIP="7z"
    elif command -v 7za &>/dev/null; then
        SEVEN_ZIP="7za"
    else
        echo "ERROR: 7-Zip is required but was not found. Install p7zip-full (Linux) or 7-zip (macOS: brew install p7zip)." >&2
        exit 1
    fi
}

docker_image_exists() {
    docker image inspect "$1" &>/dev/null
}

# ---------------------------------------------------------------------------
# DK required-files list
# ---------------------------------------------------------------------------

get_required_dk_files() {
    grep '^\./' "$REQUIRED_DK_LIST" | sed 's|^\./||'
}

# ---------------------------------------------------------------------------
# DK path resolution (user-home cache → repo-local fallback → interactive)
# ---------------------------------------------------------------------------

resolve_dk_path() {
    local provided="$1"

    if [[ -n "$provided" ]]; then
        printf "%s" "$provided" > "$DK_PATH_FILE"
        echo "$provided"
        return
    fi

    # 1. Shared cache in ~/.keeperfx-dev/
    if [[ -f "$DK_PATH_FILE" ]]; then
        local cached
        cached="$(tr -d '\r\n' < "$DK_PATH_FILE")"
        if [[ -n "$cached" && -d "$cached" ]]; then
            echo "Using cached DK path: $cached" >&2
            echo "$cached"
            return
        fi
    fi

    # 2. Legacy per-repo .local/ (backward compat — read-only)
    local legacy_path="$WS/.local/dk-install-path.txt"
    if [[ -f "$legacy_path" ]]; then
        local cached
        cached="$(tr -d '\r\n' < "$legacy_path")"
        if [[ -n "$cached" && -d "$cached" ]]; then
            echo "Migrating DK path from .local/ to ~/.keeperfx-dev/ ..." >&2
            printf "%s" "$cached" > "$DK_PATH_FILE"
            echo "$cached"
            return
        fi
    fi

    echo ""
}

# ---------------------------------------------------------------------------
# DK file validation
# ---------------------------------------------------------------------------

assert_dk_files_present() {
    local dk_root="$1"
    local missing=()
    while IFS= read -r rel; do
        local dir
        dir="$(dirname "$rel")"
        local leaf
        leaf="$(basename "$rel")"
        local search_dir="$dk_root/$dir"
        if [[ ! -d "$search_dir" ]]; then
            missing+=("$rel")
            continue
        fi
        if ! find "$search_dir" -maxdepth 1 -type f -iname "$leaf" | grep -q .; then
            missing+=("$rel")
        fi
    done < <(get_required_dk_files)

    if [[ "${#missing[@]}" -gt 0 ]]; then
        echo "ERROR: Dungeon Keeper path is missing required files:" >&2
        for f in "${missing[@]}"; do echo "  - $f" >&2; done
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Normalized DK Docker context
# ---------------------------------------------------------------------------

new_normalized_dk_context() {
    local dk_root="$1"
    local tmp_dir
    tmp_dir="$(mktemp -d)"

    while IFS= read -r rel; do
        local dir
        dir="$(dirname "$rel")"
        local leaf
        leaf="$(basename "$rel")"
        local search_dir="$dk_root/$dir"
        local src
        src="$(find "$search_dir" -maxdepth 1 -type f -iname "$leaf" 2>/dev/null | head -n 1 || true)"
        if [[ -n "$src" ]]; then
            local dest_dir="$tmp_dir/$(echo "$dir" | tr '[:upper:]' '[:lower:]')"
            mkdir -p "$dest_dir"
            cp -f "$src" "$dest_dir/$(echo "$leaf" | tr '[:upper:]' '[:lower:]')"
        fi
    done < <(get_required_dk_files)

    # Copy ldata/ wholesale (lowercased) for the wildcard COPY in the Dockerfile
    local ldata_src
    ldata_src="$(find "$dk_root" -maxdepth 1 -type d -iname 'ldata' 2>/dev/null | head -n 1 || true)"
    if [[ -n "$ldata_src" && -d "$ldata_src" ]]; then
        mkdir -p "$tmp_dir/ldata"
        find "$ldata_src" -maxdepth 1 -type f | while IFS= read -r f; do
            local name
            name="$(basename "$f" | tr '[:upper:]' '[:lower:]')"
            cp -f "$f" "$tmp_dir/ldata/$name"
        done
    fi

    echo "$tmp_dir"
}

# ---------------------------------------------------------------------------
# Docker layer management
# ---------------------------------------------------------------------------

ensure_dk_originals_layer() {
    if docker_image_exists "$DK_IMAGE" && [[ "$REFRESH_DK_LAYER" -eq 0 ]]; then
        echo "Using existing $DK_IMAGE"
        return
    fi

    if [[ -z "$RESOLVED_DK_PATH" ]]; then
        echo "ERROR: --dk-path is required when creating or refreshing $DK_IMAGE" >&2; exit 1
    fi

    local dk_root="$RESOLVED_DK_PATH"
    assert_dk_files_present "$dk_root"

    echo "Normalizing DK file casing into temp context..."
    local tmp_ctx
    tmp_ctx="$(new_normalized_dk_context "$dk_root")"

    echo "Building $DK_IMAGE from DK install at $dk_root"
    (
        cd "$WS"
        docker build --build-context "dk=$tmp_ctx" -f "$DK_DOCKERFILE" -t "$DK_IMAGE" .
    )
    local exit_code=$?
    rm -rf "$tmp_ctx"
    [[ $exit_code -eq 0 ]] || { echo "ERROR: Failed to build $DK_IMAGE" >&2; exit 1; }
}

build_runtime_assets_in_docker() {
    echo "Generating pkg runtime assets in build/docker/linux ..."
    docker compose -f "$COMPOSE_FILE" run --rm --remove-orphans linux bash -lc \
        "make pkg-gfx && make pkg-sfx && make pkg-languages"
}

ensure_runtime_layer() {
    if [[ "$SKIP_PKG_BUILD" -eq 0 ]]; then
        build_runtime_assets_in_docker
    fi

    echo "Building $RUNTIME_IMAGE (local runtime assets cache) ..."
    local no_cache_flag=""
    [[ "$REFRESH_RUNTIME_LAYER" -eq 1 ]] && no_cache_flag="--no-cache"
    docker build $no_cache_flag -f "$RUNTIME_DOCKERFILE" -t "$RUNTIME_IMAGE" "$WS"
}

copy_image_tree_to_host() {
    local image="$1" container_path="$2" dest="$3"
    local cid
    cid="$(docker create "$image" /)"
    docker cp "${cid}:${container_path}/." "$dest"
    docker rm "$cid" &>/dev/null || true
}

reset_deploy_directory() {
    if [[ ! -d "$DEPLOY_DIR" ]]; then
        mkdir -p "$DEPLOY_DIR"
        return
    fi
    rm -rf "${DEPLOY_DIR:?}"/*
}

# ---------------------------------------------------------------------------
# KFX version resolution via GitHub API
# ---------------------------------------------------------------------------

github_api() {
    local url="$1"
    local auth_header=""
    if [[ -n "${GITHUB_TOKEN:-}" ]]; then
        auth_header="-H \"Authorization: Bearer $GITHUB_TOKEN\""
    fi
    curl -fsSL \
        -H "Accept: application/vnd.github+json" \
        -H "User-Agent: keeperfx-init-deploy/1.0" \
        ${GITHUB_TOKEN:+-H "Authorization: Bearer $GITHUB_TOKEN"} \
        "$url"
}

assert_github_token() {
    if [[ -z "${GITHUB_TOKEN:-}" ]]; then
        echo "ERROR: A GitHub token is required to download alpha patch artifacts." >&2
        echo "Set GITHUB_TOKEN before running this script, or create one at:" >&2
        echo "  https://github.com/settings/tokens" >&2
        echo "(Only 'public_repo' read scope needed for public repo artifacts)" >&2
        exit 1
    fi
}

resolve_kfx_latest_release() {
    echo "Querying GitHub for latest KFX stable release..." >&2
    local releases
    releases="$(github_api "https://api.github.com/repos/dkfans/keeperfx/releases")"
    local tag url
    tag="$(echo "$releases" | python3 -c "
import sys, json
for r in json.load(sys.stdin):
    if not r.get('prerelease') and not r.get('draft'):
        print(r['tag_name']); break
" 2>/dev/null)"
    url="$(echo "$releases" | python3 -c "
import sys, json
for r in json.load(sys.stdin):
    if not r.get('prerelease') and not r.get('draft'):
        for a in r.get('assets', []):
            if '_complete.7z' in a['name']:
                print(a['browser_download_url']); break
        break
" 2>/dev/null)"
    local name
    name="$(basename "$url")"
    [[ -n "$tag" && -n "$url" ]] || { echo "ERROR: Could not find a stable KFX release on GitHub." >&2; exit 1; }
    echo "$tag|$url|$name"
}

resolve_kfx_specific_release() {
    local version="$1"
    local tag="$version"
    [[ "$tag" == v* ]] || tag="v$tag"
    echo "Querying GitHub for KFX release $tag ..." >&2
    local release
    release="$(github_api "https://api.github.com/repos/dkfans/keeperfx/releases/tags/$tag" 2>/dev/null)" || {
        echo "ERROR: KFX release $tag not found on GitHub." >&2; exit 1
    }
    local url name
    url="$(echo "$release" | python3 -c "
import sys, json
r = json.load(sys.stdin)
for a in r.get('assets', []):
    if '_complete.7z' in a['name']:
        print(a['browser_download_url']); break
" 2>/dev/null)"
    name="$(basename "$url")"
    [[ -n "$url" ]] || { echo "ERROR: No _complete.7z asset found in release $tag." >&2; exit 1; }
    echo "$tag|$url|$name"
}

get_kfx_alpha_builds() {
    # Returns newline-separated: BUILD_NUM|VERSION|ARTIFACT_ID|ARTIFACT_NAME|DATE|SIZE_MB
    # Signed Windows alpha patches, newest first, deduplicated by build number.
    echo "Fetching alpha build list from GitHub Actions..." >&2
    local page=1 all_artifacts=""
    while true; do
        local page_data
        page_data="$(github_api "https://api.github.com/repos/dkfans/keeperfx/actions/artifacts?per_page=100&page=$page")"
        local count
        count="$(echo "$page_data" | python3 -c "import sys,json; d=json.load(sys.stdin); print(len(d.get('artifacts',[])))" 2>/dev/null)"
        all_artifacts+="$(echo "$page_data" | python3 -c "
import sys, json, re
d = json.load(sys.stdin)
pattern = re.compile(r'^keeperfx-(\d+)_(\d+)_(\d+)_(\d+)_Alpha-patch-signed$')
for a in d.get('artifacts', []):
    if a.get('expired'): continue
    m = pattern.match(a['name'])
    if m:
        ver = '.'.join(m.groups()[:3])
        build = m.group(4)
        date = a['created_at'][:10]
        size = round(a['size_in_bytes'] / 1048576, 1)
        print(f\"{build}|{ver}|{a['id']}|{a['name']}|{date}|{size}\")
" 2>/dev/null)"$'\n'
        [[ "$count" -lt 100 ]] && break
        (( page++ ))
        [[ $page -gt 5 ]] && break  # cap at 500 artifacts
    done
    # Deduplicate by build number (keep first = newest per page ordering), sort desc
    echo "$all_artifacts" | sort -t'|' -k1 -rn | awk -F'|' '!seen[$1]++'
}

select_kfx_alpha() {
    # Prints selected entry as: BUILD_NUM|VERSION|ARTIFACT_ID|ARTIFACT_NAME|DATE|SIZE_MB
    local builds_raw
    builds_raw="$(get_kfx_alpha_builds)"
    local -a builds
    while IFS= read -r line; do
        [[ -n "$line" ]] && builds+=("$line")
    done <<< "$builds_raw"

    [[ "${#builds[@]}" -gt 0 ]] || { echo "ERROR: No alpha builds found (may have expired after 90 days)." >&2; exit 1; }

    local latest_build
    latest_build="$(echo "${builds[0]}" | cut -d'|' -f1)"

    local page_size=10
    local page=0
    local total="${#builds[@]}"
    local total_pages=$(( (total + page_size - 1) / page_size ))

    while true; do
        local start=$(( page * page_size ))
        local i=1
        echo "" >&2
        echo "KeeperFX Alpha Patches (page $(( page + 1 )) of $total_pages):" >&2
        local entries=()
        while IFS='|' read -r bnum ver _id _name date size; do
            local tag=""
            [[ "$page" -eq 0 && "$i" -eq 1 ]] && tag="  <- latest"
            printf "  %2d) Build #%s  v%s  %s  %s MB%s\n" "$i" "$bnum" "$ver" "$date" "$size" "$tag" >&2
            entries+=("$bnum|$ver|$_id|$_name|$date|$size")
            (( i++ ))
            [[ "${#entries[@]}" -ge "$page_size" ]] && break
        done < <(printf '%s\n' "${builds[@]}" | tail -n "+$(( start + 1 ))")

        local slice_count="${#entries[@]}"
        local prompt="[Enter]=latest (#${latest_build})  [1-${slice_count}]=select"
        [[ $(( page + 1 )) -lt "$total_pages" ]] && prompt+="  [n]=next"
        [[ "$page" -gt 0 ]] && prompt+="  [p]=prev"
        prompt+="  [q]=quit"

        local input
        read -r -p "$prompt: " input </dev/tty

        if [[ -z "$input" ]]; then
            echo "${builds[0]}"; return
        fi
        [[ "$input" == "q" ]] && { echo "ERROR: Alpha selection cancelled." >&2; exit 1; }
        [[ "$input" == "n" && $(( page + 1 )) -lt "$total_pages" ]] && { (( page++ )); continue; }
        [[ "$input" == "p" && "$page" -gt 0 ]] && { (( page-- )); continue; }
        if [[ "$input" =~ ^[0-9]+$ ]] && (( input >= 1 && input <= slice_count )); then
            echo "${entries[$(( input - 1 ))]}"; return
        fi
        echo "Invalid selection — try again." >&2
    done
}

# ---------------------------------------------------------------------------
# Archive download + extraction (cached in ~/.keeperfx-dev/)
# ---------------------------------------------------------------------------

get_cached_archive() {
    local url="$1" filename="$2"
    mkdir -p "$KFX_CACHE_DIR"
    local dest="$KFX_CACHE_DIR/$filename"
    if [[ -f "$dest" ]]; then
        echo "Using cached archive: $filename" >&2
    else
        echo "Downloading $filename ..." >&2
        curl -fSL -o "$dest" "$url"
        echo "Download complete: $filename" >&2
    fi
    echo "$dest"
}

get_cached_artifact() {
    local artifact_id="$1" artifact_name="$2"
    mkdir -p "$KFX_CACHE_DIR"
    local dest_zip="$KFX_CACHE_DIR/${artifact_name}.zip"
    if [[ -f "$dest_zip" ]]; then
        echo "Using cached artifact: $artifact_name" >&2
    else
        echo "Downloading alpha artifact $artifact_name ..." >&2
        curl -fSL \
            -H "Authorization: Bearer $GITHUB_TOKEN" \
            -H "Accept: application/vnd.github+json" \
            -H "User-Agent: keeperfx-init-deploy/1.0" \
            -L -o "$dest_zip" \
            "https://api.github.com/repos/dkfans/keeperfx/actions/artifacts/${artifact_id}/zip"
        echo "Download complete: $artifact_name" >&2
    fi
    echo "$dest_zip"
}

expand_kfx_archive() {
    local archive="$1" label="$2"
    mkdir -p "$KFX_EXTRACT_DIR"
    local out_dir="$KFX_EXTRACT_DIR/$label"
    if [[ -d "$out_dir" ]]; then
        echo "Using cached extraction: $label" >&2
    else
        echo "Extracting $label ..." >&2
        mkdir -p "$out_dir"
        "$SEVEN_ZIP" x "$archive" -o"$out_dir" -y >/dev/null || {
            rm -rf "$out_dir"
            echo "ERROR: Extraction failed for $archive" >&2; exit 1
        }
        echo "Extraction complete: $label" >&2
    fi
    echo "$out_dir"
}

expand_kfx_artifact_zip() {
    # Expands GitHub artifact .zip wrapper; if it contains a single .7z, also extracts that.
    local zip_path="$1" label="$2"
    mkdir -p "$KFX_EXTRACT_DIR"
    local out_dir="$KFX_EXTRACT_DIR/$label"
    if [[ -d "$out_dir" ]]; then
        echo "Using cached artifact extraction: $label" >&2
        echo "$out_dir"; return
    fi
    echo "Extracting artifact $label ..." >&2
    local zip_stage="${out_dir}_zip"
    mkdir -p "$zip_stage"
    unzip -q "$zip_path" -d "$zip_stage"
    # Check if inner .7z exists
    local inner_7z
    inner_7z="$(find "$zip_stage" -maxdepth 1 -name '*.7z' | head -n 1)"
    if [[ -n "$inner_7z" ]]; then
        mkdir -p "$out_dir"
        "$SEVEN_ZIP" x "$inner_7z" -o"$out_dir" -y >/dev/null || {
            rm -rf "$out_dir" "$zip_stage"
            echo "ERROR: Extraction of inner archive failed." >&2; exit 1
        }
        rm -rf "$zip_stage"
    else
        mv "$zip_stage" "$out_dir"
    fi
    echo "Extraction complete: $label" >&2
    echo "$out_dir"
}

# ---------------------------------------------------------------------------
# KFX release overlay
# ---------------------------------------------------------------------------

apply_kfx_overlay() {
    local source_root="$1"
    local deploy_path="$2"
    local overwrite="${3:-0}"
    local label="${4:-$source_root}"

    echo "Overlaying assets from $label ..."

    local overlay_dirs=("levels" "data" "ldata" "sound" "music" "fxdata" "campgns" "creatrs" "mods" "multiplayer")
    local skip_exts=(".txt" ".log" ".md")

    for dir in "${overlay_dirs[@]}"; do
        local src_dir="$source_root/$dir"
        [[ -d "$src_dir" ]] || continue

        while IFS= read -r -d '' file; do
            local ext="${file##*.}"
            ext=".${ext,,}"
            local skip=0
            for s in "${skip_exts[@]}"; do
                [[ "$ext" == "$s" ]] && { skip=1; break; }
            done
            [[ "$skip" -eq 1 ]] && continue

            local rel="${file#$src_dir/}"
            local dest="$deploy_path/$dir/$rel"
            local dest_dir
            dest_dir="$(dirname "$dest")"
            mkdir -p "$dest_dir"
            if [[ "$overwrite" -eq 1 || ! -f "$dest" ]]; then
                cp -f "$file" "$dest"
            fi
        done < <(find "$src_dir" -type f -print0)
    done

    # Root-level DLLs and support executables (skip keeperfx.exe -- from dev build)
    local skip_root=("keeperfx.exe" "keeperfx.map" "keeperfx.ilk" "keeperfx.pdb")
    while IFS= read -r -d '' file; do
        local fname
        fname="$(basename "$file")"
        local ext=".${fname##*.}"
        local skip=0
        for s in "${skip_exts[@]}"; do [[ "${ext,,}" == "$s" ]] && { skip=1; break; }; done
        [[ "$skip" -eq 1 ]] && continue
        for s in "${skip_root[@]}"; do [[ "${fname,,}" == "${s,,}" ]] && { skip=1; break; }; done
        [[ "$skip" -eq 1 ]] && continue
        local dest="$deploy_path/$fname"
        if [[ "$overwrite" -eq 1 || ! -f "$dest" ]]; then
            cp -f "$file" "$dest"
        fi
    done < <(find "$source_root" -maxdepth 1 -type f -print0)

    echo "Overlay complete: $label"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

check_docker
mkdir -p "$KFX_DEV_DIR"

# Resolve DK path
RESOLVED_DK_PATH="$(resolve_dk_path "$DK_PATH")"
if [[ -z "$RESOLVED_DK_PATH" ]]; then
    if docker_image_exists "$DK_IMAGE"; then
        echo "No DK path provided; using existing $DK_IMAGE"
    else
        read -r -p "Enter path to original Dungeon Keeper install: " RESOLVED_DK_PATH
        if [[ -z "$RESOLVED_DK_PATH" ]]; then
            echo "ERROR: A DK install path is required to build $DK_IMAGE for the first time." >&2; exit 1
        fi
        printf "%s" "$RESOLVED_DK_PATH" > "$DK_PATH_FILE"
    fi
fi

ensure_dk_originals_layer
ensure_runtime_layer

echo "Resetting .deploy at $DEPLOY_DIR"
reset_deploy_directory

copy_image_tree_to_host "$RUNTIME_IMAGE" "/kfx" "$DEPLOY_DIR"
copy_image_tree_to_host "$DK_IMAGE"      "/dk"  "$DEPLOY_DIR"

if [[ "$SKIP_KFX_OVERLAY" -eq 0 ]]; then
    if [[ -n "$KFX_RELEASE_PATH" ]]; then
        apply_kfx_overlay "$KFX_RELEASE_PATH" "$DEPLOY_DIR" 0 "local release at $KFX_RELEASE_PATH"
        [[ "$USE_ALPHA" -eq 1 ]] && echo "WARNING: --use-alpha is ignored when --kfx-release-path is specified."
    else
        check_7z

        # Resolve full release
        if [[ -n "$KFX_VERSION" ]]; then
            IFS='|' read -r RELEASE_TAG RELEASE_URL RELEASE_FILENAME < <(resolve_kfx_specific_release "$KFX_VERSION")
        else
            IFS='|' read -r RELEASE_TAG RELEASE_URL RELEASE_FILENAME < <(resolve_kfx_latest_release)
        fi

        echo "KFX release: $RELEASE_TAG"
        RELEASE_ARCHIVE="$(get_cached_archive "$RELEASE_URL" "$RELEASE_FILENAME")"
        RELEASE_EXTRACTED="$(expand_kfx_archive "$RELEASE_ARCHIVE" "$RELEASE_TAG")"

        # Handle single top-level subfolder in the archive
        RELEASE_ROOT="$RELEASE_EXTRACTED"
        local_children=()
        while IFS= read -r -d '' d; do
            local_children+=("$d")
        done < <(find "$RELEASE_EXTRACTED" -maxdepth 1 -mindepth 1 -type d -print0)
        if [[ "${#local_children[@]}" -eq 1 && ! -d "$RELEASE_EXTRACTED/levels" ]]; then
            RELEASE_ROOT="${local_children[0]}"
        fi

        apply_kfx_overlay "$RELEASE_ROOT" "$DEPLOY_DIR" 1 "KFX $RELEASE_TAG"

        if [[ "$USE_ALPHA" -eq 1 ]]; then
            assert_github_token
            ALPHA_ENTRY="$(select_kfx_alpha)"
            IFS='|' read -r ALPHA_BUILD ALPHA_VER ALPHA_ID ALPHA_NAME ALPHA_DATE ALPHA_SIZE <<< "$ALPHA_ENTRY"
            echo "KFX alpha:   Build #$ALPHA_BUILD v$ALPHA_VER ($ALPHA_DATE)"
            ALPHA_LABEL="alpha-build-$ALPHA_BUILD"
            ALPHA_ZIP="$(get_cached_artifact "$ALPHA_ID" "$ALPHA_NAME")"
            ALPHA_EXTRACTED="$(expand_kfx_artifact_zip "$ALPHA_ZIP" "$ALPHA_LABEL")"

            ALPHA_ROOT="$ALPHA_EXTRACTED"
            alpha_children=()
            while IFS= read -r -d '' d; do
                alpha_children+=("$d")
            done < <(find "$ALPHA_EXTRACTED" -maxdepth 1 -mindepth 1 -type d -print0)
            if [[ "${#alpha_children[@]}" -eq 1 && ! -d "$ALPHA_EXTRACTED/levels" ]]; then
                ALPHA_ROOT="${alpha_children[0]}"
            fi

            apply_kfx_overlay "$ALPHA_ROOT" "$DEPLOY_DIR" 1 "KFX alpha build #$ALPHA_BUILD"
        fi
    fi
fi

echo ""
echo "Initialized .deploy from:"
echo "  $RUNTIME_IMAGE"
echo "  $DK_IMAGE"
if [[ "$SKIP_KFX_OVERLAY" -eq 0 ]]; then
    if [[ -n "$KFX_RELEASE_PATH" ]]; then
        echo "  KFX release overlay (local): $KFX_RELEASE_PATH"
    elif [[ -n "${RELEASE_TAG:-}" ]]; then
        echo "  KFX release overlay: $RELEASE_TAG"
        [[ "$USE_ALPHA" -eq 1 && -n "${ALPHA_BUILD:-}" ]] && echo "  KFX alpha overlay:   Build #$ALPHA_BUILD v$ALPHA_VER"
    fi
fi
