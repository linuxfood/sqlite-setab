#!/bin/bash
# Copyright Brian Smith <brian@linuxfood.net> 2016
set -x

FLOOP="${1}"
OUT="${2}"
ROOT="$(buck root)"

for d in ${ROOT}/externals/*; do
    rm -f $d/include && \
    rm -f $d/lib && \
    ln -s  $FLOOP/include $d/include && \
    ln -s  $FLOOP/lib     $d/lib
done

if [[ "${FLOOP:0:4}" = "/nix" ]]; then
    ln -s $ROOT/build/nix $ROOT/.buckconfig.local
else
    ln -s $ROOT/build/ubuntu $ROOT/.buckconfig.local
fi

touch $OUT
