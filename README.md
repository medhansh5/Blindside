# 🛡️ Blindside

> Paranoia, but automated.

Blindside is an edge-native, local-first physical privacy daemon that watches for visual eavesdropping and triggers defensive responses the moment someone looks over your shoulder.

No cloud.
No screenshots.
No facial database.
No telemetry.

Just your webcam, real-time spatial computer vision, and an uncompromising stance on endpoint privacy.

---

## 👁️ The Problem: Visual Eavesdropping

You can have full-disk encryption, a zero-trust network perimeter, hardware security keys, and hardened browser sandboxes. None of that saves you when a stranger on a commuter train or coffee shop stares directly at your screen while you review source code, private Slack channels, or confidential customer databases.

"Shoulder surfing" is one of the most common physical security attack vectors, yet standard operating systems completely ignore it.

Blindside fixes that at the edge. By running lightweight face detection and 6D head pose estimation locally, Blindside detects when someone behind you turns their gaze toward your monitor. If their gaze holds for more than a second, Blindside reacts instantly—redacting your active workspace window or locking your session before sensitive data leaks.

---

## 🔒 Privacy Principles

Blindside is built on a strict privacy model:
- **100% Local Inference**: All computer vision runs on your machine's CPU/GPU. Zero network sockets are opened for detection.
- **No Remote Telemetry**: Camera frames are never transmitted over the internet or written to disk.
- **No Facial Biometric Database**: Blindside does not identify *who* is looking; it calculates *where* a face is pointed relative to your screen. It never generates or stores persistent facial recognition vectors.
- **No Screenshots**: The privacy overlay covers your window; it never reads, captures, or records your screen contents.
- **Structured Audit Trails Only**: Blindside logs *events* (`blindside_threats.log`) for NIST SP 800-53 / ISO 27001 compliance, never biometric identifiers.

---

## ✨ Features

- **OpenCV YuNet Face Detection**: High-speed, lightweight convolutional neural network for real-time edge face detection and 5-point facial landmarking.
- **True 6D Head Pose Estimation**: Computes spatial pitch, yaw, and roll using `cv::solvePnP` against an anthropometric 3D face model—no naive 2D bounding-box heuristics.
- **Spatial Gaze Bounds**: Filters out people who happen to walk behind you; triggers only when a secondary face looks directly at your screen ($\pm 30^\circ$ yaw, $\pm 25^\circ$ pitch).
- **Hysteresis Temporal Filtering**: Enforces a configurable dwell timer (default: `1.0s`) to eliminate false alarms from transient passersby.
- **Per-Track Rotational Micro-Motion & FAR Proxy Liveness**: Evaluates involuntary pitch/yaw micro-movement variance and geometric Facial Aspect Ratio (FAR) deformation proxies independently per secondary face track over rolling time windows to reject static photo spoofs.
- **Primary User Absence Defense (THREAT-03)**: Automatically detects primary user departure from calibrated screen bounds and triggers screen locking or workspace redaction after a configurable timeout with recovery dwell protection.
- **Synthetic Eavesdropper Injection**: Deterministic synthetic camera simulation enables comprehensive CI pipeline testing of detection, solvePnP pose estimation, liveness, and threat triggering without physical hardware webcams.
- **Deduplicated Transition Audit Logging**: Writes structured security audit events on activation, escalation, and deactivation transitions rather than spamming every frame.
- **Native Screen Redaction**: Spawns an instant, click-through, non-activating redaction mask over your active workspace window on supported platforms.
- **Discrete Platform Capability Diagnostics**: Transparently reports native OS capabilities without misrepresenting platform features.
- **Zero-Allocation Core Loop**: Fixed-capacity ring buffers ensure frame cycles run with predictable memory behavior.

---

## 📊 Platform Support Matrix

| Platform | Detection Pipeline | Privacy Response | Support Status |
| :--- | :--- | :--- | :--- |
| **Windows** | YuNet + solvePnP | Alpha redaction overlay / session lock | Supported *(Phase 6 hardened; requires MSVC C++20 for native builds)* |
| **Linux X11** | YuNet + solvePnP | Opaque screen redaction / session lock | Supported *(X11 ScreenCount multi-monitor limitation applies)* |
| **Linux Wayland** | YuNet + solvePnP | Notification & session lock fallback | Limited *(Protocol restricted; arbitrary overlays prohibited)* |
| **macOS** | — | — | Unsupported |

### Discrete Capability Matrix

