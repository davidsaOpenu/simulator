FROM ubuntu:18.04

# Install base packages
ENV DEBIAN_FRONTEND noninteractive # to avoid keyboard conf questions during the update
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
  sudo \
  bear \
  qemu \
  qemu-kvm \
  valgrind \
  curl



# Install Ansible
RUN apt-get -y install ansible



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


# prepare image on fly
ARG IMAGE_URL="https://cloud-images.ubuntu.com/bionic/20180917/bionic-server-cloudimg-amd64.img"
ARG IMAGE="bionic-server-cloudimg-amd64.img"
RUN wget $IMAGE_URL
RUN apt-get update -y
RUN apt-get install -y libguestfs-tools
ENV LIBGUESTFS_BACKEND direct



RUN ssh-keygen -t rsa -N "" -f /root/.ssh/id_rsa
RUN apt-get -y install linux-image-generic # https://bugzilla.redhat.com/show_bug.cgi?id=1542892
RUN LIBGUESTFS_DEBUG=1 LIBGUESTFS_TRACE=1 \
    /usr/bin/virt-customize --ssh-inject root \ 
                            --root-password password:PASSWORD \
                            --run-command 'useradd -m -p esd -s /bin/bash esd && usermod -a -G sudo esd' -a $IMAGE

# resize image
ARG TMP="tmp_file"
RUN qemu-img info $IMAGE && \
    qemu-img resize $IMAGE +20G && \
    cp $IMAGE $TMP && \
    virt-resize --expand /dev/sda1 $TMP $IMAGE && \
    qemu-img info $IMAGE && \
    virt-filesystems --long -h --all -a $IMAGE && \
    rm $TMP

RUN mv $IMAGE hda_clean.qcow2

# Clean package cache (making the image smaller)
RUN apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /code
ADD container_aliases /root/.bash_aliases
#ENTRYPOINT ["/code/simulator/infra/docker/ubuntu/entry_point.sh"]
RUN echo "ALL            ALL = (ALL) NOPASSWD: ALL" >> /etc/sudoers

#those lines are appended from outside
#echo RUN groupadd `stat -c "%G" .` -g `stat -c "%g" .` >> ${WORKSPACE}/simulator/infra/docker/ubuntu/Dockerfile
#echo RUN useradd -ms /bin/bash `stat -c "%U" .` -u `stat -c "%u" .` -g `stat -c "%g" .` -G sudo >> ${WORKSPACE}/simulator/infra/docker/ubuntu/Dockerfile
RUN groupadd datos -g 1000
RUN useradd -ms /bin/bash datos -u 1000 -g 1000 -G sudo
