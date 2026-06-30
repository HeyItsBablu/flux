// flux_mic_macos.mm
// macOS AVAudioEngine input-tap capture backend for FluxMic.
// Must be compiled as Objective-C++.
// Link against: AVFoundation
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <AVFoundation/AVFoundation.h>
#include "flux/flux_mic.hpp"
#include <atomic>
#include <cmath>
#include <vector>

struct FluxMic::Impl
{
    AVAudioEngine *engine = nil;

    int sampleRate = FluxMic::kDefaultSampleRate;
    int channels   = FluxMic::kDefaultChannels;

    std::atomic<bool> opened{false};
    std::atomic<bool> capturing{false};
    std::atomic<float> level{0.f};

    FrameCallback cb;
    std::vector<float> monoBuf;

    bool doOpen(int sr, int ch)
    {
        sampleRate = sr;
        channels   = ch;
        engine = [[AVAudioEngine alloc] init];
        opened = (engine != nil);
        return opened.load();
    }

    void installTap()
    {
        AVAudioInputNode *input = engine.inputNode;
        AVAudioFormat *inputFormat = [input outputFormatForBus:0];

        AVAudioFormat *targetFormat = [[AVAudioFormat alloc]
            initWithCommonFormat:AVAudioPCMFormatFloat32
                      sampleRate:(double)sampleRate
                        channels:1
                     interleaved:NO];

        __weak Impl *weakSelfNotUsed = nullptr; (void)weakSelfNotUsed;
        Impl *self = this;

        [input installTapOnBus:0
                     bufferSize:1024
                         format:inputFormat
                          block:^(AVAudioPCMBuffer *buffer, AVAudioTime *) {
            if (!self->capturing.load()) return;

            AVAudioFrameCount frames = buffer.frameLength;
            int srcCh = (int)buffer.format.channelCount;

            self->monoBuf.resize(frames);
            float **chData = buffer.floatChannelData;
            for (AVAudioFrameCount i = 0; i < frames; i++)
            {
                float sum = 0.f;
                for (int c = 0; c < srcCh; c++)
                    sum += chData[c][i];
                self->monoBuf[i] = sum / (float)srcCh;
            }

            float rms = 0.f;
            for (float s : self->monoBuf) rms += s * s;
            self->level.store(std::sqrt(rms / (float)std::max<AVAudioFrameCount>(1, frames)));

            if (self->cb) self->cb(self->monoBuf.data(), self->monoBuf.size());
        }];
    }
};

FluxMic &FluxMic::get() { static FluxMic inst; return inst; }
FluxMic::FluxMic() : m_impl(new Impl()) {}
FluxMic::~FluxMic() { close(); delete m_impl; }

bool FluxMic::open(int sampleRate, int channels)
{
    if (m_impl->opened.load()) return true;
    return m_impl->doOpen(sampleRate, channels);
}

bool FluxMic::start(FrameCallback cb)
{
    if (!m_impl->opened.load() && !open(m_impl->sampleRate, m_impl->channels))
        return false;
    if (m_impl->capturing.load()) return true;

    m_impl->cb = std::move(cb);
    m_impl->installTap();

    NSError *err = nil;
    if (![m_impl->engine startAndReturnError:&err])
    {
        NSLog(@"[FluxMic] engine start failed: %@", err);
        return false;
    }
    m_impl->capturing = true;
    return true;
}

void FluxMic::stop()
{
    if (!m_impl->capturing.load()) return;
    m_impl->capturing = false;
    [m_impl->engine.inputNode removeTapOnBus:0];
    [m_impl->engine stop];
    m_impl->cb = nullptr;
    m_impl->level = 0.f;
}

void FluxMic::close()
{
    stop();
    m_impl->engine = nil;
    m_impl->opened = false;
}

FluxMic::State FluxMic::getState() const
{
    if (!m_impl->opened.load()) return State::Idle;
    return m_impl->capturing.load() ? State::Recording : State::Open;
}
bool  FluxMic::isRecording()   const { return m_impl->capturing.load(); }
float FluxMic::getInputLevel() const { return m_impl->level.load(); }
int   FluxMic::getSampleRate() const { return m_impl->sampleRate; }
int   FluxMic::getChannels()   const { return m_impl->channels; }

#endif // TARGET_OS_OSX
#endif // __APPLE__