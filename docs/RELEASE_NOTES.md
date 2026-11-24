# Blindside Release Notes

> For complete release history and roadmap, see [../RELEASE_NOTES.md](../RELEASE_NOTES.md).

---

## Blindside V4.1.0 — Architecture Integrity, Per-Track Liveness & Primary Absence Release (Pending Publication / Milestone Baseline: 2026-11-13)

> **Release Status**: Formal Maintenance & Feature Hardening Release.

Blindside V4.1.0 resolves key architectural debt identified in the V4.0 generational audit, delivering per-track liveness isolation, automated primary user absence defense (THREAT-03), synthetic end-to-end pipeline testing, transition-based threat log deduplication, and cross-platform runtime reliability.

### Key Highlights of V4.1.0

#### 1. Per-Track Liveness & Anti-Spoofing Architecture
- **Dedicated History Buffers**: Migrated rolling 45-sample pose history buffers into individual `SecondaryFaceTrack` instances.
- **Cross-Contamination Eliminated**: Involuntary angular micro-motion (pitch/yaw variance $\ge 0.02$) is evaluated strictly per track ID. A moving live human and a static spoof photograph in the same frame maintain independent classifications.
- **FAR Geometric Proxy**: Clarified that 5-point landmark models provide eye centroids without eyelid contours. Replaced misleading EAR/blink claims with an anthropometric Facial Aspect Ratio (FAR) deformation proxy (`compute_facial_aspect_ratio()`), preserving `compute_ear()` compatibility aliases.

#### 2. Primary User Absence Defense (THREAT-03) & Inference Failure Semantics
- **Configurable Dwell & Timeout**: Enforces `primary_absence_timeout_sec` (default: 5.0s) before elevating to screen lock or workspace redaction, preventing false triggers from single missed frames or brief head turns.
- **Return Recovery Hysteresis**: Requires sustained presence for `primary_recovery_dwell_sec` (default: 0.5s) when the user returns before clearing active defenses.
- **Inference Failure Semantics**: Introduced explicit `DetectionResult` distinguishing neural inference failures from successful zero-face detections. On inference exceptions (`cv::Exception` or `std::exception`), absence timers are suspended rather than advancing, preventing false THREAT-03 absence locks. If an absence defense was already active, it is safely preserved without false unlocks. Failures are cleanly logged with one-time deduplication, and normal absence monitoring and eavesdropper tracking resume when inference succeeds.
- **Camera Failure & Disconnect Isolation**: If the camera fails or disconnects, absence timers are paused. If an absence defense was already active when disconnection occurred, the lock state is preserved so the workstation does not unlock.

#### 3. Synthetic Eavesdropper Injection & Pipeline Testing
- **Deterministic Simulation**: Added `CameraCapture::set_synthetic_eavesdropper()`, generating synthetic primary and secondary face detections with anthropometric landmarks and realistic micro-movement jitter.
- **Downstream Logic Coverage**: Synthetic mode injects structured detections directly into the frame pipeline, exercising downstream OpenCV `solvePnP` head pose estimation, gaze geometry, temporal tracking, per-track liveness evaluation, and privacy triggers without requiring physical webcams or live ONNX inference.
- **Pipeline Test Suite (`tests/test_pipeline.cpp`)**:
  - Validates full synthetic pipeline progression through calibration, threat elevation, and calm recovery.
  - Validates live YuNet ONNX model inference on empty frames when the model file is available on disk (skipping gracefully if the model is absent from the test environment).
  - Validates detector error handling for uninitialized instances, corrupt/mismatched buffer sizes, invalid frame dimensions, and verifies that clean empty frames are distinguished from inference failures.

#### 4. Transition-Based Audit Log Deduplication
- **Transition Gating**: Physical-access security event auditing to `blindside_threats.log` records state transitions (`_ACTIVATED`, `_ESCALATED`, `_DEACTIVATED`) tagged with informational control metadata (`Control=NIST_SP_800_53_PE_3`) instead of logging at active framerate (30 Hz). Emitting this metadata tag provides structured categorization for event review, but does not establish or certify NIST, ISO 27001, or other regulatory compliance.
- **Reduced Disk I/O**: Eliminates redundant log spam during sustained threats while maintaining an auditable security event trail.
- **Platform Contract Test #11**: Added `test_audit_log_deduplication` to `tests/test_platform.cpp`, verifying that repeated alerts do not create redundant log entries.

