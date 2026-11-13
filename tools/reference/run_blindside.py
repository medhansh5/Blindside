"""
Blindside: Python Developer Reference Implementation & Validation Harness
NOTE: This is NOT the production runtime. The shipping Blindside daemon is written in C++20.
This harness is used for algorithmic prototyping, mathematics validation (solvePnP, gaze, EAR),
and offline testing. See tools/reference/README.md for architectural separation details.
"""

import argparse
import math
import os
import sys
import time
import threading
from collections import deque
from dataclasses import dataclass, field
from datetime import datetime
from typing import List, Optional, Tuple

if sys.platform == "win32":
    import ctypes
    import ctypes.wintypes

import cv2
import numpy as np

# Ensure Windows console supports Unicode output safely
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

# ==============================================================================
# Domain Types (matching include/blindside/types.hpp)
# ==============================================================================

@dataclass
class Point2f:
    x: float = 0.0
    y: float = 0.0

@dataclass
class Point3f:
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

@dataclass
class FaceBox:
    x: float = 0.0
    y: float = 0.0
    width: float = 0.0
    height: float = 0.0
    confidence: float = 0.0
    landmarks: List[Point2f] = field(default_factory=lambda: [Point2f() for _ in range(5)])

    def center_x(self) -> float:
        return self.x + self.width * 0.5

    def center_y(self) -> float:
        return self.y + self.height * 0.5

@dataclass
class HeadPose:
    pitch_deg: float = 0.0
    yaw_deg: float = 0.0
    roll_deg: float = 0.0
    gaze_vector: Point3f = field(default_factory=Point3f)
    is_looking_at_screen: bool = False
    valid: bool = False

@dataclass
class WindowRect:
    x: int = 0
    y: int = 0
    width: int = 0
    height: int = 0
    valid: bool = False

@dataclass
class PlatformDiagnostics:
    os_name: str = "Windows"
    supports_native_redaction: bool = True
    supports_screen_lock: bool = True
    supports_desktop_notifications: bool = True
    monitor_count: int = 1

@dataclass
class Config:
    camera_index: int = 0
    capture_width: int = 640
    capture_height: int = 480
    active_fps: float = 30.0
    idle_fps: float = 5.0
    idle_timeout_sec: float = 4.0
    hysteresis_sec: float = 1.0
    max_allowed_yaw_deg: float = 25.0
    max_allowed_pitch_deg: float = 20.0
    ear_blink_threshold: float = 0.20
    liveness_window_sec: float = 3.0
    model_path: str = ""
    log_file_path: str = "blindside_threats.log"
    daemon_state: str = "graceful"      # "graceful", "strict", "pause"
    trigger_mode: str = "both"          # "soft", "hard", "both", "log"
    enable_screen_lock: bool = True

@dataclass
class FrameResult:
    faces: List[FaceBox] = field(default_factory=list)
    secondary_gaze_detected: bool = False
    secondary_gaze_duration_sec: float = 0.0
    secondary_liveness_verified: bool = False
    is_spoof_photo: bool = False
    trigger_soft_alert: bool = False
    trigger_hard_defense: bool = False
    trigger_targeted_blur: bool = False
    daemon_state: str = "graceful"

# ==============================================================================
# Camera Engine (matching src/core/camera.cpp)
# ==============================================================================

