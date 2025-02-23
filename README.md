# Pintos
This repo contains skeleton code for undergraduate Operating System course honor track at Peking University. 

[Pintos](http://pintos-os.org) is a teaching operating system for 32-bit x86, challenging but not overwhelming, small
but realistic enough to understand OS in depth (it can run on x86 machine and simulators 
including QEMU, Bochs and VMWare Player!). The main source code, documentation and assignments 
are developed by Ben Pfaff and others from Stanford (refer to its [LICENSE](./LICENSE)).

## Acknowledgement
This source code is adapted from professor ([Ryan Huang](huang@cs.jhu.edu)) at JHU, who also taught a similar undergraduate OS course. He made some changes to the original
Pintos labs (add lab0 and fix some bugs for MacOS). For students in PKU, please
download the release version skeleton code by `git clone git@github.com:PKU-OS/pintos.git`.

docker run -it --rm --name pintos --mount type=bind,source=D:/wksp/pintos,target=/home/PKUOS/pintos pkuflyingpig/pintos bash


/bin/sh: 1: ../../tests/make-grade: not found

首先将 src/tests/make-grade 改成 LF

: open: No such file or directory

注意需要先 make clean 然后再 make grade 否则 up to date

修改 src/tests/threads/Grading, src/tests/threads/Rubric.foo