FROM alpine
RUN apk add --no-cache make g++ linux-headers
COPY . /build
WORKDIR /build
RUN make RELEASE=1
