# 🏗️ Blindside Architecture

> Deep technical specification and pipeline reference for Blindside.

Blindside is an edge-native, local-first physical privacy daemon designed to protect computer screens from unauthorized visual eavesdropping ("shoulder surfing").

This document describes the system architecture, processing pipeline, layer boundaries, and failure modes.

---

## 1. System Overview & Principles

```text
Camera Sensor
     │
     ▼
┌─────────────────────────────────────────────────────────────┐
│ Core Computer Vision Pipeline                               │
│                                                             │
│   CameraCapture ──► FaceDetector (YuNet ONNX)               │
│                            │                                │
│                            ▼                                │
│                     PoseEstimator (cv::solvePnP)            │
│                            │                                │
│                            ▼                                │
│                     FaceTracker (Temporal Identity)         │
│                            │                                │
│                            ▼                                │
│              LivenessEngine (Per-Track Micro-motion / FAR)  │
│                            │                                │
│                            ▼                                │
│                     ThreatEngine (Spatial Gaze / Dwell)     │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼ ThreatAssessment / FrameResult
┌─────────────────────────────────────────────────────────────┐
│ Response & Platform Abstraction Layer                       │
│                                                             │
│   PrivacyTriggerManager                                     │
│          │                                                  │
│          ▼                                                  │
│   PlatformManager                                           │
│          ├─► Windows (Alpha Redaction Overlay, Lock)        │
│          ├─► Linux X11 (Override-Redirect Redaction, Lock)  │
│          └─► Linux Wayland (Notification & Lock Fallback)   │
└─────────────────────────────────────────────────────────────┘
```

### Core Design Rules
1. **Unidirectional Dependency Flow**: Detection and threat evaluation never depend on platform response code. Platform implementations never perform computer vision or threat evaluation.
2. **Local-First & Ephemeral**: Video frames are processed in-memory and immediately discarded. No camera frames, cropped faces, or biometric vectors are ever saved to disk or transmitted across network interfaces.
3. **Zero-Allocation Hot Path**: Once initialized, frame cycling and rolling history buffers operate on fixed-capacity containers (`RingBuffer`, `std::array`).
4. **Honest Capability Reporting**: Platform layers expose genuine OS primitives. Alpha redaction overlays are never mislabeled as compositor blur, and protocol-restricted environments (such as Wayland) explicitly report missing capabilities.

---

## 2. Processing Pipeline

### Frame Ingestion (`src/core/camera.cpp`)
- `CameraCapture` manages the hardware video stream via OpenCV `cv::VideoCapture` (V4L2 on Linux, Media Foundation / DirectShow on Windows).
- Uses a fixed-capacity `RingBuffer<RawFrame, 4>` to prevent memory allocations per frame.
- **Resilience & Fallback**: If the hardware webcam is disconnected or unavailable, the capture pipeline safely falls back to a synthetic frame generator (`SyntheticCameraFallback`), preventing daemon crashes.

### Face Detection (`src/engine/face_detector.cpp`)
- Employs OpenCV's **YuNet** ONNX model (`face_detection_yunet_2023mar.onnx`), an ultra-lightweight, high-speed convolutional neural network tailored for edge face detection. Requires OpenCV 4.10+ for YuNet ONNX layer compatibility.
- Produces:
  - Bounding box (`x, y, width, height`)
  - Detection confidence score (thresholded at `min_confidence`, default 0.60)
  - 5 facial landmarks: right eye, left eye, nose tip, right mouth corner, left mouth corner.
- **Inference Failure Semantics (`DetectionResult`)**:
  - `FaceDetector::detect()` returns an explicit `DetectionResult` struct (`success`, `faces`, `error_message`), strictly distinguishing valid zero-face detections (`success = true`, empty vector) from inference exceptions (`success = false`).
  - Pre-inference validation catches uninitialized detector state, null instance pointers, invalid frame dimensions ($w \le 0$ or $h \le 0$), empty frame buffers, and buffer size mismatches ($< w \times h \times 3$ bytes).
  - Neural inference catches both `const cv::Exception&` (OpenCV runtime/model exceptions) and `const std::exception&` (standard library exceptions), recording the error string under a thread-safe mutex and returning `DetectionResult::failure(...)`.
  - When `EavesdropperDetector` receives an inference failure:
    - Sets `FrameResult::inference_failure = true`.
    - Resets trigger state (`reset_trigger_state()`) to clear secondary hysteresis timers.
    - Suspends primary absence timers rather than advancing them, preventing sensor or inference glitches from triggering false THREAT-03 absence defenses.
    - If a primary absence defense was already active when inference failed, the defense state is safely preserved to prevent unauthorized access from an accidental unlock.
    - Logs errors with one-time deduplication (`inference_failure_logged_`) to avoid log flooding.
    - Once inference succeeds again (`det.success == true`), `inference_failure_logged_` is cleared, and both primary user absence monitoring and secondary face / eavesdropper tracking immediately resume normal processing.

