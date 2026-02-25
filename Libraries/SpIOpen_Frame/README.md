# SpIOpen Frame Library

C++ Library for interacting with SpIOpen Frames. The format of the SpIOpen frame is defined in @FrameFormat.md.

## Contents

- spiopen_frame.h : contains the spiopen::Frame class which defines structured data types for representing a SpIOpen data frame
- spiopen_frame_pool.h : contains the common frame pool which acts as the static, shared memory resource for all frames
- spiopen_frame_router.h : contains the router responsible for moving frames between the pool, producers, and consumers using IRQ safe queues
- spiopen_frame_producer.h : base implementation of a task that takes empty frames from the pool, populated them (based on internal processing or a physical port), then sends them back to the router for distribution to consumers.
- spiopen_frame_consumer.h : base implementation of a task that takes populated frames from producers, processes them (either internally or onto a physical port), then frees them back to the pool.
- spiopen_frame_parser.h : used by producers to find frames in bytestreams and get buffers from the shared memory pool

## Configuration