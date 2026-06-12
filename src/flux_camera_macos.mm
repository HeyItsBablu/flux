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

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

#include "flux/flux_camera.hpp"

#include <cstdio>
#include <ctime>

// ── Logging helpers ──────────────────────────────────────────────────────────

#define CAM_LOGI(fmt, ...) \
    do { fprintf(stderr, "[FluxCamera] " fmt "\n", ##__VA_ARGS__); } while(0)
#define CAM_LOGE(fmt, ...) \
    do { fprintf(stderr, "[FluxCamera][ERR] " fmt "\n", ##__VA_ARGS__); } while(0)

// ── File-save helper — forward declaration ───────────────────────────────────

static std::string FluxMac_saveJpegToPictures(const std::string& filename,
                                               const uint8_t* data,
                                               size_t length);

// ── ObjC class forward declarations ─────────────────────────────────────────
// These are needed so MacOSImpl (defined below) can hold typed pointers before
// the full @interface definitions appear later in this file.

@class FluxVideoCaptureDelegate;
@class FluxPhotoCaptureDelegate;

// ============================================================================
// MacOSImpl — plain C++ struct that owns all ObjC strong references.
// Kept out of the public header so no ObjC types leak into mixed-language TUs.
// ============================================================================

struct MacOSImpl {
    AVCaptureSession*            session         = nil;
    AVCaptureDeviceInput*        currentInput    = nil;
    AVCaptureVideoDataOutput*    videoOutput     = nil;
    AVCapturePhotoOutput*        photoOutput     = nil;
    FluxVideoCaptureDelegate*    videoDelegate   = nil;
    FluxPhotoCaptureDelegate*    photoDelegate   = nil;
    dispatch_queue_t             videoQueue      = nullptr;

    // Frame buffer — BGRA32, protected by FluxCamera::_frameMutex
    std::vector<uint8_t>         frameData;

    // Snapshot of the device list; index mirrors FluxCamera::_cameraIndex
    NSArray<AVCaptureDevice*>*   devices         = nil;
    int                          deviceCount     = 0;
};

// ============================================================================
// FluxCameraMacOSAccess — C++ bridge granted friendship in flux_camera.hpp.
// ObjC delegate methods call these static helpers instead of touching
// FluxCamera private members directly, which avoids any ObjC/C++ symbol-kind
// conflict from putting @class or @interface names in a friend declaration.
// ============================================================================

struct FluxCameraMacOSAccess {
    static MacOSImpl* impl(FluxCamera* c) {
        return static_cast<MacOSImpl*>(c->_macosImpl);
    }
    static std::mutex& mutex(FluxCamera* c) {
        return c->_frameMutex;
    }
    static FluxCamera::State getState(FluxCamera* c) {
        return c->_state.load();
    }
    static void setState(FluxCamera* c, FluxCamera::State s) {
        c->_state = s;
    }
    static void setPreviewW(FluxCamera* c, int v) { c->_previewW = v; }
    static void setPreviewH(FluxCamera* c, int v) { c->_previewH = v; }
    static void setPendingFrame(FluxCamera* c, bool v) { c->_pendingFrame = v; }
    static bool exchangeCapture(FluxCamera* c) {
        return c->_captureRequested.exchange(false);
    }
    static void saveJpeg(FluxCamera* c, const uint8_t* d, int w, int h) {
        c->_saveJpegFromBGRA(d, w, h);
    }
    static void setLastPhotoPath(FluxCamera* c, const std::string& p) {
        c->_lastPhotoPath = p;
    }
    static void fireOnPhoto(FluxCamera* c, const std::string& p) {
        if (c->_onPhoto) c->_onPhoto(p);
    }
};

// ============================================================================
// AVCaptureVideoDataOutputSampleBufferDelegate  (preview frames)
// ============================================================================

@interface FluxVideoCaptureDelegate
    : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, assign) FluxCamera* owner;
@end

@implementation FluxVideoCaptureDelegate

- (void)captureOutput:(AVCaptureOutput*)      __unused output
didOutputSampleBuffer:(CMSampleBufferRef)              sampleBuffer
       fromConnection:(AVCaptureConnection*)  __unused connection
{
    FluxCamera* cam = self.owner;
    if (!cam || FluxCameraMacOSAccess::getState(cam) == FluxCamera::State::Closed)
        return;

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) return;

    CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

    int      w      = (int)CVPixelBufferGetWidth(imageBuffer);
    int      h      = (int)CVPixelBufferGetHeight(imageBuffer);
    size_t   stride = CVPixelBufferGetBytesPerRow(imageBuffer);
    uint8_t* src    = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(imageBuffer));

    if (src && w > 0 && h > 0) {
        MacOSImpl* impl = FluxCameraMacOSAccess::impl(cam);

        {
            std::lock_guard<std::mutex> lock(FluxCameraMacOSAccess::mutex(cam));
            impl->frameData.resize((size_t)(w * h * 4));
            // CVPixelBuffer rows may be stride-padded — copy row by row
            for (int row = 0; row < h; ++row) {
                std::memcpy(impl->frameData.data() + row * w * 4,
                            src + row * stride,
                            (size_t)(w * 4));
            }
        }

        FluxCameraMacOSAccess::setPreviewW(cam, w);
        FluxCameraMacOSAccess::setPreviewH(cam, h);
        FluxCameraMacOSAccess::setPendingFrame(cam, true);

        // Fallback still-capture: encode the current preview frame as JPEG
        // when AVCapturePhotoOutput is unavailable.
        if (FluxCameraMacOSAccess::exchangeCapture(cam)) {
            FluxCameraMacOSAccess::saveJpeg(cam, impl->frameData.data(), w, h);
        }
    }

    CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
}

- (void)captureOutput:(AVCaptureOutput*)     __unused output
  didDropSampleBuffer:(CMSampleBufferRef)    __unused sampleBuffer
       fromConnection:(AVCaptureConnection*) __unused connection
{
    // Silently discard late frames.
}

@end

// ============================================================================
// AVCapturePhotoCaptureDelegate  (full-resolution still via AVCapturePhotoOutput)
// ============================================================================

@interface FluxPhotoCaptureDelegate
    : NSObject <AVCapturePhotoCaptureDelegate>
@property (nonatomic, assign) FluxCamera* owner;
@end

@implementation FluxPhotoCaptureDelegate

- (void)captureOutput:(AVCapturePhotoOutput*) __unused output
didFinishProcessingPhoto:(AVCapturePhoto*)             photo
                error:(NSError*)                       error
{
    FluxCamera* cam = self.owner;
    if (!cam) return;

    if (error) {
        CAM_LOGE("AVCapturePhoto error: %s", error.localizedDescription.UTF8String);
        FluxCameraMacOSAccess::setState(cam, FluxCamera::State::Previewing);
        return;
    }

    NSData* jpegData = [photo fileDataRepresentation];
    if (!jpegData) {
        CAM_LOGE("fileDataRepresentation returned nil");
        FluxCameraMacOSAccess::setState(cam, FluxCamera::State::Previewing);
        return;
    }

    std::time_t t  = std::time(nullptr);
    std::tm     tm = {};
    localtime_r(&t, &tm);
    char fname[64];
    std::strftime(fname, sizeof(fname), "IMG_%Y%m%d_%H%M%S.jpg", &tm);

    std::string path = FluxMac_saveJpegToPictures(
            fname,
            static_cast<const uint8_t*>(jpegData.bytes),
            (size_t)jpegData.length);

    if (!path.empty()) {
        FluxCameraMacOSAccess::setLastPhotoPath(cam, path);
        CAM_LOGI("Photo saved: %s (%zu bytes)", fname, (size_t)jpegData.length);
        FluxCameraMacOSAccess::fireOnPhoto(cam, path);
    }

    FluxCameraMacOSAccess::setState(cam, FluxCamera::State::Previewing);
}

@end

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
    return (impl->currentInput.device.position == AVCaptureDevicePositionFront);
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

// ── Flash ────────────────────────────────────────────────────────────────────

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
        AVCaptureTorchMode mode = _flashOn.load() ? AVCaptureTorchModeOn
                                                  : AVCaptureTorchModeOff;
        if ([dev isTorchModeSupported:mode])
            dev.torchMode = mode;
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

    MacOSImpl* impl = new MacOSImpl();
    _macosImpl = impl;

    // ── Enumerate video capture devices ───────────────────────────────────
    NSMutableArray<AVCaptureDevice*>* devList = [NSMutableArray array];

    if (@available(macOS 10.15, *)) {
        NSArray<AVCaptureDeviceType>* types;
        if (@available(macOS 14.0, *)) {
            types = @[
                AVCaptureDeviceTypeBuiltInWideAngleCamera,
                AVCaptureDeviceTypeExternal
            ];
        } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            types = @[
                AVCaptureDeviceTypeBuiltInWideAngleCamera,
                AVCaptureDeviceTypeExternalUnknown
            ];
#pragma clang diagnostic pop
        }

        AVCaptureDeviceDiscoverySession* disc =
            [AVCaptureDeviceDiscoverySession
                discoverySessionWithDeviceTypes:types
                                      mediaType:AVMediaTypeVideo
                                       position:AVCaptureDevicePositionUnspecified];
        for (AVCaptureDevice* d in disc.devices)
            [devList addObject:d];
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        for (AVCaptureDevice* d in [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo])
            [devList addObject:d];
#pragma clang diagnostic pop
    }

    if (devList.count == 0) {
        CAM_LOGE("No video capture devices found");
        _state = State::Error;
        return false;
    }

    impl->devices     = [devList copy];
    impl->deviceCount = (int)devList.count;

    int idx = 0;
    if (useFront) {
        for (int i = 0; i < impl->deviceCount; ++i) {
            if (impl->devices[i].position == AVCaptureDevicePositionFront) {
                idx = i;
                break;
            }
        }
    }
    _cameraIndex = idx;

    if (!_openDeviceAtIndex(idx)) {
        _state = State::Error;
        return false;
    }

    _state = State::Previewing;
    CAM_LOGI("Preview started (device %d: %s)",
             idx, impl->devices[idx].localizedName.UTF8String);
    return true;
}