class CameraCapture:
    def __init__(self, config: Config):
        self.config = config
        self.current_fps = config.active_fps
        self.cap: Optional[cv2.VideoCapture] = None
        self.synthetic_mode = False
        self.frame_counter = 0
        self.is_open = False
        self.last_frame_time = time.time()
        self.synthetic_eavesdropper_present = False
        self.synthetic_eavesdropper_yaw = 0.0

    def open(self) -> bool:
        if self.synthetic_mode:
            self.is_open = True
            print("[CameraCapture] Initialized in Synthetic / Emulation Mode.")
            return True

        self.cap = cv2.VideoCapture(self.config.camera_index)
        if self.cap.isOpened():
            self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.config.capture_width)
            self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.config.capture_height)
            self.cap.set(cv2.CAP_PROP_FPS, self.current_fps)
            self.is_open = True
            print(f"[CameraCapture] Hardware camera index {self.config.camera_index} opened successfully.")
            return True

        print("[CameraCapture] Hardware camera unavailable. Falling back to synthetic stream.")
        self.synthetic_mode = True
        self.is_open = True
        return True

    def close(self):
        if self.cap is not None:
            self.cap.release()
            self.cap = None
        self.is_open = False

    def set_fps(self, target_fps: float):
        self.current_fps = target_fps

    def set_synthetic_eavesdropper(self, present: bool, gaze_yaw: float = 0.0):
        self.synthetic_eavesdropper_present = present
        self.synthetic_eavesdropper_yaw = gaze_yaw

    def grab_frame(self) -> Optional[np.ndarray]:
        if not self.is_open:
            return None

        self.frame_counter += 1

        if self.synthetic_mode:
            # Generate synthetic gradient test frame
            frame = np.zeros((self.config.capture_height, self.config.capture_width, 3), dtype=np.uint8)
            base_color = (self.frame_counter * 5) % 256
            frame[:, :] = [base_color, 120, (200 - base_color) % 256]

            # Sleep to match target FPS
            delay = 1.0 / max(1.0, self.current_fps)
            time.sleep(delay)
            return frame

        if self.cap is not None and self.cap.isOpened():
            now = time.time()
            target_interval = 1.0 / max(1.0, self.current_fps)
            if now - self.last_frame_time < target_interval:
                self.cap.grab()
                return None

            ret, frame = self.cap.read()
            if ret and frame is not None:
                self.last_frame_time = time.time()
                return frame
            else:
                print("[CameraCapture] Hardware frame read failed. Closing camera...")
                self.close()
                return None

        return None

# ==============================================================================
# Vision Engine: YuNet Face Detector & SolvePnP Pose Estimator
# (matching src/engine/face_detector.cpp & pose_estimator.cpp)
# ==============================================================================

class FaceDetector:
    def __init__(self, config: Config):
        self.config = config
        self.detector = None
        self.initialized = False

    def initialize(self, model_path: str) -> bool:
        if not os.path.exists(model_path):
            print(f"[FaceDetector] Model file not found at: {model_path}")
            return False

        try:
            self.detector = cv2.FaceDetectorYN.create(
                model_path, "", (320, 320), 0.6, 0.3, 5000
            )
            self.initialized = (self.detector is not None)
            if self.initialized:
                print(f"[FaceDetector] Successfully loaded YuNet model: {model_path}")
            return self.initialized
        except Exception as e:
            print(f"[FaceDetector] Failed to load YuNet model: {e}")
            return False

    def detect(self, frame: np.ndarray) -> List[FaceBox]:
        results: List[FaceBox] = []
        if not self.initialized or self.detector is None or frame is None:
            return results

        h, w = frame.shape[:2]
        self.detector.setInputSize((w, h))

        _, faces = self.detector.detect(frame)
        if faces is None:
            return results

        for face in faces:
            conf = float(face[14])
            if conf > 0.6:
                box = FaceBox(
                    x=float(face[0]),
                    y=float(face[1]),
                    width=float(face[2]),
                    height=float(face[3]),
                    confidence=conf,
                    landmarks=[
                        Point2f(float(face[4]), float(face[5])),   # Right eye
                        Point2f(float(face[6]), float(face[7])),   # Left eye
                        Point2f(float(face[8]), float(face[9])),   # Nose
                        Point2f(float(face[10]), float(face[11])), # Right mouth
                        Point2f(float(face[12]), float(face[13]))  # Left mouth
                    ]
                )
                results.append(box)

        return results

