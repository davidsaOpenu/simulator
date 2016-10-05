Run the guest tests like so:

	sudo py.test -x --nvme-cli-dir=<directory of nvme-cli executable>
	
The last parameter can be excluded if the tests are being run from the directory containing
the nvme-cli executable.
