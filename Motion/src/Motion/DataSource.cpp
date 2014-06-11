#ifndef MOTION_DATASOURCE_CPP
#define MOTION_DATASOURCE_CPP

#include <Motion/DataSource.h>
#include <Motion/DataSource.hpp>

#include <Motion/DataSourceStruct.h>

#define MAX_AUDIO_SAMPLES 192000
#define PACKET_QUEUE_AMOUNT 10

namespace mt
{
    DataSource::DataSource() :
        m_videostreamid(-1),
        m_audiostreamid(-1),
        m_videolength(-1),
        m_videosize(-1, -1),
        m_audiochannelcount(-1),
        m_formatcontext(nullptr),
        m_videocontext(nullptr),
        m_audiocontext(nullptr),
        m_videocodec(nullptr),
        m_audiocodec(nullptr),
        m_videorawframe(nullptr),
        m_videorgbaframe(nullptr),
        m_audiorawbuffer(nullptr),
        m_videorawbuffer(nullptr),
        m_videorgbabuffer(nullptr),
        m_audiopcmbuffer(nullptr),
        m_videoswcontext(nullptr),
        m_audioswcontext(nullptr),
        m_state(mt::DataSource::Stopped),
        m_decodethread(nullptr),
        m_shouldthreadrun(false),
        m_playingtoeof(false),
        m_decodedqueuelock(),
        m_decodedvideopackets(),
        m_decodedaudiopackets()
    {
        av_register_all();
    }

    DataSource::~DataSource()
    {
        Cleanup();
    }

    void DataSource::Cleanup()
    {
        Stop();
        StopDecodeThread();
        m_videostreamid = -1;
        m_audiostreamid = -1;
        m_videolength = -1;
        m_videosize = { -1, -1 };
        m_audiochannelcount = -1;
        if (m_videocontext)
        {
            avcodec_close(m_videocontext);
            m_videocontext = nullptr;
        }
        m_videocodec = nullptr;
        if (m_audiocontext)
        {
            avcodec_close(m_audiocontext);
            m_audiocontext = nullptr;
        }
        m_audiocodec = nullptr;
        if (m_videorawframe) DestroyPictureFrame(m_videorawframe, m_videorawbuffer);
        if (m_videorgbaframe) DestroyPictureFrame(m_videorgbaframe, m_videorgbabuffer);
        if (m_audiorawbuffer)
        {
            avcodec_free_frame(&m_audiorawbuffer);
            m_audiorawbuffer = nullptr;
        }
        if (m_audiopcmbuffer)
        {
            av_free(m_audiopcmbuffer);
            m_audiopcmbuffer = nullptr;
        }
        if (m_videoswcontext)
        {
            sws_freeContext(m_videoswcontext);
            m_videoswcontext = nullptr;
        }
        if (m_audioswcontext)
        {
            swr_free(&m_audioswcontext);
            m_audioswcontext = nullptr;
        }
        if (m_formatcontext)
        {
            av_close_input_file(m_formatcontext);
            m_formatcontext = nullptr;
        }
        m_playingtoeof = false;
        {
            sf::Lock lock(m_decodedqueuelock);
            while (m_decodedvideopackets.size() > 0)
            {
                m_decodedvideopackets.pop();
            }
            while (m_decodedaudiopackets.size() > 0)
            {
                m_decodedaudiopackets.pop();
            }
        }
    }

