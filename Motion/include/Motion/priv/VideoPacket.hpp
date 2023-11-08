#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <cstring>

namespace mt
{
    namespace priv
    {
        class VideoPacket
        {
        private:
            uint8_t* m_rgbaBuffer;

        public:
            VideoPacket(uint8_t* RGBABufferSource, int Width, int Height);
            ~VideoPacket();
            const uint8_t* GetRGBABuffer();
        };

        typedef std::shared_ptr<mt::priv::VideoPacket> VideoPacketPtr;
    }
}