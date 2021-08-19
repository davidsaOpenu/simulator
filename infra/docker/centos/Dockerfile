FROM centos:centos7

# Install base packages
RUN yum group install -y "Development Tools"
RUN yum install -y vim \
  git \
  gcc \
  compat-gcc-44 \
  wget \
  make \
  cmake \
  valgrind \
  curl \
  sqlite-devel \
  libpng12 \
  unzip \
  xz \
  tar \
  epel-release \
  SDL-devel \
  libaio-devel \
  libvncserver-devel \
  openssl-devel \
  pciutils-devel \
  libpng-1.2.49-2.el6_7.x86_64 \
  libvirt-client \
  python-pip \
  python-setuptools \
  sudo \
  libjson-c-dev \
  flex \
  bison

# Clean yum cache (making the image smaller)
RUN yum clean all

# Install Bear
ENV BEAR_ZIP_FILENAME bear.zip
RUN wget https://github.com/rizsotto/Bear/archive/refs/tags/2.4.4.zip -O $BEAR_ZIP_FILENAME; \
  unzip $BEAR_ZIP_FILENAME; \
  rm $BEAR_ZIP_FILENAME && \
  cd Bear-2.4.4 && cmake . && \
  make install && \
  ln -s /usr/local/bin/bear /usr/bin/bear && \
  sed  -i -e "s/env\ python/env python2.7/g" /usr/local/bin/bear

# Install Python 2.7
RUN wget http://python.org/ftp/python/2.7.6/Python-2.7.6.tar.xz && \
  unxz Python-2.7.6.tar.xz && \
  tar -xf  Python-2.7.6.tar && \
  cd Python-2.7.6 && \
  ./configure --prefix=/usr/local --enable-unicode=ucs4 --enable-shared LDFLAGS="-Wl,-rpath /usr/local/lib" && \
  make && make altinstall

# Install Ansible
RUN yum install -y ansible


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
    make install && \
    ldconfig

ADD container_aliases /root/.bash_aliases
WORKDIR /code
ENTRYPOINT ["/code/simulator/infra/docker/centos/entry_point.sh"]
RUN echo "ALL            ALL = (ALL) NOPASSWD: ALL" >> /etc/sudoers

#those lines are appended from outside
#echo RUN groupadd `stat -c "%G" .` -g `stat -c "%g" .` >> ${WORKSPACE}/simulator/infra/docker/centos/Dockerfile
#echo RUN useradd -ms /bin/bash `stat -c "%U" .` -u `stat -c "%u" .` -g `stat -c "%g" .` -G wheel >> ${WORKSPACE}/simulator/infra/docker/centos/Dockerfile