### Pose & Gaze Estimation (`src/engine/pose_estimator.cpp`)
- Solves the Perspective-n-Point problem using OpenCV `cv::solvePnP` (`SOLVEPNP_EPNP`).
- Maps 2D image coordinates from the 5 YuNet landmarks against a canonical 3D anthropometric face model.
- Derives 3D rotational head pose and 3D gaze vector:
  - **Pitch** (degrees, up/down head rotation relative to screen center)
  - **Yaw** (degrees, left/right horizontal head turn relative to screen center)
  - **Roll** (degrees, lateral head tilt)
  - **Gaze Vector**: Normalized 3D unit vector extending outward from the face center, computed from pitch and yaw.
  *(Note: While `cv::solvePnP` calculates internal translation vectors, `HeadPose` only reports 3 rotational degrees of freedom and unit gaze direction; translation coordinates are not exposed).*
- **Screen Gaze Bounds (Production C++)**: Gaze is classified as looking at the screen when:
  $$\text{Pitch} \in [-25^\circ, +25^\circ] \quad \text{and} \quad \text{Yaw} \in [-30^\circ, +30^\circ]$$
  *(Note: The Python reference harness in `tools/reference/` uses default bounds of $\text{Pitch} \in [-20^\circ, +20^\circ]$ and $\text{Yaw} \in [-25^\circ, +25^\circ]$).*

### Temporal Tracking
- Assigns persistent identities across consecutive frames to distinguish:
  - **Primary User**: Calibrated at startup based on centered screen position and dominant bounding box area.
  - **Secondary Faces**: Candidates detected in the background or periphery.
- Tracks transition through lifecycle states: `New` $\to$ `Tentative` $\to$ `Confirmed` $\to$ `Lost` $\to$ `Expired`.
- Prevents spurious single-frame detections from triggering privacy responses.

### Liveness Verification
- Distinguishes physical human presence from static photographic spoofing (e.g. photos, printed posters, or digital portraits).
- **Per-Track History Isolation**: Rolling 45-sample fixed histories (`SecondaryFaceTrack::pose_history`) are isolated strictly per secondary track ID. Micro-motion variance from one person never contaminates or alters another subject's spoof classification.
- Evaluates two primary signals over a rolling time window (`config.liveness_window_sec`):
  1. **Rotational Micro-motion**: Evaluates pitch and yaw angular variance over time. Physical subjects naturally display involuntary micro-movements (variance $\ge 0.02$), whereas static photographs exhibit near-zero angular variance.
  2. **Geometric Facial Aspect Ratio (FAR) Proxy**: Evaluates vertical deformation ratios between eye centers and nose tip relative to interocular distance:
     $$\text{FAR} = \frac{|y_{\text{nose}} - y_{\text{eye\_center}}|}{1.8 \cdot d_{\text{interocular}}}$$
     > [!IMPORTANT]
     > **No Physiological Blink Claims**: YuNet outputs 5 facial points (2 eye centroids, nose tip, 2 mouth corners) without eyelid contours. FAR is an anthropometric geometric deformation proxy, not an anatomical palpebral fissure Eye Aspect Ratio (EAR). Blindside relies primarily on 3D head pose angular micro-motion for anti-spoofing and does not claim genuine physiological blink detection.
- Tracks exhibiting insufficient motion variance and unchanging facial aspect ratio are marked as `is_spoof_static = true` and suppressed from escalating to hard lock.

### Primary User Absence Defense (THREAT-03)
- Continuously monitors primary user presence against the calibrated baseline bounding box.
- **Configurable Absence Timeout**: Brief occlusions or single frame drops do not disrupt the session. Primary departure must persist beyond `config.primary_absence_timeout_sec` (default: `5.0s`) before elevating to active defense.
- **Return Recovery Dwell**: When returning from absence, the primary user must maintain continuous presence for `config.primary_recovery_dwell_sec` (default: `0.5s`) before clearing defenses, preventing window flicker.
- **Camera Failure & Disconnect Isolation**: If camera capture returns corrupt or empty frames, absence tracking is paused to avoid false locks. However, if absence defense was already active when disconnection occurred, the lock state is safely preserved.

