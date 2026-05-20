Versions are now mandatory for build-docker-image.sh and compile-qemu.sh.
The scripts will not continue unless provided an image version, that also conforms to the current set of supported images (14.04 | 26.04).

build-docker-image.sh currently defaults to setting the evssim:14.04 image as latest, this is used for the current tests.
Expected docker images:
	evssim:14.04 
	evssim:26.04 
	evssim:latest
build-docker-image.sh accepts the version on the first argument (others aren't used nor tested), such as build-docker-image.sh 14.04.
The above will create evssim:14.04, and tag it as latest as well.

compile-qemu.sh is working in a similar manner, accepting a version.
compile-qemu.sh also cleans up the qemu repository environment from artifacts of previous builds, prior to building the qemu image.
In 14.04 it will switch to the qemu master branch, and in 26.04 it will switch to the qemu 11.0 branch.
Note that 26.04 builds with ninja, and 14.04 with make, this is due to the new structure of qemu.
26.04 skips open-osd/exofs legacy code.

run-ci.sh first compiles 26.04 and then 14.04, please maintain this order to run 14.04 tests with the correct qemu 14.04 files.
Else, the 11.0 branch for 26.04 will be exposed to the tests, which is not yet maintained.
I deemed checking out into master in the test scripts unnecessary as tests are usually done on a specific target -> maintain that target in ci pipeline.
