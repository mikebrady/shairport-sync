
To produce a PDF manual, issue the command "make pdf" after running
`./configure'.

				 -*-

If you do not wish to build the C++ components, use:

./configure --disable-cxx

				 -*-

The `examples' subdirectory contains some examples of using libconfig
from C and C++.

				 -*-

Flex and Bison are not required in order to build this
package. However, you will need them if you modify the .l or .y files. See
the list of required build tool versions below.

				 -*-

To successfully compile using MinGW (with gcc 4.4.0 or later), it may
be necessary to modify the file "mingw32\4.4.0\libstdc++.la", changing line 11
from:

library_names='libstdc++.dll.a'

to:

library_names='libstdc++.a'

(See http://trac.osgeo.org/geos/ticket/282 for more information).

				 -*-

To cross-compile for Android:

1) Create a standalone toolchain using the 'make-standalone-toolchain.sh'
script included with the Android NDK. For example:

$ /pkg/android-ndk-r9d/build/tools/make-standalone-toolchain.sh \
  --platform=android-19 \
  --install-dir=/pkg/android-toolchain-19 \
  --system=linux-x86_64 \
  --arch=arm

2) Add the standalone toolchain's bin directory to the front of the path:

$ export PATH=/pkg/android-toolchain-19/bin:$PATH

3) Configure libconfig for cross-compilation:

$ ./configure --host=arm-linux-androideabi \
  --with-sysroot=/pkg/android-toolchain-19/sysroot

4) Build libconfig

$ make

				 -*-

Build tools dependencies. (These are only required if you are a maintainer.)

Autoconf 2.69
Automake 1.14.1
Bison 3.0.2
Flex 2.5.39
Libtool 2.4.2

				 -*-

Libconfig may be found on github at https://github.com/hyperrealm/libconfig



For some reason, we need to symlink /pkg/android-toolchain-19/sysroot/pkg -> /pkg
