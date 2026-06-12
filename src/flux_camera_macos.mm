// flux_camera_macos.mm
// macOS AVFoundation camera capture engine for FluxUI.
//
// Pipeline:
//   AVCaptureDevice → AVCaptureSession
//       └── Preview  → AVCaptureVideoDataOutput → CVPixelBuffer (BGRA32)
//                      → CPU copy → lockFrame() → NSImage / CoreGraphics blit
//       └── Capture  → AVCapturePhotoOutput → CGImageDestination JPEG encode
//                      → save to ~/Pictures/FluxCam/
//
// Compile flags: -x objective-c++ -fobjc-arc  (set in CMakeLists via SKILL)
// Frameworks:    AVFoundation, CoreMedia, CoreVideo, CoreGraphics,
//                Foundation, AppKit

#if defined(__APPLE__) && !defined(__IPHONE_OS_VERSION_MIN_REQUIRED)

#import  <AVFoundation/AVFoundation.h>
#import  <CoreMedia/CoreMedia.h>
#import  <CoreVideo/CoreVideo.h>
#import  <CoreGraphics/CoreGraphics.h>
#import  <ImageIO/ImageIO.h>
#import  <Foundation/Foundation.h>
#import  <AppKit/AppKit.h>

#include "flux/flux_camera.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

// ── Logging helpers ─────────────────────────────────────────────────────────

#define CAM_LOGI(...) do { \
    NSLog(@"[FluxCamera] " @__VA_ARGS__); \
} while(0)
#define CAM_LOGE(...) do { \
    NSLog(@"[FluxCamera][ERR] " @__VA_ARGS__); \
} while(0)

// ── Forward declarations ────────────────────────────────────────────────────

@class FluxVideoCaptureDelegate;
@class FluxPhotoCaptureDelegate;

// ============================================================================
// Internal Objective-C state — stored as a plain struct of strong references
// inside the FluxCamera singleton via a void* _macosImpl pointer.
//
// We use a plain struct rather than a dedicated ObjC class so that the C++
// header stays free of ObjC types (important for mixed-language TUs).
// ============================================================================

struct MacOSImpl {
    AVCaptureSession*            session              = nil;
    AVCaptureDeviceInput*        currentInput         = nil;
    AVCaptureVideoDataOutput*    videoOutput          = nil;
    AVCapturePhotoOutput*        photoOutput          = nil;
    FluxVideoCaptureDelegate*    videoDelegate        = nil;
    FluxPhotoCaptureDelegate*    photoDelegate        = nil;
    dispatch_queue_t             videoQueue           = nullptr;

    // Frame buffer — BGRA32, protected by _frameMutex in FluxCamera
    std::vector<uint8_t>         frameData;

    // Device list snapshots (index mirrors _cameraIndex)
    NSArray<AVCaptureDevice*>*   devices              = nil;
    int                          deviceCount          = 0;
};

// ============================================================================
// AVCaptureVideoDataOutputSampleBufferDelegate  (preview frames)
// ============================================================================

@interface FluxVideoCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, assign) FluxCamera* owner;
@end

@implementation FluxVideoCaptureDelegate

- (void)captureOutput:(AVCaptureOutput*)__unused output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection*)__unused connection
{
    FluxCamera* cam = self.owner;
    if (!cam || cam->getState() == FluxCamera::State::Closed) return;

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) return;

    CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

    int    w      = (int)CVPixelBufferGetWidth(imageBuffer);
    int    h      = (int)CVPixelBufferGetHeight(imageBuffer);
    size_t stride = CVPixelBufferGetBytesPerRow(imageBuffer);
    uint8_t* src  = static_cast<uint8_t*>(
                        CVPixelBufferGetBaseAddress(imageBuffer));

    if (src && w > 0 && h > 0) {
        MacOSImpl* impl = static_cast<MacOSImpl*>(cam->_macosImpl);

        // Copy under the frame mutex
        {
            std::lock_guard<std::mutex> lock(cam->_frameMutex);
            impl->frameData.resize((size_t)(w * h * 4));
            // stride may be padded — copy row-by-row
            for (int row = 0; row < h; ++row) {
                std::memcpy(impl->frameData.data() + row * w * 4,
                            src + row * stride,
                            (size_t)(w * 4));
            }
        }

        cam->_previewW   = w;
        cam->_previewH   = h;
        cam->_pendingFrame = true;

        // If a capture was requested, save the current frame as JPEG.
        // (Real photo capture uses AVCapturePhotoOutput below; this path
        //  acts as a fast fallback when photoOutput is unavailable.)
        if (cam->_captureRequested.exchange(false)) {
            cam->_saveJpegFromBGRA(impl->frameData.data(), w, h);
        }
    }

    CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
}

