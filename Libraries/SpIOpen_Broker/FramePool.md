# SpIOpen Frame Pool

This section is responsible for initializing and managing all of the FrameBuffer objects and FrameMessage objects that will be used across every Publisher and Subscriber on the device.

## Overview

The Frame Pool keeps several internal pools of available FrameMessage objects (each containing a FrameBuffer), one for each supported CAN Frame size (CC, FD, XL).
Frame Publishers request a FrameBuffer object from the pool (and must handle the case where none are available!).
Frame publishers may hand the frame off to another process (like the broker) for further processing, but eventually the owner of the FrameBuffer will call a release function on the Frame Pool to signal that it can be returned to the pool of available FrameBuffers.
Frame Allocator is a thin wrapper that combines all of the Frame Pools (one for each supported frame size) into one interface for producers to get new messages 

## Design Considerations

- Share one pool (per size) for all messaging to optimize resource usage on constrained devices, implemented as a queue for thread safety
- Intelligently split resources across frame lengths, only use the buffer that you need
- Safe to get and release frames from ISR
- Because the frame pool supports both configurable (heap) and static allocation modes, it needs to contains a state and track the lifecycle of the memory allocated. It has configure(config)/initialize/deinitialize state change functions that can get called to manage the state and it exposes.
- The configure and initialize fucntions can return an error code, using the etl::expected<void,ErrorEnum> format.

## Implementation Details

- spiopen_frame_pool.h / .cpp contains the class
- Function for getting an empty Frame Message from the queue, parameterized by the required minimum buffer length in bytes (likely to always just be the CC/FD/XL MAX lengths)
- Function for releasing a used Frame Message back to the queue. The Release function exists on the message and decrements the reference counter, but is responsible for reaching out to the pool to requeue the message structure if the counter is reduced to zero.
- All Queues are thread-safe and *may* be called from interrupts. Blocking Time can be specified when getting a new empty buffer from the queue, but an ISR must not block.
- FrameMessages retain a pointer back to their owning FramePool so that they can re-enter the queue of available Messages once all of their references counters are gone.
- Reference counting is accomplished with an atomic primitive just large enough to fit the maximum number of potential subscribers
- When the SPIOPEN_BROKER_FRAME_POOL_SIZE_CONFIGURABLE setting is true, there is a more complex state machine within the Frame Pool to manage the lifecycle of the memory allocated for the frame buffers.
- Because FrameBuffers only have a non-owning pointer to their buffer memory (span) Frame Messages have an internal array that actually owns the memory within the Frame Buffer. This way, the FrameMessage itself has a data size that contains the control structure for the message as well as the Frame object and Buffer byte array.
- The frame pool is just a state machine and a queue, so it does not need a thread to "run". The Frame Allocator also does not need a thread.
- The frame allocator is responsible for aggregating not just the AllocateFrameMessage(length) call, but also aggregating the state transition calls (init, config, deinit) of each contained Frame Pool.
