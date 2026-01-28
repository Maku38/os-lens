# 🦈 OS-Lens

<div align="center">

### The AI SRE for your terminal

**🔍 eBPF** to catch the crash. **🧠 Local LLM** to explain the fix.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Docker](https://img.shields.io/badge/docker-ready-blue.svg)](https://hub.docker.com)
[![Linux](https://img.shields.io/badge/platform-linux-lightgrey.svg)](https://www.kernel.org)
[![Twitter Follow](https://img.shields.io/twitter/follow/maiku344?style=social)](https://twitter.com/maiku344)

[Quick Start](#-quick-start) • [Features](#-features) • [Demo](#-demo) • [Architecture](#%EF%B8%8F-architecture) • [Roadmap](#-roadmap)

</div>

---

## 🎯 The Problem

**It's 2 AM. Production is down. Your database won't connect.**

You open your terminal and run `strace`, `tcpdump`, or `ss` to debug. You get:

```bash
connect(3, {sa_family=AF_INET, sin_port=htons(5432), ...}) = -1 ECONNREFUSED
```

**Great. But WHY?** 

- Is it a firewall blocking the port?
- Did the service crash?
- Wrong configuration?
- Network issue?

You spend the next 2 hours searching through Stack Overflow, man pages, and documentation just to understand what `errno 111` means.

## ✨ The Solution

**OS-Lens** is your AI-powered debugging companion that:

1. **Hooks directly into the Linux kernel** using eBPF to detect network failures in real-time
2. **Captures complete context** - Process name, PID, target address, error codes, timing
3. **Analyzes with AI** - Uses local LLMs (Llama 3) or cloud models (Gemini, OpenAI) to provide instant root cause analysis
4. **Suggests fixes** - Actionable commands to resolve the issue

**All running locally. Your data never leaves your machine (unless you opt for cloud AI).**

### Example Output

```bash
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
🚨 TCP CONNECTION FAILURE DETECTED
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Process:  nc (PID: 12311)
Target:   127.0.0.1:9999
Time: 16:39:37 -> 16:39:39
Error:    ECONNREFUSED (-111)
Latency:  2.00ms


━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

🦈 AI Analysis (Llama 3.1):

The connection was refused by the server.

Root Cause: 
  No process is listening on port 9999.

Suggested Fixes:
  1. Check if the server is running:
     $ sudo ss -tulpn | grep 9999
  
  2. If it's a Docker container, verify port mapping:
     $ docker ps
  
  3. Check if the service failed to start:
     $ sudo systemctl status your-service

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

---

## 🚀 Quick Start

### Prerequisites

- **Docker** installed and running
- **Linux** with kernel version 5.8+ (for eBPF CO-RE support)
- **root/sudo** privileges (required for eBPF programs)

### One-Command Installation

```bash
git clone https://github.com/Maku38/os-lens
cd os-lens
./run.sh
```

The setup script will:
1. ✅ Verify Docker installation
2. 🔨 Build the eBPF container with CO-RE for kernel portability
3. 🤖 Let you choose your AI backend (Local Llama, Gemini, or OpenAI)
4. 🚀 Start monitoring your system

---

## 🎬 Demo

![OS-Lens Demo](assets/Screencast_from_2026_01_28_23_56_01_V1.mp4)

Watch OS-Lens detect a connection failure and provide instant AI-powered diagnosis.

---

## 🧪 Try it Yourself

Once OS-Lens is running, open a **new terminal window** and run these commands to see the AI in action.

### Scenario 1: The "Service Down" (Connection Refused)
Simulate a database that crashed or isn't running.
```bash
# Try to connect to a random port where nothing is listening
nc -v 127.0.0.1 9999
```
### Scenario 2: The "Firewall Blackhole" (Timeout)

Simulate a packet being dropped by a cloud firewall or wrong IP.
```bash

# Try to connect to a TEST-NET IP (Reserved for documentation, usually drops packets)
# We use -w 4 to force a timeout after 4 seconds
nc -v -w 4 192.0.2.1 80
```

### Scenario 3: The "Application Crash" (Python Script)

Prove that OS-Lens debugs code, not just terminal commands.
```bash

# Run a Python one-liner that fails to connect to a fake Postgres port
python3 -c "import socket; s=socket.socket(); s.connect(('127.0.0.1', 5432))"
```

---

### ✨ Features

### 🔬 Deep Kernel Visibility
- **eBPF-powered tracing** - Zero overhead monitoring of kernel network stack
- **CO-RE (Compile Once, Run Everywhere)** - Works across different kernel versions
- **Real-time detection** - Catches failures as they happen, not after

### 🧠 AI-Powered Analysis
- **Multiple AI backends** supported:
  - 🦙 **Ollama** (Local) - Privacy-first, runs on your machine
  - 🌐 **Google Gemini** - Fast cloud inference
  - 🤖 **OpenAI GPT** - Most advanced reasoning
- **Context-aware explanations** - Understands your specific error in system context
- **Actionable suggestions** - Get commands to run, not just theory

### 🎯 Production-Ready
- **Multi-threaded architecture** - Separate threads for event collection and AI analysis
- **Ring buffer design** - Efficient kernel-to-userspace communication
- **Noise filtering** - Only alerts on actual failures, not normal retries
- **Low latency** - Sub-millisecond event capture

---

## 🛠️ Architecture

OS-Lens bridges the gap between kernel space and generative AI:

```
┌─────────────────┐
│  Linux Kernel   │
│  (TCP/IP Stack) │
└────────┬────────┘
         │ eBPF Tracepoint
         │ (inet_sock_set_state)
         ↓
┌─────────────────┐
│  Ring Buffer    │  ← Zero-copy, lock-free
└────────┬────────┘
         │ Poll (libbpf)
         ↓
┌─────────────────┐
│  OS-Lens Agent  │
│  (User Space)   │
│                 │
│  Thread 1:      │ ← Event Collection
│    Event Loop   │
│                 │
│  Thread 2:      │ ← AI Analysis
│    AI Worker    │
└────────┬────────┘
         │ HTTP/JSON
         ↓
┌─────────────────┐
│   AI Backend    │
│ Llama/Gemini/GPT│
└─────────────────┘
```

### Component Details

| Component | Technology | Purpose |
|-----------|------------|---------|
| **Kernel Hook** | eBPF + BTF | Attach to TCP state changes without kernel modules |
| **Event Transport** | Ring Buffer | High-performance kernel → user space data transfer |
| **Parser** | C (libbpf) | Convert raw kernel events to structured data |
| **AI Integration** | libcurl + JSON | Send context to LLM, parse responses |
| **Output** | ANSI Terminal | Pretty-print with colors and formatting |

---

## 🔍 Supported Failure Types

| Error Code | Description | AI Detection | Example Scenario |
|------------|-------------|--------------|------------------|
| `ECONNREFUSED` (-111) | Target port closed or blocked | ✅ Yes | Service not running, firewall blocking |
| `ETIMEDOUT` (-110) | Connection timed out | ✅ Yes | Wrong IP, packets dropped, slow network |
| `ECONNRESET` (-104) | Connection reset by peer | 🚧 Planned | Service crashed mid-connection |
| `EPIPE` (-32) | Broken pipe | 🚧 Planned | Client disconnected unexpectedly |

---

## 🎯 Use Cases

### For Developers
- **Debug faster** - No more cryptic error codes, get instant explanations
- **Learn as you code** - Understand why connections fail in real-time
- **Test network scenarios** - Validate your app's error handling

### For SREs / DevOps
- **Reduce MTTR** - Mean time to resolution drops from hours to minutes
- **Root cause analysis** - AI explains the "why" behind failures
- **Production monitoring** - Deploy as a sidecar or DaemonSet (coming soon)

### For Security Teams
- **Network intrusion detection** - Spot suspicious connection attempts
- **Audit trail** - Log all network failures with full context
- **Compliance** - Monitor outbound connections from containers

---

## ⚠️ Known Limitations (v0.1)

OS-Lens is currently a **Layer 4 (TCP) Debugger**. It excels at connection failures but has specific blind spots by design:

| Limitation | Details | Status |
| :--- | :--- | :--- |
| **Pre-Handshake Errors** | Errors that happen *before* a TCP socket is created (e.g., DNS `getaddrinfo` failures or immediate `Network Unreachable` routing errors) are currently silent. | 🚧 Coming in v0.2 (Syscall Hooks) |
| **Application Logic** | We track **Infrastructure**, not credentials. If you connect successfully but get a "403 Forbidden" or "MySQL Access Denied," OS-Lens sees a healthy TCP connection and stays silent. | ❌ Out of Scope |
| **Privacy (Cloud Mode)** | If you select **Gemini** or **OpenAI**, error metadata (Process Name, IP, Error Code) is sent to their APIs. For 100% privacy, use the **Local Ollama** mode. | ✅ User Choice |
| **UDP Support** | Currently supports **TCP Only**. UDP (QUIC/DNS) traffic is ignored. | ⏳ Planned |

---

## 🗺️ Roadmap

### ✅ v0.1 - Current (MVP)
- [x] eBPF TCP connection failure detection
- [x] Multi-threaded C agent
- [x] Ollama/Gemini/OpenAI integration
- [x] Docker packaging with CO-RE

### 🚧 v0.2 - Q1 2025
- [ ] **Kubernetes support** - Deploy as DaemonSet
- [ ] **Grafana integration** - Visualize failure patterns
- [ ] **Persistent logging** - SQLite storage for historical analysis
- [ ] **Web UI** - Dashboard for non-terminal users

### 🔮 v0.3 - Q2 2025
- [ ] **DNS resolution tracing** - Hook `getaddrinfo()` calls
- [ ] **HTTP failure detection** - Track 4xx/5xx errors
- [ ] **Slack/PagerDuty webhooks** - Alert your team
- [ ] **Multi-language support** - Python, Go bindings

### 🌟 v0.4 - Beyond
- [ ] **Windows support** - Via eBPF for Windows
- [ ] **WASM plugin system** - Custom analyzers
- [ ] **Distributed tracing** - Correlate failures across services
- [ ] **SaaS platform** - Managed service for teams

---

## 🤝 Contributing

We welcome contributions! Here's how you can help:

1. **Report bugs** - Open an issue with details and logs
2. **Request features** - Tell us what you need
3. **Submit PRs** - Fix bugs, add features, improve docs
4. **Spread the word** - Star the repo, share on Twitter

### Development Setup

```bash
# Clone the repository
git clone https://github.com/Maku38/os-lens
cd os-lens

# Install dependencies (Ubuntu/Debian)
sudo apt-get install -y clang llvm libbpf-dev libelf-dev libcurl4-openssl-dev

# Build locally (without Docker)
make local-build

# Run tests
make test
```

### Project Structure

```
os-lens/
├── main.c              # User-space agent (event loop + AI)
├── main.bpf.c          # eBPF kernel program
├── vmlinux.h           # Kernel type definitions (BTF)
├── Dockerfile          # Container with libbpf + clang
├── Makefile            # Build automation
├── run.sh              # Setup script
└── assets/
    └── demo.gif        # Demo recording
```

---

## 🔒 Security & Privacy

- **No data exfiltration** - When using Ollama, everything stays local
- **No kernel panics** - eBPF is verified by the kernel before execution
- **No production impact** - Read-only tracing, no modifications
- **Open source** - Audit the code yourself

---

## 🆚 Comparison

| Tool | Real-time | AI-Powered | User-Friendly | Kernel Access |
|------|-----------|------------|---------------|---------------|
| **OS-Lens** | ✅ | ✅ | ✅ | ✅ (eBPF) |
| strace | ✅ | ❌ | ❌ | ✅ (ptrace) |
| tcpdump | ✅ | ❌ | ❌ | ✅ (raw sockets) |
| Wireshark | ✅ | ❌ | ⚠️ | ✅ (libpcap) |
| sysdig | ✅ | ❌ | ⚠️ | ✅ (kernel module) |

---

## 📚 Resources

- **eBPF Documentation** - [ebpf.io](https://ebpf.io)
- **libbpf** - [github.com/libbpf/libbpf](https://github.com/libbpf/libbpf)
- **Ollama** - [ollama.ai](https://ollama.ai)
- **BPF CO-RE** - [nakryiko.com](https://nakryiko.com/posts/bpf-portability-and-co-re/)

---

## 🙏 Acknowledgments

Built with:
- **eBPF** - Linux kernel observability
- **libbpf** - User-space eBPF library
- **Ollama** - Local LLM runtime
- **Llama 3** - Meta's open-source LLM
- **Docker** - Containerization

Special thanks to the eBPF and Linux communities for making this possible.

---

## 👤 Author

**Mayank Joshi** ([@Maku38](https://github.com/Maku38))

- 🎓 Undergraduate at BITS Pilani, Goa
- 🚀 Building the future of AI-powered observability
- 🐦 Twitter: [@maiku344](https://twitter.com/maiku344)
- 💼 LinkedIn: [Mayank Joshi](https://linkedin.com/in/mayank-joshi)

---

## 📄 License

MIT License - see [LICENSE](LICENSE) file for details.

**TLDR:** Do whatever you want with this code. Attribution appreciated but not required.

---

## 🌟 Show Your Support

If OS-Lens helped you debug faster, please:
- ⭐ **Star this repo**
- 🐦 **Tweet about it** using #OSLens
- 📧 **Share with your team**
- 🤝 **Contribute** to make it better

---

<div align="center">

**Made with 🦈 and ❤️ for the DevOps community**

[⬆ Back to top](#-os-lens)

</div>