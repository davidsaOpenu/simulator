#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

python3 "$EVSSIM_ROOT_PATH/simulator/infra/ELK/host_metrics.py" gate "$version"