class PoseEstimator:
    # 5 generic 3D model points matching YuNet 5 landmarks
    MODEL_POINTS = np.array([
        [-225.0,  170.0, -135.0],  # Right eye
        [ 225.0,  170.0, -135.0],  # Left eye
        [   0.0,    0.0,    0.0],  # Nose
        [-150.0, -150.0, -125.0],  # Right mouth
        [ 150.0, -150.0, -125.0]   # Left mouth
    ], dtype=np.float64)

    def __init__(self, config: Config):
        self.config = config

    @staticmethod
    def compute_ear(face: FaceBox) -> float:
        eye_r = face.landmarks[0]
        eye_l = face.landmarks[1]
        nose  = face.landmarks[2]

        dx = eye_l.x - eye_r.x
        dy = eye_l.y - eye_r.y
        interocular_dist = math.sqrt(dx * dx + dy * dy)
        if interocular_dist < 0.001:
            return 0.30

        eye_center_y = (eye_r.y + eye_l.y) * 0.5
        nose_dist_v = abs(nose.y - eye_center_y)
        ear = (nose_dist_v / (1.8 * interocular_dist)) if (nose_dist_v > 0.001) else 0.30
        return max(0.05, min(0.45, ear))

    @staticmethod
    def compute_gaze_vector(pitch_deg: float, yaw_deg: float) -> Point3f:
        p_rad = math.radians(pitch_deg)
        y_rad = math.radians(yaw_deg)
        return Point3f(
            x=math.sin(y_rad) * math.cos(p_rad),
            y=-math.sin(p_rad),
            z=math.cos(y_rad) * math.cos(p_rad)
        )

    def estimate_pose(self, face: FaceBox, frame_w: int, frame_h: int) -> HeadPose:
        pose = HeadPose()
        if frame_w <= 0 or frame_h <= 0:
            return pose

        image_points = np.array([
            [face.landmarks[i].x, face.landmarks[i].y] for i in range(5)
        ], dtype=np.float64)

        focal_length = float(frame_w)
        center = (frame_w / 2.0, frame_h / 2.0)
        camera_matrix = np.array([
            [focal_length, 0, center[0]],
            [0, focal_length, center[1]],
            [0, 0, 1]
        ], dtype=np.float64)
        dist_coeffs = np.zeros((4, 1), dtype=np.float64)

        try:
            success, rvec, tvec = cv2.solvePnP(
                self.MODEL_POINTS, image_points, camera_matrix, dist_coeffs, flags=cv2.SOLVEPNP_EPNP
            )
            if not success or rvec is None:
                return pose

            rmat, _ = cv2.Rodrigues(rvec)

            m00, m10, m20 = rmat[0, 0], rmat[1, 0], rmat[2, 0]
            m21, m22 = rmat[2, 1], rmat[2, 2]
            sy = math.sqrt(m00 * m00 + m10 * m10)

            if sy >= 1e-6:
                x = math.atan2(m21, m22)
                y = math.atan2(-m20, sy)
                z = math.atan2(m10, m00)
            else:
                x = math.atan2(-rmat[1, 2], rmat[1, 1])
                y = math.atan2(-m20, sy)
                z = 0.0

            pose.pitch_deg = math.degrees(x)
            pose.yaw_deg = math.degrees(y)
            pose.roll_deg = math.degrees(z)

            if pose.pitch_deg > 90.0:
                pose.pitch_deg -= 180.0
            elif pose.pitch_deg < -90.0:
                pose.pitch_deg += 180.0

            pose.yaw_deg = -pose.yaw_deg
            pose.gaze_vector = self.compute_gaze_vector(pose.pitch_deg, pose.yaw_deg)
            pose.is_looking_at_screen = self.is_gaze_directed_at_screen(pose)
            pose.valid = True

        except Exception:
            pose.valid = False

        return pose

    def is_gaze_directed_at_screen(self, pose: HeadPose) -> bool:
        return (abs(pose.yaw_deg) <= self.config.max_allowed_yaw_deg and
                abs(pose.pitch_deg) <= self.config.max_allowed_pitch_deg)

