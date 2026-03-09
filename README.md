# SpIOpen
High Speed Industrial IO Backplane Using CanOpen Messages Over a Dual SPI Bus

## SpIOpen Protocol

SpIOpen uses a simplex SPI drop-bus (clock and MOSI, no chip select or MISO) to communicate from a single master to all slaves simultaneously. 
The slaves communicate in a daisy-chain configuration on an identical, but reversed bus, that terminates back at the master. 

The bus is designed around tunneling CAN-CC, CAN-FD, and CAN-XL frames at a target rate of 10MHz to enable CANopen-like behavior of IO slave modules on an industrial backplane. 
The advantage over the CAN physical layer is the lack of Transcievers and removing the open-drain configuration which allows for lower cost higher speed data.
This comes at the expense of some added latency, lack of prioritization-through-arbitration, and decentralization of physical CAN.

A library for interacting with these Frames (such as writing or parsing them from a byte stream) is implemented at LibrarieSpIOpen_Frame, and the protocol is defined in Libraries\SpIOpen_Frame\FrameFormat.md.

## SpIOpen Broker

SpIOpen devices use a broker that connects all of the frame publishers and subscribers within a node.
The broker allocates memory from a common frame pool to all publishers, passes parsed frames to all subscribers in turn, and finally reclaims them when no longer needed.
Publishers could be input ports (like the master-out drop bus, downstream slave input port, or a potential gateway port) or applications (like a local CANopen device).
Subscribers could be output ports (like the upstream slave output port or a potential gateway output port) or applications (like a local CANopen device).

Ports are one type of publisher and subscriber, often coupled with a specific hardware component (like an SPI port). 
A loopback port is implemented for testing, but other ports for specific hardware (Raspberry Pi PIO state machine for example) are also available.

It is designed to run on a real-time system and requires proper porting for real-time scheduling, semaphores, and queues.
At a minimum, the project aims to support linux with the PREEMPT_RT patch and FreeRTOS, with future support for ZephyrOS planned.

The broker is implemented in Libraries\SpIOpen_Broker.

## SpIOpen Node

This project aims to be tightly coupled with the open source [CanOpenNode](https://github.com/CANopenNode/CANopenNode) project.
A combination of SpIOpen Broker and CanOpenNode on one device allows settings within the CanOpen object dictionary to control the behavior and configuration of the SpIOpen Broker.
A base library is implemented to manage the shared communicaiton properties, and a master and slave library are also implemented as a launching point for further development.
