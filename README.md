# OpenJDK 25 for Linux/IA-64 (Itanium)

This branch ports OpenJDK 25 (`jdk-25-ga`) to Linux on Intel Itanium (IA-64). The last JDK
released for the platform was Java 6u45. Every commit on top of `jdk-25-ga` is one self-contained,
`IA-64:`-prefixed change, so the series can be applied as a patch set (it is meant for T2 SDE's
`package/java/openjdk`).

## What works

| JVM variant | What it is | State |
|---|---|---|
| `server` | Template interpreter, C1 and C2, tiered | Working. Faster than Java 6u45's IA-64 server VM on most of the benchmarks measured (DaCapo 9.12, microbenchmarks) |
| `client` | Template interpreter and C1 | Working |
| `core` | Template interpreter only, no JIT | Working |
| `zero` | HotSpot's portable C++ interpreter | Working; 10–50× slower than the JIT variants |

The IA-64 HotSpot backend is in `src/hotspot/cpu/ia64/` and `src/hotspot/os_cpu/linux_ia64/`.
The JIT's code generator (the encoder) comes from the IA-64 SpiderMonkey JIT carried by T2's
Firefox. Generated code is packed into bundles and instruction groups.

Limitations:

- **Garbage collectors:** Serial, Parallel and Epsilon only. G1, ZGC and Shenandoah need
  IA-64 barrier code that has not been written; disable them at configure time (below). Zero
  builds all of them.
- **`java.lang.foreign`** works through the libffi-based fallback linker, which configure enables
  by default on IA-64. There is no native IA-64 downcall ABI.
- **No Serviceability Agent** (`jhsdb`): `INCLUDE_SA` is off for IA-64.
- **No JVMCI** and no default CDS archive (cross builds skip generating it).
- Some jtreg tests fail for known reasons: IR-shape tests that expect vector instructions, tests
  whose flags or sizes assume x86 minimums, and a few others.

Tested on an HP Integrity rx2800 i2 (Itanium 9340, Tukwila), an HP Integrity rx2660 (Itanium 9020 Montecito) machine
and an SGI Altix 350 (Itanium 2, Madison). The OS is T2/Linux with an out-of-tree IA-64 kernel (7.x), glibc 2.43 and
16 KiB pages. Other distributions and page sizes are untested.

## Building

The JDK is cross-compiled from x86_64 Linux. 

### What you need

- **A boot JDK 24 or 25 for the build host**, e.g. Debian's `openjdk-25-jdk`.
- **An `ia64-linux-gnu` cross toolchain with C++**: binutils and GCC built against the target's
  glibc. GCC 15.2.0 and binutils 2.46 are what this branch is built with. GCC's own IA-64
  unwinder is used; do not configure GCC with `--with-system-libunwind`, since libunwind is
  broken on IA-64.
- **A sysroot copied from the target**, with headers and libraries for glibc, **libffi** (Zero
  and the fallback linker need it), zlib, freetype, fontconfig, libjpeg, libpng and the X11
  headers. X11 headers are needed even for a headless build. On T2, X11 lives in `/usr/X11R7`.
  Copy it with:

  ```
  rsync -azR root@target:/lib :/usr/lib :/usr/include :/usr/X11R7 sysroot-ia64/
  ```

  Refresh the sysroot after installing anything new on the target.

### Configure

```
SYSROOT=/path/to/sysroot-ia64
XTOOLS=/path/to/x-tools/bin              # contains ia64-linux-gnu-gcc etc.
IA64_FLAGS="-fno-selective-scheduling -fno-selective-scheduling2"

bash configure \
  --openjdk-target=ia64-linux-gnu \
  --with-boot-jdk=/usr/lib/jvm/java-25-openjdk-amd64 \
  --with-sysroot=$SYSROOT \
  --with-toolchain-path=$XTOOLS \
  --with-extra-path=$XTOOLS \
  --with-jvm-variants=server \
  --disable-jvm-feature-g1gc \
  --disable-jvm-feature-zgc \
  --disable-jvm-feature-shenandoahgc \
  --enable-headless-only \
  --with-debug-level=release \
  --disable-warnings-as-errors \
  --with-libffi=$SYSROOT/usr \
  --x-includes=$SYSROOT/usr/include \
  --x-libraries=$SYSROOT/usr/X11R7/lib \
  --with-extra-cflags="$IA64_FLAGS" \
  --with-extra-cxxflags="$IA64_FLAGS"
```

What the less obvious options are for:

- **`-fno-selective-scheduling -fno-selective-scheduling2`: required.** GCC enables selective
  scheduling by default at `-O2` on IA-64 (and on almost no other target), and that pass hits an
  internal compiler error building HotSpot (`sel-sched.cc`). Setting the flags explicitly
  overrides the target default.
- **The three `--disable-jvm-feature-*gc`:** required for `server`, `client` and `core`
  (see Limitations). Leave them out for `--with-jvm-variants=zero`.
- **`--x-includes`, `--x-libraries`:** libawt needs X11 headers even with
  `--enable-headless-only`, and T2 does not keep X11 in the default paths.
- **`--disable-warnings-as-errors`:** the cross GCC is newer than the compilers OpenJDK 25 is
  tested with, and its new diagnostics would otherwise stop the build.
- **`--with-debug-level=fastdebug`:** worth using during development. Assertions turn many
  IA-64 stack and register-stack errors into clean aborts.
- **`--build=x86_64-unknown-linux-gnu`:** add this when building under WSL. OpenJDK's
  `config.guess` otherwise takes the build machine for Windows.

### Build

```
make images JOBS=8
```

The image is in `build/linux-ia64-server-release/images/jdk/`; copy it to the target.
`make hotspot` rebuilds only `libjvm.so`.
To compile jtreg tests' native parts as well, add `build-test-hotspot-jtreg-native` and
`build-test-jdk-jtreg-native` to the targets.

Watch the memory use: a Zero build's `bytecodeInterpreter.cpp` alone needs about 1.7 GB to
compile, and running out of memory shows up as `internal compiler error: Bus error`, with no GCC backtrace, followed by
bogus assembler errors.

Building natively on IA-64 has not been tried. It should work with a boot JDK built by the cross
build, but would take hours.

## Running

Use the image as any JDK. Things that differ from x86:

- **Stacks:** an IA-64 thread has a memory stack and a register backing store. Each takes half of
  the thread stack, so `-Xss` and `ThreadStackSize` are larger than on x86 to give the same Java
  recursion depth.
- **Compressed oops** are off by default. Compressed class pointers are on.
- **Floating-point software-assist faults** fill the kernel log while Java programs run. Java
  6u45 causes them too. They cost under 1% of run time in the workloads measured.

For more on OpenJDK itself, see the upstream README below.

---

# Welcome to the JDK!

For build instructions please see the
[online documentation](https://openjdk.org/groups/build/doc/building.html),
or either of these files:

- [doc/building.html](doc/building.html) (html version)
- [doc/building.md](doc/building.md) (markdown version)

See <https://openjdk.org/> for more information about the OpenJDK
Community and the JDK and see <https://bugs.openjdk.org> for JDK issue
tracking.
