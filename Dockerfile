FROM alpine:latest

ENV JAVA_HOME /usr/lib/jvm/java-1.8-openjdk

RUN apk update \
  && apk --update --upgrade add \
      bash \
      ca-certificates \
      curl \
      g++ \
      git \
      musl \
      openjdk8 \
      pkgconfig \
      python \
      unzip \
      zip \
      zlib-dev \
  && curl -o /etc/apk/keys/sgerrand.rsa.pub https://raw.githubusercontent.com/sgerrand/alpine-pkg-glibc/master/sgerrand.rsa.pub \
  && curl -LO https://github.com/sgerrand/alpine-pkg-glibc/releases/download/2.23-r3/glibc-2.23-r3.apk \
  && apk add glibc-2.23-r3.apk \
  && git clone --depth=1 https://github.com/mrdomino/bazel /bazel \
  && cd /bazel \
  && ./compile.sh \
  && ln /bazel/output/bazel /usr/local/bin/bazel \
  && rm -rf /bazel \
  && addgroup -g 1001 dev \
  && adduser -G dev -u 1001 -D dev \
  && mkdir /src \
  && chown dev:dev /src

USER dev
WORKDIR /src
RUN bazel version

CMD bash