# ==============================================================================
# Threat Engine: Eavesdropper & Liveness Detector
# (matching src/engine/eavesdropper_detector.cpp)
# ==============================================================================

@dataclass
class PoseSample:
    timestamp: float
    pitch: float
    yaw: float
    ear: float
    valid: bool = True

class EavesdropperDetector:
    def __init__(self, config: Config, detector: FaceDetector, estimator: PoseEstimator):
        self.config = config
        self.detector = detector
        self.estimator = estimator
        self.primary_calibration_box = FaceBox(x=0.35, y=0.30, width=0.30, height=0.40)
        self.calibrated = False
        self.secondary_gaze_active = False
        self.secondary_gaze_start_time = 0.0
        self.pose_history: deque[PoseSample] = deque(maxlen=64)

    def calibrate(self, frame: np.ndarray) -> bool:
        faces = self.detector.detect(frame)
        if not faces:
            print("[EavesdropperDetector] Calibration failed: No face detected.")
            self.calibrated = False
            return False

        h, w = frame.shape[:2]
        fc_x, fc_y = w * 0.5, h * 0.5
        best_box = min(faces, key=lambda f: (f.center_x() - fc_x)**2 + (f.center_y() - fc_y)**2)

        self.primary_calibration_box = FaceBox(
            x=best_box.x / w,
            y=best_box.y / h,
            width=best_box.width / w,
            height=best_box.height / h
        )
        self.calibrated = True
        print(f"[EavesdropperDetector] Primary user calibrated at [{self.primary_calibration_box.center_x():.2f}, {self.primary_calibration_box.center_y():.2f}]")
        return True

    def evaluate_liveness(self, pitch: float, yaw: float, ear: float) -> Tuple[bool, bool]:
        now = time.time()
        self.pose_history.append(PoseSample(timestamp=now, pitch=pitch, yaw=yaw, ear=ear))

        if len(self.pose_history) < 4:
            return True, False  # Not enough samples yet, assume live

        samples = [s for s in self.pose_history if (now - s.timestamp) <= self.config.liveness_window_sec]
        if len(samples) < 3:
            return True, False

        mean_p = sum(s.pitch for s in samples) / len(samples)
        mean_y = sum(s.yaw for s in samples) / len(samples)
        min_ear = min(s.ear for s in samples)

        var_p = sum((s.pitch - mean_p)**2 for s in samples) / len(samples)
        var_y = sum((s.yaw - mean_y)**2 for s in samples) / len(samples)
        total_variance = var_p + var_y

        # Anti-spoofing: static photo has total_variance < 0.02 and min_ear > blink threshold
        if total_variance < 0.02 and min_ear > self.config.ear_blink_threshold:
            return False, True  # (not live, is spoof)

        return True, False

    def process_frame(self, frame: np.ndarray, synthetic_eavesdropper: bool = False, synthetic_yaw: float = 0.0) -> FrameResult:
        result = FrameResult()
        h, w = frame.shape[:2]

        faces = self.detector.detect(frame)

        # Inject synthetic faces if requested for simulation testing
        if synthetic_eavesdropper:
            # Primary user face centered
            primary_box = FaceBox(
                x=w * 0.35, y=h * 0.30, width=w * 0.30, height=h * 0.40, confidence=0.98,
                landmarks=[
                    Point2f(w * 0.42, h * 0.45), Point2f(w * 0.58, h * 0.45), Point2f(w * 0.50, h * 0.52),
                    Point2f(w * 0.44, h * 0.60), Point2f(w * 0.56, h * 0.60)
                ]
            )
            # Secondary shoulder-surfer face looking towards screen
            synth_box = FaceBox(
                x=w * 0.05, y=h * 0.10, width=w * 0.20, height=h * 0.25, confidence=0.95,
                landmarks=[
                    Point2f(w * 0.10, h * 0.20), Point2f(w * 0.20, h * 0.20), Point2f(w * 0.15, h * 0.25),
                    Point2f(w * 0.12, h * 0.30), Point2f(w * 0.18, h * 0.30)
                ]
            )
            faces.extend([primary_box, synth_box])

        result.faces = faces
        if not faces:
            self.secondary_gaze_active = False
            return result

        # Identify primary face (closest to calibration center)
        cal_cx = self.primary_calibration_box.center_x() * w
        cal_cy = self.primary_calibration_box.center_y() * h
        primary_idx = min(range(len(faces)), key=lambda i: (faces[i].center_x() - cal_cx)**2 + (faces[i].center_y() - cal_cy)**2)

        # Process secondary faces
        secondary_looking_at_screen = False
        for i, face in enumerate(faces):
            if i == primary_idx:
                continue

            # Estimate pose
            if synthetic_eavesdropper and i == len(faces) - 1:
                pose = HeadPose(pitch_deg=0.0, yaw_deg=synthetic_yaw, valid=True, is_looking_at_screen=(abs(synthetic_yaw) <= self.config.max_allowed_yaw_deg))
            else:
                pose = self.estimator.estimate_pose(face, w, h)

            if pose.valid and pose.is_looking_at_screen:
                secondary_looking_at_screen = True
                ear = self.estimator.compute_ear(face)
                live, spoof = self.evaluate_liveness(pose.pitch_deg, pose.yaw_deg, ear)
                result.secondary_liveness_verified = live
                result.is_spoof_photo = spoof
                break

        now = time.time()
        if secondary_looking_at_screen:
            if not self.secondary_gaze_active:
                self.secondary_gaze_active = True
                self.secondary_gaze_start_time = now

            duration = now - self.secondary_gaze_start_time
            result.secondary_gaze_detected = True
            result.secondary_gaze_duration_sec = duration

            # Trigger evaluation
            if self.config.trigger_mode in ("soft", "both"):
                result.trigger_soft_alert = True

            if duration >= self.config.hysteresis_sec:
                if self.config.daemon_state == "strict":
                    result.trigger_hard_defense = True
                else:
                    result.trigger_targeted_blur = True
        else:
            self.secondary_gaze_active = False

        return result