### Threat Assessment
- Evaluates persistent secondary gaze directed at the user's display.
- **Hysteresis Window**: Transient glances or passersby do not trigger disruption. A secondary face must maintain a sustained gaze toward the screen exceeding the hysteresis threshold (default: `1.0s` via `config.hysteresis_sec`).
- Elevation levels:
  - **Benign**: Primary user present; secondary faces looking away.
  - **Candidate**: Secondary gaze directed at screen, but within hysteresis threshold ($< 1.0\text{s}$). Triggers advisory soft alerts.
  - **Active Eavesdropper**: Confirmed secondary gaze holding $> 1.0\text{s}$. Triggers hard privacy responses (workspace redaction or session lock).
  - **Absent Primary User**: Confirmed primary user absence exceeding timeout threshold. Triggers configured defense state (workspace redaction or session lock).

### Synthetic Eavesdropper Injection
- Built-in deterministic synthetic frame simulation in `CameraCapture` (`--synthetic`) generates valid synthetic face fixtures (`synthetic_faces`) with anthropometric landmarks and realistic micro-movement jitter.
- In synthetic mode, `FaceDetector::detect()` returns `DetectionResult::successful(frame.synthetic_faces)`, directly bypassing ONNX neural network inference.
- Exercises the complete downstream pipeline (`solvePnP` $\to$ `calibrate` $\to$ `tracking` $\to$ `liveness` $\to$ `hysteresis` $\to$ `defense`) deterministically without requiring physical hardware webcams or live ONNX inference in CI runners.

---

## 3. Response & Platform Layer

### Privacy Trigger Manager (`src/trigger/privacy_trigger.cpp`)
`PrivacyTriggerManager` serves as the mediator between the detection engine and the operating system:
- Receives immutable threat assessment results (`const FrameResult&`).
- Evaluates daemon configuration (`config.daemon_state`):
  - `DaemonState::GracefulTargetedBlur`: Requests active workspace window redaction.
  - `DaemonState::StrictFullLock`: Requests OS workstation lock.
- Coordinates overlay lifecycle: triggers targeted overlays when threats appear, repositions overlays if active windows move, and clears overlays when the space is clear.
- Drains the non-blocking platform message loop (`pump_events()`) every frame to maintain UI thread responsiveness.
- Writes structured physical-access security event logs to `blindside_threats.log`.

### Platform Manager Abstraction (`include/blindside/platform.hpp`)
`PlatformManager` defines the pure virtual interface for native OS integrations:

```cpp
class PlatformManager {
public:
    virtual ~PlatformManager() = default;
    virtual bool initialize() = 0;
    virtual PlatformDiagnostics get_diagnostics() const = 0;
    virtual PlatformCapabilities get_capabilities() const = 0;
    virtual WindowRect get_active_window_geometry() = 0;
    virtual void trigger_targeted_blur(const WindowRect& rect) = 0;
    virtual void trigger_soft_alert(const std::string& message) = 0;
    virtual void trigger_hard_defense(const std::string& message) = 0;
    virtual void clear_alerts() = 0;
    virtual void pump_events() {}
    static std::unique_ptr<PlatformManager> create();
};
```

---

## 4. Platform Backends

### Windows (`src/platform/windows.cpp`)
- **Overlay Window**: Creates a top-level popup using a registered custom Win32 class `BlindsidePrivacyOverlay`.
- **Extended Styles**: `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`.
- **Input Pass-Through**: Handles `WM_NCHITTEST` returning `HTTRANSPARENT`. Combined with `WS_EX_TRANSPARENT`, all mouse clicks, movements, and scroll events pass through to the underlying application.
- **Focus Safety**: `WS_EX_NOACTIVATE` and `SWP_NOACTIVATE` prevent the overlay from stealing keyboard focus.
- **Redaction Mode**: High-opacity alpha mask (`LWA_ALPHA` = 225/255, ~88% opacity). Honestly reports `targeted_blur = false`.
- **DPI & Multi-Monitor**: Initializes Per-Monitor DPI Awareness V2. Fallback geometry uses `SM_XVIRTUALSCREEN`, `SM_YVIRTUALSCREEN`, `SM_CXVIRTUALSCREEN`, and `SM_CYVIRTUALSCREEN` to support displays placed left of or above the primary monitor (negative virtual coordinates).
- **Message Loop**: Non-blocking `PeekMessageA` pump drains Win32 message queues per frame to avoid DWM thread freezing.
- **Session Lock**: Calls Win32 `LockWorkStation()`.

