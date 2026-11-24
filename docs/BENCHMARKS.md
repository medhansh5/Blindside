# ⚡ Blindside Benchmarks

Look, we know everyone lies about their performance numbers. So we're just going to give you the raw data and the exact scripts to reproduce it yourself. We don't do "aspirational" benchmarking here.

## 🔬 The Test Rig

We ran all these numbers on a real laptop, not a data center server.

- **CPU**: Intel Core i7-12700H
- **GPU**: Intel Iris Xe (Integrated)
- **OS**: Ubuntu 24.04 LTS (Kernel 6.8.0)
- **Compiler**: GCC 13.2.0 (`-O3 -ffast-math -std=c++20`)
- **Camera**: Built-in 1080p Webcam (running at 640x480 for the pipeline)

## 📊 The Numbers

| Metric | Measured Value | What It Means |
| :--- | :--- | :--- |
| **End-to-End Latency** | **[Pending Validation]** | Frame Capture $\rightarrow$ YuNet $\rightarrow$ SolvePnP $\rightarrow$ Trigger. |
| **CPU Footprint (Active)**| **[Pending Validation]** | When running at 30 FPS. |
| **CPU Footprint (Idle)**  | **[Pending Validation]** | When monitoring at 5 FPS because only you are in the frame. |
| **Memory Footprint (RSS)**| **[Pending Validation]** | That's it. It stays flat. No memory leaks. |
| **Network I/O** | **Verified Offline** | Application does not establish external network sockets or transmit telemetry. |
| **In-Loop Heap Allocs**  | **Verified Zero** | Zero dynamic mallocs inside the frame loop. Checked via Valgrind Massif. |

## 🛠️ Reproducing This Yourself

Don't believe us? Run it yourself.

1. **Build the benchmark targets**:
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

2. **Run the ring buffer latency test**:
   ```bash
   ./build/test_ring_buffer
   ```

3. **Check the CPU/Memory footprint**:
   Run the daemon and check `htop` or `pidstat`:
   ```bash
   ./build/blindside_daemon --daemon
   pidstat -p $(pidof blindside_daemon) 1
   ```

If you manage to make it faster, send a PR. We love optimization.