# ==============================================================================
# Platform Manager: Native / Reference Prototypes
# (matching src/platform/)
# ==============================================================================

class PlatformManagerBase:
    def initialize(self) -> bool:
        return True

    def get_diagnostics(self) -> PlatformDiagnostics:
        return PlatformDiagnostics(
            os_name="Generic (Reference Prototype)",
            supports_native_redaction=False,
            supports_screen_lock=True,
            supports_desktop_notifications=True,
            monitor_count=1
        )

    def get_active_window_geometry(self) -> WindowRect:
        return WindowRect(x=100, y=100, width=1280, height=800, valid=True)

    def trigger_targeted_blur(self, rect: WindowRect):
        pass

    def trigger_soft_alert(self, message: str):
        pass

    def trigger_hard_defense(self, message: str):
        pass

    def clear_alerts(self):
        pass


class WindowsPlatformManager(PlatformManagerBase):
    def __init__(self):
        if sys.platform == "win32":
            self.user32 = ctypes.windll.user32
            self.dwmapi = ctypes.windll.dwmapi
        else:
            self.user32 = None
            self.dwmapi = None
        self.overlay_hwnd = None

    def initialize(self) -> bool:
        return sys.platform == "win32"

    def get_diagnostics(self) -> PlatformDiagnostics:
        if not self.user32:
            return super().get_diagnostics()
        monitors = self.user32.GetSystemMetrics(80) # SM_CMONITORS
        return PlatformDiagnostics(
            os_name="Windows (Reference Prototype)",
            supports_native_redaction=True,
            supports_screen_lock=True,
            supports_desktop_notifications=True,
            monitor_count=max(1, monitors)
        )

    def get_active_window_geometry(self) -> WindowRect:
        if not self.user32:
            return super().get_active_window_geometry()
        hwnd = self.user32.GetForegroundWindow()
        if hwnd:
            rect = ctypes.wintypes.RECT()
            if self.user32.GetWindowRect(hwnd, ctypes.byref(rect)):
                return WindowRect(
                    x=rect.left,
                    y=rect.top,
                    width=rect.right - rect.left,
                    height=rect.bottom - rect.top,
                    valid=True
                )
        return WindowRect(x=100, y=100, width=1280, height=800, valid=True)

    def trigger_targeted_blur(self, rect: WindowRect):
        if not self.user32:
            return
        WS_EX_LAYERED = 0x00080000
        WS_EX_TRANSPARENT = 0x00000020
        WS_EX_TOPMOST = 0x00000008
        WS_EX_TOOLWINDOW = 0x00000080
        WS_POPUP = 0x80000000
        WS_VISIBLE = 0x10000000
        LWA_ALPHA = 0x00000002

        if not self.overlay_hwnd:
            self.overlay_hwnd = self.user32.CreateWindowExW(
                WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                "STATIC", "BlindsidePrivacyBlur",
                WS_POPUP | WS_VISIBLE,
                rect.x, rect.y, rect.width, rect.height,
                0, 0, 0, 0
            )
            if self.overlay_hwnd:
                self.user32.SetLayeredWindowAttributes(self.overlay_hwnd, 0, 220, LWA_ALPHA)
        else:
            self.user32.SetWindowPos(
                self.overlay_hwnd, -1, rect.x, rect.y, rect.width, rect.height, 0x0040
            )

    def trigger_soft_alert(self, message: str):
        if self.user32:
            self.user32.MessageBeep(0x00000030)  # MB_ICONWARNING

    def trigger_hard_defense(self, message: str):
        if self.user32:
            self.user32.LockWorkStation()

    def clear_alerts(self):
        if self.user32 and self.overlay_hwnd:
            self.user32.DestroyWindow(self.overlay_hwnd)
            self.overlay_hwnd = None