    bool DataSource::LoadFromFile(const std::string& Filename, bool EnableVideo, bool EnableAudio)
    {
        Cleanup();
        if (avformat_open_input(&m_formatcontext, Filename.c_str(), nullptr, nullptr) != 0)
        {
            return false;
        }
        if (avformat_find_stream_info(m_formatcontext, nullptr) < 0)
        {
            return false;
        }
        for (unsigned int i = 0; i < m_formatcontext->nb_streams; i++)
        {
            switch (m_formatcontext->streams[i]->codec->codec_type)
            {
                case AVMEDIA_TYPE_VIDEO:
                    if (m_videostreamid == -1 && EnableVideo) m_videostreamid = i;
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    if (m_audiostreamid == -1 && EnableAudio) m_audiostreamid = i;
                    break;
                default:
                    break;
            }
        }
        if (HasVideo())
        {
            m_videocontext = m_formatcontext->streams[m_videostreamid]->codec;
            if (!m_videocontext) m_videostreamid = -1;
            else
            {
                m_videocodec = avcodec_find_decoder(m_videocontext->codec_id);
                if (!m_videocodec) m_videostreamid = -1;
                else
                {
                    if (avcodec_open2(m_videocontext, m_videocodec, nullptr) != 0) m_videostreamid = -1;
                    else
                    {
                        m_videosize = { m_videocontext->width, m_videocontext->height };
                        m_videorawframe = CreatePictureFrame(m_videocontext->pix_fmt, m_videosize.x, m_videosize.y, m_videorawbuffer);
                        m_videorgbaframe = CreatePictureFrame(PIX_FMT_RGBA, m_videosize.x, m_videosize.y, m_videorgbabuffer);
                        if (!m_videorawframe || !m_videorgbaframe) m_videostreamid = -1;
                        else
                        {
                            int swapmode = SWS_FAST_BILINEAR;
                            if (m_videosize.x * m_videosize.y <= 500000 && m_videosize.x % 8 != 0) swapmode |= SWS_ACCURATE_RND;
                            m_videoswcontext = sws_getCachedContext(nullptr, m_videosize.x, m_videosize.y, m_videocontext->pix_fmt, m_videosize.x, m_videosize.y, PIX_FMT_RGBA, swapmode, nullptr, nullptr, nullptr);
                        }
                    }
                }
            }
        }
        if (HasAudio())
        {
            m_audiocontext = m_formatcontext->streams[m_audiostreamid]->codec;
            if (!m_audiocontext) m_audiostreamid = -1;
            else
            {
                m_audiocodec = avcodec_find_decoder(m_audiocontext->codec_id);
                if (!m_audiocodec) m_audiostreamid = -1;
                else
                {
                    if (avcodec_open2(m_audiocontext, m_audiocodec, nullptr) != 0) m_audiostreamid = -1;
                    else
                    {
                        m_audiorawbuffer = avcodec_alloc_frame();
                        if (!m_audiorawbuffer) m_audiostreamid = -1;
                        else
                        {
                            if (av_samples_alloc(&m_audiopcmbuffer, nullptr, m_audiocontext->channels, av_samples_get_buffer_size(nullptr, m_audiocontext->channels, MAX_AUDIO_SAMPLES, AV_SAMPLE_FMT_S16, 0), AV_SAMPLE_FMT_S16, 0) < 0) m_audiostreamid = -1;
                            else
                            {
                                avcodec_get_frame_defaults(m_audiorawbuffer);
                                m_audioswcontext = swr_alloc();
                                uint64_t inchanlayout = m_audiocontext->channel_layout;
                                if (inchanlayout == 0) inchanlayout = av_get_default_channel_layout(m_audiocontext->channels);
                                uint64_t outchanlayout = inchanlayout;
                                if (outchanlayout != AV_CH_LAYOUT_MONO) outchanlayout = AV_CH_LAYOUT_STEREO;
                                av_opt_set_int(m_audioswcontext, "in_channel_layout", inchanlayout, 0);
                                av_opt_set_int(m_audioswcontext, "out_channel_layout", outchanlayout, 0);
                                av_opt_set_int(m_audioswcontext, "in_sample_rate", m_audiocontext->sample_rate, 0);
                                av_opt_set_int(m_audioswcontext, "out_sample_rate", m_audiocontext->sample_rate, 0);
                                av_opt_set_sample_fmt(m_audioswcontext, "in_sample_fmt", m_audiocontext->sample_fmt, 0);
                                av_opt_set_sample_fmt(m_audioswcontext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                                swr_init(m_audioswcontext);
                                m_audiochannelcount = av_get_channel_layout_nb_channels(outchanlayout);
                            }
                        }
                    }
                }
            }
        }
        if (m_formatcontext->duration != AV_NOPTS_VALUE) m_videolength = m_formatcontext->duration;
        if (HasVideo() || HasAudio())
        {
            StartDecodeThread();
            return true;
        }
        else
        {
            Cleanup();
            return false;
        }
    }

    bool DataSource::HasVideo()
    {
        return m_videostreamid != -1;
    }

    bool DataSource::HasAudio()
    {
        return m_audiostreamid != -1;
    }

    void DataSource::Play()
    {

    }

    void DataSource::Pause()
    {

    }

    void DataSource::Stop()
    {

    }

    void DataSource::StartDecodeThread()
    {
        if (m_shouldthreadrun) return;
        m_shouldthreadrun = true;
        m_decodethread.reset(new std::thread(&DataSource::DecodeThreadRun, this));
    }

    void DataSource::StopDecodeThread()
    {
        if (!m_shouldthreadrun) return;
        m_shouldthreadrun = false;
        if (m_decodethread->joinable()) m_decodethread->join();
        m_decodethread.reset(nullptr);
    }

    void DataSource::DecodeThreadRun()
    {
        while (m_shouldthreadrun)
        {
            bool shoulddecode;
            {
                sf::Lock lock(m_decodedqueuelock);
                if (((m_decodedvideopackets.size() < PACKET_QUEUE_AMOUNT && HasVideo()) || (m_decodedaudiopackets.size() < PACKET_QUEUE_AMOUNT && HasAudio())) && !m_playingtoeof) shoulddecode = true;
                else shoulddecode = false;
            }
            while (shoulddecode && m_shouldthreadrun)
            {
                bool validpacket = false;
                while (!validpacket && m_shouldthreadrun)
                {
                    AVPacket* packet;
                    packet = (AVPacket*)av_malloc(sizeof(*packet));
                    av_init_packet(packet);
                    if (av_read_frame(m_formatcontext, packet) == 0)
                    {
                        if (packet->stream_index == m_videostreamid)
                        {
                            int decoderesult = 0;
                            if (avcodec_decode_video2(m_videocontext, m_videorawframe, &decoderesult, packet) >= 0)
                            {
                                if (decoderesult)
                                {
                                    sws_scale(m_videoswcontext, m_videorawframe->data, m_videorawframe->linesize, 0, m_videocontext->height, m_videorgbaframe->data, m_videorgbaframe->linesize);
                                    validpacket = true;
                                    priv::VideoPacketPtr packet(std::make_shared<priv::VideoPacket>(m_videorgbaframe->data[0], m_videosize.x, m_videosize.y));
                                    {
                                        sf::Lock lock(m_decodedqueuelock);
                                        m_decodedvideopackets.push(packet);
                                        if (((m_decodedvideopackets.size() < PACKET_QUEUE_AMOUNT && HasVideo()) || (m_decodedaudiopackets.size() < PACKET_QUEUE_AMOUNT && HasAudio())) && !m_playingtoeof) shoulddecode = true;
                                        else shoulddecode = false;
                                    }
                                }
                            }
                        }
                        else if (packet->stream_index == m_audiostreamid)
                        {
                            int decoderesult = 0;
                            if (avcodec_decode_audio4(m_audiocontext, m_audiorawbuffer, &decoderesult, packet) > 0)
                            {
                                if (decoderesult)
                                {
                                    int convertlength = swr_convert(m_audioswcontext, &m_audiopcmbuffer, m_audiorawbuffer->nb_samples, (const uint8_t**)m_audiorawbuffer->extended_data, m_audiorawbuffer->nb_samples);
                                    if (convertlength > 0)
                                    {
                                        validpacket = true;
                                        priv::AudioPacketPtr packet(std::make_shared<priv::AudioPacket>(m_audiopcmbuffer, convertlength, m_audiochannelcount));
                                        {
                                            sf::Lock lock(m_decodedqueuelock);
                                            m_decodedaudiopackets.push(packet);
                                            if (((m_decodedvideopackets.size() < PACKET_QUEUE_AMOUNT && HasVideo()) || (m_decodedaudiopackets.size() < PACKET_QUEUE_AMOUNT && HasAudio())) && !m_playingtoeof) shoulddecode = true;
                                            else shoulddecode = false;
                                        }
                                    }
                                }
                            }
                        }
                        av_free_packet(packet);
                        av_free(packet);
                    }
                    else
                    {
                        m_playingtoeof = true;
                        validpacket = true;
                        av_free_packet(packet);
                        av_free(packet);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    AVFrame* DataSource::CreatePictureFrame(enum PixelFormat SelectedPixelFormat, int Width, int Height, uint8_t*& PictureBuffer)
    {
        AVFrame *picture;
        picture = avcodec_alloc_frame();
        if (!picture) return nullptr;
        int size = avpicture_get_size(SelectedPixelFormat, Width, Height);
        PictureBuffer = (uint8_t*)av_malloc(size);
        if (!PictureBuffer)
        {
            avcodec_free_frame(&picture);
            return nullptr;
        }
        avpicture_fill((AVPicture*)picture, PictureBuffer, SelectedPixelFormat, Width, Height);
        return picture;
    }

    void DataSource::DestroyPictureFrame(AVFrame*& PictureFrame, uint8_t*& PictureBuffer)
    {
        av_free(PictureBuffer);
        avcodec_free_frame(&PictureFrame);
        PictureBuffer = nullptr;
        PictureFrame = nullptr;
    }
}

mtDataSource* mtDataSource_Create(void)
{
    mtDataSource* datasource = new mtDataSource();
    datasource->Value = new mt::DataSource();
    return datasource;
}

void mtDataSource_Destroy(mtDataSource* DataSource)
{
    delete DataSource->Value;
    delete DataSource;
}

sfBool mtDataSource_LoadFromFile(mtDataSource* DataSource, const char* Filename, sfBool EnableVideo, sfBool EnableAudio)
{
    return DataSource->Value->LoadFromFile(Filename, EnableVideo == sfTrue, EnableAudio == sfTrue);
}

#endif
