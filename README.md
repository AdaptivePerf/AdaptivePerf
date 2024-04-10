# AdaptivePerf
A comprehensive profiling tool with Linux ```perf``` as its main foundation.

## Recent redesign
AdaptivePerf has been redesigned recently and its automated tests are not updated yet. This will change soon! In the meantime, testing is disabled.

## Disclaimer
This is currently a dev version and the tool is under active development. The test coverage is currently limited and bugs are to be expected. Use at your own risk!

All feedback is welcome.

## License
Copyright (C) CERN. 

The project is distributed under the GNU GPL v2 license. See LICENSE for details. **Only version 2 of GNU GPL can be used!**

## Current features
* Profiling on-CPU and off-CPU activity with ```perf```, hot-and-cold flame graphs, and hot-and-cold time-ordered flame charts
* Profiling thread/process tree by tracing relevant syscalls with ```perf```
* Profiling stack traces of functions spawning new threads/processes
* Profiling any event supported by ```perf``` for sampling
* Streaming profiling events in real-time through TCP to a different machine running adaptiveperf-server (part of this repository)
* Detecting inappropriate CPU and kernel configurations automatically and therefore helping ```perf``` traverse stack completely in all cases

Both single-threaded and multi-threaded programs are supported. All CPU architectures and vendors should also be supported (provided that the installation requirements are met, see below) since the main features of AdaptivePerf are based on kernel-based performance counters and portable stack traversal methods. However, if extra ```perf``` events are used for sampling, the list of available events should be checked beforehand by running ```perf list``` as this is architecture-dependent.

## Current limitations
* Support for CPUs only (GPUs coming soon!)

## Installation
### Requirements
* Linux 5.8 or newer with [the ss command](https://man7.org/linux/man-pages/man8/ss.8.html) and compiled with ```CONFIG_DEBUG_INFO_BTF=y``` (or equivalent)
* [Patched perf](https://gitlab.cern.ch/adaptiveperf/linux) compiled with Python support and the BPF skeletons
* Python 3
* CMake 3.9.6 or newer
* [bashly](https://bashly.dannyb.co)
* [CLI11](https://github.com/CLIUtils/CLI11)
* [nlohmann-json](https://github.com/nlohmann/json)
* [PocoNet + PocoFoundation](https://pocoproject.org)

Additionally, a profiled program along with dependencies should be compiled with frame pointers (i.e. with ```-fno-omit-frame-pointer``` and ```-mno-omit-leaf-frame-pointer``` flags in case of gcc). If you can, it is recommended to have everything in the system compiled with frame pointers (this can be achieved e.g. in Gentoo and Fedora 38+).

### Manually
Please clone this repository and run ```./build.sh``` (as either non-root or root, non-root recommended) followed by ```./install.sh``` (as root unless you run the installation for a non-system prefix).

AdaptivePerf is installed in ```/usr/local``` by default. If you want to change this path, specify it as an argument to ```install.sh```, e.g. ```./install.sh /usr```.

### Gentoo-based virtual machine image with frame pointers
Given the complexity of setting up a machine with a recent enough Linux kernel, frame pointers, patched ```perf``` etc., we make available ready-to-use x86 Gentoo-based qcow2 images with AdaptivePerf set up. They're also configured for out-of-the-box reliable ```perf``` profiling, such as permanently-set profiling-related kernel parameters and ensuring that everything in the system is compiled with frame pointers.

The images are denoted by commit tags and can be downloaded from https://cernbox.cern.ch/s/FalGlNqzsdj0K5P.

## How to use
Before running AdaptivePerf for the first time, run ```sysctl kernel.perf_event_paranoid=-1```. Otherwise, the tool will refuse to run due to its inability to reliably obtain kernel stack traces. This is already done for the VM image.

You may also want to run ```sysctl kernel.perf_event_max_stack=<value>``` if your profiled program produces deep callchains (```<value>``` is a number of your choice describing the maximum number of stack entries to be collected). The default value in the VM image is 1024.

**IMPORTANT:** Max stack sizes larger than 1024 are currently not supported for off-CPU stacks! The maximum number of entries in off-CPU stacks is always set to 1024, regardless of the value of ```kernel.perf_event_max_stack```.

If your machine has NUMA (non-uniform memory access), you should note that NUMA memory balancing in Linux limits the reliability of obtaining complete stacks across all CPUs / CPU cores. In this case, you must either disable NUMA balancing by running ```sysctl kernel.numa_balancing=0``` or run AdaptivePerf on a single NUMA node (for **both** CPU and memory).

To profile your program, please run the following command:
```
adaptiveperf "<command to be profiled>"
```
(quoting is important if your command has whitespaces)

You can adjust profiling settings, e.g. specify extra perf events for sampling and change the sampling rate. See ```adaptiveperf --help``` for details (```man``` pages coming soon!).

After profiling is completed, you can check the results inside ```results```.

You can run ```adaptiveperf``` multiple times, all profiling results will be saved inside the same ```results``` directory provided that every ```adaptiveperf``` execution is done inside the same working directory.

The structure of ```results``` is as follows:
* **(year)\_(month)\_(day)\_(hour)\_(minute)\_(second)\_(hostname)\_\_(command)**: the directory for a given profiling session
  * **out**: the directory with output logs
    * **perf\_(event)\_stdout.log, perf\_(event)\_stderr.log**: stdout and stderr logs from perf. (event) can be either "main" (on-CPU/off-CPU profiling), "syscall" (syscall profiling for tracing threads/processes), or a custom perf event specified by the user.
    * **stdout.log, stderr.log**: stdout and stderr logs from the profiled command.
    * **event\_dict.data**: mappings between custom perf events and their website titles as specified by the user (it is not created when no custom events are provided).
  * **processed**: the directory with processed profiling information
    * **metadata.json**: metadata (such as the thread/process tree and thread/process spawning stack traces) stored in JSON.
    * **(event)\_callchains.json**: mappings between compressed callchain names and uncompressed ones stored in JSON. (event) can be either "walltime" (on-CPU/off-CPU profiling), "syscall" (syscall profiling for tracing threads/processes, applicable to metadata.json), or a custom perf event specified by the user.
    * **(PID)\_(TID).json**: all samples gathered by on-CPU/off-CPU profiling and custom perf event profiling (if any) stored in JSON, per thread/process.

It is recommended to use [AdaptivePerfHTML](https://github.com/AdaptivePerf/adaptiveperfhtml) for creating an interactive HTML summary of your profiling sessions.
