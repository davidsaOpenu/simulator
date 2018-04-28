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
  lsof


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
RUN git clone --branch v2.4-stable https://github.com/warmcat/libwebsockets.git /opt/libwebsockets && \
    cd /opt/libwebsockets && cmake . && make && make install

# Old Monitor Support
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    libpixman-1-dev libxft-dev libaudio-dev libcups2-dev libcups2-dev libcups2 \
    libjpeg62 libaudio2 libfontconfig1 libfreetype6 libice6 libice-dev libsm6 \
    libsm-dev libxcursor1 libxcursor-dev libxft2 libxi6 libxi-dev libxinerama1 \
    libxinerama-dev libxrandr2 libxrandr-dev fontconfig libxmu6 libxmu-dev \
    liblcms1 liblcms-dev libfontconfig1-dev

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    qtdeclarative5-dev qt5-default qttools5-dev-tools

# Copy our folder into the scripts folder
ADD docker /scripts
ADD env.sh /scripts
RUN chmod -R 777 /scripts

# Remove kernel scripts
RUN rm -rf /etc/kernel/postinst.d/*

# Install python libraries
RUN pip install -r /scripts/requirements.txt

VOLUME /code
WORKDIR /code

ENTRYPOINT ["/scripts/entrypoint.sh"]
CMD ["bash"]
