# 🤝 Contributing to Blindside

We want your PRs, but we also want them to be good. We don't want bloat, we don't want telemetry, and we definitely don't want dynamic allocations inside the frame loop.

If you're down with building fast, zero-latency privacy tools in C++20, let's go.

---

## 🏗️ Getting Started

### Prerequisites
You need the holy trinity:
- **C++20 Compiler**: GCC 11+, Clang 14+, or MSVC 2022+ (Visual Studio 17.0+)
- **Build System**: CMake 3.20+ and Ninja
- **Libraries**: OpenCV 4.10+ (with `core`, `imgproc`, `videoio`, `highgui`, `objdetect`, `calib3d`, `dnn`)

### The Build Loop (Linux)
```bash
git clone https://github.com/medhansh5/Blindside.git
cd Blindside
cmake -S . -B build && cmake --build build
```

Don't forget to run the diagnostics tool to see what your platform actually supports:
```bash
./build/blindside_daemon --diagnostics
```

---

## 🎨 The Unspoken (Now Spoken) Rules

If you break these, your PR gets closed:

1. **No Cloud BS**: Blindside is strictly edge-native. Do not add any networking code.
2. **Zero In-Loop Allocations**: Do not `new` or `malloc` inside `process_frame`. We use fixed-capacity arrays and `RingBuffer`.
3. **Use the Abstractions**: Do not dump Win32 or X11 code inside the core engine. Use `src/platform/`.
4. **Don't Fake the AI**: If you touch `FaceDetector`, use the actual YuNet OpenCV pipeline (`FaceDetectorYN`) with OpenCV 4.10+. Synthetic test fixtures are reserved for deterministic pipeline testing via `CameraCapture::set_synthetic_eavesdropper()`; production detection requires real inference.

### Naming Conventions
- Classes: `PascalCase`
- Methods/Variables: `snake_case`
- Member vars: `name_` (trailing underscore, don't forget it)

---

## 🧪 Tests

If you break the tests, CI will yell at you. Run them locally:

```bash
cd build
ctest --output-on-failure
```

---

## 📝 PR Checklist

Before you hit submit:
- [ ] You compiled it without warnings (`-Wall -Wextra` or `/W4`).
- [ ] You tested it on Wayland, X11, or Windows. (Specify which in your PR).
- [ ] You didn't add a gigabyte of dependencies.

Let's build something actually useful.
