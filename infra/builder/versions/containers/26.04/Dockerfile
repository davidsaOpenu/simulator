FROM ubuntu:26.04

ARG UBUNTU_MIRROR="http://archive.ubuntu.com/ubuntu/"
ARG SECURITY_MIRROR="http://security.ubuntu.com/ubuntu/"

RUN sed -e "s|http://archive.ubuntu.com/ubuntu/|${UBUNTU_MIRROR}|g" -e "s|http://security.ubuntu.com/ubuntu/|${SECURITY_MIRROR}|g" -i /etc/apt/sources.list.d/ubuntu.sources

# Install base packages
RUN apt update && DEBIAN_FRONTEND=noninteractive apt -y install \
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
  python3-pip \
  python3-setuptools \
  python3-requests \
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
  uuid-dev

# Install build packages
RUN apt update && DEBIAN_FRONTEND=noninteractive apt -y install \
  bear \
  qemu-system-x86 \
  valgrind \
  curl \
  bc

# Install debootstrap
RUN DEBIAN_FRONTEND=noninteractive apt -y install debootstrap

# Install Gtest
ENV GTEST_HOME=/opt
RUN wget -qO- https://github.com/google/googletest/archive/release-1.7.0.tar.gz |tar xz -C $GTEST_HOME/ && mv $GTEST_HOME/googletest-release-1.7.0 $GTEST_HOME/gtest
RUN sed -e "s/cmake_minimum_required(VERSION .*)/cmake_minimum_required(VERSION 3.5)/g" -i $GTEST_HOME/gtest/CMakeLists.txt
RUN mkdir -p $GTEST_HOME/gtest/lib && cd $GTEST_HOME/gtest/lib && cmake .. && make

# Install websockets
RUN export GIT_SSL_NO_VERIFY=1 && git clone --branch v4.5-stable https://github.com/warmcat/libwebsockets.git /opt/libwebsockets && \
    cd /opt/libwebsockets && cmake -DCMAKE_C_FLAGS="-Wno-error=discarded-qualifiers" . && make && make install && ldconfig

RUN apt update && DEBIAN_FRONTEND=noninteractive apt install -y \
    libpixman-1-dev

# Remove kernel scripts
RUN rm -rf /etc/kernel/postinst.d/*

# Delete default ubuntu user
RUN userdel --remove ubuntu

# Copy our folder into the scripts folder
ADD docker /scripts
ADD env.sh /scripts
RUN chmod -R 777 /scripts

VOLUME /code
WORKDIR /code

# TODO fix virtualization check in entrypoint.sh
ENTRYPOINT ["/scripts/entrypoint.sh"]
CMD ["bash"]