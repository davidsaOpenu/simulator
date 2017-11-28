FROM tianon/centos:6.5

# Install base packages
RUN yum install -y vim \
  gcc \
  wget \
  make \
  cmake \
  gcc-c++ \
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
  pciutils-devel \
  libpng-1.2.49-2.el6_7.x86_64 \
  sshpass && \
  yum install -y ansible

# Clean yum cache (making the image smaller)
RUN yum clean all

# Install Python 2.7
RUN wget http://python.org/ftp/python/2.7.6/Python-2.7.6.tar.xz && \
  unxz Python-2.7.6.tar.xz && \
  tar -xf  Python-2.7.6.tar && \
  cd Python-2.7.6 && \
  ./configure --prefix=/usr/local --enable-unicode=ucs4 --enable-shared LDFLAGS="-Wl,-rpath /usr/local/lib" && \
  make && make altinstall


# Install Gtest
ENV GTEST_HOME /opt
RUN wget -qO- https://github.com/google/googletest/archive/release-1.7.0.tar.gz | tar xz -C $GTEST_HOME/ && \
  mv $GTEST_HOME/googletest-release-1.7.0 $GTEST_HOME/gtest

RUN mkdir -p $GTEST_HOME/gtest/lib && \
  cd $GTEST_HOME/gtest/lib && \
  cmake .. && \
  make

# Install Bear
ENV BEAR_ZIP_FILENAME bear.zip
RUN wget https://github.com/rizsotto/Bear/archive/master.zip -O $BEAR_ZIP_FILENAME; \
  unzip $BEAR_ZIP_FILENAME; \
  rm $BEAR_ZIP_FILENAME && \
  cd Bear-master && cmake . && \
  make install && \
  ln -s /usr/local/bin/bear /usr/bin/bear && \
  sed  -i -e "s/env\ python/env python2.7/g" /usr/local/bin/bear

ADD container_aliases /root/.bash_aliases
ADD run-ansible /script/run-ansible
ADD entry_point.sh /script/entry_point.sh
WORKDIR /code
ENTRYPOINT ["/script/entry_point.sh"]

