## Installing

- On OS X, run:
```
brew install ansible
brew install https://raw.githubusercontent.com/kadwanev/bigboybrew/master/Library/Formula/sshpass.rb
```

`sshpass` is not available in Homebrew due to security considerations. We're using `sshpass` to login into the guest VM after logging in into the host VM, so it doesn't present a security risk in our case.

- On Ubuntu 12.04, run `sudo ./ansible_install.sh`

## Running

Create a file named e.g. "my_hosts" containing your hosts. For example:
```
[host_testers]
tester1.my-hosting.com

[guest_testers]
tester1.my-hosting.com

[guests]
tester1.my-hosting.com__guest ansible_host=tester1.my-hosting.com
```

and then run:

```sh
ansible-playbook -K -i my_hosts site.yml
```
To run only host/guest tests pass this as tags:

```sh
ansible-playbook -K -i my_hosts --tags guest site.yml
```

To do a re-run on an existing setup, you can skip the clean actions so there is no need to re-build the packages: 

```sh
ansible-playbook -K -i my_hosts --skip-tags clean --tags guest site.yml
```

To skip to QEMU execution tasks without running all the lengthy setup and build tasks, run:

```sh
ansible-playbook -K -i my_hosts --tags qemu site.yml
```

## Guest hosts

Guest hosts are special kind of hosts, used to communicate with the guest VM within the guest testers. The guest hosts are not given a role; instead, they're used explicitly with `delegate_to` when time comes.

Each guest tester should be accompanied with a matching guest entry, i.e.:
```
[guest_testers]
foobar1
foobar2

[guests]
foobar1__guest ansible_host=foobar1
foobar2__guest ansible_host=foobar2
```

To see how they're impemented, see `group_vars/guests.yml`.

## Customizing

It is possible to customize variables per host. For example, if your test machine has little disk space in your home directory but has ample disk space in /var/tmp, change your "my_hosts" file as follows:

```
[guest_testers]
tester1.my-hosting.com hda_dir=/var/tmp
```

## Useful variables

- `hda_dir` — specifies the directory to download the ~2GB `hda.zip` and unpack the ~6GB `hda.img`; defaults to the home directory on the host
- `qemu_machine` — defaults to `accel=tcg` (no acceleration); can be set to `accel=kvm` or `accel=xen` on supporting hosts

## TODO

- Add a public key to the guest VM image's `~esd/.ssh/authorized_keys` and get rid of `sshpass`.
