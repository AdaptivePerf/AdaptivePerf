# AdaptivePerf
A comprehensive profiling tool for heterogeneous architectures, designed for utilising various profilers and with Linux ```perf``` as its main foundation.

## Current features
* Profiling on-CPU and off-CPU activity with ```perf``` and hot-and-cold flame graphs
* Profiling thread/process tree by tracing relevant syscalls with ```perf```

## Current limitations
* Support for CPUs only (GPUs coming soon!)

## Installation
### Requirements
* Linux with kernel-specific packages enabling profiling (for RHEL/CentOS/AlmaLinux: ```kernel-headers```, ```kernel-modules```). For best reliability of off-CPU profiling, usage of the newest Linux kernel version is recommended.
* [The patched ```perf```](https://gitlab.cern.ch/syclops/linux/-/tree/master/tools/perf) compiled with ```BUILD_BPF_SKEL=1```.
* Dependencies of your profiled code compiled with frame pointers (i.e. with the ```-fno-omit-frame-pointer``` and ```-mno-omit-leaf-frame-pointer``` gcc flags). It is recommended that everything in your system is compiled with frame pointers, otherwise you may get broken stacks in profiling results.

### Local
Clone this repository and run ```source setup.sh``` inside the repository directory.

### Global / System-wide
Clone this repository and copy ```adaptiveperf``` and all ```adaptiveperf-*``` files to where programs are usually installed system-wide in your machine, e.g. ```/usr/bin``` or ```/usr/local/bin```.

### Gentoo-based virtual machine image with frame pointers
Coming soon!

## How to use
To profile your program, please run the following command:
```
adaptiveperf "<command to be profiled>" <number of threads to run for post-processing> <sampling frequency per second for perf> 
```

After profiling is completed, you can check the results inside ```results```.

You can run ```adaptiveperf``` multiple times, all profiling results will be saved inside the same ```results``` directory provided that every ```adaptiveperf``` execution is done inside the same working directory.

It is recommended to use [AdaptivePerfHTML](https://gitlab.cern.ch/syclops/adaptiveperfhtml) for creating an interactive HTML summary of your profiling sessions (or setting up a web server displaying all profiling sessions run so far).
