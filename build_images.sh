#!/bin/bash -ex

P="${P:-podman}"        # Run with P=docker ./build_images.sh to change
JOBS="${JOBS:-4}"       # Max concurrent .sif conversions (each mksquashfs is multi-threaded)

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pushd "${SOURCE_DIR}"

# Tarballs go outside the repo so they never leak into a build context.
TMP_DIR="$(mktemp -d)"
LOG_DIR="${SOURCE_DIR}/sifs/logs"
mkdir -p sifs "${LOG_DIR}"

SIF_PIDS=()
SIF_NAMES=()

function cleanup() {
    # Kill any conversions still running if we bail out early.
    for pid in "${SIF_PIDS[@]}"; do kill "$pid" 2>/dev/null || true; done
    wait 2>/dev/null || true
    rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

function build_im() {
    "$P" image build -t $1 -f env/$1 $2
}

function convert_sif() {
    rm -vf sifs/$1.sif
    "$P" image save -o "${TMP_DIR}/$1.tar" $1
    singularity build sifs/$1.sif "docker-archive://${TMP_DIR}/$1.tar"
    rm -v "${TMP_DIR}/$1.tar"
}

# Start a .sif conversion in the background, throttled to $JOBS at a time.
function sif_bg() {
    # Poll rather than `wait -n`, which would consume exit statuses we check later.
    { set +x; } 2>/dev/null
    while (( $(jobs -rp | wc -l) >= JOBS )); do sleep 1; done
    set -x
    convert_sif $1 > "${LOG_DIR}/$1.log" 2>&1 &
    SIF_PIDS+=($!)
    SIF_NAMES+=($1)
}

function build_sif() {
    build_im "$@"
    sif_bg $1
}

# Image builds stay sequential (each depends on the previous one); .sif
# conversions start in the background as soon as their image exists.
build_im pando_base_os env
build_sif pando_dev env         # converts while pando_build compiles
build_im pando_build .
build_im pando_run env
build_sif pando_proxy env
build_sif pando_pardb env
build_sif pando_client env
build_sif pando_mon env
build_sif pando_top env
build_sif pando_python env
build_sif pando_jupyter env

# Check every conversion and report failures with their logs.
set +x
FAILED=()
for i in "${!SIF_PIDS[@]}"; do
    if ! wait "${SIF_PIDS[$i]}"; then
        FAILED+=("${SIF_NAMES[$i]}")
        echo "===== ${SIF_NAMES[$i]} FAILED (log: ${LOG_DIR}/${SIF_NAMES[$i]}.log) ====="
        tail -n 30 "${LOG_DIR}/${SIF_NAMES[$i]}.log"
    fi
done
SIF_PIDS=()
if (( ${#FAILED[@]} )); then
    echo "sif conversion failed for: ${FAILED[*]}"
    exit 1
fi
echo "All sifs built; logs in ${LOG_DIR}"

popd
