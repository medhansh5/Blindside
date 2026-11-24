# 🛡️ Threat Model: Blindside

Listen, everyone talks about zero-day exploits and network perimeters, but the most common data leak is some guy on a train staring at your laptop screen. Visual eavesdropping (or "shoulder surfing") is a massive vulnerability that most security tooling completely ignores.

Blindside fixes that at the edge. This document outlines exactly what we're protecting you from, how we do it, and what happens to your data (spoiler: nothing, because we don't save it).

---

## 2. Who Are We Protecting You From?

```
  [ Attacker / Rando on the train ]
               │
               ▼ (Line-of-Sight)
    ┌─────────────────────┐
    │  Your Laptop Screen │ ◄── [ You ] 
    └─────────────────────┘
               ▲
               │ (Continuous 30 FPS YuNet + SolvePnP Scanning)
    ┌─────────────────────┐
    │   Webcam Sensor     │
    └──────────┬──────────┘
               │
               ▼
    [ Blindside Daemon ] ──► [ Screen Redaction / Hard Lock ]
```

### Threat Taxonomy

| Threat ID | Threat Category | Attack Vector | Impact Level | Mitigated by Blindside? |
| :--- | :--- | :--- | :--- | :--- |
| **THREAT-01** | Visual Shoulder Surfing | A stranger looks directly over your shoulder at confidential code or data. | **CRITICAL** (Data Exfiltration) | **YES** (SolvePnP gaze vector math triggers hard lock > 1.0s) |
| **THREAT-02** | Optical Capture / Photos | Someone points a smartphone camera at your screen while standing behind you. | **HIGH** (Persistent Data Theft) | **YES** (Secondary face detection triggers active window redaction) |
| **THREAT-03** | Abandoned Workstation | You step away from your desk and forget to hit `Win+L`. | **HIGH** (Physical Access) | **YES** (Automated absence defense with configurable timeout `primary_absence_timeout_sec` and return dwell `primary_recovery_dwell_sec`; pauses on sensor/inference failure without false locks and preserves active lock states on disconnect) |
| **THREAT-04** | Cloud Telemetry Leakage | A "security tool" uploads your face to an AWS bucket for "processing". | **CRITICAL** (Privacy Nightmare) | **YES** (100% local on-device inference; no remote telemetry, image transmission, or biometric exfiltration) |

---

## 3. How We Actually Stop It

### A. Edge-Native Processing (No Cloud BS)
Everything runs locally on your CPU/GPU using C++20 and OpenCV. 
We allocate a fixed-size `RingBuffer<RawFrame, 4>` at startup. No frames are written to disk. The daemon contains no networking telemetry code and does not transmit camera frames or biometric data over external networks. Your face stays on your machine.

### B. True Spatial Gaze Math
1. **Calibration**: When you start the daemon with `--calibrate`, we memorize where *your* face sits in the frame.
2. **Pose Vector Math**:
   We map YuNet's 5 facial landmarks to a 3D model using `cv::solvePnP`. This gives us accurate pitch and yaw.
3. **Trigger**: If a secondary face has a gaze vector pointing at your screen (within $\pm 30^\circ$ yaw, $\pm 25^\circ$ pitch), we flag it.

### C. Hysteresis & Absence Dwell (No Jitter)
We don't lock your screen because someone walked past you:
- **Eavesdropper Hysteresis**: Secondary gaze must hold for $> 1.0$ second (`config.hysteresis_sec`) before elevating from a soft alert to a hard lock or redaction overlay.
- **Primary Absence Dwell**: Primary user absence must persist beyond `config.primary_absence_timeout_sec` (default: 5.0s) before triggering THREAT-03 defenses, with a sustained return dwell (`config.primary_recovery_dwell_sec`, default: 0.5s) before clearing.
- **Sensor & Inference Failure Isolation**: If the camera disconnects or YuNet inference fails (`DetectionResult::failure`), absence tracking is suspended to prevent false triggers, while an already-active defense is safely preserved without false unlocks.

### D. Per-Track Rotational Micro-Motion & FAR Anti-Spoofing
We evaluate involuntary 3D head pose angular micro-movements (pitch/yaw variance $\ge 0.02$) and geometric Facial Aspect Ratio (FAR) deformation proxies across rolling 45-sample history buffers isolated strictly per secondary track ID (`SecondaryFaceTrack::pose_history`). A live user moving in the foreground will never cause a static photo spoof held behind them to pass liveness verification.

---

## 4. Physical-Access Security Event Auditing

For physical-access security event auditing, Blindside writes structured security event logs to `blindside_threats.log` tagged with an informational control identifier (`Control=NIST_SP_800_53_PE_3`). Logging this identifier assists log categorization but does not establish or certify NIST, ISO 27001, or other regulatory compliance. Events are recorded strictly on state transitions (`_ACTIVATED`, `_ESCALATED`, `_DEACTIVATED`) to prevent disk spam. It logs *what* happened, not *who* it was (zero biometric vectors or images are saved).

```log
2026-11-13 20:24:45 [AUDIT_ALERT] Threat=HARD_WORKSTATION_LOCK_ACTIVATED GazeDurationSec=1.20 FacesDetected=2 LivenessVerified=1 Control=NIST_SP_800_53_PE_3
```

---

## 5. Summary

Blindside takes physical security and pushes it to the edge hardware. It kills the shoulder surfing attack vector without draining your laptop battery and without violating your privacy.

- **Check the benchmarks**: [docs/BENCHMARKS.md](docs/BENCHMARKS.md)
- **Found a vuln?**: [SECURITY.md](SECURITY.md)
- **Want to write some code?**: [CONTRIBUTING.md](CONTRIBUTING.md)