// Internal — creates/replaces the AVCaptureSession for the given device index.
bool FluxCamera::_openDeviceAtIndex(int index) {
    MacOSImpl* impl = static_cast<MacOSImpl*>(_macosImpl);
    if (!impl || index < 0 || index >= impl->deviceCount) return false;

    AVCaptureDevice* dev = impl->devices[index];

    // ── Session ───────────────────────────────────────────────────────────
    impl->session = [[AVCaptureSession alloc] init];
    [impl->session beginConfiguration];
    impl->session.sessionPreset = AVCaptureSessionPresetHigh;

    // ── Input ─────────────────────────────────────────────────────────────
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

    // ── Video data output (preview frames → CPU BGRA32) ───────────────────
    impl->videoOutput = [[AVCaptureVideoDataOutput alloc] init];
    impl->videoOutput.alwaysDiscardsLateVideoFrames = YES;
    impl->videoOutput.videoSettings = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
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
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if (vidConn && [vidConn isVideoOrientationSupported])
        vidConn.videoOrientation = AVCaptureVideoOrientationLandscapeRight;
#pragma clang diagnostic pop

    // ── Photo output (full-resolution still capture) ──────────────────────
    impl->photoOutput = [[AVCapturePhotoOutput alloc] init];
    if ([impl->session canAddOutput:impl->photoOutput]) {
        [impl->session addOutput:impl->photoOutput];
        impl->photoDelegate = [[FluxPhotoCaptureDelegate alloc] init];
        impl->photoDelegate.owner = this;
    } else {
        CAM_LOGI("canAddOutput(photoOutput) returned NO — will use frame fallback");
        impl->photoOutput   = nil;
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
    MacOSImpl* impl  = static_cast<MacOSImpl*>(_macosImpl);
    int        count = impl ? impl->deviceCount : 0;
    int        next  = (count > 1) ? ((_cameraIndex.load() + 1) % count) : 0;
    bool wantFront   = false;
    if (impl && next < impl->deviceCount)
        wantFront = (impl->devices[next].position == AVCaptureDevicePositionFront);
    close();
    open(wantFront);
}

// =============================================================================
// UPDATE FRAME — call once per render tick on the UI thread
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
        _captureRequested = true;
    }

    CAM_LOGI("Capture requested");
}

