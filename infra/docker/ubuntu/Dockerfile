FROM ubuntu:14.04

# Install base packages
RUN apt-get update && apt-get -y install software-properties-common vim wget build-essential gcc-4.6 cmake g++ unzip libsqlite3-dev libxen-dev zlib1g-dev libsdl-image1.2-dev libgnutls-dev libvncserver-dev libpci-dev libaio-dev sshpass


# Install build packages
RUN apt-get update && apt-get -y install bear qemu qemu-kvm valgrind curl
#RUN apt-get update && apt-get -y install qt3-dev-tools libqt3-mt-dev

# Install Ansible
RUN add-apt-repository -y ppa:ansible/ansible && apt-get update && apt-get -y install ansible

# Clean APT cache (making the image smaller)
RUN apt-get clean && rm -rf /var/lib/apt/lists/*

# Install Gtest
ENV GTEST_HOME /opt
RUN wget -qO- https://github.com/google/googletest/archive/release-1.7.0.tar.gz |tar xz -C $GTEST_HOME/ && mv $GTEST_HOME/googletest-release-1.7.0 $GTEST_HOME/gtest
RUN mkdir -p $GTEST_HOME/gtest/lib && cd $GTEST_HOME/gtest/lib && cmake .. && make

ADD container_aliases /root/.bash_aliases
ADD entry_point.sh /script/entry_point.sh
WORKDIR /code
ENTRYPOINT ["/script/entry_point.sh"]