- (void)captureOutput:(AVCaptureOutput*)__unused output
  didDropSampleBuffer:(CMSampleBufferRef)__unused sampleBuffer
       fromConnection:(AVCaptureConnection*)__unused connection
{
    // Silently drop; no action needed.
}

@end

// ============================================================================
// AVCapturePhotoCaptureDelegate  (still capture via AVCapturePhotoOutput)
// ============================================================================

@interface FluxPhotoCaptureDelegate : NSObject <AVCapturePhotoCaptureDelegate>
@property (nonatomic, assign) FluxCamera* owner;
@end

@implementation FluxPhotoCaptureDelegate

- (void)captureOutput:(AVCapturePhotoOutput*)__unused output
didFinishProcessingPhoto:(AVCapturePhoto*)photo
                error:(NSError*)error
{
    FluxCamera* cam = self.owner;
    if (!cam) return;

    if (error) {
        CAM_LOGE("AVCapturePhoto error: %s", error.localizedDescription.UTF8String);
        cam->_state = FluxCamera::State::Previewing;
        return;
    }

    NSData* jpegData = [photo fileDataRepresentation];
    if (!jpegData) {
        CAM_LOGE("fileDataRepresentation returned nil");
        cam->_state = FluxCamera::State::Previewing;
        return;
    }

    // Build timestamped filename
    std::time_t t  = std::time(nullptr);
    std::tm     tm = {};
    localtime_r(&t, &tm);
    char fname[64];
    std::strftime(fname, sizeof(fname), "IMG_%Y%m%d_%H%M%S.jpg", &tm);

    std::string path = FluxMac_saveJpegToPictures(
            fname,
            static_cast<const uint8_t*>(jpegData.bytes),
            jpegData.length);

    if (!path.empty()) {
        cam->_lastPhotoPath = path;
        CAM_LOGI("Photo saved: %s (%zu bytes)", fname, (size_t)jpegData.length);
        if (cam->_onPhoto) cam->_onPhoto(path);
    }

    cam->_state = FluxCamera::State::Previewing;
}

@end

// ============================================================================
// File-save helper (forward-declared below, defined at bottom of file)
// ============================================================================

static std::string FluxMac_saveJpegToPictures(const std::string& filename,
                                               const uint8_t* data,
                                               size_t length);

// ============================================================================
// Singleton
// ============================================================================

FluxCamera& FluxCamera::get() {
    static FluxCamera instance;
    return instance;
}

bool FluxCamera::isFrontCamera() const {
    MacOSImpl* impl = static_cast<MacOSImpl*>(_macosImpl);
    if (!impl || !impl->currentInput) return false;

    AVCaptureDevice* dev = impl->currentInput.device;
    // On Mac, "front" ≈ built-in FaceTime camera (position == front)
    return (dev.position == AVCaptureDevicePositionFront);
}

// ── Frame access ─────────────────────────────────────────────────────────────

FluxCamera::FrameLock FluxCamera::lockFrame() {
    MacOSImpl* impl = static_cast<MacOSImpl*>(_macosImpl);
    FrameLock fl;
    fl.lock   = std::unique_lock<std::mutex>(_frameMutex);
    fl.width  = _previewW.load();
    fl.height = _previewH.load();
    fl.stride = fl.width * 4;
    fl.data   = (impl && !impl->frameData.empty()) ? impl->frameData.data()
                                                    : nullptr;
    return fl;
}

// ── Flash ─────────────────────────────────────────────────────────────────────

void FluxCamera::setFlash(bool on) {
    _flashOn = on;
    _applyFlash();
}

