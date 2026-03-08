# SpIOpen Broker

C++ Library for coordinating the processing of SpIOpen frames by applications and physical ports on the same device.

## Overview

SpIOpen devices use a broker that connects all of the frame publishers and subscribers within a node.
The router allocates memory from a common frame pool to all publishers, passes parsed frames to all subscribers in turn, and finally reclaims them when no longer needed.
Publishers could be input ports (like the master-out drop bus, downstream slave input port, or a potential gateway port) or applications (like a local CANopen device).
subscribers could be output ports (like the upstream slave output port or a potential gateway output port) or applications (like a local CANopen device).

Ports are one type of publisher and subscriber, often coupled with a specific hardware component (like an SPI port). 
A loopback port is implemented for testing, but other ports for specific hardware (Raspberry Pi PIO state machine for example) are also available.

It is designed to run on a real-time system and uses CMSIS-RTOSv2 as the standard interface. While the design and syntax is heavily inspired by the Embedded Template Library's Reference Counted Messaging Framework it is optimized for use with CMSIS mult-thread capabilities.

## Contents

- spiopen_broker_frame_pool.h contains the `spiopen::broker::FramePool` class which is responsible for managing the lifetime of byte array buffers within `FrameBuffer` objects to publishers and subscribers of SpiOpen frames. it also has the `spiopen::broker::FrameMessageAllocator` class which wraps the multiple frame pools together behind one interactive layer that allocates the right size FrameMessage based on the request of the producing thread.
- spiopen_broker_frame_router.h contains the `spiopen::broker::FrameBroker` class which is responsible for distributing frames from publishers out to all subscribing subscribers, along with initializing the routing logic based on device configuration
- spiopen_broker_frame_message.h contains the `spiopen::broker::FrameMessage` class, along with the definitions of the Message Type as `spiopen::broker::MessageType` enum.
- spiopen_broker_frame_publisher.h contains the `spiopen::broker::publisher` namespace and the `spiopen::broker::publisher::FramePublisherHandle_t` struct definition which is passed into the publish function by publishers, along with other utility functions for publishers.
- spiopen_broker_frame_subscriber.h contains the `spiopen::broker::subscriber` namespace and the `spiopen::broker::publisher::FrameSubscriberHandle_t` struct definition which is passed into the publish function by publishers, along with other utility functions for publishers and the preferred implementation of a thread-safe queue for receiving messages `spiopen::broker::publisher::FrameMailbox`.

## Configuration

### Real Time OS

Real time behavior is achieved through the CMSIS-RTOSv2 interface, with a specific implementation (including FreeRTOS wrapper) selected at build time. The interface is located at modules/CMSIS/CMSIS/RTOS2/Include/cmsis_os2.h

This interface provides functions for task scheduling, queues, symaphores,and potentially other future features

### KConfig

Use KConfig to enable/disable features of the library.

Router:
- SPIOPEN_ROUTER_MAX_subscriber_COUNT : The maximum number of frame subscribers the router can keep track of.

Frame Pool:
- SPIOPEN_ROUTER_FRAME_POOL_SIZE_CONFIGURABLE : If true, the frame pool memory is initialized from the heap based on a configuration (and may fail). If False, the available frame pool memory is fixed at compile time (defined by the *_MAX_XX_FRAMES constants)
- SPIOPEN_ROUTER_FRAME_POOL_MAX_CC_FRAMES : Maximum number of CAN-CC sized frames that can be initialized in the memory pool.
- SPIOPEN_ROUTER_FRAME_POOL_MAX_FD_FRAMES : Maximum number of CAN-FD sized frames that can be initialized in the memory pool. Zero to disable CAN-FD.
- SPIOPEN_ROUTER_FRAME_POOL_MAX_XL_FRAMES : Maximum number of CAN-XL sized frames that can be initialized in the memory pool. Zero to disable CAN-XL.


## Usage

### Publishers

Publishers start by requesting one or more free and available FrameBuffers from the FramePool that lives within the router (potentially from an ISR). They must ensure that the internal Frame object is valid, even if the coupled memory buffer is invalid (perhaps due to a TTL decrement).

To send a Frame through the router it must be combined with a MessageType to form a FrameMessage. The MessageType often just describes the source (MOSI Dropbus, MISO Chain Downstream, CanOpenNode, etc). Some message types are preconfigured or reserved, others can be freely used in derivative software.

The publisher then enqueues the FrameMessage into the Router's inbox. This can potentially happen from an ISR.

Finally the publisher "releases" the Message, which decrements its reference count by one and potentially frees the FrameBuffer back to the pool.

### Routing Rules

When the router receives a message it walks through every registered subscriber and enqueuess the Reference Counted Message (incrementing the counter each time on success) for each subscriber that is configured to receive that MessageType. Then the router releases its own reference on the message, decrementing it by one and potentialyl releasing the frame buffer back to the pool.

### Subscribers

Subscribers must register with the router to receive FrameMessages. They submit to the router's registration function a reference to their own queue, along with a reference to their own subscriber structure which includes things like a bitmask that describes what Message Types they want to receive and a "name" string for debug. subscribers should have their own Task that blocks on their queue so they can process messages immediately.

When subscribers are done processing a Reference Counted Message they *must* call the release function on the message to decrement the counter. When it decrements to zero, that same function will know to submit it back to the Frame Pool (even if it happens in an ISR).

