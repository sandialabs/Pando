#!/bin/bash -ex

P="${P:-podman}"        # Run with P=docker ./build_images.sh to change

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Build Docker archives and SIFs on local disk, not /ascldap.
# Override if needed:
#   BQS_LOCAL_BUILD_ROOT=/var/tmp/$USER/bqs-build P=docker ./build_images.sh
LOCAL_BUILD_ROOT="${BQS_LOCAL_BUILD_ROOT:-/tmp/${USER}/bqs-build}"
APPTAINER_WORK_ROOT="${LOCAL_BUILD_ROOT}/apptainer"
SIF_WORK_ROOT="${LOCAL_BUILD_ROOT}/sifs"
TAR_WORK_ROOT="${LOCAL_BUILD_ROOT}/tars"

export TMPDIR="${APPTAINER_WORK_ROOT}/tmp"
export APPTAINER_TMPDIR="${APPTAINER_WORK_ROOT}/tmp"
export APPTAINER_CACHEDIR="${APPTAINER_WORK_ROOT}/cache"
export SINGULARITY_TMPDIR="${APPTAINER_WORK_ROOT}/tmp"
export SINGULARITY_CACHEDIR="${APPTAINER_WORK_ROOT}/cache"

mkdir -p \
    "${TMPDIR}" \
    "${APPTAINER_CACHEDIR}" \
    "${SIF_WORK_ROOT}" \
    "${TAR_WORK_ROOT}"

pushd "${SOURCE_DIR}"

function build_sif() {
    local name="$1"

    build_im "$@"

    mkdir -p sifs

    local tar_file="${TAR_WORK_ROOT}/${name}.tar"
    local sif_tmp="${SIF_WORK_ROOT}/${name}.sif"
    local sif_final="sifs/${name}.sif"

    rm -vf "${tar_file}" "${sif_tmp}"

    "$P" image save -o "${tar_file}" "${name}"

    local build_opts=()

    # Some Apptainer/Singularity versions support this. It helps avoid
    # mksquashfs segfaults/races on this system.
    if singularity build --help 2>&1 | grep -q -- '--mksquashfs-args'; then
        build_opts+=(--mksquashfs-args "-processors 1")
    fi

    local ok=0
    local attempt

    for attempt in 1 2 3; do
        echo "Attempt ${attempt}/3: building ${name}.sif"
        rm -vf "${sif_tmp}"
        rm -rf "${APPTAINER_WORK_ROOT}/tmp/"*

        if singularity build "${build_opts[@]}" "${sif_tmp}" "docker-archive://${tar_file}"; then
            ok=1
            break
        fi

        echo "WARNING: singularity build failed for ${name} on attempt ${attempt}"
        sleep 5
    done

    if [ "${ok}" -ne 1 ]; then
        echo "ERROR: failed to build SIF for ${name} after 3 attempts"
        exit 1
    fi

    cp -vf "${sif_tmp}" "${sif_final}"

    rm -vf "${tar_file}" "${sif_tmp}"
}

function build_im() {
    "$P" image build -t "$1" -f "env/$1" "$2"
}

build_im pando_base_os env
build_sif pando_dev env
build_im pando_build .
build_im pando_run env
build_sif pando_proxy env
build_sif pando_pardb env
build_sif pando_client env
build_sif pando_mon env
build_sif pando_top env
build_sif pando_python env
build_sif pando_jupyter env

popd
