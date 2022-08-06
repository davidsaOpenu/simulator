FROM ubuntu:18.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get -y install software-properties-common && \
    add-apt-repository "deb http://archive.ubuntu.com/ubuntu/ trusty main" && \
    add-apt-repository "deb http://archive.ubuntu.com/ubuntu/ trusty universe"

# Install base packages
RUN apt-get update && apt-get -y install \
  git \
  vim \
  wget \
  build-essential \
  gcc-4.6 \
  cmake \
  g++-4.6 \
  unzip \
  libsqlite3-dev \
  libxen-dev \
  zlib1g-dev \
  libsdl-image1.2-dev \
  libgnutls28-dev \
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

RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.8 1 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-4.8 1



# Install Ansible
RUN add-apt-repository -y ppa:ansible/ansible && apt-get update && apt-get -y install ansible

# Clean APT cache (making the image smaller)
RUN apt-get clean && rm -rf /var/lib/apt/lists/*

# Install Gtest
ENV GTEST_HOME /opt
RUN wget -qO- https://github.com/google/googletest/archive/release-1.7.0.tar.gz |tar xz -C $GTEST_HOME/ && \
mv $GTEST_HOME/googletest-release-1.7.0 $GTEST_HOME/gtest

RUN mkdir -p $GTEST_HOME/gtest/lib && \
    cd $GTEST_HOME/gtest/lib && \
    cmake .. && \
    make

# Install websockets
RUN git clone --branch v4.1-stable https://github.com/warmcat/libwebsockets.git /opt/libwebsockets && \
    cd /opt/libwebsockets && cmake . && \
    make && \
    make install &&\
    ldconfig

ADD container_aliases /root/.bash_aliases
WORKDIR /code
ENTRYPOINT ["/code/simulator/infra/docker/ubuntu/entry_point.sh"]
RUN echo "ALL            ALL = (ALL) NOPASSWD: ALL" >> /etc/sudoers

#those lines are appended from outside
#echo RUN groupadd `stat -c "%G" .` -g `stat -c "%g" .` >> ${WORKSPACE}/simulator/infra/docker/ubuntu/Dockerfile
#echo RUN useradd -ms /bin/bash `stat -c "%U" .` -u `stat -c "%u" .` -g `stat -c "%g" .` -G sudo >> ${WORKSPACE}/simulator/infra/docker/ubuntu/Dockerfile
