# 🦈 OS-Lens

> **The AI SRE for your terminal.**
>
> 🔍 **eBPF** to catch the crash. 🧠 **Local LLM** to explain the fix.

![Demo](assets/demo.gif)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Docker Image Size](https://img.shields.io/badge/docker%20image-200MB-blue)]()
[![Twitter Follow](https://img.shields.io/twitter/follow/maiku344?style=social)](https://twitter.com/maiku344)

---

## ⚡ The Problem

**It's 2 AM. Production is down. Your database won't connect.**

Standard tools (`strace`, `tcpdump`) give you cryptic error codes:

connect(3, {sa_family=AF_INET, sin_port=htons(5432), ...}) = -1 ECONNREFUSED


**Great. But WHY?** Is it a firewall? A wrong port? A crashed process?
You spend 2 hours googling errno 111.

## ✨ The Solution

**OS-Lens** hooks into the Linux Kernel using **eBPF** to detect TCP failures in real-time. It then pipes the context (PID, Comm, Error Code) into a **Local LLM** (Llama 3 / Gemini) to give you an instant Root Cause Analysis.

**All running locally. No data leaves your machine.**

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ 🚨 TCP CONNECTION FAILURE DETECTED ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ Process: nc (PID: 12311) Target: 127.0.0.1:9999 Error: ECONNREFUSED (-111) Latency: 0.02ms ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

🦈 AI Analysis (Llama 3.1):

The connection was refused by the server. Root Cause: No process is listening on port 9999.

Suggested Fix:

    Check if the server is running: sudo ss -tulpn | grep 9999

    If it is a Docker container, check port mapping: docker ps ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━


---

## 🚀 Quick Start (One Command)

Requires **Docker** and a Linux Kernel (5.8+).

```bash
# Clone and Run (Magic Script)
git clone [https://github.com/Maku38/os-lens](https://github.com/Maku38/os-lens)
cd os-lens
./run.sh
```

That's it. The script will:

    Check for Docker.

    Build the eBPF container (using CO-RE for portability).

    Ask you to choose an AI Backend (Local Llama, Gemini, or OpenAI).

🛠️ Architecture

OS-Lens bridges the gap between Kernel Space and Generative AI.
Code snippet

graph TD
    A[Linux Kernel] -->|eBPF Tracepoint| B(Ring Buffer)
    B -->|Raw Events| C{OS-Lens User Agent}
    C -->|Filter Noise| D[Event Queue]
    D -->|Context + Error| E[AI Worker Thread]
    E -->|JSON Prompt| F[Llama 3 / Gemini]
    F -->|Analysis| G[Terminal Output]

    Kernel: Hooks inet_sock_set_state to track TCP state changes.

    User Space: Multi-threaded C application.

        Thread 1: Polls Kernel Ring Buffer.

        Thread 2: Manages AI API calls (libcurl).

    AI: Supports Ollama (Local), Google Gemini (Cloud), OpenAI (Cloud).

🧪 Supported Errors
Error Code	Description	AI Detection
ECONNREFUSED (-111)	Target port closed / Firewall block	✅ Yes
ETIMEDOUT (-110)	Packet dropped / Wrong IP	✅ Yes
EHOSTUNREACH (-113)	Routing issue / VPN down	✅ Yes
ENETUNREACH (-101)	Network interface down	✅ Yes
🔮 Roadmap

    [x] v0.1: TCP Connection Failures (MVP)

    [ ] v0.2: Kubernetes Pod Support (DaemonSet)

    [ ] v0.3: DNS Resolution Tracing (getaddrinfo)

    [ ] v0.4: Slack/PagerDuty Webhooks

👤 Author

Mayank Joshi (@Maku38)

    Undergrad at BITS Pilani, Goa.

    Building the future of AI Observability.

Twitter • LinkedIn

License: MIT. Go wild.