FROM ubuntu:14.04

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
  zlib1g-dev \
  libpci-dev \
  libaio-dev \
  libssl-dev \
  sshpass \
  python-pip \
  python-setuptools \
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

RUN apt-get install -y genisoimage

RUN apt-get update && \
    apt-get install -y python-yaml && \
    curl -o /usr/local/bin/cloud-localds https://git.launchpad.net/cloud-utils/plain/bin/cloud-localds && \
    chmod +x /usr/local/bin/cloud-localds

RUN apt-get install -y pkg-config


RUN apt-get install -y   libglib2.0-dev \
  libpcre3-dev # for QEMU compilation
RUN apt-get install -y libxml++2.6-dev

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
