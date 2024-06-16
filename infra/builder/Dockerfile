FROM ubuntu:24.04

# Install base packages
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install \
	build-essential \
	bc \
	bear \
	bison \
	bridge-utils \
	build-essential \
	bzip2 \
	cmake \
	curl \
	eatmydata \
	flex \
	g++ \
	g++-multilib \
	gcc \
	git \
	libaio-dev \
	libboost-all-dev \
	libelf-dev \
	libgmp-dev \
	libgnutls28-dev \
	libisl-dev \
	libjson-c-dev \
	libmpc-dev \
	libmpfr-dev \
	libosinfo-bin \
	libpci-dev \
	libsqlite3-dev \
	libssl-dev \
	libvncserver-dev \
	libvirt-daemon \
	libvirt-daemon-system \
	libxen-dev \
	libsdl-image1.2-dev \
	libssl-dev \
	libvirt-daemon \
	libvirt-daemon-system \
	libxml++2.6-dev \
	libxen-dev \
	lsof \
	qemu-kvm \
	sshpass \
	strace \
	software-properties-common \
	valgrind \
	virtinst \
	vim \
	wget \
	zlib1g-dev \
	&& rm -rf /var/lib/apt/lists/*


# Install debootstrap
RUN add-apt-repository universe && apt update
RUN DEBIAN_FRONTEND=noninteractive apt-get -y install debootstrap

# Add zip unzip (required for the gcc compilation)
RUN apt-get install zip unzip

# ******************************   Downgrade to gcc 4.6   ******************************************
# First install gcc 9 using apt-get
RUN apt-get update && apt-get install -y gcc-9

# Set gcc 9 as the default compiler
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 100

# Downgrade gcc version to 4.8.4
RUN export VERSION="4.8.4"; echo "Installing gcc-$version"; \
    cd /; \
    wget http://ftp.gnu.org/gnu/gcc/gcc-$VERSION/gcc-$VERSION.tar.gz \
    && tar xf gcc-$VERSION.tar.gz \
    && cd gcc-$VERSION \
    && sed -i 's/struct ucontext/ucontext_t/g' ./libgcc/config/i386/linux-unwind.h \
    && export CXXFLAGS="-std=gnu++14 -fno-gnu89-inline" \ 
    && ./configure --disable-bootstrap \
                   --disable-libstdcxx \
                   --enable-languages=c \
                   --disable-libjava \
                   --disable-isl-version-check \
                   --disable-libsanitizer \
                   --disable-multilib \
    && make -j$(nproc) > /dev/null \
    && make install > /dev/null \
    && update-alternatives --install \
       /usr/bin/gcc gcc /usr/local/bin/x86_64-unknown-linux-gnu-gcc-$VERSION 100 \
    && update-alternatives --set gcc /usr/local/bin/x86_64-unknown-linux-gnu-gcc-$VERSION;
# ***************************************************************************************

# ******************** Install other tools compiled form a source code ************************
# NOTE keep those after the gcc 4.8.4 instalation. We will be linking them in the code that will
#      also be compiled with the same toolchain. 

# Install Gtest
ENV GTEST_HOME /opt
RUN wget -qO- https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz | \
    tar xz -C $GTEST_HOME/ && mv $GTEST_HOME/googletest-1.14.0 $GTEST_HOME/gtest
RUN mkdir -p $GTEST_HOME/gtest/lib && cd $GTEST_HOME/gtest/lib && cmake .. && make

# Install websockets
RUN export GIT_SSL_NO_VERIFY=1 && git clone --branch v4.3-stable \
    https://github.com/warmcat/libwebsockets.git /opt/libwebsockets && \
    cd /opt/libwebsockets && cmake . && make && make install && ldconfig


# Remove kernel scripts
RUN rm -rf /etc/kernel/postinst.d/*

# 24.04 comes with ubuntu user uid=1000
RUN userdel ubuntu

# Install additional packages that we had on 14.04
RUN apt-get install -y initramfs-tools \
                       libpixman-1-dev \
                       rsync \
                       sudo

# 24.04 has no python command
RUN update-alternatives --install /usr/bin/python python /usr/bin/python3 1 \
    && update-alternatives --config python

RUN apt-get -y install libxml++2.6-2v5 \
                       libboost-filesystem-dev \
                       python3-nose

# Services which should run immediately
RUN RUNLEVEL=1 apt-get -y install ntp ntpdate

# Copy our folder into the scripts folder
ADD docker /scripts
ADD env.sh /scripts
RUN chmod -R 777 /scripts

VOLUME /code
WORKDIR /code

ENTRYPOINT ["/scripts/entrypoint.sh"]
CMD ["bash"]
