/*
SpIOpen Frame Pool : Used to manage a pool of SpIOpen frames that is shared among multiple producers and consumers in a SpIOpen device.

Copyright 2026 Andrew Burks, Burks Engineering
SPDX-License-Identifier: Apache-2.0
*/
#pragma once
#include "spiopen_frame.h"


namespace SpIOpen {


    class FramePool {

        public:
            struct Config {
                size_t max_can-cc_frames;
                #ifdef CONFIG_SPIOPEN_CAN_FD_ENABLE
                size_t max_can-fd_frames;
                #endif
                #ifdef CONFIG_SPIOPEN_CAN_XL_ENABLE
                size_t max_can_xl_frames;
                #endif
            };

            SpIOpenFramePool(const Config &config);
            ~SpIOpenFramePool();
            
            Frame *GetFrame();
            Frame *GetFrameFromISR(BaseType_t *pxHigherPriorityTaskWoken);
            void ReleaseFrame(Frame *frame);
            void ReleaseFrameFromISR(Frame *frame, BaseType_t *pxHigherPriorityTaskWoken);

        private:
            Config config_;
            Frame::Header *frame;
    };
}