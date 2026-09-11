The local SDK Docker recipes build ndl/arm-gcc and ndl/sdk.

From this directory:

~~~sh
make docker
~~~

The compiler recipe retains the upstream ndless/gcc base image. The original upstream SDK image is described at https://registry.hub.docker.com/repos/ndless/ndless-sdk.

The wrappers in bin-docker run the local ndl/sdk image. These recipes do not publish images.
