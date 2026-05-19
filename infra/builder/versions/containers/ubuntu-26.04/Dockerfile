FROM ubuntu:26.04

# Install base packages
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install \
  sudo \
  initramfs-tools \
  rsync \
  software-properties-common \
  git \
  vim \
  wget \
  build-essential \
  gcc \
  cmake \
  g++ \
  unzip \
  libsqlite3-dev \
  libncurses-dev \
  libxen-dev \
  zlib1g-dev \
  libsdl-image1.2-dev \
  libgnutls28-dev \
  libvncserver-dev \
  libpci-dev \
  libaio-dev \
  libssl-dev \
  sshpass \
  libvirt-daemon-system \
  libvirt-clients \
  python3 \
  python3-pip \
  python3-setuptools \
  python3-requests \
  python-is-python3 \
  libxml++2.6-dev \
  libboost-all-dev \
  eatmydata \
  strace \
  lsof \
  libjson-c-dev \
  flex \
  bison \
  gdbserver \
  libelf-dev \
  uuid-dev \
  meson \
  ninja-build

# Replace uutils with the more stable gnu utils
# CI fails with the new uutils, at the very least sudo's -E (preserve environment)
# flag which is crucial for many different scripts (allows the usage of the
# EVSSIM_ environment variables from within sudo'd commands) self reports as
# not being supported by uutils
RUN apt update && DEBIAN_FRONTEND=noninteractive apt -y --allow-remove-essential install \
  coreutils-from-gnu \
  coreutils-from-uutils- \
  rust-coreutils- \
  sudo-rs-

# Install build packages
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install \
  bear \
  qemu-system-x86 \
  qemu-utils \
  qemu-block-extra \
  ovmf \
  cpu-checker \
  bridge-utils \
  linux-image-virtual \
  libguestfs-tools \
  valgrind \
  curl \
  bc

# Add coreutils-from-gnu to the list of pachages supermin copies into the
# mini "virtual machine" it runs for the guestfs tools.
# Without this, supermin's virtual machine runs without coreutils and hence
# can't do anything.
RUN echo 'coreutils-from-gnu' >> /usr/lib/x86_64-linux-gnu/guestfs/supermin.d/packages

# Install Gtest
ENV GTEST_HOME=/opt
RUN wget -qO- https://github.com/google/googletest/archive/release-1.7.0.tar.gz |tar xz -C $GTEST_HOME/ && mv $GTEST_HOME/googletest-release-1.7.0 $GTEST_HOME/gtest
RUN mkdir -p $GTEST_HOME/gtest/lib && \
    cd $GTEST_HOME/gtest/lib && \
    cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 .. && \
    make

# Install websockets
RUN export GIT_SSL_NO_VERIFY=1 && git clone --branch v4.5-stable https://github.com/warmcat/libwebsockets.git /opt/libwebsockets && \
    cd /opt/libwebsockets && cmake -DCMAKE_C_FLAGS="-Wno-error=discarded-qualifiers" . && make && make install && ldconfig

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install \
    libpixman-1-dev

# Remove kernel scripts
RUN rm -rf /etc/kernel/postinst.d/*

# Delete default ubuntu user
RUN userdel --remove ubuntu

# Copy our folder into the scripts folder
ADD docker /scripts
ADD env.sh /scripts
ADD versions/image-maker /scripts/image-maker
RUN chmod -R 777 /scripts

VOLUME /code
WORKDIR /code

ENTRYPOINT ["/scripts/entrypoint.sh"]
CMD ["bash"]