### Linux X11 (`src/platform/linux_x11.cpp`)
- **Overlay Window**: Creates an unmanaged X11 window with `override_redirect = True` and a solid `BlackPixel` background.
- **Repositioning**: Idempotently repositions existing windows via `XMoveResizeWindow` and `XRaiseWindow` without creating duplicate window handles.
- **Event Draining**: Non-blocking `XPending` / `XNextEvent` loop in `pump_events()` discards pending server events.
- **Session Lock**: Executes standard desktop session lock commands (`loginctl lock-session`, `xset ss activate`, or D-Bus screensaver lock).
- **Limitations**: Standard X11 override-redirect consumes mouse events without XShape masks (`click_through_overlay = false`). `ScreenCount()` reflects X11 screens, not individual XRandR physical heads.

### Linux Wayland (`src/platform/linux_wayland.cpp`)
- **Protocol Restrictions**: Pure Wayland security architecture explicitly restricts unprivileged clients from reading global active window coordinates or spawning absolute-positioned overlays over other applications.
- **Honest Capability Reporting**: Reports `screen_redaction = false`, `targeted_blur = false`, `click_through_overlay = false`, `multi_monitor_aware = false`.
- **Safe Fallback**: Overlay requests gracefully degrade to desktop notifications (`notify-send`) and compositor-agnostic workstation locking (`loginctl lock-session` / D-Bus screensaver). Never crashes or throws unhandled exceptions.

---

## 5. Diagnostic & Capability Specification

Applications and administrative scripts inspect native runtime capabilities via `get_capabilities()` and `get_diagnostics()`:

```cpp
struct PlatformCapabilities {
    bool screen_redaction = false;       // Can render an opaque or translucent privacy overlay
    bool targeted_blur = false;          // True DWM/compositor-backed blur shader
    bool session_lock = false;           // OS session lock primitive
    bool click_through_overlay = false; // Overlay passes mouse input to underlying application
    bool multi_monitor_aware = false;   // Can enumerate and position across virtual screen bounds
};
```

---

## 6. Physical-Access Security Event Auditing

Blindside logs structured security alerts for physical-access security event auditing, including informational metadata tags (`Control=NIST_SP_800_53_PE_3`):
- File: `blindside_threats.log`
- Format:
  ```text
  2026-11-13 20:30:15 [AUDIT_ALERT] Threat=HARD_WORKSTATION_LOCK_ACTIVATED GazeDurationSec=1.25 FacesDetected=2 LivenessVerified=1 Control=NIST_SP_800_53_PE_3
  ```
- **Transition-Based Deduplication**: Logging is triggered on state transitions (`_ACTIVATED`, `_ESCALATED`, `_DEACTIVATED`) rather than on every frame, eliminating 30 Hz disk spam during prolonged active threats.
- **Privacy Assurance**: The log records *events and measurements* (gaze duration, face count, verification flags, control reference). It **never** writes image buffers, facial biometric vectors, or identity identifiers.

> [!NOTE]
> **Informational Reference Metadata**: The string `Control=NIST_SP_800_53_PE_3` is emitted strictly as an informational event tag for security event review. Emitting this identifier does not establish, certify, or guarantee compliance with NIST SP 800-53, ISO 27001, or any other regulatory framework.

---

## 7. Testing & Verification Architecture

Blindside employs strict isolation between test tiers:
- **Platform Hardening Suite (`tests/test_platform.cpp`)**: Verifies 11 platform contracts (capability detection, overlay lifecycle, idempotent resizing, cleanup, unsupported fallbacks, invalid geometry, multi-monitor negative coordinates, resilience, threat engine decoupling, state immutability, and audit log transition deduplication).
- **Synthetic Pipeline Suite (`tests/test_pipeline.cpp`)**: Integration tests validating synthetic camera injection, downstream solvePnP pose estimation, primary user calibration, eavesdropper elevation, and recovery using structured detection fixtures. Also validates live YuNet ONNX inference on empty frames when the model file is available on disk (skipping gracefully if absent), and detector error handling under malformed or uninitialized inputs.
- **Detector & Liveness Suites (`tests/test_eavesdropper.cpp`, `tests/test_liveness.cpp`)**: Unit tests validating per-track multi-face liveness isolation, photo spoof rejection, primary absence lifecycle, and geometric FAR proxy calculation.
- **Pipeline & Math Tests**: Dedicated tests for ring buffers (`test_ring_buffer.cpp`), gaze math (`test_gaze_math.cpp`), and camera failure handling (`test_camera_failure.cpp`).
- **Python Reference Suite (`tools/reference/test_blindside.py`)**: Independent algorithmic reference test suite validating synthetic camera fallback, gaze hysteresis, calibration, gaze vectors, and liveness.
