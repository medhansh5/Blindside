# Changelog

All notable changes to the Blindside project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [4.1.0] - Unreleased (Milestone Baseline: 2026-11-13)

### Added
- Per-track liveness isolation (`SecondaryFaceTrack::pose_history`): rolling 45-sample history buffers evaluated independently per track ID, preventing cross-contamination between moving live users and static photo spoofs.
- Primary User Absence Defense (THREAT-03): automated screen lock or workspace redaction when the primary operator leaves the calibrated bounding box beyond `primary_absence_timeout_sec` (default: 5.0s), with recovery dwell hysteresis (`primary_recovery_dwell_sec`, default: 0.5s) upon return.
- Inference failure isolation (`DetectionResult`): explicit detection outcome type distinguishing neural inference failures from zero-face presence, setting `FrameResult::inference_failure`, suspending absence timers without false absence locks, preserving active defense states if already locked, and preventing log flooding with one-time deduplication.
- Deterministic synthetic camera simulation pipeline (`CameraCapture::set_synthetic_eavesdropper()`) generating structured face detections to exercise downstream solvePnP pose estimation, tracking, liveness, and privacy triggers without requiring physical hardware webcams or live ONNX inference.
- Transition-based physical-access security event auditing (`_ACTIVATED`, `_ESCALATED`, `_DEACTIVATED`) in `PrivacyTriggerManager`, logging informational control metadata (`Control=NIST_SP_800_53_PE_3`; logging this identifier does not establish NIST, ISO 27001, or regulatory compliance) while eliminating 30 Hz disk I/O spam.
- Audit log deduplication test (`test_audit_log_deduplication`) expanding `test_platform.cpp` to an 11-test platform contract suite.
- Dedicated pipeline integration and error-handling test suite (`tests/test_pipeline.cpp`), validating synthetic pipeline flow, live YuNet ONNX inference on empty frames when the model file is available on disk (skipping gracefully if absent), and detector error handling for uninitialized/corrupt buffers.
- Native camera failure test suite (`tests/test_camera_failure.cpp`).

### Changed
- Clarified anti-spoofing terminology: YuNet provides 5 facial landmarks without eyelid contours; replaced EAR blink detection claims with anthropometric Facial Aspect Ratio (FAR) deformation proxy (`compute_facial_aspect_ratio()`) while preserving `compute_ear()` compatibility aliases.
- Pinned Linux CI to Ubuntu 24.04 LTS runner with OpenCV 4.10.0 built from source and cached across runs (avoiding Ubuntu's standard `libopencv-dev` 4.6 package and resolving YuNet ONNX `getLayerData` crashes).
- Enforced `find_package(OpenCV 4.10 REQUIRED COMPONENTS core imgproc videoio highgui objdetect calib3d dnn)` in `CMakeLists.txt`.
- Upgraded GitHub Actions workflow actions to Node.js 24 runtimes (`actions/checkout@v5`, `actions/cache@v5`, `actions/upload-artifact@v7`).

### Fixed
- Daemon state thread safety: resolved data race on `config_.daemon_state` across threads via thread-safe atomic overrides.
- Camera disconnect and grab failure isolation: paused absence timers to avoid false locks while preserving active defense states.
- Consumer wait deadlocks in concurrent queue tests with explicit capacity boundaries and stop notifications.
- Linux X11 headless display resilience: handled `XOpenDisplay` returning `nullptr` gracefully without crashes.
- Python reference cross-platform portability: guarded Windows-specific `ctypes.wintypes` imports.

---

## [4.0.0] - 2026-10-04

### Added
- Discrete platform capability abstraction (`PlatformCapabilities`) and diagnostics struct (`PlatformDiagnostics`).
- Native Win32 `WM_NCHITTEST -> HTTRANSPARENT` pass-through handler for reliable click-through behavior.
- Support for negative multi-monitor coordinate spaces via virtual desktop bounds (`SM_XVIRTUALSCREEN`, `SM_YVIRTUALSCREEN`).
- Non-blocking event pump interface (`pump_events()`) preventing UI thread starvation across Windows (`PeekMessageA`) and Linux X11 (`XPending`).
- Dedicated 10-test platform hardening suite (`tests/test_platform.cpp`) registered under CTest.
- Developer Python reference implementation and test suite (`tools/reference/`).

### Changed
- Clarified redaction vs. blur distinction across all platforms; explicitly reports `targeted_blur = false`.
- Updated Windows privacy overlay styles to include `WS_EX_NOACTIVATE` to prevent keyboard focus stealing.
- Enhanced Linux X11 overlay with idempotent resizing via `XMoveResizeWindow` without leaking window handles.
- Hardened Linux Wayland backend to declare `screen_redaction = false` and cleanly degrade to notifications and session lock.
- Clarified diagnostic output indicating that X11 `monitor_count` reflects X11 screens rather than physical XRandR heads.

### Fixed
- Fixed Win32 overlay freezing caused by unserviced desktop window manager messages.
- Fixed overlay displacement on secondary displays positioned above or to the left of the primary monitor.
- Fixed potential duplicate window creation on rapid consecutive threat events.

### Known Limitations
- Native C++ compilation and CTest execution require a local C++20 toolchain (Visual Studio 2022 or GCC 11+).
- Pure Wayland compositors prohibit client-directed absolute screen overlays; fallback to notification and session lock is enforced.

---

## [3.1.0] - 2026-09-20

### Added
- Explicit distinction between raw candidate face detections and validated primary/secondary faces in standard logging.
- Comprehensive frame-by-frame face count diagnostics.
- Camera recovery integration preventing busy-loop starvation on GStreamer / OpenCV device reconnect.

### Changed
- Refined `cv::solvePnP`-based head-orientation and spatial gaze-vector bounds.
- Preserved strict 1.0-second gaze hysteresis threshold.

### Fixed
- GStreamer capture segmentation fault during abrupt hardware webcam disconnection.
- Unhandled camera acquisition race condition on Linux V4L2 drivers.
