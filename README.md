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
To run only host/guest tests pass this as tags:

```sh
ansible-playbook -i hosts --tags guest site.yml
```

To do a re-run on an existing setup, you can skip the clean actions so there is no need to re-build the packages: 

```sh
ansible-playbook -i hosts --skip-tags clean --tags guest site.yml
```

To skip to QEMU execution tasks without running all the lengthy setup and build tasks, run:

```sh
ansible-playbook -i hosts --tags qemu site.yml
```

## Customizing

It is possible to customize variables per host. For example, if your test machine has little disk space in your home directory but has ample disk space in /var/tmp, change your "hosts" file as follows:

```
tester1.my-hosting.com hda_dir=/var/tmp
```

## Useful variables

- `hda_dir` — specifies the directory to download the ~2GB `hda.zip` and unpack the ~6GB `hda.img`; defaults to the home directory on the host
- `qemu_machine` — defaults to `accel=tcg` (no acceleration); can be set to `accel=kvm` or `accel=xen` on supporting hosts

## TODO

- Add a public key to the guest VM image's `~esd/.ssh/authorized_keys` and get rid of `sshpass`.
