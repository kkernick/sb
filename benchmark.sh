#!/bin/sh

ROOT=$(pwd)
subdir="${1}"
RECIPE="${2}"
EXAMPLE="${3}"

cd "$ROOT/$subdir"

# Build
if [[ "${RECIPE}" != "none" && "${RECIPE}" != "sys" ]]; then
  make $RECIPE
fi

# Export so our built version is used.

if [[ "${RECIPE}" != "sys" ]]; then
  export PATH="$(pwd):$(pwd)/examples:$PATH"
fi
echo "Using: $(which sb)"

# Run the examples
cd examples

if [[ -n $EXAMPLE ]]; then
  EXAMPLES="${EXAMPLE}"
else
  EXAMPLES=$(ls)
fi

for PROFILE in $EXAMPLES; do

  # Cooldown between profiles
  sleep 1

  # Populate caches.
  if [[ "${subdir}" == "cpp" ]]; then
    $PROFILE --dry --update all
  else
    $PROFILE --dry --update-libraries --update-cache
  fi

  sleep 1

  program=$(sed '2q;d' "$ROOT/$subdir/examples/$PROFILE" | awk '{print $2}')


  echo "======================= $PROFILE ======================="

  # Cold Boot--there is no cache.
  hyperfine --command-name "Cold $PROFILE" --time-unit millisecond --prepare "sb-clean || true" "$PROFILE --dry"

  sleep 1

  # Hot Boot--like after sb.service has run.
  hyperfine --command-name "Hot $PROFILE" --time-unit millisecond --shell=none --prepare "$PROFILE --dry" "$PROFILE --dry"

  sleep 1

  # Hot Boot, but we need to refresh libraries like after an update.
  if [[ "${subdir}" == "cpp" ]]; then
    hyperfine --command-name "Lib $PROFILE" --time-unit millisecond --shell=none --prepare "$PROFILE --dry --update libraries" "$PROFILE --dry --update libraries"
  else
    hyperfine --command-name "Lib $PROFILE" --time-unit millisecond --shell=none --prepare "$PROFILE --dry --update libraries" "$PROFILE --dry --update-libraries"
  fi

  sleep 1

  # Hot Boot, but we need to refresh libraries like after an update.
  if [[ "${subdir}" == "cpp" ]]; then
    hyperfine --command-name "Cache $PROFILE" --time-unit millisecond --shell=none --prepare "$PROFILE --dry --update all" "$PROFILE --dry --update all"
  else
    hyperfine --command-name "Cache $PROFILE" --time-unit millisecond --shell=none --prepare "$PROFILE --dry --update all" "$PROFILE --dry --update-libraries --update-cache"
  fi
done