| Capability | Windows | Linux X11 | Linux Wayland |
| :--- | :---: | :---: | :---: |
| **Screen Redaction** | **Yes** *(AlphaRedaction)* | **Yes** *(OpaqueRedaction)* | **No** *(Protocol Restricted)* |
| **Targeted Blur** | **No** *(Deprecated DWM no-op)* | **No** | **No** |
| **Session Lock** | **Yes** *(`LockWorkStation`)* | **Yes** *(`loginctl` / `dbus`)* | **Fallback** *(`loginctl` / `dbus`)* |
| **Click-Through Overlay** | **Yes** *(`WM_NCHITTEST` pass-through)* | **No** *(Consumes pointer events)* | **No** |
| **Multi-Monitor Awareness** | **Yes** *(Virtual Desktop / negative X/Y)* | **Limited** *(`ScreenCount` reports X screens)* | **No** |

> [!NOTE]
> **Redaction vs. Blur**: Blindside uses high-opacity **Screen Redaction** (an opaque or translucent dark mask covering active window bounds). Modern Windows 10/11 DWM deprecated `DwmEnableBlurBehindWindow` into a no-op, so Blindside honestly reports `targeted_blur = false` across all platforms. See [docs/PLATFORM_SUPPORT.md](docs/PLATFORM_SUPPORT.md).

---

## ⚡ Quick Start

### 1. Clone the Repository
```bash
git clone https://github.com/AshcrestHQ/Blindside.git
cd Blindside
```

### 2. Install Dependencies

#### Linux (Ubuntu 24.04 LTS+ / Debian)
```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build build-essential pkg-config \
    libopencv-dev libx11-dev libxext-dev
```

#### Windows
Ensure you have:
- Visual Studio 2022 (with the **Desktop development with C++** workload)
- CMake 3.20+
- OpenCV 4.10+ (set `OpenCV_DIR` environment variable to your OpenCV build directory)

### 3. Download the YuNet Model
```bash
# Linux / macOS
./scripts/download_models.sh

# Windows (PowerShell)
Invoke-WebRequest -Uri "https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx" -OutFile "models/face_detection_yunet_2023mar.onnx"
```

### 4. Build the Project

#### Linux
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

#### Windows
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

### 5. Run Platform Diagnostics
Verify how Blindside detects your current OS window manager and privacy capabilities:
```bash
# Linux
./build/blindside_daemon --diagnostics

# Windows
.\build\Release\blindside_daemon.exe --diagnostics
```

### 6. Run the Daemon
```bash
# Run with active webcam
./build/blindside_daemon --daemon

# Run with synthetic test stream (no webcam required)
./build/blindside_daemon --synthetic
```

---

## 🔍 Diagnostic Capabilities

Blindside exposes explicit platform diagnostics via the `--diagnostics` CLI flag:

```text
[Platform] OS: Windows
[Platform] Native Redaction: Supported
[Platform] Targeted Blur: Unsupported (AlphaRedaction in use)
[Platform] Redaction Mode: AlphaRedaction (Universal High-Opacity Mask)
[Platform] Screen Lock: Supported
[Platform] Click-Through: Supported
[Platform] Monitor Count: 2
```

Diagnostics inspect:
- `os_name`: Host operating system and windowing environment.
- `supports_native_redaction`: Whether the platform can spawn privacy masks over active windows.
- `supports_targeted_blur`: Real-time compositor blur shader support (honestly reported as `false`).
- `supports_screen_lock`: Availability of native session lock primitives (`LockWorkStation` or `loginctl`).
- `supports_desktop_notifications`: System desktop notification availability.
- `supports_click_through`: Whether overlays allow mouse clicks to reach the underlying application.
- `monitor_count`: Detected screen count.
- `redaction_mode`: Specific redaction strategy (`AlphaRedaction`, `OpaqueRedaction`, or `None`).

---

## 🏗️ Architecture

Blindside enforces strict unidirectional boundaries between computer vision, threat evaluation, and platform defense:

```text
Camera Sensor
     ↓
Face Detection (OpenCV YuNet ONNX, 5 facial landmarks)
     ↓
Face Tracking (Primary user calibration & temporal identity)
     ↓
Liveness Verification (Per-track micro-motion variance & FAR geometric proxy)
     ↓
Threat Assessment (Spatial gaze bounds & hysteresis dwell timers)
     ↓
Privacy Trigger Manager (Translates threat decisions into actions)
     ↓
Platform Manager
 ├── Windows (Alpha Redaction Overlay, LockWorkStation)
 ├── Linux X11 (Override-Redirect Redaction, loginctl)
 └── Linux Wayland (Desktop Notification & Session Lock Fallback)
```

