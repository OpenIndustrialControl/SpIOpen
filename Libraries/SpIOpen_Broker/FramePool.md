# SpIOpen Frame Pool

This class is responsible for initializing and managing all of the FrameBuffer objects that will be used across every Publisher and Subscriber on the device.

## Overview

The Frame Pool keeps many internal pools of available FrameBuffer objects, one for each supported CAN Frame size (CC, FD, XL).
Frame Publishers request a FrameBuffer object from the pool (and must handle the case where none are available!).
Frame Publishers may hand the frame off to another process (like the router) for further processing, but eventually the owner of the FrameBuffer will call a release function on the Frame Pool to signal that it can be returned to the pool of available FrameBuffers.

## Design Considerations

- Share one pool for all messaging to optimize resource usage on constrained devices
- Intelligently split resources across frame lengths, only use the buffer that you need
- Safe to get and release frames from ISR

## Implementation Details

- spiopen_frame_pool.h / .cpp contains the class
- Function for getting an empty FrameBuffer from the queue, parameterized by the required minimum buffer length in bytes (likely to always just be the CC/FD/XL MAX lengths)
- Function for releasing a used FrameBuffer back to the queue
- Queues are thread-safe and *may* be called from interrupts. Blocking Time can be specified when getting a new empty buffer from the queue, but an ISR must not block.