void FluxCamera::_applyFlash() {
    MacOSImpl* impl = static_cast<MacOSImpl*>(_macosImpl);
    if (!impl || !impl->currentInput) return;

    AVCaptureDevice* dev = impl->currentInput.device;
    if (![dev hasTorch]) return;

    NSError* err = nil;
    if ([dev lockForConfiguration:&err]) {
        if (_flashOn.load()) {
            if ([dev isTorchModeSupported:AVCaptureTorchModeOn])
                dev.torchMode = AVCaptureTorchModeOn;
        } else {
            if ([dev isTorchModeSupported:AVCaptureTorchModeOff])
                dev.torchMode = AVCaptureTorchModeOff;
        }
        [dev unlockForConfiguration];
    } else {
        CAM_LOGE("lockForConfiguration failed: %s",
                 err.localizedDescription.UTF8String);
    }
}

// =============================================================================
// OPEN
// =============================================================================

bool FluxCamera::open(bool useFront) {
    if (_state != State::Closed) close();

    _state = State::Opening;

    // Allocate impl struct
    MacOSImpl* impl = new MacOSImpl();
    _macosImpl = impl;

    // ── Enumerate devices ──────────────────────────────────────────────────
    // Prefer the Discovery session API (macOS 10.15+); fall back to
    // AVCaptureDevice.devices for older SDKs.

    NSMutableArray<AVCaptureDevice*>* devList = [NSMutableArray array];

    if (@available(macOS 10.15, *)) {
        NSArray<AVCaptureDeviceType>* types = @[
            AVCaptureDeviceTypeBuiltInWideAngleCamera,
            AVCaptureDeviceTypeExternalUnknown
        ];
        AVCaptureDeviceDiscoverySession* disc =
            [AVCaptureDeviceDiscoverySession
                discoverySessionWithDeviceTypes:types
                                      mediaType:AVMediaTypeVideo
                                       position:AVCaptureDevicePositionUnspecified];
        for (AVCaptureDevice* d in disc.devices) {
            [devList addObject:d];
        }
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        for (AVCaptureDevice* d in [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo]) {
            [devList addObject:d];
        }
#pragma clang diagnostic pop
    }

    if (devList.count == 0) {
        CAM_LOGE("No video capture devices found");
        _state = State::Error;
        return false;
    }

    impl->devices     = [devList copy];
    impl->deviceCount = (int)devList.count;

    // Pick device: prefer front/back based on argument; on Mac most built-ins
    // report AVCaptureDevicePositionFront (FaceTime HD).
    int idx = 0;
    if (useFront) {
        for (int i = 0; i < impl->deviceCount; ++i) {
            if (impl->devices[i].position == AVCaptureDevicePositionFront) {
                idx = i; break;
            }
        }
    }
    _cameraIndex = idx;

    if (![self _openDeviceAtIndex:idx]) {
        _state = State::Error;
        return false;
    }

    _state = State::Previewing;
    CAM_LOGI("Preview started (device %d: %s)",
             idx, impl->devices[idx].localizedName.UTF8String);
    return true;
}

// Internal — (re-)opens a device by index and (re-)wires the session.
// Must be called with _state != Previewing (caller manages state transitions).
bool FluxCamera::_openDeviceAtIndex(int index) {
    MacOSImpl* impl = static_cast<MacOSImpl*>(_macosImpl);
    if (!impl || index < 0 || index >= impl->deviceCount) return false;

    AVCaptureDevice* dev = impl->devices[index];

    // ── Session ────────────────────────────────────────────────────────────
    impl->session = [[AVCaptureSession alloc] init];
    [impl->session beginConfiguration];
    impl->session.sessionPreset = AVCaptureSessionPresetHigh;

    // ── Input ──────────────────────────────────────────────────────────────
    NSError* err = nil;
    impl->currentInput = [AVCaptureDeviceInput deviceInputWithDevice:dev error:&err];
    if (!impl->currentInput) {
        CAM_LOGE("deviceInputWithDevice failed: %s",
                 err.localizedDescription.UTF8String);
        return false;
    }
    if (![impl->session canAddInput:impl->currentInput]) {
        CAM_LOGE("canAddInput returned NO");
        return false;
    }
    [impl->session addInput:impl->currentInput];

    // ── Video output (preview frames → CPU BGRA32) ─────────────────────────
    impl->videoOutput = [[AVCaptureVideoDataOutput alloc] init];
    impl->videoOutput.alwaysDiscardsLateVideoFrames = YES;
    impl->videoOutput.videoSettings = @{
        (id)kCVPixelBufferPixelFormatTypeKey :
            @(kCVPixelFormatType_32BGRA)
    };

    impl->videoQueue = dispatch_queue_create(
            "com.fluxui.camera.video", DISPATCH_QUEUE_SERIAL);

    impl->videoDelegate = [[FluxVideoCaptureDelegate alloc] init];
    impl->videoDelegate.owner = this;

    [impl->videoOutput setSampleBufferDelegate:impl->videoDelegate
                                         queue:impl->videoQueue];

    if (![impl->session canAddOutput:impl->videoOutput]) {
        CAM_LOGE("canAddOutput(videoOutput) returned NO");
        return false;
    }
    [impl->session addOutput:impl->videoOutput];

    // Fix orientation for landscape-primary apps
    AVCaptureConnection* vidConn =
        [impl->videoOutput connectionWithMediaType:AVMediaTypeVideo];
    if (vidConn && [vidConn isVideoOrientationSupported])
        vidConn.videoOrientation = AVCaptureVideoOrientationLandscapeRight;

    // ── Photo output ───────────────────────────────────────────────────────
    impl->photoOutput = [[AVCapturePhotoOutput alloc] init];
    if ([impl->session canAddOutput:impl->photoOutput]) {
        [impl->session addOutput:impl->photoOutput];

        impl->photoDelegate = [[FluxPhotoCaptureDelegate alloc] init];
        impl->photoDelegate.owner = this;
    } else {
        CAM_LOGI("canAddOutput(photoOutput) returned NO — will use frame fallback");
        impl->photoOutput  = nil;
        impl->photoDelegate = nil;
    }

    [impl->session commitConfiguration];
    [impl->session startRunning];

    return true;
}