- **Threat Logic Decoupling**: The detection pipeline and `ThreatEngine` have zero awareness of OS window handles, monitors, or display protocols.
- **PrivacyTriggerManager**: Translates immutable threat decisions (`FrameResult`) into platform commands and coordinates overlay lifecycles.
- **PlatformManager**: Implements native OS calls, event queue pumping, and coordinate mapping.

For complete architectural details, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

---

## 🔒 Privacy & Security Model

Blindside is designed around complete physical privacy:
- **No Cloud Dependency**: Runs entirely locally. No internet access is required.
- **No Image Retention**: Frames are processed in volatile RAM buffers and immediately overwritten.
- **No Facial Recognition Database**: Blindside does not identify who is looking at your screen, only their spatial gaze angle.
- **Structured Audit Logging**: Writes structured security event summaries to `blindside_threats.log` (e.g. `Threat=TARGETED_WORKSPACE_BLUR GazeDurationSec=1.20 FacesDetected=2 Control=NIST_SP_800_53_PE_3`).

For full details, see [docs/PRIVACY.md](docs/PRIVACY.md).

---

## ⚠️ Known Limitations

Real-world computer vision on edge devices has inherent physical limitations:
- **Webcam Dependency**: If your camera is disabled, physically covered, or disconnected, Blindside cannot operate.
- **Lighting & Occlusions**: Heavy facial occlusions (sunglasses, medical masks) or extreme backlighting degrade YuNet landmark precision.
- **Digital / Rendered Faces**: High-resolution faces displayed on posters, background TV screens, or magazine covers can occasionally trigger false-positive secondary detections.
- **Alpha Redaction vs. True Blur**: Windows uses a high-opacity dark overlay (~88% opacity, click-through), not GPU-backed compositor blur.
- **X11 ScreenCount Limitation**: On Linux X11, `ScreenCount()` reports X screens; on modern multi-head setups, all physical monitors are merged into Screen 0.
- **Wayland Protocol Restrictions**: Pure Wayland security models prohibit applications from spawning absolute overlays over other windows; Blindside degrades gracefully to desktop notifications and session locks.
- **Session Lock Dependencies**: Wayland session locking requires a functional `systemd-logind` or GNOME screensaver D-Bus service.
- **Native Host Toolchain Requirement**: Automated CTest execution requires a host equipped with Visual Studio 2022 or GCC 11+.

---

## 🧪 Testing

### 1. Python Algorithmic Reference Suite
Blindside maintains an independent Python reference test suite in `tools/reference/`:
```bash
python tools/reference/test_blindside.py
```
**Current Status**: 7/7 tests passed (synthetic fallback, calibration, gaze vectors, screen bounds, hysteresis, micro-movement liveness, spoof detection).

### 2. Native C++ Platform Test Suite (CTest)
```bash
ctest --test-dir build --output-on-failure
```
The native test suite includes `test_platform.cpp`, validating 11 platform contracts: capability detection, overlay lifecycles, idempotent repositioning, clean deactivation, unsupported fallbacks, invalid geometries, negative multi-monitor offsets, headless resilience, threat/platform separation, state immutability, and audit log transition deduplication.

*(Note: Native CTest execution requires a configured local C++ toolchain).*

For full testing procedures, see [docs/TESTING.md](docs/TESTING.md).

---

## 🛠️ Development

- **Language Standard**: ISO C++20 (`std::array`, designated initializers, concepts).
- **Core Library**: OpenCV 4.6+ / 4.10+ (core, imgproc, videoio, highgui, objdetect, calib3d).
- **Architecture Principle**: Keep platform-specific code strictly contained within `src/platform/`. Never introduce threat classification logic into platform managers.

For release history and roadmap, see [RELEASE_NOTES.md](RELEASE_NOTES.md) and [CHANGELOG.md](CHANGELOG.md).

---

## 📚 Documentation Index

- [Architecture Specification](docs/ARCHITECTURE.md)
- [Platform Support & OS Integration](docs/PLATFORM_SUPPORT.md)
- [Privacy & Data Model](docs/PRIVACY.md)
- [Testing & Validation Guide](docs/TESTING.md)
- [Release Notes](RELEASE_NOTES.md)
- [Changelog](CHANGELOG.md)
- [Threat Model](THREAT_MODEL.md)
- [Contributing Guidelines](CONTRIBUTING.md)
- [Security Policy](SECURITY.md)

---

## 📄 License

MIT License. See [LICENSE](LICENSE) for details.
