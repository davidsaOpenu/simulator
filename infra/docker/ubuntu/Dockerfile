FROM ubuntu:18.04

ENV DEBIAN_FRONTEND noninteractive

# Install base packages
RUN apt-get update && apt-get -y install \
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
  libxen-dev \
  zlib1g-dev \
  libsdl-image1.2-dev \
  libvncserver-dev \
  libpci-dev \
  libaio-dev \
  libssl-dev \
  sshpass \
  libvirt-bin \
  python-pip \
  python-setuptools \
  sudo

# Install build packages
RUN apt-get update && apt-get -y install \
  bear \
  qemu \
  qemu-kvm \
  valgrind \
  curl

# Install Ansible
RUN add-apt-repository -y ppa:ansible/ansible && apt-get update && apt-get -y install ansible

# Clean APT cache (making the image smaller)
RUN apt-get clean && rm -rf /var/lib/apt/lists/*

# Install libpng12
RUN wget -q -O /tmp/libpng12.deb http://mirrors.kernel.org/ubuntu/pool/main/libp/libpng/libpng12-0_1.2.54-1ubuntu1_amd64.deb \
  && sudo dpkg -i /tmp/libpng12.deb \
  && rm /tmp/libpng12.deb

# Install libtasn1-3
RUN wget -q -O /tmp/libtasn1-3.deb http://security.debian.org/debian-security/pool/updates/main/libt/libtasn1-3/libtasn1-3_2.13-2+deb7u5_amd64.deb \
  && sudo dpkg -i /tmp/libtasn1-3.deb \
  && rm /tmp/libtasn1-3.deb

# Install libgcrypt11
RUN wget -q -O /tmp/libgcrypt11.dev http://security.debian.org/debian-security/pool/updates/main/libg/libgcrypt11/libgcrypt11_1.5.0-5+deb7u6_amd64.deb \
  && sudo dpkg -i /tmp/libgcrypt11.dev \
  && rm /tmp/libgcrypt11.dev

# Install libgnutls26
RUN wget -q -O /tmp/libgnutls26.deb http://ftp.us.debian.org/debian/pool/main/g/gnutls26/libgnutls26_2.12.20-8+deb7u5_amd64.deb \
  && sudo dpkg -i /tmp/libgnutls26.deb \
  && rm /tmp/libgnutls26.deb

# Install libxen-4.4
RUN wget -q -O /tmp/libxen-4.4.deb http://archive.ubuntu.com/ubuntu/pool/main/x/xen/libxen-4.4_4.4.2-0ubuntu0.14.04.14_amd64.deb \
  && sudo dpkg -i /tmp/libxen-4.4.deb \
  && rm /tmp/libxen-4.4.deb

# Install Gtest
ENV GTEST_HOME /opt
RUN wget -qO- https://github.com/google/googletest/archive/release-1.7.0.tar.gz |tar xz -C $GTEST_HOME/ && \
mv $GTEST_HOME/googletest-release-1.7.0 $GTEST_HOME/gtest

RUN mkdir -p $GTEST_HOME/gtest/lib && \
    cd $GTEST_HOME/gtest/lib && \
    cmake .. && \
    make

# Install websockets
RUN git clone --branch v2.4-stable https://github.com/warmcat/libwebsockets.git /opt/libwebsockets && \
    cd /opt/libwebsockets && cmake . && \
    make && \
    make install

# Install qt5
RUN echo "deb http://archive.ubuntu.com/ubuntu trusty main universe multiverse" | tee /etc/apt/sources.list.d/trusty.list && \
    apt-get update && \
    apt-get -y install qtdeclarative5-dev qt5-default qttools5-dev-tools

ADD container_aliases /root/.bash_aliases
WORKDIR /code
ENTRYPOINT ["/code/simulator/infra/docker/ubuntu/entry_point.sh"]
RUN echo "ALL            ALL = (ALL) NOPASSWD: ALL" >> /etc/sudoers

#those lines are appended from outside
#echo RUN groupadd `stat -c "%G" .` -g `stat -c "%g" .` >> ${WORKSPACE}/simulator/infra/docker/ubuntu/Dockerfile
#echo RUN useradd -ms /bin/bash `stat -c "%U" .` -u `stat -c "%u" .` -g `stat -c "%g" .` -G sudo >> ${WORKSPACE}/simulator/infra/docker/ubuntu/Dockerfile
