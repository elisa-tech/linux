.. SPDX-License-Identifier: (GPL-2.0+ OR CC-BY-4.0)

=================================================
Minimizing a Linux kernel for a single workload
=================================================

:Authors: - ELISA Aerospace Working Group

This document describes a repeatable workflow for shrinking a Linux kernel
down to what a single, known workload actually needs, and for measuring the
result at each step. The goal is a *process* any integrator can apply to their
own hardware, board support package, and workload -- not a specific
configuration to copy.

A smaller kernel carries less code to build, boot, review, and maintain. How
much that matters depends on the situation: a developer trimming a test kernel
cares about build time, while an integrator preparing a kernel for analysis or
for a safety- or security-critical deployment cares about how much code must be
understood and justified. This guide stays with the mechanics of trimming and
measuring; where a particular step is useful for one of those analyses, it is
called out as an example, not as a requirement.

This guide builds on, rather than repeats, several existing documents:

* :doc:`quickly-build-trimmed-linux` for the mechanics of getting, building,
  and installing a kernel. This guide assumes you have followed it at least
  once and reuses its sections by name below -- in particular its "Download the
  sources" and "Define the build configuration for your kernel" steps. Note
  that guide targets a kernel built **natively for the machine you are on**; the
  cross-compiled, boot-in-QEMU angle used here is the main thing this guide adds
  on top of it (see `Building: native vs cross-compiled`_).
* :doc:`workload-tracing` for discovering the subsystems a workload uses, and
  :doc:`/trace/ftrace` for tracing the kernel functions it reaches.
* :doc:`/dev-tools/kcov` and :doc:`/dev-tools/gcov` for coverage collection.
* :doc:`/kbuild/kconfig` and :doc:`/kbuild/kconfig-language` for the
  configuration mechanics, and :doc:`/kbuild/kbuild` for build variables such
  as ``ARCH`` and ``CROSS_COMPILE``.

Baseline scenario
=================

The walkthrough assumes the simplest possible launch model, which keeps the
chain from "what the workload needs" to "what the kernel must provide" short:

   **The kernel launches the workload directly as PID 1. There is no init
   system, no service manager, and no general multi-user user space.**

For the initramfs example below, the kernel runs ``/init`` by default; use
``rdinit=<path>`` to select a different program in that archive. ``init=<path>``
selects init after mounting a real root filesystem; it does not override a
working initramfs ``/init``.

With this baseline, the user space is essentially the application plus the C
library it links against. The decision cascades into the configuration: no
service management, no getty/login, only the library and kernel features the
application itself exercises.

The worked example boots an ``arm64`` kernel under QEMU with a static "hello
world" program as PID 1. See `A minimal PID 1 workload`_ below.

The workflow
============

A practical way to approach minimization is to pick a direction and iterate:
**change -> build -> test -> measure -> record**. Do not remove things in bulk;
remove or add one group at a time and confirm the workload still runs before
measuring.

Prerequisites
-------------

Follow "Install build requirements" in :doc:`quickly-build-trimmed-linux`
for the native build tools. On a Debian/Ubuntu x86_64 host, this example adds::

    sudo apt install gcc-aarch64-linux-gnu qemu-system-arm file

The cross-compiler package brings in matching binutils and, with the normal
recommended packages, the target libc headers and static library. ``qemu-system-arm`` provides
``qemu-system-aarch64``; ``file`` checks the application binary. Other tools
are introduced where they are used.

Get the sources
---------------

Retrieve the kernel sources exactly as described in
:doc:`quickly-build-trimmed-linux` ("Download the sources"). For a workload
that must track a long-lived kernel, follow that guide's instructions for
adding a stable/longterm branch rather than building bleeding-edge mainline, so
you measure against a kernel with a long maintenance window. All commands below
run from the resulting source tree.

Building: native vs cross-compiled
----------------------------------

