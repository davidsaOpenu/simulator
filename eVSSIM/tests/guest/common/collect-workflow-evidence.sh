#!/bin/bash
# Run as root from guest/ before replacing this guest image.
set -e
mkdir -p Logs/workflow
cp guest_tests_results.xml Logs/workflow/
sha256sum nvme tnvme dnvme.ko > Logs/workflow/guest-tools.sha256
uname -r > Logs/workflow/guest-kernel.txt

if [ "$1" = nvme_compliance_tests ]; then
    # Raw suite output can be large. Preserve it before workspace cleanup,
    # excluding this collector's directory so the archive cannot include itself.
    tar -czf Logs/workflow/nvme-compliance-logs.tar.gz --exclude='./workflow' -C Logs .
fi

if [ "$1" = fio_tests ]; then
    mkdir -p Logs/workflow/fio/current Logs/workflow/fio/references
    result_dir=$(python -c 'import tempfile; print(tempfile.gettempdir())')
    find "$result_dir" -maxdepth 1 -type f -name 'test_*_res*' \
        -exec cp -t Logs/workflow/fio/current/ -- {} +
    cp -a fio_tests/reference_results/. Logs/workflow/fio/references/
    cp -a fio_tests/fio_jobs Logs/workflow/fio/jobs
fi

# The host collector runs as the build user; fio's root-owned raw files may
# otherwise retain mktemp's mode 0600 after extraction from the guest image.
chmod -R a+rX Logs/workflow