// =============================================================================
// CLOSE
// =============================================================================

void FluxCamera::close() {
    MacOSImpl* impl = static_cast<MacOSImpl*>(_macosImpl);

    if (impl) {
        if (impl->session) {
            [impl->session stopRunning];

            if (impl->videoOutput)
                [impl->videoOutput setSampleBufferDelegate:nil queue:nullptr];

            impl->session       = nil;
            impl->currentInput  = nil;
            impl->videoOutput   = nil;
            impl->photoOutput   = nil;
            impl->videoDelegate = nil;
            impl->photoDelegate = nil;
            impl->devices       = nil;
            impl->frameData.clear();
            impl->videoQueue    = nullptr;
        }

        delete impl;
        _macosImpl = nullptr;
    }

    _state            = State::Closed;
    _pendingFrame     = false;
    _newFrame         = false;
    _captureRequested = false;
    _previewW         = 0;
    _previewH         = 0;

    CAM_LOGI("Camera closed");
}

// =============================================================================
// JPEG ENCODE + SAVE  (BGRA32 fallback path via ImageIO / CoreGraphics)
// =============================================================================

void FluxCamera::_saveJpegFromBGRA(const uint8_t* bgra, int w, int h) {
    if (!bgra || w <= 0 || h <= 0) return;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();

    CGContextRef ctx = CGBitmapContextCreate(
            const_cast<uint8_t*>(bgra),
            (size_t)w, (size_t)h,
            8,
            (size_t)(w * 4),
            cs,
            (CGBitmapInfo)kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaPremultipliedFirst);
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

    std::time_t t  = std::time(nullptr);
    std::tm     tm = {};
    localtime_r(&t, &tm);
    char fname[64];
    std::strftime(fname, sizeof(fname), "IMG_%Y%m%d_%H%M%S.jpg", &tm);

    CFMutableDataRef jpegData = CFDataCreateMutable(kCFAllocatorDefault, 0);

    // Use the JPEG UTI string directly — avoids both the kUTTypeJPEG
    // deprecation warning and the need to import UniformTypeIdentifiers.
    CGImageDestinationRef dest =
        CGImageDestinationCreateWithData(jpegData, CFSTR("public.jpeg"), 1, nullptr);

    if (!dest) {
        CAM_LOGE("CGImageDestinationCreateWithData failed");
        CGImageRelease(cgImage);
        CFRelease(jpegData);
        return;
    }

    NSDictionary* props = @{
        (id)kCGImageDestinationLossyCompressionQuality : @(0.90f)
    };
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
// =============================================================================

static std::string FluxMac_saveJpegToPictures(const std::string& filename,
                                               const uint8_t*     data,
                                               size_t             length) {
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
                       error:&err];

    NSURL* fileURL = [dirURL URLByAppendingPathComponent:
                        [NSString stringWithUTF8String:filename.c_str()]];

    NSData* nsData = [NSData dataWithBytes:data length:length];
    if (![nsData writeToURL:fileURL options:NSDataWritingAtomic error:&err]) {
        CAM_LOGE("writeToURL failed: %s", err.localizedDescription.UTF8String);
        return {};
    }

    return std::string(fileURL.path.UTF8String);
}

#endif // __APPLE__ && !__IPHONE_OS_VERSION_MIN_REQUIRED