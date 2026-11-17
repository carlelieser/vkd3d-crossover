# Shared helpers for tools/dev scripts. Source it: . "$(dirname "$0")/lib.sh"
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$ROOT/src"
PATCHES="$ROOT/patches"
OUT="$ROOT/out"
COMPONENTS=(moltenvk spirv-cross vkd3d-proton dxil-spirv dxvk)

# Neutral identity for commits made by git am (never the user's global identity).
GIT_ID=(-c user.name=vkd3d-crossover -c user.email=noreply@vkd3d-crossover.invalid)

log()  { printf '\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33mwarning:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

# lock_field <component> <url|sha>
lock_field() {
	local col; case "$2" in url) col=2 ;; sha) col=3 ;; *) die "lock_field: bad field $2" ;; esac
	awk -v c="$1" -v n="$col" '$1 == c { print $n }' "$ROOT/versions.lock" | grep . || die "$1 not in versions.lock"
}

# Where each component's tree lives. SPIRV-Cross is built inside MoltenVK; dxil-spirv inside vkd3d-proton.
component_dir() {
	case "$1" in
		moltenvk)     echo "$SRC/moltenvk" ;;
		spirv-cross)  echo "$SRC/moltenvk/External/SPIRV-Cross" ;;
		vkd3d-proton) echo "$SRC/vkd3d-proton" ;;
		dxil-spirv)   echo "$SRC/vkd3d-proton/subprojects/dxil-spirv" ;;
		dxvk)         echo "$SRC/dxvk" ;;
		*) die "unknown component: $1" ;;
	esac
}

# shallow_checkout <dir> <url> <sha>: create or reuse a checkout of exactly <sha>.
shallow_checkout() {
	local dir="$1" url="$2" sha="$3"
	if [ -d "$dir/.git" ] || [ -f "$dir/.git" ]; then
		if git -C "$dir" cat-file -e "$sha^{commit}" 2>/dev/null; then
			return 0
		fi
		git -C "$dir" fetch -q --depth 1 origin "$sha"
		return 0
	fi
	mkdir -p "$dir"
	git -C "$dir" init -q
	git -C "$dir" remote add origin "$url"
	git -C "$dir" fetch -q --depth 1 origin "$sha"
	git -C "$dir" checkout -q --detach FETCH_HEAD
}

require_clean() {
	local dir="$1"
	if [ -n "$(git -C "$dir" status --porcelain --untracked-files=no --ignore-submodules=all)" ]; then
		die "$dir has uncommitted changes (use --force to discard them)"
	fi
}
