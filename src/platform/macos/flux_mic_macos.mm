// flux_mic_macos.mm
// macOS AVAudioEngine microphone backend for FluxMic.
//
// This file MUST be compiled as Objective-C++ (.mm).
// It is the only file in the project that may include ObjC / AVFoundation headers.
// All other TUs use only the extern "C" bridge declared in flux_mic.hpp.
//
// Link against: AVFoundation  (already linked by the flux CMakeLists Apple block)
//
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX

// ObjC / Apple SDK headers — kept strictly in this .mm file
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

// C++ headers
#include "flux/flux_mic.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

// ============================================================================
// Logging (stderr — no ObjC runtime needed by callers)
// ============================================================================
#define MIC_LOGI(fmt, ...) fprintf(stderr, "[FluxMic] " fmt "\n", ##__VA_ARGS__)
#define MIC_LOGE(fmt, ...) MIC_LOGI(fmt, ##__VA_ARGS__)

// ============================================================================
// FluxMacBridgeImpl — ObjC object that owns the AVAudioEngine
// ============================================================================
@interface FluxMacBridgeImpl : NSObject {
@public
    AVAudioEngine*       engine;
    AVAudioInputNode*    inputNode;

    // Pointers into the C++ FluxMic::Impl — set on open, never moved.
    float*                    ring;
    size_t                    ringSize;
    std::atomic<size_t>*      writePos;
    std::atomic<float>*       amplitude;
    std::atomic<bool>*        stopFlag;
    std::atomic<int>*         stateInt;
}
@end

@implementation FluxMacBridgeImpl
- (instancetype)init {
    self = [super init];
    if (self) {
        engine    = [[AVAudioEngine alloc] init];
        inputNode = engine.inputNode;
    }
    return self;
}
@end

// ============================================================================
// Save-directory helper
// ============================================================================

// Returns ~/Music/FluxMic (sandbox-safe on macOS).
// Override: define FLUXMIC_SAVE_DIR and provide your own FluxMac_getMicSaveDir()
// in another .mm file before this one is compiled.
#ifndef FLUXMIC_SAVE_DIR
static std::string FluxMac_getMicSaveDir() {
    NSArray<NSString*>* dirs =
        NSSearchPathForDirectoriesInDomains(NSMusicDirectory, NSUserDomainMask, YES);
    NSString* base = dirs.firstObject ?: NSHomeDirectory();
    NSString* dir  = [base stringByAppendingPathComponent:@"FluxMic"];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    return dir.UTF8String;
}
#endif

// ============================================================================
// C bridge — called from the C++ FluxMic implementation
// ============================================================================

extern "C"
void* FluxMacBridge_open(float*               ring,
                          size_t               ringSize,
                          std::atomic<size_t>* writePos,
                          std::atomic<float>*  amplitude,
                          std::atomic<bool>*   stopFlag,
                          std::atomic<int>*    stateInt)
{
    FluxMacBridgeImpl* b = [[FluxMacBridgeImpl alloc] init];
    b->ring      = ring;
    b->ringSize  = ringSize;
    b->writePos  = writePos;
    b->amplitude = amplitude;
    b->stopFlag  = stopFlag;
    b->stateInt  = stateInt;

    AVAudioFormat* fmt = [b->inputNode inputFormatForBus:0];
    double         sr  = fmt.sampleRate;
    uint32_t       ch  = fmt.channelCount;

    // RMS window = 50 ms at the device sample rate
    __block float    rmsAccum   = 0.f;
    __block int      rmsSamples = 0;
    const   int      kRmsWindow = (int)(sr / 20.0);

    [b->inputNode installTapOnBus:0
                       bufferSize:512
                           format:fmt
                            block:^(AVAudioPCMBuffer* buf, AVAudioTime* /*when*/)
    {
        if (stopFlag->load()) return;

        uint32_t frames = buf.frameLength;
        if (frames == 0) return;

        const float* const* chData = buf.floatChannelData;
        if (!chData) return;

        size_t wp    = writePos->load();
        size_t space = ringSize - wp;
        size_t n     = (frames < space) ? frames : space;

        for (size_t i = 0; i < n; i++) {
            // Mix down to mono
            float mono = 0.f;
            for (uint32_t c = 0; c < ch; c++)
                mono += chData[c][i];
            if (ch > 1) mono /= (float)ch;

            ring[wp + i] = mono;
            rmsAccum    += mono * mono;
            rmsSamples++;
            if (rmsSamples >= kRmsWindow) {
                float rms = std::sqrt(rmsAccum / rmsSamples);
                amplitude->store(amplitude->load() * 0.7f + rms * 0.3f);
                rmsAccum   = 0.f;
                rmsSamples = 0;
            }
        }

        writePos->store(wp + n);

        if (wp + n >= ringSize) {
            MIC_LOGI("Max duration reached in tap");
            stopFlag->store(true);
            stateInt->store(2 /*Stopping*/);
        }
    }];

    NSError* err = nil;
    if (![b->engine startAndReturnError:&err]) {
        MIC_LOGE("AVAudioEngine start failed: %s",
                 err.localizedDescription.UTF8String ?: "unknown");
        [b->inputNode removeTapOnBus:0];
        return nullptr;
    }

    MIC_LOGI("AVAudioEngine started: %.0f Hz  %u ch", sr, ch);
    return (__bridge_retained void*)b;
}

extern "C"
void FluxMacBridge_close(void* bridge) {
    if (!bridge) return;
    FluxMacBridgeImpl* b = (__bridge FluxMacBridgeImpl*)bridge;
    [b->inputNode removeTapOnBus:0];
    [b->engine stop];
    MIC_LOGI("AVAudioEngine stopped");
}

