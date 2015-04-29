../x86_64-softmmu/qemu-system-x86_64 -m 2048 -smp 4 -hda ../../../../../qtux30.img -device nvme -enable-kvm -redir tcp:2222::22 2>&1 | tee /tmp/o
