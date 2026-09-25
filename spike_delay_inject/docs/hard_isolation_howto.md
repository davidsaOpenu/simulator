# Hard-isolation run: host setup and revert

Isolates physical cores 1-3 **with their SMT siblings** (logical 1-3,9-11), leaving
0,4-8,12-15 (10 logical cpus) for everything else. Needs root and one reboot each way.

## 1. Enable

```bash
sudo tee /etc/default/grub.d/99-evssim-isolation.cfg >/dev/null <<'CFG'
GRUB_CMDLINE_LINUX_DEFAULT="$GRUB_CMDLINE_LINUX_DEFAULT isolcpus=domain,managed_irq,1-3,9-11 nohz_full=1-3,9-11 rcu_nocbs=1-3,9-11 irqaffinity=0,4-8,12-15"
CFG
sudo update-grub
sudo reboot
```

- `isolcpus=domain` removes the cpus from the scheduler's load balancing: nothing lands
  there unless it is explicitly pinned. `managed_irq` additionally asks the kernel to keep
  managed (nvme blk-mq) interrupts off them where a housekeeping cpu is in the mask -
  best effort, the single-cpu queue vectors may remain.
- `nohz_full` stops the 1000 Hz tick while exactly one task is runnable on the cpu;
  `rcu_nocbs` moves RCU callbacks off it.
- `irqaffinity` sets the default mask for every non-managed IRQ. `irqbalance` is not
  running on this host, so nothing will undo it.

## 2. Verify after reboot, then measure

```bash
cat /sys/devices/system/cpu/isolated      # expect 1-3,9-11
cat /sys/devices/system/cpu/nohz_full     # expect 1-3,9-11
cd simulator/spike_delay_inject
ISOLATED=1 ./scripts/run_demo_matrix.sh      # the three isolated-boot rows, ~4 min, no root
```

The matrix pins the benchmark's main-loop thread to isolated cpu 1 (`--pin-cpu 1`) for the
"isolated" rows and loads the 10 housekeeping cpus for the third one.

## 3. Revert

```bash
sudo rm /etc/default/grub.d/99-evssim-isolation.cfg && sudo update-grub && sudo reboot
```

Until reverted the desktop, Docker builds and QEMU have 10 logical cpus instead of 16.
