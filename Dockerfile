FROM alpine:3.4

ENV JAVA_HOME /usr/lib/jvm/java-1.8-openjdk

RUN apk update \
  && apk --update --upgrade add \
      bash \
      ca-certificates \
      curl \
      g++ \
      git \
      linux-headers \
      musl \
      openjdk8 \
      pkgconfig \
      python \
      unzip \
      zip \
      zlib-dev \
  && curl -o /etc/apk/keys/sgerrand.rsa.pub https://raw.githubusercontent.com/sgerrand/alpine-pkg-glibc/master/sgerrand.rsa.pub \
  && curl -LO https://github.com/sgerrand/alpine-pkg-glibc/releases/download/2.23-r3/glibc-2.23-r3.apk \
  && apk add glibc-2.23-r3.apk

#ENV BAZEL_VERSION 0.3.2

RUN git clone https://github.com/mrdomino/bazel /bazel \
  && cd /bazel \
  && ./compile.sh \
  && ln /bazel/output/bazel /usr/local/bin/bazel \
  && rm -rf /bazel

RUN export uid=1000 gid=1000 \
  && echo $uid $gid \
  && addgroup -g ${gid} dev \
  && adduser -G dev -u ${uid} -D dev \
  && mkdir /src \
  && chown dev:dev /src

USER dev
WORKDIR /src
RUN bazel version

CMD bash
