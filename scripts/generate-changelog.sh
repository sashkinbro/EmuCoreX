#!/usr/bin/env bash
set -euo pipefail

CURRENT="${GITHUB_SHA:-HEAD}"
PREVIOUS_TAG=""
if git describe --tags --abbrev=0 "${CURRENT}^" >/dev/null 2>&1; then
  PREVIOUS_TAG="$(git describe --tags --abbrev=0 "${CURRENT}^")"
  RANGE="${PREVIOUS_TAG}..${CURRENT}"
else
  RANGE="$CURRENT"
fi

mapfile -t SUBJECTS < <(git log --no-merges --format='%s' "$RANGE")
if (("${#SUBJECTS[@]}" == 0)); then
  echo "## Changes"
  echo "- No non-merge commit subjects were found for this release range."
  exit 0
fi

declare -a ADDED=() REMOVED=() FIXED=() PERFORMANCE=() CHANGED=()
for subject in "${SUBJECTS[@]}"; do
  lower="$(printf '%s' "$subject" | tr '[:upper:]' '[:lower:]')"
  case "$lower" in
    feat:*|feat\(*|add\ *|add:*|added\ *|introduce\ *|introduces\ *)
      ADDED+=("$subject") ;;
    remove\ *|removed\ *|remove:*|drop\ *|drop:*|delete\ *|delete:*)
      REMOVED+=("$subject") ;;
    fix:*|fix\(*|fix\ *|bugfix:*|repair\ *)
      FIXED+=("$subject") ;;
    perf:*|perf\(*|performance:*|optim*|speed\ *|faster\ *)
      PERFORMANCE+=("$subject") ;;
    *)
      CHANGED+=("$subject") ;;
  esac
done

echo "# EmuCoreX release notes"
echo
if [[ -n "$PREVIOUS_TAG" ]]; then
  echo "Changes since **$PREVIOUS_TAG**."
else
  echo "Changes included in this build (no earlier version tag was found)."
fi
echo

print_section() {
  local heading="$1"
  shift
  local -a items=("$@")
  if (("${#items[@]}" > 0)); then
    echo "## $heading"
    for item in "${items[@]}"; do
      echo "- $item"
    done
    echo
  fi
}

print_section "Added" "${ADDED[@]}"
print_section "Removed" "${REMOVED[@]}"
print_section "Fixed" "${FIXED[@]}"
print_section "Performance" "${PERFORMANCE[@]}"
print_section "Changed" "${CHANGED[@]}"

echo "---"
echo "Built from commit: `$CURRENT`"
