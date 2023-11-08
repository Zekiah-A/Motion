#pragma once
#include <SFML/Audio/SoundStream.hpp>

#include "Export.h"
#include "AudioPlaybackBase.hpp"

namespace mt
{
    class MOTION_CXX_API SFMLAudioPlayback : public AudioPlaybackBase, private sf::SoundStream
    {
    private:
        bool onGetData(Chunk& data);
        void onSeek(sf::Time timeOffset);

    public:
        SFMLAudioPlayback(DataSource& DataSource, sf::Time OffsetCorrection = sf::milliseconds(50));
        const float GetVolume();
        void SetVolume(float Volume);

    protected:
        void SetupStream(unsigned int ChannelCount, int SampleRate);
        void SetPlaybackSpeed(float PlaybackSpeed);
        void StartStream();
        void PauseStream();
        void StopStream();
    };
}