The trimmed-build guide builds a kernel **natively, for the machine you are
running on**, and installs it there. This guide instead builds for a target
architecture and boots the result under QEMU, so nothing is installed on your
host. The mechanics of cross-building are not specific to minimization and are
documented in :doc:`/kbuild/kbuild` (the ``ARCH`` and ``CROSS_COMPILE``
variables); set them once for the worked example::

    export ARCH=arm64
    export CROSS_COMPILE=aarch64-linux-gnu-

Every ``make`` below (``defconfig``, ``tinyconfig``, ``olddefconfig``,
``Image``) then honours them. For a native arm64 build, omit ``CROSS_COMPILE`` and use the native tools.
An x86 kernel or application cannot substitute for the arm64 images in the
QEMU commands. Other architectures need their own image target, console,
platform support, and emulator. The trimmed-build guide's "Install your kernel"
and Secure Boot steps do not apply to this QEMU experiment.

Build in the source tree, as in the trimmed-build guide. Before changing a
configuration or cleaning the build, save its resolved ``.config``, kernel
image, measurements, and boot log under an iteration-specific name. Keep the
matching ``vmlinux`` too when collecting DWARF counts or coverage. ``make clean``
preserves ``.config`` but removes build artifacts; do not use ``mrproper``
between iterations.

Iterate toward minimal
----------------------

Pick a direction. These mirror the two bases the trimmed-build guide's "Define
the build configuration for your kernel" section describes -- starting from an
existing (large) config versus starting from scratch:

* **Top-down** -- start from a working configuration and remove features,
  re-testing after each removal. For this arm64 QEMU example, build
  ``defconfig`` as the baseline::

      make defconfig
      make clean
      make -j"$(nproc)" Image

  Follow `A minimal PID 1 workload`_ to compile, pack, and boot the application.
  Require ``hello world`` and no panic before treating this as a known-good
  baseline. For a native target, the trimmed-build guide's
  ``localmodconfig``/``olddefconfig`` approach is another starting point; do not
  use the host's configuration for an unrelated cross-compiled target.
  Save the baseline results, then change one group::

      cp .config config.before
      ./scripts/config -d IPV6
      make olddefconfig
      make clean
      make -j"$(nproc)" Image

* **Bottom-up** -- start from almost nothing and add only what the workload
  needs::

      make tinyconfig
      ./scripts/config \
          -e PRINTK -e TTY -e SERIAL_AMBA_PL011 -e SERIAL_AMBA_PL011_CONSOLE \
          -e BINFMT_ELF -e BLK_DEV_INITRD
      make olddefconfig
      make clean
      make -j"$(nproc)" Image

  Beyond the serial options, this example needs ``PRINTK``, ``BLK_DEV_INITRD``,
  and ``BINFMT_ELF`` for visible output, the external initramfs, and ELF
  execution. Arm64 selects its device-tree, interrupt-controller, timer, and
  AMBA support; those are not supplied by the
  serial driver. This is a small bootable starting point for QEMU ``virt``,
  not a universally minimal or production-ready configuration. Inspect the
  resolved config: ``scripts/config`` edits requests, and unmet dependencies
  can cause ``olddefconfig`` to discard them. Pack and boot the same PID 1
  application as above; building ``defconfig`` first is not necessary for this
  direction.

To browse the configuration tree interactively with help text in view, use
``make menuconfig`` (needs ``libncurses-dev``), as described in the
trimmed-build guide's "Individual adjustments" section.

After each change, repeat the boot command in `A minimal PID 1 workload`_ with
the rebuilt image and the same application archive. For this example, require
``hello world`` and no panic during a recorded observation interval. A QEMU
timeout alone is not success: a failed boot can also hang. Capture the console
output and record the delta against the config saved before the change::

    scripts/diffconfig config.before .config > config.delta

Do not assume that ``tinyconfig`` to ``defconfig`` measures one subsystem:
optimization policy, platform drivers, and many unrelated settings change too.
For a single-group comparison, save the last resolved config as
``config.before``, change just that group, and clean/rebuild.

