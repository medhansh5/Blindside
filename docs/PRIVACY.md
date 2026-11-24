# 🔒 Blindside Privacy & Data Model

> How Blindside handles sensor data, audit trails, and physical privacy boundaries.

The core motivation behind Blindside is simple: **a privacy tool must never become spyware.**

Many commercial physical-security and attention-tracking tools capture continuous webcam streams, upload frames to cloud processing pipelines, or build facial recognition embedding databases. Blindside is fundamentally architected to eliminate these risks.

---

## 1. Privacy Principles

1. **Local-Only Inference**: All computer vision algorithms—face detection, 3D head pose estimation, liveness analysis, and threat classification—execute entirely on the local host machine using CPU/GPU hardware.
2. **Ephemeral Memory Model**: Webcam frames exist strictly in volatile RAM within a small, fixed pre-allocated buffer (`RingBuffer<RawFrame, 4>`). Frames are processed and immediately overwritten by subsequent camera captures.
3. **No Biometric Identification**: Blindside does not identify *who* is looking at your screen. It measures *spatial geometry* (where a face is oriented relative to the display). It never computes facial recognition embeddings, maintains face databases, or assigns persistent human identities across daemon restarts.
4. **No Image Storage or Transmission**: Blindside has no code paths for saving JPEG/PNG images to disk, taking screenshots, or opening outbound network connections for camera telemetry.

---

## 2. Sensor Data Flow

```text
Webcam Sensor
     │
     ▼
[ OS Driver / V4L2 / Media Foundation ]
     │
     ▼ Raw YUYV / BGR frame (In Volatile RAM only)
┌─────────────────────────────────────────────────────────────┐
│ Blindside Process Memory                                    │
│                                                             │
│   RingBuffer<RawFrame, 4>                                   │
│          │                                                  │
│          ▼                                                  │
│   OpenCV YuNet Detection                                    │
│   (Extracts: Bounding Box & 5 Landmark Points)              │
│          │                                                  │
│          ▼                                                  │
│   cv::solvePnP Head Pose & Gaze Vector                      │
│   (Extracts: Pitch, Yaw, Roll, FAR float)                   │
│          │                                                  │
│          ▼                                                  │
│   [ FRAME IMMEDIATELY DISCARDED / OVERWRITTEN ]             │
└─────────────────────────────────────────────────────────────┘
     │
     ▼ Numerical Events Only
[ Structured Audit Log: blindside_threats.log ]
```

At no point does raw or compressed visual data leave the process boundary.

---

## 3. What Blindside Stores

Blindside writes structured physical-access security event audit logs to a local file (`blindside_threats.log`) for physical security review and event auditing (tagged with informational reference metadata such as `Control=NIST_SP_800_53_PE_3`).

Each log entry records only high-level metadata:
- **Timestamp**: Local system time of the event.
- **Threat Type & Transition**: Trigger action state and lifecycle transition (e.g. `TARGETED_WORKSPACE_BLUR_ACTIVATED`, `HARD_WORKSTATION_LOCK_ACTIVATED`, `PRIMARY_USER_ABSENT_BLUR_ACTIVATED`, `TARGETED_WORKSPACE_BLUR_DEACTIVATED`).
- **Gaze Duration**: Measured duration of sustained secondary gaze or absence in seconds.
- **Faces Detected**: Total number of faces present in the scene.
- **Liveness Verified**: Boolean indicator confirming whether the detected face exhibited live micro-movements.
- **Security Control Metadata**: Informational control tag referencing physical-access security standards (`Control=NIST_SP_800_53_PE_3`).

> [!IMPORTANT]
> **Informational Metadata Only**: The string `Control=NIST_SP_800_53_PE_3` emitted in event logs is strictly informational metadata for log classification. Emitting or logging this identifier does not establish, certify, or guarantee compliance with NIST SP 800-53, ISO 27001, or any other regulatory framework.

> [!NOTE]
> **Transition-Based Deduplication**: Audit events are recorded only on state transitions (`_ACTIVATED`, `_ESCALATED`, `_DEACTIVATED`) rather than on every frame, eliminating 30 Hz disk I/O while preserving a full security trail.

Example log entry:
```text
2026-11-13 20:30:15 [AUDIT_ALERT] Threat=TARGETED_WORKSPACE_BLUR_ACTIVATED GazeDurationSec=1.35 FacesDetected=2 LivenessVerified=1 Control=NIST_SP_800_53_PE_3
```

---

## 4. What Blindside NEVER Stores or Collects

- **No Video Recordings or Photos**: No camera frames or face crops are ever written to disk.
- **No Screenshots**: The daemon inspects active window bounding rectangles (`WindowRect`), never the graphical contents of your screen.
- **No Biometric Templates**: No facial geometry vectors, feature hashes, or recognition representations are generated or stored.
- **No Telemetry / Analytics**: There are no telemetry services, tracking beacons, error report uploads, or cloud accounts.
- **No External Telemetry or Transmission**: The core detection daemon contains no remote networking code and never transmits camera frames, biometric features, or surveillance telemetry over external networks.

---

## 5. Primary User Calibration vs. Identity

When Blindside calibrates the primary user (`--calibrate`):
- It memorizes the **spatial coordinates** (normalized center X, center Y, width, height) of the person sitting directly in front of the laptop.
- It does **not** create a biometric profile of the user's face.
- If a different person sits in the primary chair, Blindside treats whoever occupies that primary bounding box as the active operator.

---

## 6. Real-World Privacy Boundaries & Limitations

While Blindside is engineered for physical privacy, users should recognize its operational boundaries:
1. **Host Compromise**: If the host machine is compromised by kernel-level malware or an administrative rootkit, malicious actors can tap camera drivers directly. Blindside does not provide host OS malware defenses.
2. **Physical Line-of-Sight**: Blindside's field of view is bounded by your camera sensor. An eavesdropper observing your screen from an angle outside the webcam's lens cannot be detected.
3. **Local Log Exposure**: `blindside_threats.log` is stored locally in the current working directory. While it contains no images or identities, it records timestamps and presence counts. Ensure appropriate filesystem permissions on multi-user systems.
