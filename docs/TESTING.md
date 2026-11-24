# 🧪 Blindside Testing Guide

> Complete reference for running, validating, and extending tests across Blindside.

Blindside maintains a multi-tiered test strategy to ensure that computer vision algorithms, temporal state management, and native operating system privacy triggers perform deterministically and reliably.

---

## 1. Test Architecture Overview

```text
┌─────────────────────────────────────────────────────────────┐
│ 1. Python Algorithmic Reference Suite                       │
│    (tools/reference/test_blindside.py)                      │
│    Fast, headless validation of CV math & hysteresis logic  │
└─────────────────────────────────────────────────────────────┘
                             │ Validates algorithms
                             ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. Native C++ Unit & Core Engine Tests (CTest)              │
│    - test_ring_buffer       - test_eavesdropper             │
│    - test_gaze_math         - test_pipeline                 │
│    - test_liveness          - test_camera_failure           │
└─────────────────────────────────────────────────────────────┘
                             │ Validates contracts
                             ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. Platform Hardening Test Suite                            │
│    (tests/test_platform.cpp)                                │
│    Direct OS windowing, lifecycle, multi-monitor & leaks    │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Python Reference Test Suite

### Purpose
The Python reference implementation located in `tools/reference/` serves as an executable specification and developer validation harness. It mirrors the core mathematics of Blindside without requiring native C++ build tooling.

### How to Run
```bash
python tools/reference/test_blindside.py
```

### Reference Test Cases (7 Unit Tests)
- `test_synthetic_camera_fallback`: Validates fallback to synthetic frame stream when hardware camera is missing.
- `test_primary_user_calibration`: Validates spatial calibration of the primary operator bounding box.
- `test_gaze_vector_computation`: Validates 3D head pose estimation and spatial gaze vector projection.
- `test_screen_gaze_bounds`: Validates bounding logic for gaze directed toward display bounds.
- `test_eavesdropper_gaze_hysteresis`: Validates that transient glances do not trigger defense until the hysteresis threshold ($> 1.0\text{s}$) is exceeded.
- `test_live_user_micro_movements`: Validates rotational micro-movement variance detection for live users.
- `test_spoof_detection`: Validates rejection of static photos exhibiting zero motion variance.

---

## 3. Native C++ Test Suite (CTest)

Native C++ tests are integrated with CMake and CTest.

### Prerequisites
- C++20 compliant compiler (`MSVC 19.30+` / Visual Studio 2022, `GCC 11+`, or `Clang 14+`)
- CMake 3.20+
- OpenCV 4.10+ C++ development packages (required modules: `core, imgproc, videoio, highgui, objdetect, calib3d, dnn`)

> [!NOTE]
> **OpenCV Version Requirement**: OpenCV 4.10+ is strictly required for compatibility with the YuNet 2023mar ONNX model. On Ubuntu 24.04 LTS runners, the default distribution package (`libopencv-dev` 4.6) is incompatible and triggers an ONNX layer failure (`Layer with requested id=-1 not found in function 'getLayerData'`). The Linux CI workflow builds OpenCV 4.10.0 from source with the required modules and caches it across runs.

### How to Build & Run

#### Linux (Ubuntu 24.04 LTS+ / Debian)
```bash
# 1. Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 2. Compile
cmake --build build -j"$(nproc)"

# 3. Execute all tests
ctest --test-dir build --output-on-failure
```

#### Windows (Visual Studio 2022 x64)
```powershell
# 1. Configure
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# 2. Compile Release binaries
cmake --build build --config Release --parallel

# 3. Execute all tests
ctest --test-dir build -C Release --output-on-failure
```

### Registered Native Test Targets
1. `test_ring_buffer`: Fixed-capacity zero-allocation circular buffer push, pop, overwrite, and boundary safety.
2. `test_gaze_math`: 3D head pose math (pitch, yaw, roll), perspective projection, and Euler angle conversion.
3. `test_eavesdropper`: Multi-face calibration, secondary face filtering, temporal dwell timers, per-track liveness isolation, primary user absence lifecycle (THREAT-03), and inference failure semantics.
4. `test_liveness`: Angular variance thresholds and Facial Aspect Ratio (FAR) geometric deformation proxy anti-spoofing.
5. `test_pipeline`: Synthetic eavesdropper injection pipeline (deterministic structured detection fixtures exercising solvePnP, tracking, threat detection, and triggers without physical hardware webcams or live ONNX inference), YuNet ONNX model pipeline inference on empty frames when the model file is available on disk (skipping gracefully if absent), and detector error handling with recovery.
6. `test_camera_failure`: Pipeline resilience when camera frames become corrupted or hardware disconnects.
7. `test_platform`: Dedicated 11-test platform hardening suite.

---

## 4. Platform Hardening Test Suite (`tests/test_platform.cpp`)

`test_platform` exercises native OS windowing, event handling, and memory lifecycle without opening hardware cameras:

| # | Test Name | Purpose & Verification Scope |
|---|---|---|
| 1 | `test_platform_capability_detection` | Inspects capability flags. Verifies that `targeted_blur` is honestly reported as `false` across all platforms. |
| 2 | `test_repeated_overlay_lifecycle` | Cycles 10 rapid overlay creations and destructions; verifies no native handle or GDI/X11 surface leaks. |
| 3 | `test_idempotent_overlay_activation` | Calls overlay activation multiple times with identical and changing geometry; ensures repositioning without duplicate overlays. |
| 4 | `test_cleanup_after_deactivation` | Verifies that calling `clear_alerts()` repeatedly or when no alert is active is safe and idempotent. |
| 5 | `test_unsupported_capability_fallback` | Validates graceful degradation through `PrivacyTriggerManager` when screen lock or overlays are unsupported. |
| 6 | `test_invalid_display_geometry` | Passes zero dimensions, negative sizes, and unflagged rects; ensures platform handles them safely without crashing. |
| 7 | `test_multi_monitor_negative_coordinates` | Validates overlay positioning for secondary monitors positioned left of (`x = -1920`) or above (`y = -1080`) the primary monitor. |
| 8 | `test_platform_resilience_and_graceful_recovery` | Drains event pumps when no windows exist; tests geometry queries in headless environments. |
| 9 | `test_threat_independent_from_platform` | Verifies that threat assessment data remains identical and pure regardless of whether platform overlays are active. |
| 10 | `test_platform_response_does_not_modify_threat_engine` | Verifies that platform trigger execution never mutates or feeds back into threat assessment models. |
| 11 | `test_audit_log_deduplication` | Verifies that audit logging records security events strictly on state transitions (`_ACTIVATED`, `_ESCALATED`, `_DEACTIVATED`) and deduplicates repeated alerts across frames. |

---

## 5. Development Host Validation Status

> [!NOTE]
> **Environment Note:**
> - On the current development host, Python reference tests and native CTest targets have not been executed locally due to the absence of required environment dependencies (`numpy` for Python, Visual Studio MSVC / GCC for C++).
> - Test suites are designed for execution in configured CI environments (Ubuntu 24.04 and Windows runners) with full dependency toolchains.
> - Native binaries must be compiled and executed on a host or CI runner equipped with Visual Studio 2022 or GCC 11+.