def create_platform_manager() -> PlatformManagerBase:
    if sys.platform == "win32":
        return WindowsPlatformManager()
    return PlatformManagerBase()


# ==============================================================================
# Privacy Trigger Manager (matching src/trigger/privacy_trigger.cpp)
# ==============================================================================

class PrivacyTriggerManager:
    def __init__(self, config: Config):
        self.config = config
        self.platform = create_platform_manager()
        self.soft_alert_active = False
        self.hard_defense_active = False
        self.targeted_blur_active = False

    def initialize(self) -> bool:
        return self.platform.initialize()

    def clear_alerts(self):
        if self.soft_alert_active or self.hard_defense_active or self.targeted_blur_active:
            print("[PrivacyTrigger] Clearing active privacy alerts and overlays.")
            self.soft_alert_active = False
            self.hard_defense_active = False
            self.targeted_blur_active = False
            self.platform.clear_alerts()

    def log_threat_event(self, threat_type: str, gaze_duration: float, face_count: int, live_verified: bool):
        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        log_line = (f"{ts} [AUDIT_ALERT] Threat={threat_type} "
                    f"GazeDurationSec={gaze_duration:.2f} FacesDetected={face_count} "
                    f"LivenessVerified={'1' if live_verified else '0'}\n")
        print(f"[AUDIT LOG] Threat: {threat_type} | Duration: {gaze_duration:.2f}s | Faces: {face_count} | Live: {live_verified}")
        with open(self.config.log_file_path, "a") as f:
            f.write(log_line)

    def execute_triggers(self, result: FrameResult):
        if result.trigger_targeted_blur:
            if not self.targeted_blur_active:
                rect = self.platform.get_active_window_geometry()
                print(f"\033[1;36m[TARGETED PRIVACY OVERLAY] Redacting Active Workspace Window [{rect.x}, {rect.y}, {rect.width}x{rect.height}]\033[0m")
                self.platform.trigger_targeted_blur(rect)
                self.targeted_blur_active = True
                self.log_threat_event("TARGETED_WORKSPACE_BLUR", result.secondary_gaze_duration_sec, len(result.faces), result.secondary_liveness_verified)
        elif result.trigger_hard_defense:
            if not self.hard_defense_active:
                print("\033[1;31m[HARD DEFENSE TRIGGERED] Workstation Lock Engaged!\033[0m")
                if self.config.enable_screen_lock:
                    self.platform.trigger_hard_defense("Eavesdropper detected")
                self.hard_defense_active = True
                self.log_threat_event("HARD_WORKSTATION_LOCK", result.secondary_gaze_duration_sec, len(result.faces), result.secondary_liveness_verified)
        elif result.trigger_soft_alert:
            if not self.soft_alert_active:
                print("\033[1;33m[SOFT ALERT] Secondary face looking towards screen!\033[0m")
                self.platform.trigger_soft_alert("Secondary gaze detected")
                self.soft_alert_active = True
                self.log_threat_event("SOFT_SECONDARY_FACE_DETECTED", result.secondary_gaze_duration_sec, len(result.faces), result.secondary_liveness_verified)
        else:
            self.clear_alerts()

