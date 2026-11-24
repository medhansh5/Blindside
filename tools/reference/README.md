# 🐍 Blindside Python Reference Implementation & Validation Harness

> **NOTICE**: This directory contains an auxiliary Python reference implementation and developer validation harness.
> It is **NOT** production runtime code. The official production daemon for Blindside is and remains **100% C++20**.

---

## 1. Why This Reference Implementation Exists

During development and architecture planning (such as preparing for V4), engineers need a fast, interactive way to:
* Validate mathematical transformations (e.g., Rodrigues rotation vectors to Euler angles, gaze vector projection).
* Experiment with computer vision model behavior (e.g., YuNet landmark outputs, confidence scoring).
* Prototype threat evaluation logic (e.g., Eye Aspect Ratio blink thresholds, micro-motion variance) without requiring an incremental C++ compile cycle.
* Provide an offline algorithmic baseline for automated sanity checks and test assertions.

---

## 2. Production Status & Separation

* **Production Binary**: The shipping application is compiled from C++20 source files in `src/` and `include/` into `blindside_daemon` (or `blindside_daemon.exe` on Windows).
* **Zero Production Dependency**: Neither Python, `opencv-python`, nor `numpy` are required to build or run the production C++ daemon.
* **Scope**: The code in this directory is restricted to developer evaluation, algorithm prototyping, and reference testing.

---

## 3. Algorithms Validated by This Harness

* **YuNet ONNX Face Detection**: Loading `models/face_detection_yunet_2023mar.onnx` and interpreting 5 facial landmarks (right eye, left eye, nose, right mouth, left mouth).
* **3D Head Pose Estimation (`solvePnP`)**: Projecting canonical 3D facial points against 2D landmarks via `cv2.solvePnP` (`SOLVEPNP_EPNP`), converting rotation vectors to Euler angles (pitch, yaw, roll), and calculating 3D unit gaze vectors.
* **Gaze Tolerance Bounds**: Testing yaw/pitch bounds ($\pm 25^\circ$ yaw, $\pm 20^\circ$ pitch in reference defaults, compared to $\pm 30^\circ$ yaw, $\pm 25^\circ$ pitch in production C++ defaults) to verify whether head pose is directed toward the screen.
* **Facial Aspect Ratio (FAR) Geometric Proxy**: Ratio of vertical eye-to-nose distance relative to interocular distance as a geometric proxy (`compute_facial_aspect_ratio()`, with `compute_ear()` retained as an alias; YuNet's 5 landmarks provide centroids without eyelid contours and do not support physiological EAR blink detection).
* **Liveness Anti-Spoofing Baseline**: Angular variance of pitch/yaw micro-movements combined with geometric deformation thresholds over a rolling temporal window to reject static printed photos.

---

## 4. Known Differences from Production C++20 Daemon

While this harness provides algorithmic parity for isolated mathematical calculations, it intentionally differs from the production C++ daemon in critical ways:

| Dimension | Production C++20 Daemon (`src/`, `include/`) | Python Reference Harness (`tools/reference/`) |
| :--- | :--- | :--- |
| **Language & Toolchain** | ISO C++20 (`cmake`, MSVC / GCC / Clang). | Python 3.10+ (`opencv-python`, `numpy`). |
| **Pipeline Architecture** | Decoupled multithreaded pipeline (`std::jthread`, `std::stop_token`) with lock-free `RingBuffer<RawFrame, 4>`. Congestion drops oldest frame. | Synchronous single-threaded execution loop. |
| **Memory Allocation** | Zero heap allocations inside the hot frame-processing loop (`RingBuffer`, static rolling history array). | Dynamic Python object allocation on every frame and detection. |
| **Gaze Bounds Defaults** | $\pm 30^\circ$ yaw, $\pm 25^\circ$ pitch (`max_allowed_yaw_deg = 30.0`, `max_allowed_pitch_deg = 25.0`). | $\pm 25^\circ$ yaw, $\pm 20^\circ$ pitch (`max_allowed_yaw_deg = 25.0`, `max_allowed_pitch_deg = 20.0`). |
| **Multi-Face Tracking & Liveness** | `SecondaryFaceTrack` tracking table with persistent IDs, hit/miss counters, 1.0s timeout pruning, and per-track rolling 45-sample pose histories (`pose_history`) for isolated liveness evaluation. | Raw per-frame detection iteration without temporal track persistence or per-track history isolation. |
| **Primary User Absence Defense** | Calibrated baseline bounding with configurable absence timeout (`primary_absence_timeout_sec`), recovery dwell (`primary_recovery_dwell_sec`), and sensor/inference failure isolation. | Unconditional `min()` distance selection without a maximum tolerance radius or absence state machine. |
| **Windows Platform Response** | Native Win32 `CreateWindowEx` with high-opacity Alpha Redaction (`WS_EX_LAYERED`, `LWA_ALPHA` 225/255, `HTTRANSPARENT` input pass-through). Honestly reports `targeted_blur = false`. | Basic alpha-layered transparent window (`SetLayeredWindowAttributes`). No input pass-through hit-testing. |
| **Hardware Throttling** | Dynamic hardware capture frame-rate throttling (30 FPS active $\leftrightarrow$ 5 FPS idle). | Static frame delay simulation. |

---

## 5. Usage

To run the reference harness:
```bash
# Diagnostic capability report
python tools/reference/run_blindside.py --diagnostics

# Headless synthetic simulation demo (no webcam required)
python tools/reference/run_blindside.py --synthetic --trigger-mode soft

# Run reference test suite
python tools/reference/test_blindside.py
```
