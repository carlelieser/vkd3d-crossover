#!/usr/bin/env bash
# Put every component on a branch "vkd3d-crossover" = pinned commit + patch series.
#
#   tools/dev/apply-patches.sh [--dev] [--force]
#
#   --dev    also apply patches/dev/* (diagnostics and experiments) after the shipping series
#   --force  discard uncommitted changes in src/ checkouts
#
# Writes src/.variant ("release" or "dev") for build.sh.
. "$(dirname "$0")/lib.sh"

dev=0 force=0
for a in "$@"; do
	case "$a" in
		--dev) dev=1 ;;
		--force) force=1 ;;
		-h|--help) sed -n '2,9p' "$0"; exit 0 ;;
		*) die "unknown option: $a" ;;
	esac
done

for c in "${COMPONENTS[@]}"; do
	dir="$(component_dir "$c")"
	sha="$(lock_field "$c" sha)"
	[ -e "$dir/.git" ] || die "$dir missing; run tools/dev/fetch.sh first"
	git -C "$dir" am --abort >/dev/null 2>&1 || true
	if [ "$force" = 1 ]; then git -C "$dir" reset -q --hard; else require_clean "$dir"; fi
	git -C "$dir" checkout -q -B vkd3d-crossover "$sha"

	series=("$PATCHES/$c"/*.patch)
	[ "$dev" = 1 ] && [ -d "$PATCHES/dev/$c" ] && series+=("$PATCHES/dev/$c"/*.patch)
	n=0
	for p in "${series[@]}"; do
		[ -f "$p" ] || continue
		git "${GIT_ID[@]}" -C "$dir" am -q --committer-date-is-author-date "$p" \
			|| die "$c: $(basename "$p") failed to apply (left in 'git am' state in $dir)"
		n=$((n + 1))
	done
	log "$c: $n patches on ${sha:0:12}"
done

if [ "$dev" = 1 ]; then echo dev > "$SRC/.variant"; else echo release > "$SRC/.variant"; fi
log "Applied ($(cat "$SRC/.variant")). Next: tools/dev/build.sh"
