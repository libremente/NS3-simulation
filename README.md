# NS3 Simulation 

This repo contains a single simulation environment developed using the NS3
network simulation tool. 

## Topology
```
================== NETWORK SETUP ===========================
*
*  [10.1.5.0] (wifi)                    n2 -> TS
*                                      /
*  *    *    *    *                [10.1.2.0] (ptp)
*  |    |    |    |   [10.1.1.0]     /
* n5   n6   n7   n0 -------------- n1-[10.1.3.0]--n3 -> IUS
* ITC  IUC  TC   AP      (ptp)       \
*                                  [10.1.4.0] (ptp)
*                                      \
*                                       n4 -> ITS
*
* ITC/S: Interference TCP Client/Server - WifiSTA
* IUC/S: Interference UDP Client/Server - WifiSTA
* TC/S: Test Client/Server - WifiSTA
* AP: Access Point
============================================================
```

## Running
A working installation of NS3 has to be deployed on the machine. 
Check the [NS3](https://www.nsnam.org/ns-3-28/documentation/) Docs page for
more infor regarding the initial setup. 
Once the setup is ready:

1. Export NS_LOG variable  
`export NS_LOG=WIFI_TCP_simulation=level_info`

2. Launch as:  
`RUN AS = ./waf --run "WIFI_TCP_simulation --filein=schedfile --tracing --interference`

As you can see an input file is needed. You may use the one provided for
testing purposes. 

## Waff Hack 
Potentially it is also possible to use an in BASH function to launch the
simulation. To do this, you have to define a function like this:

```
function waff() {
    CWD="$PWD"
    cd $NS3DIR >/dev/null
    ./waf --cwd="$CWD" "$@"
    cd - >/dev/null
}

export -f waff
```

With waff launch as:  
`waff --run "WIFI_TCP_simulation --filein=s --tracing --interference"`

## VALGRIND
For debugging purposes, I suggest using `valgring` with the following launch
command:  
`waff --run WIFI_TCP_simulation --command-template="valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all %s --filein=schedulefile"`

## GDB
If you want to use `gdb`, this is the way to do it:  
`waff --run WIFI_TCP_simulation --command-template="gdb --args %s --filein=schedulefile"`

# Licensing
## License
The whole repo is covered by a GNU GPL v3 license.

## Author
Copyright (c) 2018 - libremente  
`<surf> AT <libremente> DOT eu`


