# NS3 Simulation 

This repo contains a single simulation environment developed using the NS3
network simulation tool.  This **single cpp file** contains all the functions
needed to setup the topology, create all the components needed, run the
simulation and collect logs.  The main idea here is to let a TCP "Sender" read
an external file with some information regarding the size of the next packet to
be sent and the timeout to wait and then send it. In exchange, the TCP
"Receiver" waits for the packet to arrive, saves the time of arrival and sends
a `feedback` packet back to the Sender in order to certify the time of
receival. In order to increase the amount of noise and simulate some network
congestion, it is possible to use the `--interference` option and generate some
UDP and TCP noise on the network. Everything is logged and can be easily
plotted. Check the topology below to better understand 
what happens and where.

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
In the case represented here, `n2` represents the Test Server and it is
accessed by `n7`, the test client.
However, in order to create some noise, a set of Server/Clients both in TCP and
UDP are created and deployed over some nodes. By means of the tracking system
of NS3, it is possible to log the interactions and also see an interactive
visual representation of the situation. 
The Receiver, once the packet is received, sends a packet back to the
server with the info about when the packet was received. In
this way further analysis can be carried out.

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