Deciding what to remove
=======================

Follow :doc:`workload-tracing` for syscall and kernel-path collection,
including "Tracing a minimal user space" for this PID 1 launch model and
"Mapping observations to configuration" for source/Kconfig analysis.
Re-test each removal with the same PID 1 workload and record the resolved
configuration delta.

Separating code that is present from code that runs
---------------------------------------------------

????  thinking the sloc tool might want the option to print the file / function listing as well beyond just the summary.  thinking a new arg

??? plus this whole section feels like it should just be in the counting kernel sloc below.  or at best a ref to it.

The SLOC metrics below describe built code, not execution. For coverage setup,
collection, and limitations, use :doc:`/dev-tools/gcov` (execution counts) and
:doc:`/dev-tools/kcov` (task-scoped kernel PCs). `Dumping kernel coverage (kcov)`_
below covers only the adaptations needed for this PID 1 example.

The DWARF SLOC metric needs a ``vmlinux`` built with debug information.
Enable it before rebuilding and counting; this does not instrument execution.
Follow the trimmed-build guide's "Debug symbols" section to enable it. Only the
DWARF line table is needed here, so the symbol-table options that section also
turns on (``KALLSYMS``, ``KALLSYMS_ALL``) are left out::

    ./scripts/config -d DEBUG_INFO_NONE \
        -e DEBUG_KERNEL -e DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT \
        -d DEBUG_INFO_BTF -d DEBUG_INFO_SPLIT
    make olddefconfig
    make clean
    make -j"$(nproc)" Image

Selecting the DWARF choice enables ``CONFIG_DEBUG_INFO`` through Kconfig; do not
rely on setting that hidden symbol directly. BTF is unnecessary for this
metric. Split DWARF needs separate files and is not used in this walkthrough.
Check that ``CONFIG_DEBUG_INFO=y`` survives ``olddefconfig``. Reapply these
settings after starting from ``tinyconfig``, which resets debug options, and
rebuild before measuring. Keep this debug policy consistent across rows.

Save the target configuration and results before enabling instrumentation.
Record the collection scope: unobserved boot, interrupt, other-task, and error
paths may still be required. Use workload requirements and source analysis to
review removal candidates, then restore the target configuration and
clean/rebuild for uninstrumented testing.

Measuring
=========

Capture the same measurements at each iteration so configurations are
comparable across contributors and architectures:

* **Source lines of code (SLOC)** -- a proxy for the amount of code to review.
  Report two complementary numbers (see `Counting kernel SLOC`_): a DWARF-based
  estimate of source lines represented in the linked image, and a ``cloc``
  count of whole compiled source files. Neither measures runtime coverage.
* **Image size** -- a sanity bound only, not a score; it depends on
  compression, compiler version, and optimization level.
* **Boots? / workload passes?** -- the configuration is only valid if the
  target still runs.

For each row, keep the resolved config and delta, source revision (and patches),
application source/binary and libc, archive contents, compiler and counter
versions, exact build and QEMU commands, observation interval and boot log,
image sizes, and metric stdout/stderr. Use the same inputs and instrumentation
policy across comparisons. These QEMU results do not validate target hardware,
timing, failure recovery, or a larger workload.

Counting kernel SLOC
--------------------

Install ``cloc`` for compiled-source counts and ensure ``python3`` is available
for generating the compilation database. On Debian/Ubuntu::

    sudo apt install cloc python3

Use ``scripts/kernel-sloc``; ``scripts/kernel-sloc --help`` lists its modes,
requirements, and tool overrides. Successful runs print one integer on stdout
and a human-readable summary on stderr. Errors exit non-zero without a count.
It offers two kernel counts:

* ``dwarf`` -- resolves DWARF line-table addresses in a ``vmlinux`` built with
  ``CONFIG_DEBUG_INFO`` to distinct full-path ``file:line`` pairs using GNU
  ``objdump`` and ``addr2line``. Optimization, inlining, and the quality of the
  debug information affect this estimate; it is not an exact mapping of every
  instruction to all contributing source lines.
* ``kernel`` -- wraps ``cloc`` over the unique sources listed in
  ``compile_commands.json``. It counts code lines in whole files, including
  inactive preprocessor branches, but does not follow included headers. It is
  therefore not a strict upper bound on the DWARF count.

For comparable results, record the configuration, kernel and tool versions,
compiler options, and whether loadable modules are included. ``dwarf vmlinux``
measures only that ELF, not separate modules; ``kernel`` counts whatever sources
are listed in its database, including generated sources, assembly, and host
helpers when present. ``Image`` alone does not build all configured loadable
modules; run ``make modules`` and record them separately if they are part of the
workload. A database is an inventory of compilations, not proof that every file
is linked into the target image. Use ``make clean`` before each changed build
and regenerate the database after building. Old ``.cmd`` files or a stale
database can otherwise include sources from a previous configuration.

If the database is missing, ``kernel`` runs the generator automatically. For
an out-of-tree build, use the generator's ``-d`` option and pass the build
directory to the counter.

.. code-block:: sh

    # debug-info estimate: distinct source lines represented in vmlinux
    scripts/kernel-sloc dwarf vmlinux > sloc-dwarf.txt

    # regenerate the database only after completing this clean build
    python3 scripts/clang-tools/gen_compile_commands.py
    scripts/kernel-sloc kernel . > sloc-compiled.txt

For a cross build, select matching tools with
``CROSS_COMPILE=aarch64-linux-gnu- scripts/kernel-sloc dwarf vmlinux``.
``OBJDUMP`` and ``ADDR2LINE`` override the individual tool names; if only
``OBJDUMP`` is set to a conventional name ending in ``objdump``, the script
uses the same prefix for ``addr2line``.

Measuring the image
-------------------

Track image size to stay under a ceiling, not as a score::

    make Image.gz
    stat -c%s arch/arm64/boot/Image      # raw
    stat -c%s arch/arm64/boot/Image.gz   # compressed

Example progression
--------------------

Starting from the bootable ``tinyconfig`` recipe and adding one subsystem group
at a time
toward ``defconfig`` (the groups are what ``scripts/diffconfig`` shows between
the two), each step still booting the PID 1 workload, illustrates what each
group costs in code. Recording the SLOC delta per group is the useful output:
it shows which subsystems dominate the code base for a given workload, and a
large single-group jump is a prompt to check whether that group is really
required. The figures depend entirely on kernel version, architecture, and
toolchain, so generate your own with the steps above rather than reusing
published numbers.

Reducing the user space too
============================

For this small static program, the linked C library can dominate the user-space
binary size. A different libc (for example musl) can change both size and
syscall behavior; fewer library entry points do not automatically mean fewer
required kernel features. Rebuild, trace, and re-test the same workload before
accepting a library change.

???? can this source tree counting detail actually just be in the syntax of the tool or comment header in the tool?  doesnt feel needed here

Source-tree counts can provide context for user-space alternatives (use paths
to unpacked source trees):

.. code-block:: sh

    scripts/kernel-sloc files   ainit.c  # the application alone
    scripts/kernel-sloc tree    /path/to/busybox  # a minimal multi-tool
    scripts/kernel-sloc tree    /path/to/systemd  # a full init system

``files`` measures only the named application source; it omits linked libc.
``tree`` counts whole C/C++/header trees, including code not used by a configured
build. These are not comparable deployed-footprint totals or full component
counts for projects using other languages. For an actual user-space comparison,
record the deployed binaries and libraries, build options, and their sizes as
well as a consistently scoped source metric.

A minimal PID 1 workload
========================