# ==============================================================================
# Daemon Orchestrator (Reference Prototype)
# ==============================================================================

class Daemon:
    def __init__(self, config: Config, synthetic_mode: bool = False):
        self.config = config
        self.synthetic_mode = synthetic_mode
        self.camera = CameraCapture(config)
        if synthetic_mode:
            self.camera.synthetic_mode = True
        self.detector = FaceDetector(config)
        self.estimator = PoseEstimator(config)
        self.eavesdropper = EavesdropperDetector(config, self.detector, self.estimator)
        self.trigger = PrivacyTriggerManager(config)
        self.running = False
        self.stop_event = threading.Event()

    def initialize(self) -> bool:
        if not self.camera.open():
            return False
        if not self.detector.initialize(self.config.model_path):
            return False
        if not self.trigger.initialize():
            return False
        return True

    def calibrate_primary_user(self) -> bool:
        frame = self.camera.grab_frame()
        if frame is not None:
            return self.eavesdropper.calibrate(frame)
        return False

    def run_cycle(self, synthetic_surf: bool = False, synth_yaw: float = 0.0):
        frame = self.camera.grab_frame()
        if frame is None:
            return
        result = self.eavesdropper.process_frame(frame, synthetic_surf, synth_yaw)
        self.trigger.execute_triggers(result)

    def start(self, run_forever: bool = False):
        self.running = True
        print("[Daemon] Starting Blindside Python Reference Daemon...")
        try:
            if run_forever:
                print("[Blindside CLI] Reference daemon active. Press Ctrl+C to stop.")
                while not self.stop_event.is_set():
                    self.run_cycle()
            else:
                # Diagnostic cycle
                print("[Blindside CLI] Running diagnostic verification cycle...")
                for _ in range(10):
                    self.run_cycle()
                    time.sleep(0.1)

                if self.synthetic_mode:
                    print("\n[Blindside CLI] Injecting synthetic live shoulder surfer event...")
                    for _ in range(15):
                        self.run_cycle(synthetic_surf=True, synth_yaw=0.0)
                        time.sleep(0.1)
        except KeyboardInterrupt:
            print("\n[Blindside CLI] Interrupt received. Shutting down...")
        finally:
            self.stop()

    def stop(self):
        self.running = False
        self.stop_event.set()
        self.trigger.clear_alerts()
        self.camera.close()
        print("[Blindside CLI] Reference process exited cleanly.")

# ==============================================================================
# CLI Entry Point
# ==============================================================================

