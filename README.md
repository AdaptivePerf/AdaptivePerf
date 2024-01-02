# AdaptivePerf
A comprehensive profiling tool with Linux ```perf``` as its main foundation.

## Disclaimer
This is currently a beta version and the tool is under active development. The test coverage is limited in some areas and bugs are to be expected. Use at your own risk!

All feedback is welcome.

## License
Copyright (C) 2023 CERN. See ```LICENSE``` for more details.

**Please note that the project is licensed under GNU GPL v2 only (no other versions).**

## Current features
* Profiling on-CPU and off-CPU activity with ```perf```, hot-and-cold flame graphs, and hot-and-cold time-ordered flame charts
* Profiling thread/process tree by tracing relevant syscalls with ```perf```
* Profiling stack traces of functions spawning new threads/processes

Both single-threaded and multi-threaded programs are supported. All CPU architectures and vendors should also be supported (provided that the requirements are met) since the main features of AdaptivePerf are based on kernel-based performance counters and portable stack unwinding methods.

## Current limitations
* Support for CPUs only (GPUs coming soon!)

## Installation
### Requirements
* Linux with kernel-specific packages enabling profiling (for RHEL/CentOS/AlmaLinux: ```kernel-headers```, ```kernel-modules```). For best reliability of off-CPU profiling, usage of the newest Linux kernel version is recommended.
* [The patched ```flamegraph.pl``` and ```stackcollapse-perf.pl``` scripts](https://gitlab.cern.ch/adaptiveperf/flamegraph)
* [The patched ```perf```](https://gitlab.cern.ch/adaptiveperf/linux/-/tree/master/tools/perf) compiled with ```BUILD_BPF_SKEL=1```.
* [Bashly](https://bashly.dannyb.co)
* numactl
* Python 3
* Perl
* [bats-core](https://github.com/bats-core/bats-core) (for testing)
* Dependencies of your profiled code compiled with frame pointers (i.e. with the ```-fno-omit-frame-pointer``` and ```-mno-omit-leaf-frame-pointer``` gcc flags). It is recommended that everything in your system is compiled with frame pointers, otherwise you may get broken stacks in profiling results.

### Local
Clone this repository, run ```make``` followed by ```source setup.sh``` inside the repository directory (running ```make test``` before using AdaptivePerf is recommended)

### Global / System-wide
Clone this repository and run ```make``` followed by ```make install``` (running ```make test``` before the install command is recommended).

### Gentoo-based virtual machine image with frame pointers
Given the complexity of setting up a machine with frame pointers, patched ```perf``` etc., we make available ready-to-use Gentoo-based qcow2 images with AdaptivePerf set up. They're also configured for out-of-the-box reliable ```perf``` profiling, such as permanently-set profiling-related kernel parameters and ensuring that everything in the system is compiled with frame pointers.

The images are denoted by commit tags and can be downloaded from https://cernbox.cern.ch/s/FalGlNqzsdj0K5P.

## How to use
Before running AdaptivePerf for the first time, run ```sysctl kernel.perf_event_paranoid=-1```. Otherwise, the tool will refuse to run due to its inability to reliably obtain kernel stack traces. This is already done for the VM image.

You may also want to run ```sysctl kernel.perf_event_max_stack=<value>``` if your profiled program produces deep callchains (```<value>``` is a number of your choice describing the maximum number of stack entries to be collected). The default value in the VM image is 1024.

**IMPORTANT:** Max stack sizes larger than 1024 are currently not supported for off-CPU stacks! The maximum number of entries in off-CPU stacks is always set to 1024, regardless of the value of ```kernel.perf_event_max_stack```.

If your machine has NUMA (non-uniform memory access), you should note that NUMA memory balancing in Linux limits the reliability of obtaining complete stacks across all CPUs / CPU cores. In this case, AdaptivePerf will run only on a single NUMA node to avoid that. If it's not desired, you need to disable NUMA balancing by running ```sysctl kernel.numa_balancing=0```.

To profile your program, please run the following command:
```
adaptiveperf "<command to be profiled>"
```
(quoting is important if your command has whitespaces)

You can adjust the sampling rate and the number of threads used for post-processing profiling data. See ```adaptiveperf --help``` for details (```man``` pages coming soon!).

After profiling is completed, you can check the results inside ```results```.

You can run ```adaptiveperf``` multiple times, all profiling results will be saved inside the same ```results``` directory provided that every ```adaptiveperf``` execution is done inside the same working directory.

The structure of ```results``` is as follows:
* ```<year>_<month>_<day>_<hour>_<minute>_<second>_<executor> <profiled process name>```: the directory corresponding to your profiling session
    * ```perf.data```: the file containing raw sampling data for post-processing by ```perf```
    * ```syscalls.data```: the file containing raw syscall tracing data for post-processing by ```perf```
    * ```offcpu.data```: the file containing sampled off-CPU regions of threads/processes with timestamps
    * ```new_proc_callchains.data```: the file containing stack traces of functions spawning new threads/processes
    * ```out```: the directory containing all output files produced by your profiled program (this includes stdout and stderr in ```stdout.log``` and ```stderr.log``` respectively)
    * ```processed```: the directory containing flame graphs and raw text data files that can be parsed by the flame graph tool, one set per thread/process in form of ```<PID>_<TID>_<type>.data```/```<PID>_<TID>_<type>.svg```

It is recommended to use [AdaptivePerfHTML](https://gitlab.cern.ch/adaptiveperf/adaptiveperfhtml) for creating an interactive HTML summary of your profiling sessions (or setting up a web server displaying all profiling sessions run so far).