This is the "hello world" program booted as PID 1 in the walkthrough. There is
no init system, no shell, and no service manager -- the kernel execs it directly
as ``/init`` from an initramfs.

.. code-block:: c

    #include <stdio.h>
    #include <unistd.h>

    int main(void)
    {
        printf("hello world\n");
        fflush(stdout);

        /* PID 1 must never return. Idle forever. */
        for (;;)
            pause();

        return 0; /* not reached */
    }

Save it as ``ainit.c`` and build it statically so the kernel can exec it
with no dynamic loader or libraries present in the rootfs::

    aarch64-linux-gnu-gcc -static -O2 -o ainit ainit.c
    file ainit     # -> ELF 64-bit ... ARM aarch64 ... statically linked ...
    aarch64-linux-gnu-readelf -l ainit   # no INTERP segment
    aarch64-linux-gnu-readelf -d ainit   # no NEEDED entries

Pack the application as ``/init`` in a gzip-compressed ``newc`` archive.
Install ``cpio`` if needed; ``gzip`` is normally present on the build host::

    sudo apt install cpio
    mkdir -p rootfs
    cp ainit rootfs/init
    chmod 0755 rootfs/init
    ( cd rootfs && find . | cpio -o -H newc ) | gzip > initramfs.cpio.gz

With ``CONFIG_INITRAMFS_SOURCE=""``, the default built-in initramfs supplies
``/dev/console``, so this external archive only needs ``/init``.

Check the resolved configuration for built-in ``PRINTK``, ``TTY``,
``SERIAL_AMBA_PL011``, ``SERIAL_AMBA_PL011_CONSOLE``, ``BLK_DEV_INITRD``,
``RD_GZIP``, and ``BINFMT_ELF`` as in the bottom-up recipe. Boot the
rebuilt image with the same application archive::

    qemu-system-aarch64 -machine virt -cpu cortex-a57 -nographic \
        -kernel arch/arm64/boot/Image \
        -initrd initramfs.cpio.gz \
        -append 'console=ttyAMA0'

There is no ``root=`` and no disk image -- the initramfs is the root and the
kernel runs ``/init`` from it by default. The program prints ``hello world`` and
then parks in ``pause()`` forever, so it never triggers the ``Attempted to kill
init!`` panic. Close QEMU with ``Ctrl-a x``.

Probing a kernel feature
------------------------

To test a configuration decision, use a complete variant of ``ainit.c`` that
opens an IPv4 UDP socket. Save this as ``ainit-net.c``:

.. code-block:: c

    #include <errno.h>
    #include <stdio.h>
    #include <string.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>

    int main(void)
    {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        int error = fd < 0 ? errno : 0;

        printf("hello world\n");
        if (fd >= 0) {
            printf("NET-PROBE: IPv4/UDP socket available\n");
            close(fd);
        } else {
            printf("NET-PROBE: failed: errno=%d (%s)\n", error,
                   strerror(error));
        }
        fflush(stdout);
        for (;;)
            pause();
        return 0;
    }

Build the variant, replace the staged ``/init``, and repack::

    aarch64-linux-gnu-gcc -static -O2 -o ainit-net ainit-net.c
    cp ainit-net rootfs/init
    chmod 0755 rootfs/init
    ( cd rootfs && find . | cpio -o -H newc ) | gzip > initramfs-net.cpio.gz

Use the QEMU command above with ``-initrd initramfs-net.cpio.gz`` for
each selected kernel. Keep the hello-world archive unchanged. Expected results:

* The baseline with built-in ``NET=y`` and ``INET=y`` can create the socket.
* The bootable tiny recipe with ``NET`` disabled fails (normally ``ENOSYS``).
* ``NET=y`` without ``INET`` cannot provide this address family (normally
  ``EAFNOSUPPORT``); it may still provide other networking features.