def print_diagnostics():
    platform = create_platform_manager()
    diag = platform.get_diagnostics()
    print("Blindside diagnostics (Python Reference Harness)")
    print("────────────────────────────────────────────────")
    print(f"Platform: {diag.os_name}")
    print(f"Monitors: {diag.monitor_count}\n")
    print("Vision engine: ✓ (OpenCV YuNet + SolvePnP)")
    print("Threat engine: ✓ (Reference prototype)\n")
    print("Privacy responses:")
    print(f"  Screen lock: {'✓' if diag.supports_screen_lock else '✗'}")
    print(f"  Redaction: {'✓' if diag.supports_native_redaction else '✗'}")
    print(f"  Notifications: {'✓' if diag.supports_desktop_notifications else '✗'}\n")
    print("Network communication: disabled")

def main():
    parser = argparse.ArgumentParser(description="🛡️ BLINDSIDE (Python Developer Reference Harness)")
    parser.add_argument("--daemon", action="store_true", help="Run continuously in reference background mode")
    parser.add_argument("--mode", choices=["graceful", "strict", "pause"], default="graceful", help="Operational mode")
    parser.add_argument("--calibrate", action="store_true", help="Calibrate primary user baseline face position")
    parser.add_argument("--synthetic", action="store_true", help="Run in synthetic headless test mode (no camera needed)")
    parser.add_argument("--trigger-mode", choices=["soft", "hard", "both", "log"], default="both")
    parser.add_argument("--fps-active", type=float, default=30.0, help="Active monitoring FPS (default: 30)")
    parser.add_argument("--fps-idle", type=float, default=5.0, help="Low-power idle FPS (default: 5)")
    parser.add_argument("--hysteresis", type=float, default=1.0, help="Gaze duration required for lock/redaction (sec)")
    parser.add_argument("--model-path", type=str, default="", help="Path to YuNet ONNX model")
    parser.add_argument("--log-path", type=str, default="blindside_threats.log", help="Path to audit log")
    parser.add_argument("--diagnostics", action="store_true", help="Print platform capability diagnostics and exit")

    args = parser.parse_args()

    if args.diagnostics:
        print_diagnostics()
        return

    config = Config(
        active_fps=args.fps_active,
        idle_fps=args.fps_idle,
        hysteresis_sec=args.hysteresis,
        daemon_state=args.mode,
        trigger_mode=args.trigger_mode,
        log_file_path=args.log_path,
        enable_screen_lock=(args.trigger_mode in ("hard", "both"))
    )

    if args.model_path:
        config.model_path = args.model_path
    else:
        # Default relative model path search
        script_dir = os.path.dirname(os.path.abspath(__file__))
        repo_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
        candidates = [
            os.path.join(repo_root, "models", "face_detection_yunet_2023mar.onnx"),
            os.path.join(os.getcwd(), "models", "face_detection_yunet_2023mar.onnx"),
            os.path.join(script_dir, "models", "face_detection_yunet_2023mar.onnx")
        ]
        config.model_path = next((p for p in candidates if os.path.exists(p)), candidates[0])

    print("========================================================")
    print("  🛡️ BLINDSIDE (Python Reference Harness)")
    print("========================================================")
    print(f"Mode: {config.daemon_state} | Active FPS: {config.active_fps} | Idle FPS: {config.idle_fps} | Hysteresis: {config.hysteresis_sec}s")

    daemon = Daemon(config, synthetic_mode=args.synthetic)
    if not daemon.initialize():
        sys.exit(1)

    if args.calibrate:
        print("[Blindside CLI] Calibrating primary user face position...")
        if daemon.calibrate_primary_user():
            print("[Blindside CLI] Primary user calibration successful!")
        else:
            print("[Blindside CLI] Primary user calibration failed! (No face detected?)")

    daemon.start(run_forever=args.daemon)

if __name__ == "__main__":
    main()
