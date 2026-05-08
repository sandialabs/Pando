#!/bin/bash -ex

SCRATCH_DIR="${SCRATCH_DIR:-/scratch}"
if [ ! -d "$SCRATCH_DIR" ] || [ ! -w "$SCRATCH_DIR" ]; then
    echo "ERROR: SCRATCH_DIR='$SCRATCH_DIR' is not a writable directory" >&2
    exit 1
fi

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pushd "${SOURCE_DIR}"

mkdir -p sifs

# Empty APPTAINER_BINDPATH during build to avoid host module-injected binds
# interfering with %post; --fakeroot needed because pgclark is not in /etc/subuid
APPTAINER_BINDPATH="" apptainer build --fakeroot \
    --bind "$SCRATCH_DIR:/scratch" \
    sifs/pando_dev.sif env/pando_dev.def

popd