Starting from the bootable tiny configuration, save its results before adding
IPv4 and rebuilding::

    cp .config config.before
    ./scripts/config -e NET -e INET
    make olddefconfig
    make clean
    make -j"$(nproc)" Image

Record actual errno, resolved configuration, and console output. Creating a
socket does not test packet I/O, routing, network interfaces, or application
protocols. This program reports an observation; a test harness must compare it
to the expected result. Any failure is not by itself proof that all networking
is absent.

Dumping kernel coverage (kcov)
------------------------------

??? make this section complete vs just the bulleted list.  so someone could read the kcov doc but have a complete working example

The hello-world program above does **not** collect coverage. For the ioctl
interface and a complete collector, use the "Coverage collection" example in
:doc:`/dev-tools/kcov` rather than expecting a config switch to add this feature
to ``ainit``. Save the known-good target configuration and results before
building with instrumentation::

    cp .config config.before
    ./scripts/config -e KCOV -e KCOV_INSTRUMENT_ALL -e DEBUG_FS
    make olddefconfig
    make clean
    make -j"$(nproc)" Image

Check ``CONFIG_KCOV=y`` and ``CONFIG_DEBUG_FS=y`` after dependency resolution.
To integrate the canonical collector into this PID 1 launch model:

#. Add ``/sys/kernel/debug`` to the staging directory with
   ``mkdir -p rootfs/sys/kernel/debug``. Mount debugfs from the application
   with ``mount(2)`` before opening ``/sys/kernel/debug/kcov``; this needs no host mount or guest shell.
#. Preserve the collector's setup/error checks. In a PID 1 program, report
   setup failures and idle instead of calling ``exit()`` and panicking. A failed
   mount, open, ioctl, or mmap is not a valid zero-coverage result.
#. Follow its enable/reset/target-syscall sequence. A library call can avoid
   kernel entry; choose an explicit syscall when testing a kernel path. Snapshot
   the entry count and disable collection before printing; check buffer limits
   and record truncation. Print all collected PCs if a full trace is needed,
   not just a few preview entries.
#. Compile the collector statically, copy it to ``rootfs/init``, and repack
   using the ``cpio`` command above. Boot it with the measurement kernel.
   Retain ``vmlinux`` and the raw console log. Use an explicit observation interval and check setup success.

Resolve captured built-in kernel PCs against that build's ``vmlinux`` with the
matching ``aarch64-linux-gnu-addr2line -f -i -e vmlinux``. KCOV can
canonicalize relocation itself on supported architectures; for a simple
measurement setup, also boot with ``nokaslr``. Do not apply an assumed relocation
adjustment or use a different build's ELF. Modules require their own ELF and
load-address treatment.

PC entries can repeat and do not constitute branch or whole-kernel structural
coverage. As described in `Separating code that is present from code that runs`_,
normal task collection misses boot, other tasks, and many asynchronous paths.
Use the results to focus investigation, not to label unobserved code dead,
deactivated, or safe to remove.

References
==========

* :doc:`quickly-build-trimmed-linux` -- building and installing a kernel.
* :doc:`workload-tracing` -- discovering subsystems used by a workload.
* :doc:`/trace/ftrace` -- in-kernel function tracing, for seeing which kernel
  functions a workload reaches.
* :doc:`/dev-tools/kcov`, :doc:`/dev-tools/gcov` -- coverage collection.
* :doc:`/kbuild/kconfig`, :doc:`/kbuild/kconfig-language` -- configuration;
  :doc:`/kbuild/kbuild` -- ARCH / CROSS_COMPILE and other build variables.
* ``scripts/kernel-sloc`` -- the SLOC counter used for the metrics above.
* ``scripts/diffconfig``, ``scripts/bloat-o-meter`` -- related size/diff
  helpers.
* `Kernel Size Reduction Work <https://elinux.org/Kernel_Size_Reduction_Work>`_
  -- the earlier tinification effort that introduced ``make tinyconfig``, for
  background on which options dominate a small kernel.
