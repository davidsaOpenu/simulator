
# TLDR

Just `cd` to this directory and execute `./log_all_operations`. logs generated at $(current working directory)/output
But make sure to execute `./setup` first, before anything else. This will initialize everything. You can execute this
file once in a boot (do it again if you rebooted the system).

# About The Executables

 - setup.sh - script to initialize all needed for mounting exofs, make sure to call it once, at the environment startup.
 - run.sh - script to analize a specific filesystem operation using ftrace. the output is log file.
            run it with one of the following parameters:
	    open, close, read, write, mount, umount.
	    e.g. ./run.sh open # this command will trace the open operation
 	    make sure to call umount after you call mount. if mounted, do not call mount again until calling umount.
 - log_all_operations.sh - script that generates all logs files for all operations (open, close, read, write and mount).
			   parses them (remove unnecessary text) and put them at `output` directory.

