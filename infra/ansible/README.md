## Installing

- On OS X, run:
```
brew install ansible
brew install https://raw.githubusercontent.com/kadwanev/bigboybrew/master/Library/Formula/sshpass.rb
```

`sshpass` is not available in Homebrew due to security considerations. We're using `sshpass` to login into the guest VM after logging in into the host VM, so it doesn't present a security risk in our case.

- On Ubuntu 12.04, run `sudo ./ansible_install.sh`

## Running

Modify 'hosts' file to containing your hosts configuration.
It is currently configured to run on localhost.

and then run:

```sh
ansible-playbook -i hosts playbooks/<some_playbook>.yml
```

## Additional arguments

Here are some arguments you can add to `ansible-playbook` invocations:

| Argument | Meaning |
| --- | --- |
| `--tags host` | Perform only host tests |
| `--tags guest` | Perform only guest tests |
| `--skip-tags clean` | When replaying, skip the clean actions so there is no need to re-build the packages |
| `--tags qemu` | Perform only QEMU execution tasks (without running all the lengthy setup and build tasks) |

## Customizing

It is possible to customize variables per host. For example, if you prefer to store the hda.img in /tmp and to use KVM hardware acceleration:

```
tester1.my-hosting.com hda_dir=/tmp qemu_machine=accel=kvm
```

## Useful variables
Here are some arguments you can add to `run_full_setup.yml` invocations (with '-e' or '--extra-vars')

Simulator role arguments:
| Variable | Meaning |
| --- | --- |
| `git_in_ansible`| Determines whether ansible playbook should pull simulator repo. Defaults to git_in_ansible=false |
| `workspace` | Directory where all related projects are located. Defaults to workspace={{ ansible_env.HOME }}|
| `dest` | Simulator output directory. Defaults to dest={{ workspace }}/simulator |
| `ref` | Used where git_in_ansible=true, determines which ref to pull from repo. Defaults to ref=refs/heads/master |
| `qemu_pid_file` | Location of pid_file. Defaults to "{{ dest }}/simulator_qemu.pid" |
| `cleanup_only` | This argument is used internally. Determines if to skip test's preparations steps and to perform cleanup steps only. Defaults to cleanup_only=false |

Guest tester role arguments:
| Variable | Meaning |
| --- | --- |
| `short_mode` | Perform tests in short/long mode (default: true) |

Guest tester pre (preparation) role arguments:
| Variable | Meaning |
| --- | --- |
| `memory`| Parameter to qemu. Default: 2048 |
| `smp`| Parameter to qemu. Default: 4 |
| `hda_dir` | Where to store hda image if downloaded. This has to be a directory with ~ 9GB free space. Default: {{ ansible_env.HOME }} |
| `hda_cmpr` | compressed hda image filename. Default: {{hda_dir}}/hda_clean.qcow2.bz2 |
| `hda_img` | hda image filename. Default: {{hda_dir}}/hda_clean.qcow2 |
| `guest_ssh_port` | Port to guest machine, passed to qemu. Default: 2222 |
| `kernel_image` | filename of kernel image. Default: vmlinuz-3.8.0-29-generic |
| `initrd_image` | filename of initrd image. Default: initrd.img-3.8.0-29-generic |
| `kernel_cmdline` | kernel cmdline. Default: "BOOT_IMAGE=/boot/{{kernel_image}} root=UUID=063018ec-674c-4c3e-a976-ac4fa950864f ro" |
| `qemu_machine` | Default: accel=kvm |
| `test_instance_id` | For future use. This is intended to be passed from e.g. Jenkins to allow multiple tests to run simultaneously. Default: some_unique_id |

Host tester role arguments:
| Variable | Meaning |
| --- | --- |
| `short_mode` | Perform tests in short/long mode (default: true) |

Prepare role arguments:
| Variable | Meaning |
| --- | --- |
| `build_ssd_monitor` | Determines whether to build ssd_monitor. Defaults to build_ssd_monitor=false |

## Example
In order to run nvme complience tests in centos docker container the following commands can be executed

A. Clone projects
  1. clone related projects to $(pwd)

B. Run docker container and get a bash prompt
  1. export WORKSPACE=$(pwd)
  2. cd ${WORKSPACE}/simulator
  3. echo RUN groupadd `stat -c "%G" .` -g `stat -c "%g" .` >> ${WORKSPACE}/simulator/infra/docker/centos/Dockerfile
  4. echo RUN useradd -ms /bin/bash `stat -c "%U" .` -u `stat -c "%u" .` -g `stat -c "%g" .` -G wheel >> ${WORKSPACE}/simulator/infra/docker/centos/Dockerfile
  5. docker build -t os-centos ${WORKSPACE}/simulator/infra/docker/centos
  6. [ ! -d ${WORKSPACE}/hda/ ] && mkdir ${WORKSPACE}/hda/
  7. [ ! -f ${WORKSPACE}/hda/hda_clean.qcow2 ] && cp ~jenkins/hda_clean.qcow2 ${WORKSPACE}/hda/
  8. docker run -u `stat -c "%u:%g" .` -it  -v /tmp/.X11-unix:/tmp/.X11-unix -v ${WORKSPACE}:/code --cap-add SYS_PTRACE --privileged=true os-centos /bin/bash

C. Setup test environment
  1. ansible-playbook -i hosts playbooks/run_full_setup.yml  --extra-vars "dest=/code/simulator" --extra-vars "@centos.yml" --extra-vars "@global_vars.yml"

D. ssh to the VM
  1. make shure VM is running (ps -ef | grep qemu)
  2. ssh -p 2222 esd@127.0.0.1

E. run tests/nvme-cli util
  1. ~/nvme-cli
  2. ~/guest/run_all_guest_tests.sh

## TODO

- Add a public key to the guest VM image's `~esd/.ssh/authorized_keys` and get rid of `sshpass`.

TODO remove obsolite/update
