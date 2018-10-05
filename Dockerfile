FROM alpine:3.8

ADD https://raw.githubusercontent.com/davido/bazel-alpine-package/master/david@ostrovsky.org-5a0369d6.rsa.pub \
    /etc/apk/keys/david@ostrovsky.org-5a0369d6.rsa.pub
ADD https://github.com/davido/bazel-alpine-package/releases/download/0.15.2/bazel-0.15.2-r0.apk \
    /tmp/bazel-0.15.2-r0.apk

RUN apk update \
  && apk --update --upgrade add \
      bash \
      /tmp/bazel-0.15.2-r0.apk \
      g++ \
      git \
      musl \
      python

RUN export uid=1000 gid=1000 \
  && echo $uid $gid \
  && addgroup -g ${gid} dev \
  && adduser -G dev -u ${uid} -D dev \
  && mkdir /src \
  && chown dev:dev /src

ENV JAVA_HOME /usr/lib/jvm/java-1.8-openjdk

USER dev
WORKDIR /src
RUN bazel version

CMD bash
