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

Note: you must configure the default host file to include the sudo password of your localhost.

and then run:

```sh
ansible-playbook -i hosts site.yml
```

## Aditional arguments

Here are some arguments you can add to `ansible-playbook` invocations:

| Argument | Meaning |
| --- | --- |
| `--tags host` | Perform only host tests |
| `--tags guest` | Perform only guest tests |
| `--skip-tags clean` | When replaying, skip the clean actions so there is no need to re-build the packages |
| `--tags qemu` | Perform only QEMU execution tasks (without running all the lengthy setup and build tasks) |
| `-e 'fio_short=true'` | Perform fio tests in short mode (default: false) |

## Customizing

It is possible to customize variables per host. For example, if you prefer to store the hda.img in /tmp and to use KVM hardware acceleration:

```
tester1.my-hosting.com hda_dir=/tmp qemu_machine=accel=kvm
```

## Useful variables

| Variable | Meaning |
| --- | --- |
| `hda_dir` | Host directory to download the ~2GB `hda.zip` and unpack the ~6GB `hda.img`; defaults to the home directory on the host. |
| `qemu_machine` | Argument passed to `qemu --machine`. Defaults to `accel=kvm` ; can be set to `accel=tcg` (no acceleration) or `accel=xen` on supporting hosts. |

## TODO

- Add a public key to the guest VM image's `~esd/.ssh/authorized_keys` and get rid of `sshpass`.
