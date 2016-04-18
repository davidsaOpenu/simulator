## Installing

- On Mac, run `brew install ansible`

- On Ubuntu 12.02, run `./ansible_install.sh`

## Running

Create a file named e.g. "my_hosts" containing your hosts. For example:
```
[host_testers]
tester1.my-hosting.com

[guest_testers]
tester1.my-hosting.com
```

and then run:

```sh
ansible-playbook -i my_hosts site.yml
```

## Customizing

It is possible to customize variables per host. For example, if your test machine has little disk space in your home directory but has ample disk space in /var/tmp, change your "my_hosts" file as follows:

```
[guest_testers]
tester1.my-hosting.com hda_dir=/var/tmp
```