// =============================================================================
// FLIP
// =============================================================================

void FluxCamera::flipCamera() {
    MacOSImpl* impl = static_cast<MacOSImpl*>(_macosImpl);
    int count = impl ? impl->deviceCount : 0;
    int next  = (count > 1) ? ((_cameraIndex.load() + 1) % count) : 0;
    bool wantFront = false;
    if (impl && next < impl->deviceCount)
        wantFront = (impl->devices[next].position == AVCaptureDevicePositionFront);
    close();
    open(wantFront);
}

// =============================================================================
// UPDATE FRAME
// =============================================================================

bool FluxCamera::updateFrame() {
    if (!_pendingFrame.load()) { _newFrame = false; return false; }
    _pendingFrame = false;
    _newFrame     = true;
    return true;
}

// =============================================================================
// CAPTURE PHOTO
// =============================================================================

void FluxCamera::capturePhoto() {
    if (_state != State::Previewing) return;
    _state = State::Capturing;

    MacOSImpl* impl = static_cast<MacOSImpl*>(_macosImpl);

    if (impl && impl->photoOutput && impl->photoDelegate) {
        // Use AVCapturePhotoOutput for full-resolution still
        AVCapturePhotoSettings* settings;
        if (@available(macOS 11.0, *)) {
            settings = [AVCapturePhotoSettings
                photoSettingsWithFormat:@{ AVVideoCodecKey : AVVideoCodecTypeJPEG }];
        } else {
            settings = [AVCapturePhotoSettings photoSettings];
        }
        settings.flashMode = _flashOn.load() ? AVCaptureFlashModeOn
                                             : AVCaptureFlashModeOff;
        [impl->photoOutput capturePhotoWithSettings:settings
                                           delegate:impl->photoDelegate];
    } else {
        // Fallback: grab next preview frame as JPEG
        _captureRequested = true;
    }

    CAM_LOGI("Capture requested");
}

// =============================================================================
// CLOSE
// =============================================================================

void FluxCamera::close() {
    MacOSImpl* impl = static_cast<MacOSImpl*>(_macosImpl);

    if (impl && impl->session) {
        [impl->session stopRunning];

        // Detach delegates before releasing to prevent callbacks after close
        if (impl->videoOutput)
            [impl->videoOutput setSampleBufferDelegate:nil queue:nullptr];

        impl->session     = nil;
        impl->currentInput = nil;
        impl->videoOutput  = nil;
        impl->photoOutput  = nil;
        impl->videoDelegate = nil;
        impl->photoDelegate = nil;
        impl->devices      = nil;
        impl->frameData.clear();

        if (impl->videoQueue) {
            impl->videoQueue = nullptr; // ARC releases under ObjC; dispatch is C
        }
    }

    delete impl;
    _macosImpl = nullptr;

    _state            = State::Closed;
    _pendingFrame     = false;
    _newFrame         = false;
    _captureRequested = false;
    _previewW         = 0;
    _previewH         = 0;

    CAM_LOGI("Camera closed");
}

