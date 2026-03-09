# SpIOpen Project

This is a series of libraries used to enable communication between embeded devices within an industrial control fieldbus. It routes CAN frames over SPI to allow devices to use CANOpen features with a higher speed and lower cost communications bus.

## Components
- The core library focused on the communication format is located in Libraries/SpIOpen_Frame
- The core library focused on the routing of frames between threads and ports in a device (broker: publishers and subscribers) is located in Libraries\SpIOpen_Broker
- The core library at the heart of every device (master or slave) implementing the protocol is located at Libraries\SpIOpen_Node