extern "C"
void FluxMacBridge_destroy(void* bridge) {
    if (!bridge) return;
    // Transfer the +1 retain count back so ARC releases the object.
    FluxMacBridgeImpl* __unused b =
        (__bridge_transfer FluxMacBridgeImpl*)bridge;
}

extern "C"
char* FluxMacBridge_getSaveDir() {
    std::string dir = FluxMac_getMicSaveDir();
    char* out = (char*)malloc(dir.size() + 1);
    if (out) memcpy(out, dir.c_str(), dir.size() + 1);
    return out;
}

// ============================================================================
// FluxMic::Impl — thin wrapper; all heavy lifting is in the ObjC bridge above
// ============================================================================
struct FluxMic::Impl {
    std::vector<float>   ring;
    std::atomic<size_t>  writePos  { 0 };
    std::atomic<float>   amplitude { 0.f };

    void*             bridge   = nullptr;
    std::thread       watchdog;
    std::atomic<bool> stopFlag { false };
    // Integer mirror of State; the ObjC tap callback writes State::Stopping (=2).
    std::atomic<int>  stateInt { 0 };
    std::string       lastSavedPath;
    SaveCallback      onSaved;

    void watchdogLoop() {
        MIC_LOGI("Watchdog started");
        while (!stopFlag.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        MIC_LOGI("Watchdog exited — %zu samples (%.2f s)",
                 writePos.load(),
                 (float)writePos.load() / FluxMic::kSampleRate);
    }

    void closeBridge() {
        if (!bridge) return;
        FluxMacBridge_close(bridge);
        FluxMacBridge_destroy(bridge);
        bridge = nullptr;
    }

    void saveWAV() {
        size_t n = writePos.load();
        if (n == 0) { MIC_LOGE("Nothing captured — no WAV written"); return; }

        char* dirC = FluxMacBridge_getSaveDir();
        std::string dir = dirC ? dirC : ".";
        free(dirC);

        std::string path = WavWriter::makeTimestampedPath(dir);
        MIC_LOGI("Saving WAV: %s (%zu samples, %.2f s)",
                 path.c_str(), n, (float)n / FluxMic::kSampleRate);

        std::string result = WavWriter::write(path, ring.data(), n,
                                              (uint32_t)FluxMic::kSampleRate, 1);
        if (!result.empty()) {
            lastSavedPath = result;
            if (onSaved) onSaved(result);
        } else {
            MIC_LOGE("WAV write FAILED: %s", path.c_str());
        }
    }
};

// ============================================================================
// FluxMic public API
// ============================================================================

FluxMic& FluxMic::get() { static FluxMic inst; return inst; }

FluxMic::FluxMic()  : m_impl(new Impl()) { m_impl->ring.reserve(kRingSize); }
FluxMic::~FluxMic() { cancel(); delete m_impl; }

FluxMic::State FluxMic::getState() const {
    return static_cast<State>(m_impl->stateInt.load());
}
bool  FluxMic::isRecording()  const { return m_impl->stateInt.load() == 1; }
float FluxMic::getAmplitude() const { return m_impl->amplitude.load(); }
float FluxMic::getProgress()  const {
    return std::min(1.f, (float)m_impl->writePos.load() / (float)kRingSize);
}
float FluxMic::getElapsedSeconds() const {
    return (float)m_impl->writePos.load() / (float)kSampleRate;
}
const std::string& FluxMic::getLastSavedPath() const { return m_impl->lastSavedPath; }
void FluxMic::setOnSaved(SaveCallback cb) { m_impl->onSaved = std::move(cb); }

bool FluxMic::start() {
    if (m_impl->stateInt.load() == 1) return true;   // Recording
    if (m_impl->stateInt.load() == 2) return false;  // Stopping

    m_impl->ring.assign(kRingSize, 0.f);
    m_impl->writePos  = 0;
    m_impl->amplitude = 0.f;
    m_impl->stopFlag  = false;

    m_impl->bridge = FluxMacBridge_open(m_impl->ring.data(), kRingSize,
                                        &m_impl->writePos, &m_impl->amplitude,
                                        &m_impl->stopFlag, &m_impl->stateInt);
    if (!m_impl->bridge) { m_impl->stateInt = 3 /*Error*/; return false; }

    m_impl->stateInt = 1; // Recording
    m_impl->watchdog = std::thread(&Impl::watchdogLoop, m_impl);
    MIC_LOGI("Recording started — max %d s", kMaxSeconds);
    return true;
}

void FluxMic::stop() {
    if (m_impl->stateInt.load() != 1) return;
    m_impl->stateInt = 2; // Stopping
    m_impl->stopFlag = true;
    if (m_impl->watchdog.joinable()) m_impl->watchdog.join();
    m_impl->closeBridge();
    m_impl->saveWAV();
    m_impl->stateInt = 0; // Idle
}

void FluxMic::cancel() {
    if (m_impl->stateInt.load() != 1) return;
    m_impl->stateInt = 2;
    m_impl->stopFlag = true;
    if (m_impl->watchdog.joinable()) m_impl->watchdog.join();
    m_impl->closeBridge();
    m_impl->writePos  = 0;
    m_impl->amplitude = 0.f;
    m_impl->stateInt  = 0;
    MIC_LOGI("Recording cancelled");
}

void FluxMic::getWaveform(std::vector<float>& out, size_t count) const {
    FluxMicDetail::buildWaveform(m_impl->ring, m_impl->writePos.load(), out, count);
}

#endif // TARGET_OS_OSX
#endif // __APPLE__