// =============================================================================
// JPEG ENCODE + SAVE via ImageIO / CoreGraphics
// =============================================================================

void FluxCamera::_saveJpegFromBGRA(const uint8_t* bgra, int w, int h) {
    if (!bgra || w <= 0 || h <= 0) return;

    // Wrap the raw BGRA pixels in a CGImage (no copy — lifetime is this scope)
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();

    CGContextRef ctx = CGBitmapContextCreate(
            const_cast<uint8_t*>(bgra),
            (size_t)w, (size_t)h,
            8,              // bits per component
            (size_t)(w * 4),
            cs,
            kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst
            // BGRA32 on little-endian = ByteOrder32Little | AlphaPremultipliedFirst
    );
    CGColorSpaceRelease(cs);

    if (!ctx) {
        CAM_LOGE("CGBitmapContextCreate failed");
        return;
    }

    CGImageRef cgImage = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);

    if (!cgImage) {
        CAM_LOGE("CGBitmapContextCreateImage failed");
        return;
    }

    // Build timestamped filename
    std::time_t t  = std::time(nullptr);
    std::tm     tm = {};
    localtime_r(&t, &tm);
    char fname[64];
    std::strftime(fname, sizeof(fname), "IMG_%Y%m%d_%H%M%S.jpg", &tm);

    // Encode to an in-memory CFMutableData
    CFMutableDataRef jpegData = CFDataCreateMutable(kCFAllocatorDefault, 0);
    CGImageDestinationRef dest = CGImageDestinationCreateWithData(
            jpegData,
            kUTTypeJPEG,
            1, nullptr);

    if (!dest) {
        CAM_LOGE("CGImageDestinationCreateWithData failed");
        CGImageRelease(cgImage);
        CFRelease(jpegData);
        return;
    }

    // JPEG quality = 0.90
    NSDictionary* props = @{ (id)kCGImageDestinationLossyCompressionQuality : @(0.90f) };
    CGImageDestinationAddImage(dest, cgImage, (__bridge CFDictionaryRef)props);
    CGImageDestinationFinalize(dest);

    CFRelease(dest);
    CGImageRelease(cgImage);

    std::string path = FluxMac_saveJpegToPictures(
            fname,
            CFDataGetBytePtr(jpegData),
            (size_t)CFDataGetLength(jpegData));

    CFRelease(jpegData);

    if (!path.empty()) {
        _lastPhotoPath = path;
        CAM_LOGI("Photo saved (BGRA fallback): %s", fname);
        if (_onPhoto) _onPhoto(path);
    }

    _state = State::Previewing;
}

// =============================================================================
// FluxMac_saveJpegToPictures
// Saves JPEG bytes to ~/Pictures/FluxCam/<filename>, returns the full path.
// =============================================================================

static std::string FluxMac_saveJpegToPictures(const std::string& filename,
                                               const uint8_t* data,
                                               size_t length) {
    NSArray<NSURL*>* picURLs = [[NSFileManager defaultManager]
        URLsForDirectory:NSPicturesDirectory
               inDomains:NSUserDomainMask];

    if (picURLs.count == 0) {
        CAM_LOGE("Could not locate Pictures directory");
        return {};
    }

    NSURL* dirURL = [picURLs.firstObject URLByAppendingPathComponent:@"FluxCam"
                                                         isDirectory:YES];

    NSError* err = nil;
    [[NSFileManager defaultManager]
        createDirectoryAtURL:dirURL
 withIntermediateDirectories:YES
                  attributes:nil
                       error:&err];  // ok if already exists

    NSString* nsName = [NSString stringWithUTF8String:filename.c_str()];
    NSURL*    fileURL = [dirURL URLByAppendingPathComponent:nsName];

    NSData* nsData = [NSData dataWithBytes:data length:length];
    if (![nsData writeToURL:fileURL options:NSDataWritingAtomic error:&err]) {
        CAM_LOGE("writeToURL failed: %s", err.localizedDescription.UTF8String);
        return {};
    }

    return std::string(fileURL.path.UTF8String);
}

#endif // __APPLE__ && !__IPHONE_OS_VERSION_MIN_REQUIRED