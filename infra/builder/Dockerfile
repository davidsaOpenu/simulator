FROM ubuntu:14.04

# Install base packages
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y install \
  software-properties-common \
  git \
  vim \
  wget \
  build-essential \
  cmake \
  unzip \
  libsqlite3-dev \
  libxen-dev \
  zlib1g-dev \
  libsdl-image1.2-dev \
  libgnutls-dev \
  #libgnutls28-dev for 18.04 \
  libvncserver-dev \
  libpci-dev \
  libaio-dev \
  libssl-dev \
  sshpass \
  libvirt-bin \
  #libvirt-clients for 18.04\
  #bridge-utils for 18.04\
  #virt-manager for 18.04\
  python-pip \
  python-setuptools \
  libxml++2.6-dev \
  libboost-all-dev \
  eatmydata \
  strace \
  lsof \
  binutils-2.26 \
  gcc \
  g++
  #sudo for 18.04\
  #apt-utils for 18.04

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
RUN git clone --branch v4.1-stable https://github.com/warmcat/libwebsockets.git /opt/libwebsockets && \
    cd /opt/libwebsockets && cmake . && make && make install && ldconfig

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    libpixman-1-dev libjson-c-dev

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
