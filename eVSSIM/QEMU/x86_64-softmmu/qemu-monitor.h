

{ "help|?", "s?", help_cmd, "[cmd]", "show the help" },

{ "commit", "s", do_commit,
"device|all", "commit changes to the disk images (if -snapshot is used) or backing files" },

{ "info", "s?", do_info,
"[subcommand]", "show various information about the system state" },

{ "q|quit", "", do_quit,
"", "quit the emulator" },

{ "eject", "-fB", do_eject,
"[-f] device", "eject a removable medium (use -f to force it)" },

{ "change", "BFs?", do_change,
"device filename [format]", "change a removable medium, optional format" },

{ "screendump", "F", do_screen_dump,
"filename", "save screen into PPM image 'filename'" },

{ "logfile", "F", do_logfile,
"filename", "output logs to 'filename'" },

{ "log", "s", do_log,
"item1[,...]", "activate logging of the specified items to '/tmp/qemu.log'" },

{ "savevm", "s?", do_savevm,
"[tag|id]", "save a VM snapshot. If no tag or id are provided, a new snapshot is created" },

{ "loadvm", "s", do_loadvm,
"tag|id", "restore a VM snapshot from its tag or id" },

{ "delvm", "s", do_delvm,
"tag|id", "delete a VM snapshot from its tag or id" },

{ "singlestep", "s?", do_singlestep,
"[on|off]", "run emulation in singlestep mode or switch to normal mode", },

{ "stop", "", do_stop,
"", "stop emulation", },

{ "c|cont", "", do_cont,
"", "resume emulation", },

{ "gdbserver", "s?", do_gdbserver,
"[device]", "start gdbserver on given device (default 'tcp::1234'), stop with 'none'", },

{ "x", "/l", do_memory_dump,
"/fmt addr", "virtual memory dump starting at 'addr'", },

{ "xp", "/l", do_physical_memory_dump,
"/fmt addr", "physical memory dump starting at 'addr'", },

{ "p|print", "/l", do_print,
"/fmt expr", "print expression value (use $reg for CPU register access)", },

{ "i", "/ii.", do_ioport_read,
"/fmt addr", "I/O port read" },

{ "o", "/ii", do_ioport_write,
"/fmt addr value", "I/O port write" },

{ "sendkey", "si?", do_sendkey,
"keys [hold_ms]", "send keys to the VM (e.g. 'sendkey ctrl-alt-f1', default hold time=100 ms)" },

{ "system_reset", "", do_system_reset,
"", "reset the system" },

{ "system_powerdown", "", do_system_powerdown,
"", "send system power down event" },

{ "sum", "ii", do_sum,
"addr size", "compute the checksum of a memory region" },

{ "usb_add", "s", do_usb_add,
"device", "add USB device (e.g. 'host:bus.addr' or 'host:vendor_id:product_id')" },

{ "usb_del", "s", do_usb_del,
"device", "remove USB device 'bus.addr'" },

{ "cpu", "i", do_cpu_set,
"index", "set the default CPU" },

{ "mouse_move", "sss?", do_mouse_move,
"dx dy [dz]", "send mouse move events" },

{ "mouse_button", "i", do_mouse_button,
"state", "change mouse button state (1=L, 2=M, 4=R)" },

{ "mouse_set", "i", do_mouse_set,
"index", "set which mouse device receives events" },

#ifdef HAS_AUDIO
{ "wavcapture", "si?i?i?", do_wav_capture,
"path [frequency [bits [channels]]]",
"capture audio to a wave file (default frequency=44100 bits=16 channels=2)" },
#endif

#ifdef HAS_AUDIO
{ "stopcapture", "i", do_stop_capture,
"capture index", "stop capture" },
#endif

{ "memsave", "lis", do_memory_save,
"addr size file", "save to disk virtual memory dump starting at 'addr' of size 'size'", },

{ "pmemsave", "lis", do_physical_memory_save,
"addr size file", "save to disk physical memory dump starting at 'addr' of size 'size'", },

