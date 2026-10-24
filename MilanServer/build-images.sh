#!/usr/bin/env bash
# Builds the MilanServer images for distribution, for linux/amd64 and linux/arm64.
# Run from WSL or Linux with Docker and buildx. No emulation is needed: the .NET SDK
# stage runs on this machine and cross-publishes for each architecture.
#
#   ./build-images.sh [--version 1.2.0]
#       Writes dist/milanserver-<version>-linux-<arch>.tar.gz for each architecture,
#       plus dist/milanserver-<version>.sha256. Each tarball loads as milanserver:<version>:
#         gunzip -c milanserver-<version>-linux-arm64.tar.gz | docker load
#
#   ./build-images.sh --push ghcr.io/<owner>/milanserver [--version 1.2.0] [--latest]
#       Pushes one multi-architecture image (log in to the registry first). Docker pulls
#       the right architecture on each server. --latest also tags it latest.
#
# The unit tests run first, in the SDK image; --skip-tests skips them. Without --version,
# the version is the current commit, with -dirty if the server sources have local changes.
set -euo pipefail

readonly PLATFORMS=(linux/amd64 linux/arm64)
readonly NAME=milanserver
# Used to push multi-architecture images when Docker's classic image store cannot hold them.
readonly PUSH_BUILDER=milanserver-multiarch

usage() { sed -n '2,16s/^# \{0,1\}//p' "$0"; }

version=
push=
latest=false
skip_tests=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --version)
            [[ $# -ge 2 ]] || { echo "option --version requires a value" >&2; exit 2; }
            version=$2
            shift 2
            ;;
        --push)
            [[ $# -ge 2 ]] || { echo "option --push requires an image name, e.g. ghcr.io/<owner>/milanserver" >&2; exit 2; }
            push=$2
            shift 2
            ;;
        --latest)
            latest=true
            shift
            ;;
        --skip-tests)
            skip_tests=true
            shift
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done
if $latest && [[ -z $push ]]; then
    echo "--latest only applies with --push; a tarball always loads as $NAME:<version>." >&2
    exit 2
fi

command -v docker >/dev/null || { echo "docker is not installed here; run this from WSL or Linux." >&2; exit 1; }
docker buildx version >/dev/null 2>&1 || { echo "docker buildx is not installed." >&2; exit 1; }
cd "$(dirname "$0")"

# Only the files the image is built from decide whether the build is -dirty.
image_sources=(Dockerfile .dockerignore global.json MilanServer.slnx MilanServer MilanServer.Tests)
revision=$(git rev-parse HEAD 2>/dev/null || echo unknown)
if [[ -z $version ]]; then
    version=$(git rev-parse --short HEAD 2>/dev/null || echo dev)
    if [[ -n $(git status --porcelain -- "${image_sources[@]}" 2>/dev/null) ]]; then
        version+=-dirty
    fi
fi
if [[ ! $version =~ ^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$ ]]; then
    echo "version '$version' is not a valid image tag (letters, digits, '_', '.', '-')." >&2
    exit 2
fi

# Standard OCI labels, so a pulled or loaded image says what it was built from.
source_url=$(git remote get-url origin 2>/dev/null \
    | sed -E 's#^git@([^:]+):#https://\1/#; s#//[^/@]*@#//#; s#\.git$##' || true)
labels=(
    --label "org.opencontainers.image.title=MilanServer"
    --label "org.opencontainers.image.version=$version"
    --label "org.opencontainers.image.revision=$revision"
    --label "org.opencontainers.image.created=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
)
if [[ $source_url == https://* ]]; then
    labels+=(--label "org.opencontainers.image.source=$source_url")
fi

if ! $skip_tests; then
    echo "==> Running the unit tests"
    docker build --pull --target test .
fi

if [[ -n $push ]]; then
    tags=(-t "$push:$version")
    if $latest; then
        tags+=(-t "$push:latest")
    fi
    builder_args=()
    if ! docker info --format '{{.DriverStatus}}' 2>/dev/null | grep -q 'io.containerd.snapshotter'; then
        # The classic image store holds one platform per tag; a container builder can still push both.
        if ! docker buildx inspect "$PUSH_BUILDER" >/dev/null 2>&1; then
            docker buildx create --name "$PUSH_BUILDER" --driver docker-container >/dev/null
        fi
        builder_args=(--builder "$PUSH_BUILDER")
    fi
    platform_list=$(IFS=,; echo "${PLATFORMS[*]}")
    echo "==> Building and pushing $push:$version for $platform_list"
    docker buildx build ${builder_args[@]+"${builder_args[@]}"} --pull --platform "$platform_list" \
        "${labels[@]}" "${tags[@]}" --push .
    docker buildx imagetools inspect "$push:$version"
    exit 0
fi

mkdir -p dist
image=$NAME:$version
tarballs=()
for platform in "${PLATFORMS[@]}"; do
    arch=${platform#linux/}
    tarball=$NAME-$version-linux-$arch.tar.gz
    echo "==> Building $image for $platform"
    docker buildx build --pull --platform "$platform" "${labels[@]}" -t "$image" --load .

    built=$(docker image inspect "$image" --format '{{.Os}}/{{.Architecture}}')
    if [[ $built != "$platform" ]]; then
        echo "built $built instead of $platform" >&2
        exit 1
    fi

    echo "==> Saving dist/$tarball"
    docker save "$image" | gzip > "dist/$tarball.part"
    mv "dist/$tarball.part" "dist/$tarball"
    # The tag is reused for the next architecture; don't leave it pointing at a foreign image.
    docker image rm "$image" >/dev/null
    tarballs+=("$tarball")
done

(cd dist && sha256sum "${tarballs[@]}" > "$NAME-$version.sha256")
echo "==> Done:"
(cd dist && ls -l "${tarballs[@]}" "$NAME-$version.sha256")