#### 5. Concurrency, Portability & Build Hardening
- **Daemon State Thread-Safety**: Eliminated data race on `config_.daemon_state` across worker threads by utilizing thread-safe `atomic_state_` overrides.
- **Linux X11 Headless Resilience**: Handled headless Linux container environments gracefully where `XOpenDisplay` returns `nullptr`, allowing daemon initialization to succeed without crashing.
- **RingBuffer Test Stability**: Resolved consumer wait deadlocks in concurrent queue unit tests with explicit capacity boundaries and stop notifications.
- **Packaging & Dependency Fixes**: Pinned Linux CI to a native Ubuntu 24.04 runner with OpenCV 4.10.0 built from source and cached across runs (avoiding Ubuntu's incompatible standard `libopencv-dev` 4.6 package); verified exact 64-hex SHA256 integrity for YuNet model download.
- **Python Reference Portability**: Guarded `ctypes.wintypes` Windows-specific imports, enabling `run_blindside.py` to execute across Linux and macOS.

---

## Blindside V4.0.0 — Platform Hardening & Architecture Release (2026-10-04)

> For complete release history, see [../RELEASE_NOTES.md](../RELEASE_NOTES.md).

## Overview
Blindside V4.0.0 marks the formal generational release featuring **Platform Hardening**, transforming OS privacy responses into robust, input-safe, multi-monitor aware, and honestly reported platform primitives across Windows, Linux X11, and Linux Wayland.

## What's New in V4.0.0

### Discrete Platform Capability Model
- Introduced `PlatformCapabilities` and `PlatformDiagnostics` structs exposing explicit capability flags (`screen_redaction`, `targeted_blur`, `session_lock`, `click_through_overlay`, `multi_monitor_aware`).
- Replaced ambiguous "blur" claims with honest reporting: Windows and Linux X11 provide high-opacity **Screen Redaction**, not compositor-backed blur shaders (`targeted_blur = false`).

### Windows Platform Backend
- **Input Pass-Through**: Implemented `WM_NCHITTEST -> HTTRANSPARENT` alongside `WS_EX_TRANSPARENT`, guaranteeing that all mouse clicks and scroll wheel events pass through the overlay to underlying workspace applications.
- **Focus Safety**: Extended window styles with `WS_EX_NOACTIVATE` and `SWP_NOACTIVATE` to prevent the overlay from stealing keyboard focus.
- **Per-Monitor High-DPI Awareness V2**: Dynamic runtime initialization of `SetProcessDpiAwarenessContext` (`PER_MONITOR_AWARE_V2`) for crisp multi-monitor coordinate alignment.
- **Virtual Desktop Negative Coordinates**: Support for displays positioned to the left of (`x < 0`) or above (`y < 0`) the primary display via `SM_XVIRTUALSCREEN` and `SM_YVIRTUALSCREEN`.
- **Non-Blocking Message Pump**: Implemented `pump_events()` with `PeekMessageA` draining to eliminate DWM "Not Responding" thread freezing without stalling the CV detection loop.

### Linux X11 Backend
- **Override-Redirect Solid Redaction**: Instant solid black redaction surface bypassing window manager compositor lag.
- **Idempotent Repositioning**: Window moves and resizes use `XMoveResizeWindow` and `XRaiseWindow` on existing surfaces, eliminating XID handle leaks.
- **Non-Blocking Event Pump**: Drains server events with non-blocking `XPending` / `XNextEvent`.

### Linux Wayland Backend
- **Honest Protocol Restrictions**: Formally declares `screen_redaction = false` due to Wayland security boundaries restricting arbitrary global surface positioning.
- **Graceful Fallbacks**: Overlay requests safely degrade to high-priority desktop notifications (`notify-send`) and compositor-agnostic workstation locking (`loginctl lock-session` / D-Bus screensaver).

### Platform Hardening Test Suite
- Registered 10 dedicated test scenarios under CTest (`tests/test_platform.cpp`) validating capability detection, overlay lifecycles, idempotent repositioning, clean deactivation, unsupported fallbacks, invalid geometries, negative multi-monitor offsets, headless resilience, threat/platform separation, and state immutability.

---

## Blindside V3.1.0 — Historical Release Baseline (2026-09-20)

### Diagnostic Observability
- **Explicit Raw vs Validated Detections**: Standard output diagnostics clearly distinguish raw YuNet candidate detections from validated primary and secondary faces.
- **Detailed Face Count Summaries**: Diagnostics output comprehensive breakdowns per frame, detailing total validated faces, primary user presence, secondary candidate count, and raw detection totals.
- **Transparent Liveness Reporting**: Diagnostic logs explicitly report the active liveness verification status and note architectural bounds.

### Pipeline & Core Stability
- **Camera Recovery Integration**: Preserves thread-safe non-busy-loop recovery for GStreamer and OpenCV camera capture streams.
- **Zero-Allocation Runtime**: Retains high-performance C++20 RingBuffer frame management and `cv::solvePnP`-based 3D head-orientation estimation.
- **Hysteresis Privacy Triggers**: Preserves strict 1.0-second gaze hysteresis and multi-platform defense responses (targeted window blur & desktop session lock).