{ "boot_set", "s", do_boot_set,
"bootdevice", "define new values for the boot device list" },

#if defined(TARGET_I386)
{ "nmi", "i", do_inject_nmi,
"cpu", "inject an NMI on the given CPU", },
#endif

{ "migrate", "-ds", do_migrate,
"[-d] uri", "migrate to URI (using -d to not wait for completion)" },

{ "migrate_cancel", "", do_migrate_cancel,
"", "cancel the current VM migration" },

{ "migrate_set_speed", "s", do_migrate_set_speed,
"value", "set maximum speed (in bytes) for migrations" },

{ "migrate_set_downtime", "s", do_migrate_set_downtime,
"value", "set maximum tolerated downtime (in seconds) for migrations" },


#if defined(TARGET_I386)
{ "drive_add", "ss", drive_hot_add, "[[<domain>:]<bus>:]<slot>\n"
"[file=file][,if=type][,bus=n]\n"
"[,unit=m][,media=d][index=i]\n"
"[,cyls=c,heads=h,secs=s[,trans=t]]\n"
"[snapshot=on|off][,cache=on|off]",
"add drive to PCI storage controller" },
#endif

#if defined(TARGET_I386)
{ "pci_add", "sss?", pci_device_hot_add, "auto|[[<domain>:]<bus>:]<slot> nic|storage|host [[vlan=n][,macaddr=addr][,model=type]] [file=file][,if=type][,bus=nr]... [host=02:00.0[,name=string][,dma=none]", "hot-add PCI device" },
#endif

#if defined(TARGET_I386)
{ "pci_del", "s", pci_device_hot_remove, "[[<domain>:]<bus>:]<slot>", "hot remove PCI device" },
#endif

{ "host_net_add", "ss?", net_host_device_add,
"tap|user|socket|vde|dump [options]", "add host VLAN client" },

{ "host_net_remove", "is", net_host_device_remove,
"vlan_id name", "remove host VLAN client" },

#ifdef CONFIG_SLIRP
{ "hostfwd_add", "ss?s?", net_slirp_hostfwd_add,
"[vlan_id name] [tcp|udp]:[hostaddr]:hostport-[guestaddr]:guestport",
"redirect TCP or UDP connections from host to guest (requires -net user)" },
{ "hostfwd_remove", "ss?s?", net_slirp_hostfwd_remove,
"[vlan_id name] [tcp|udp]:[hostaddr]:hostport",
"remove host-to-guest TCP or UDP redirection" },
#endif

{ "balloon", "i", do_balloon,
"target", "request VM to change it's memory allocation (in MB)" },

{ "set_link", "ss", do_set_link,
"name up|down", "change the link status of a network adapter" },

{ "watchdog_action", "s", do_watchdog_action,
"[reset|shutdown|poweroff|pause|debug|none]", "change watchdog action" },

{ "acl_show", "s", do_acl_show, "aclname",
"list rules in the access control list" },

{ "acl_policy", "ss", do_acl_policy, "aclname allow|deny",
"set default access control list policy" },

{ "acl_add", "sssi?", do_acl_add, "aclname match allow|deny [index]",
"add a match rule to the access control list" },

{ "acl_remove", "ss", do_acl_remove, "aclname match",
"remove a match rule from the access control list" },

{ "acl_reset", "s", do_acl_reset, "aclname",
"reset the access control list" },

#if defined(TARGET_I386)
{ "mce", "iillll", do_inject_mce, "cpu bank status mcgstatus addr misc", "inject a MCE on the given CPU"},
#endif

{ "getfd", "s", do_getfd, "getfd name",
"receive a file descriptor via SCM rights and assign it a name" },

{ "closefd", "s", do_closefd, "closefd name",
"close a file descriptor previously passed via SCM rights" },

{ "cpu_set", "is", do_cpu_set_nr,
"cpu [online|offline]", "change cpu state" },

