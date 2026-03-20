# SpIOpen Message

C++ Library for coordinating the processing of SpIOpen frames by applications and physical ports on the same device.

## Overview

SpIOpen devices use a broker that connects all of the frame publishers and subscribers within a node.
The broker allocates memory from a common frame pool to all publishers, passes parsed frames to all subscribers in turn, and finally reclaims them when no longer needed.
Publishers could be input ports (like the master-out drop bus, downstream slave input port, or a potential gateway port) or applications (like a local CANopen device).
Subscribers could be output ports (like the upstream slave output port or a potential gateway output port) or applications (like a local CANopen device).

Ports are one type of publisher and subscriber, often coupled with a specific hardware component (like an SPI port). 
A loopback port is implemented for testing, but other ports for specific hardware (Raspberry Pi PIO state machine for example) are also available.

It is designed to run on a real-time system and uses CMSIS-RTOSv2 as the standard interface. While the design and syntax is heavily inspired by the Embedded Template Library's Reference Counted Messaging Framework it is optimized for use with CMSIS mult-thread capabilities.

## Contents

- spiopen_message_frame_pool.h contains the `spiopen::message::FramePool` class which is responsible for managing the lifetime of byte array buffers within `FrameBuffer` objects for publishers and subscribers of SpIOpen frames.
- spiopen_message_frame_allocator.h contains the `spiopen::message::FrameMessageAllocator` class which wraps multiple frame pools behind one interface that allocates the right size `FrameMessage` based on publisher requests.
- spiopen_message_frame_broker.h contains the `spiopen::message::FrameBroker` class which is responsible for distributing frames from publishers to all subscribing subscribers, along with initializing the broker based on device configuration.
- spiopen_message_frame_message.h contains the `spiopen::message::FrameMessage` class, along with the definitions of the Message Type as `spiopen::message::MessageType` enum.
- spiopen_message_frame_publisher.h contains the `spiopen::message::publisher` namespace and the `spiopen::message::publisher::FramePublisherHandle_t` struct used by publishers for diagnostics and context when publishing.
- spiopen_message_frame_subscriber.h contains the `spiopen::message::subscriber` namespace and the `spiopen::message::subscriber::FrameSubscriberHandle_t` struct definition which is passed into the publish function by publishers, along with other utility functions for publishers and the preferred implementation of a thread-safe queue for receiving messages `spiopen::message::subscriber::FrameMailbox`.
- `SpIOpen_Lifecycle/include/spiopen_lifecycle.h` provides the shared lifecycle interfaces and state/error model used by message components.

## Configuration

### Real Time OS

Real time behavior is achieved through the CMSIS-RTOSv2 interface, with a specific implementation (including FreeRTOS wrapper) selected at build time. The interface is located at modules/CMSIS/CMSIS/RTOS2/Include/cmsis_os2.h

This interface provides functions for task scheduling, queues, symaphores,and potentially other future features

### KConfig

Use KConfig to enable/disable features of the library.

Broker:
- SPIOPEN_MESSAGE_MAX_SUBSCRIBER_COUNT : The maximum number of frame subscribers the broker can keep track of.

Frame Pool:
- SPIOPEN_MESSAGE_FRAME_POOL_SIZE_CONFIGURABLE : If true, the frame pool memory is initialized from the heap based on a configuration (and may fail). If false, the available frame pool memory is fixed at compile time (defined by the *_MAX_XX_FRAMES constants).
- SPIOPEN_MESSAGE_FRAME_POOL_MAX_CC_FRAMES : Maximum number of CAN-CC sized frames that can be initialized in the memory pool.
- SPIOPEN_MESSAGE_FRAME_POOL_MAX_FD_FRAMES : Maximum number of CAN-FD sized frames that can be initialized in the memory pool. Zero to disable CAN-FD.
- SPIOPEN_MESSAGE_FRAME_POOL_MAX_XL_FRAMES : Maximum number of CAN-XL sized frames that can be initialized in the memory pool. Zero to disable CAN-XL.
- SPIOPEN_MESSAGE_MAX_SUBSCRIBER_COUNT : Maximum number of subscribers that the router can route to at once

## Usage

### Lifecycle Integration

Message classes consume lifecycle interfaces from the `SpIOpen_Lifecycle` library:

- `FramePool`, `FrameMailbox`, and `FrameBroker` implement `ILifecycleComponent<ConfigT, LifecycleError>`.
- `FrameMessageAllocator` implements `IAggregateLifecycleComponent<NoLocalConfig, ...>` to fan out lifecycle transitions across child pools and aggregate child lifecycle errors.
- Lifecycle transitions should be invoked in order: `Configure -> Initialize -> Start`, and unwound in reverse: `Stop -> Deinitialize`.
- Broker-specific lifecycle behavior is documented in `FrameBroker.md` and `FramePool.md`, while shared lifecycle semantics are documented in `SpIOpen_Lifecycle/README.md`.

### Publishers

Publishers start by requesting one or more free and available FrameBuffers from the FramePool that lives within the broker (potentially from an ISR). They must ensure that the internal Frame object and internal memory buffer are both valid and synchronized (perhaps due to a TTL decrement).

To send a frame through the broker it must be combined with a MessageType to form a FrameMessage. The MessageType often just describes the source (MOSI Dropbus, MISO Chain Downstream, CanOpenNode, etc). Some message types are preconfigured or reserved, others can be freely used in derivative software.

The publisher then enqueues the FrameMessage into the broker's inbox. This can potentially happen from an ISR.

Finally the publisher "releases" the Message, which decrements its reference count by one and potentially frees the FrameBuffer back to the pool.

### Distribution Rules

When the broker receives a message it walks through every registered subscriber and enqueues the reference-counted message (incrementing the counter each time on success) for each subscriber that is configured to receive that MessageType. Then the broker releases its own reference on the message, decrementing it by one and potentially releasing the frame buffer back to the pool.

### Subscribers

Subscribers must register with the broker to receive FrameMessages. They submit to the broker's registration function a reference to their subscriber structure which includes their own queue ("mailbox") and things like a bitmask that describes what Message Types they want to receive and a "name" string for debug. Subscribers should have their own task that blocks on their queue so they can process messages immediately.

Subscribers must not modify the Frame or Buffer within the Frame Buffer, or else other subscribers will be affected. 

When subscribers are done processing a Reference Counted Message they *must* call the release function on the message to decrement the counter. When it decrements to zero, that same function will know to submit it back to the Frame Pool (even if it happens in an ISR).
