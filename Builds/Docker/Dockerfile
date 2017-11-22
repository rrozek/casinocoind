# rippled

# use the ubuntu base image
FROM ubuntu
MAINTAINER Andre Jochems andre@casinocoin.org

# make sure the package repository is up to date
RUN apt-get update
RUN apt-get -y upgrade

# install the dependencies
RUN apt-get -y install git scons pkg-config protobuf-compiler libprotobuf-dev libssl-dev libboost1.55-all-dev

# download source code from official repository
RUN git clone https://github.com/casinocoin/casinocoind.git src; cd src/; git checkout master

# compile
RUN cd src/; scons build/casinocoind

# move to root directory and strip
RUN cp src/build/casinocoind casinocoind; strip casinocoind

# copy default config
RUN cp src/doc/casinocoind-example.cfg casinocoind.cfg

# clean source
RUN rm -r src

# launch rippled when launching the container
ENTRYPOINT ./casinocoind
