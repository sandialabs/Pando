#!/bin/bash -ex

P="${P:-podman}"        # Run with P=docker ./build_images.sh to change

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pushd "${SOURCE_DIR}"

function build_sif() {
    SCRATCH_DIR="${SCRATCH_DIR:-/scratch}"
    if [ ! -d "$SCRATCH_DIR" ] || [ ! -w "$SCRATCH_DIR" ]; then
        echo "ERROR: SCRATCH_DIR='$SCRATCH_DIR' is not a writable directory" >&2
        exit 1
    fi

    build_im "$@"
    mkdir -p sifs
    rm -vf sifs/$1.sif
    "$P" image save -o $1.tar $1
    singularity build --bind "$SCRATCH_DIR:/scratch" sifs/$1.sif "docker-archive://$PWD/$1.tar"
    rm -v $1.tar
}

function build_im() {
    "$P" image build -t $1 -f env/$1 $2
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
