# AdaptivePerf
[![License: GNU GPL v2 only](https://img.shields.io/badge/license-GNU%20GPL%20v2%20only-blue)]()
[![Version: 0.1.dev](https://img.shields.io/badge/version-0.1.dev-red)]()

A comprehensive profiling tool with custom-patched Linux ```perf``` as its main foundation.

## Disclaimer
This is currently a dev version and the tool is under active development. The test coverage is currently limited to the backend (adaptiveperf-server) only and bugs are to be expected. Use at your own risk!

All feedback is welcome.

## License
Copyright (C) CERN. 

The project is distributed under the GNU GPL v2 license. See LICENSE for details. **Only version 2 of GNU GPL can be used!**

## Current features
* Profiling on-CPU and off-CPU activity with ```perf```
* Profiling thread/process tree by tracing relevant syscalls with ```perf```
* Producing data needed for rendering a thread/process timeline view with non-time-ordered and time-ordered flame graphs per thread/process (where on-CPU and off-CPU activity is visualised in one graph)
* Profiling stack traces of functions spawning new threads/processes
* Profiling any sampling-based event supported by ```perf```
* Streaming profiling events in real-time through TCP to a different machine running adaptiveperf-server (part of this repository)
* Detecting inappropriate CPU and kernel configurations automatically and therefore helping ```perf``` traverse stack completely in all cases

On-CPU profiling uses ```perf``` with the ```task-clock``` event. Off-CPU profiling is based on sampling explained with the diagram below (using the example of a single process with interleaving on-CPU and off-CPU activity). The sampling period is calculated from a user-provided off-CPU sampling frequency.

![Off-CPU sampling](https://github.com/AdaptivePerf/AdaptivePerf/assets/24892582/cfe3d882-9ce1-40f8-8f57-e286f04057dd)

Both single-threaded and multi-threaded programs are supported. All CPU architectures and vendors should also be supported (provided that the installation requirements are met, see below) since the main features of AdaptivePerf are based on kernel-based performance counters and portable stack traversal methods. However, if extra ```perf``` events are used for sampling, the list of available events should be checked beforehand by running ```perf list``` as this is architecture-dependent.

## Current limitations / TODO
Work is being done towards eliminating all of the limitations below step-by-step, stay tuned!
* No support for non-CPU devices such as GPUs
* No support for partial profiling
* No API for profiling result analysis (only flame graphs can be produced right now)
* No profiling with line number / compiler IR / assembly details

## Installation
### Requirements
* Linux 5.8 or newer compiled with:
  * ```CONFIG_DEBUG_INFO_BTF=y``` (or equivalent, you can check this by seeing if ```/sys/kernel/btf``` exists in your system)
  * ```CONFIG_FTRACE_SYSCALLS=y``` (or equivalent, you can check this by seeing if ```/sys/kernel/tracing/events/syscalls``` exists in your system and is not empty, but you may need to mount ```/sys/kernel/tracing``` first)
  * If you want complete kernel debug symbols, ```CONFIG_KALLSYMS=y``` and ```CONFIG_KALLSYMS_ALL=y``` (or equivalent) should also be set.
  * **Kernel recompilation may NOT be needed! If you have ```/sys/kernel/btf``` and ```/sys/kernel/tracing/events/syscalls``` as explained above and you don't care about having kernel debug symbols, you're already good to go here!**
* Python 3
* CMake 3.20 or newer
* libnuma (if a machine with your profiled application has NUMA)
* [CLI11](https://github.com/CLIUtils/CLI11)
* [nlohmann-json](https://github.com/nlohmann/json)
* [PocoNet + PocoFoundation](https://pocoproject.org)
* [Boost](https://www.boost.org)

Additionally, for the patched "perf", you need:
* Clang (only for building, can be removed afterwards)
* libtraceevent

You should use the newest available version of the dependencies if the version is not explicitly specified. More information about the minimum tested version of each of these required programs/libraries will be provided soon.

If you want to enable tests (see the documentation for contributors), you don't have to install [the GoogleTest framework](https://github.com/google/googletest) beforehand, this is done automatically during the compilation.

AdaptivePerf uses the patched "perf", temporarily available at https://gitlab.cern.ch/adaptiveperf/linux (inside ```tools/perf```). However, you don't have to download and install it manually, this is handled automatically by the installation scripts (see the "Manually" section below). **If there are extra dependencies actually needed by the patched "perf" and not listed above, please file an issue on GitHub.**

A profiled program along with dependencies should be compiled with frame pointers (i.e. in case of gcc, with the ```-fno-omit-frame-pointer``` flag along with ```-mno-omit-leaf-frame-pointer``` if available). If you can, it is recommended to have everything in the system compiled with frame pointers (this can be achieved e.g. in Gentoo and Fedora 38+).

### Manually
Please clone this repository and run ```./build.sh``` (as either non-root or root, non-root recommended) followed by ```./install.sh``` (as root unless you run the installation for a non-system prefix).

By default, AdaptivePerf is installed in ```/usr/local```, its support files along with the bundled patched "perf" are installed in ```/opt/adaptiveperf```, and the configuration file of AdaptivePerf is installed in ```/etc/adaptiveperf.conf```.

* If you want to change ```/usr/local```, specify an alternative path as an argument to ```install.sh```, e.g. ```./install.sh /usr```.
* If you want to change ```/opt/adaptiveperf```, run ```./build.sh -DAPERF_SCRIPT_PATH=<new path>``` before installing.
* If you want to change ```/etc/adaptiveperf.conf```, run ```./build.sh -DAPERF_CONFIG_PATH=<new path including the filename>``` before installing.

The ```-DAPERF_SCRIPT_PATH``` and ```-DAPERF_CONFIG_PATH``` options can be combined in one ```./build.sh``` command.

### Manually (adaptiveperf-server only)
If you want to install just adaptiveperf-server, please clone this repository and run ```./build_server.sh``` (as either non-root or root, non-root recommended) followed by ```./install.sh``` (as root unless you run the installation for a non-system prefix).

```./install.sh``` supports a custom installation prefix, see the "Manually" section above. adaptiveperf-server does not require the patched "perf" and should be compilable for various operating systems (the scripts are only tested on Linux though).

### Gentoo-based virtual machine image with frame pointers
Given the complexity of setting up a machine with a recent enough Linux kernel, frame pointers etc., we make available ready-to-use x86-64 Gentoo-based qcow2 images with AdaptivePerf set up. They're also configured for out-of-the-box reliable ```perf``` profiling, such as permanently-set profiling-related kernel parameters and ensuring that everything in the system is compiled with frame pointers.

The images are denoted by commit tags and can be downloaded from https://cernbox.cern.ch/s/FAzoFWvh2kzNtUx. They must be booted in the UEFI mode.

### Container image
To ease the deployment of AdaptivePerf, we also provide Docker and Apptainer/Singularity images based on Gentoo with all packages built with frame pointers. x86-64 only is available at the moment, with more architectures coming soon.

#### Docker
Please use:
* **RECOMMENDED (until the first non-dev release):** ```gitlab-registry.cern.ch/adaptiveperf/adaptiveperf:latest``` for the latest commit in the ```main``` branch.
* ```gitlab-registry.cern.ch/adaptiveperf/adaptiveperf:<branch name>``` for the latest commit in a branch of your choice.
* ```gitlab-registry.cern.ch/adaptiveperf/adaptiveperf:<commit short SHA>``` for a commit of your choice (the short SHA must have exactly 8 characters).

All images are public (no CERN login required), so no deployment to a non-CERN registry is planned.

#### Apptainer/Singularity
The images are denoted by commit tags and can be downloaded from https://cernbox.cern.ch/s/XVwsHPOjvyb2YpU.

## How to use
Before running AdaptivePerf for the first time, run ```sysctl kernel.perf_event_paranoid=-1```. Otherwise, the tool will refuse to run due to its inability to reliably obtain kernel stack traces. This is already done for the VM image.

You also need to set the maximum number of stack entries to be collected by running ```sysctl kernel.perf_event_max_stack=<value>```, where ```<value>``` is a number of your choice **larger than or equal to** 1024. Otherwise, the off-CPU profiling will fail. The default value in the VM image is 1024.

**IMPORTANT:** Max stack sizes larger than 1024 are currently not supported for off-CPU stacks! The maximum number of entries in off-CPU stacks is always set to 1024, regardless of the value of ```kernel.perf_event_max_stack```.

If your machine has NUMA (non-uniform memory access), you should note that NUMA memory balancing in Linux limits the reliability of obtaining complete stacks across all CPUs / CPU cores. In this case, you must either disable NUMA balancing by running ```sysctl kernel.numa_balancing=0``` or run AdaptivePerf on a single NUMA node.

To profile your program, please run the following command:
```
adaptiveperf <command to be profiled>
```

**IMPORTANT:** If your command has whitespaces, you must run AdaptivePerf in one of these ways:
```
adaptiveperf "<command to be profiled>"
```
or
```
adaptiveperf -- <command to be profiled>
```

AdaptivePerf can be run as non-root as long as all of the requirements below are met:
* The AdaptivePerf-patched "perf" executable has CAP_PERFMON and CAP_BPF capabilities set as permissive and effective (you can do it by running ```setcap cap_perfmon,cap_bpf+ep <path to "perf">```, the default path is ```/opt/adaptiveperf/perf/bin/perf```). **If you're in a container, the container itself must have these capabilities as well!**
* You are part of the ```tracing``` group (if it doesn't exist, you must create it first).
* ```/sys/kernel/tracing``` is mounted as tracefs **with permissions 750** or more lax **and as the ```tracing``` group**.
  * Mount ```/sys/kernel/tracing``` in a standard way if not mounted yet (i.e. run ```mount -t tracefs nodev /sys/kernel/tracing```).
  * Once ```/sys/kernel/tracing``` is mounted in a standard way, remount the directory by running ```mount -o remount,mode=0750,gid=<GID of the tracing group> /sys/kernel/tracing```.
  * You can also opt for updating your fstab file instead.

If you want to see what extra options you can set (e.g. an on-CPU/off-CPU sampling frequency, the quiet mode), run ```adaptiveperf --help```.

After profiling is completed, you can check the results inside ```results```.

You can run ```adaptiveperf``` multiple times, all profiling results will be saved inside the same ```results``` directory provided that every ```adaptiveperf``` execution is done inside the same working directory.

The structure of ```results``` is as follows:
* **(year)\_(month)\_(day)\_(hour)\_(minute)\_(second)\_(hostname)\_\_(command)**: the directory for a given profiling session
  * **out**: the directory with output logs
    * **perf\_(record or script)\_(event)\_stdout.log, perf\_(record or script)\_(event)\_stderr.log**: stdout and stderr logs from perf-record/perf-script. (event) can be either "main" (on-CPU/off-CPU profiling), "syscall" (syscall profiling for tracing threads/processes), or a custom perf event specified by the user.
    * **stdout.log, stderr.log**: stdout and stderr logs from the profiled command.
  * **processed**: the directory with processed profiling information
    * **metadata.json**: metadata (such as the thread/process tree and thread/process spawning stack traces) stored in JSON.
    * **(event)\_callchains.json**: mappings between compressed callchain names and uncompressed ones stored in JSON. (event) can be either "walltime" (on-CPU/off-CPU profiling), "syscall" (syscall profiling for tracing threads/processes, applicable to metadata.json), or a custom perf event specified by the user.
    * **(PID)\_(TID).json**: all samples gathered by on-CPU/off-CPU profiling and custom perf event profiling (if any) stored in JSON, per thread/process.
    * **event\_dict.data**: mappings between custom perf events and their website titles as specified by the user (it is not created when no custom events are provided).

It is recommended to use [AdaptivePerfHTML](https://github.com/AdaptivePerf/adaptiveperfhtml) for creating an interactive HTML summary of your profiling sessions.

## External instance of adaptiveperf-server
AdaptivePerf runs adaptiveperf-server internally by default, which means that both profiling and profiling data (post-)processing are performed on the same machine. However, you can delegate the (post-)processing to an external instance of adaptiveperf-server running e.g. on a separate machine.

To do this, run ```adaptiveperf-server``` first on the machine where you want the (post-)processing to be done and note down the IP address and port it prints. If you want to specify extra options (e.g. a custom IP address and/or port), look at the output of ```adaptiveperf-server --help```.

Afterwards, run the following command on the machine with your program to be profiled:
```
adaptiveperf -a <IP address>:<port> "<command to be profiled>"
```
or
```
adaptiveperf -a <IP address>:<port> -- <command to be profiled>
```
(you are free to specify extra options as well)

When AdaptivePerf finishes profiling, all results will be stored on the machine running ```adaptiveperf-server``` (**not the machine with the profiled program**).

## Documentation for contributors (Doxygen)
If you want to contribute to AdaptivePerf or dive deeply into how it works, please check out the Doxygen documentation [here](https://adaptiveperf.github.io/contributors).

You can also render it yourself by running ```doxygen Doxyfile``` inside the ```docs``` directory (you need to install Doxygen first).

## Troubleshooting

### libaperfserv.so not found
After installing AdaptivePerf/adaptiveperf-server, you may get an error like the one below when trying to run the tool on Linux:
```
adaptiveperf: error while loading shared libraries: libaperfserv.so: cannot open shared object file: No such file or directory
```
If this happens, please add ```<your installation prefix>/lib``` (it's ```/usr/local/lib``` by default) to ```/etc/ld.so.conf``` and run ```ldconfig``` afterwards. Alternatively, run AdaptivePerf with ```<your installation prefix>/lib``` appended to the ```LD_LIBRARY_PATH``` environment variable.

### Profiler "..." (perf-record / perf-script) has returned non-zero exit code
If you get an error message similar to the one in the title, please look at the logs in the temporary directory printed by AdaptivePerf.

If the logs mention "can't access trace events", permission denied issues, or problems with eBPF, please ensure that the requirements for running AdaptivePerf as non-root are met (see "How to use") or run AdaptivePerf as root. If it doesn't work or the logs specify a different problem (or don't say anything), feel free to file an issue on GitHub.

### One or more expected symbol maps haven't been found
If you get a warning message as in the title, you can check whether your profiled program can be configured to emit "perf" symbol maps as documented [here](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/tools/perf/Documentation/jit-interface.txt).

You should note that the lack of symbol maps **is not an error**, it will just make some symbol names unresolved and point to the name of an expected map file instead. This does not cause broken stack traces.

### "perf" compilation fails when installing AdaptivePerf
Compiling "perf" is an integral part of installing AdaptivePerf. When you get errors at this stage, please try running ```make clean``` inside ```linux/tools/perf``` first. Afterwards, if the errors persist and they point to some dependency missing (e.g. Clang), please install it and try again. **If you get complaints about a program/library missing which is not listed in this README, please install it as well and file an issue on GitHub!**

If the solutions above don't help, you can create a new issue on GitHub.

## Acknowledgements
The AdaptivePerf development is possible thanks to the following funding sources:
* The European Union HE research and innovation programme, grant agreement No 101092877 (SYCLOPS).
