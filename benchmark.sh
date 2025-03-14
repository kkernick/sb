#!/bin/sh

ROOT=$(pwd)
subdir="${1}"
COMMIT="${2}"
EXAMPLE="${3}"
NO_COMPILE="${4}"

cd "$ROOT/$subdir"

if [[ "${COMMIT}" != "main" ]]; then
	git stash push --quiet
	git checkout $COMMIT --quiet
	echo "Benchmarking version at ${COMMIT}"
fi

# Build
if [[ "${NO_COMPILE}" != "no" ]]; then
  make
fi

echo "Copying existing configuration and files so they are not used in the benchmark. If the benchmark stops prematurely, be sure to restore sb-old in ~/.config and ~/.local/share"

mv ~/.config/sb ~/.config/sb-bak
mv ~/.local/share/sb ~/.local/share/sb-bak

# Export so our built version is used.
export PATH="$(pwd):$(pwd)/examples:$PATH"
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

  ARGS="--time-unit microsecond --min-runs 10 --shell=none"
  program=$(sed '2q;d' "$ROOT/$subdir/examples/$PROFILE" | awk '{print $2}')


  echo "======================= $PROFILE ======================="

  # Cold Boot--there is no cache.
  mkdir /tmp/sb
  hyperfine --command-name "Cold $PROFILE" $ARGS --prepare "rm -r /tmp/sb" "$PROFILE --dry"
  sleep 1

  # Add warmup since we're not cold-boot anymore
  ARGS="${ARGS} --warmup 1"

  # Hot Boot--like after sb.service has run.
  hyperfine --command-name "Hot $PROFILE" $ARGS "$PROFILE --dry"
  sleep 1

  # Hot Boot, but we need to refresh libraries like after an update.
  if [[ "${subdir}" == "cpp" ]]; then
    UPDATE="--update libraries"
  else
    UPDATE="--update-libraries"
  fi
  hyperfine --command-name "Lib $PROFILE" $ARGS "$PROFILE --dry $UPDATE"
  sleep 1

  # Hot Boot, but we need to refresh libraries like after an update.
  if [[ "${subdir}" == "cpp" ]]; then
    UPDATE="--update cache"
  else
    UPDATE="--update-libraries --update-cache"
  fi
  hyperfine --command-name "Cache $PROFILE" $ARGS "$PROFILE --dry $UPDATE"
done

if [[ "$COMMIT" != "main" ]]; then
	git reset --hard --quiet
	git checkout main --quiet
	git stash pop --quiet
fi

rm -r ~/.config/sb ~/.local/share/sb

mv ~/.config/sb-bak ~/.config/sb
mv ~/.local/share/sb-bak ~/.local/share/sb
