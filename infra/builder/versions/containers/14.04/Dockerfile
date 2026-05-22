FROM ubuntu:14.04

# 14.04 is EOL, so disable ESM and use the archived package mirrors.
# Trusty needs the non-Cloudflare mirror in this Docker network and the stale ESM list removed.
RUN rm -f /etc/apt/sources.list.d/ubuntu-esm-infra-trusty.list && \
    sed -i \
      -e 's|http://archive.ubuntu.com/ubuntu/|http://us.archive.ubuntu.com/ubuntu/|g' \
      -e 's|http://security.ubuntu.com/ubuntu/|http://us.archive.ubuntu.com/ubuntu/|g' \
      /etc/apt/sources.list && \
    printf 'Acquire::ForceIPv4 "true";\n' > /etc/apt/apt.conf.d/99force-ipv4

# Install base packages
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install \
  software-properties-common \
  git \
  vim \
  wget \
  build-essential \
  gcc-4.6 \
  cmake \
  g++ \
  unzip \
  libsqlite3-dev \
  libxen-dev \
  zlib1g-dev \
  libsdl-image1.2-dev \
  libgnutls-dev \
  libvncserver-dev \
  libpci-dev \
  libaio-dev \
  libssl-dev \
  sshpass \
  libvirt-bin \
  python-pip \
  python-setuptools \
  libxml++2.6-dev \
  libboost-all-dev \
  eatmydata \
  strace \
  lsof \
  eclipse-cdt \
  libjson-c-dev \
  flex \
  bison \
  gdbserver \
  libelf-dev \
  uuid-dev

# Install build packages
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install \
  bear \
  qemu \
  qemu-kvm \
  valgrind \
  curl \
  bc

# Install debootstrap
RUN DEBIAN_FRONTEND=noninteractive apt-get -y install debootstrap

# Install Gtest
ENV GTEST_HOME /opt
RUN wget -qO- https://github.com/google/googletest/archive/release-1.7.0.tar.gz |tar xz -C $GTEST_HOME/ && mv $GTEST_HOME/googletest-release-1.7.0 $GTEST_HOME/gtest
RUN mkdir -p $GTEST_HOME/gtest/lib && cd $GTEST_HOME/gtest/lib && cmake .. && make

# Install websockets
RUN export GIT_SSL_NO_VERIFY=1 && git clone --branch v4.1-stable https://github.com/warmcat/libwebsockets.git /opt/libwebsockets && \
    cd /opt/libwebsockets && cmake . && make && make install && ldconfig

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    libpixman-1-dev

# Remove kernel scripts
RUN rm -rf /etc/kernel/postinst.d/*

# Copy our folder into the scripts folder
ADD docker /scripts
ADD env.sh /scripts
RUN chmod -R 777 /scripts

# Install python libraries
RUN pip install -r /scripts/requirements.txt

VOLUME /code
WORKDIR /code

ENTRYPOINT ["/scripts/entrypoint.sh"]
CMD ["bash"]
