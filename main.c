#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <arpa/inet.h>
#include <bpf/libbpf.h>
#include "main.skel.h"

struct tcp_event_t {
    unsigned int pid;
    int ret;
    unsigned int daddr;
    unsigned short dport;
    char comm[16];
    unsigned long long duration_ns;
};

// 1. New Helper: Convert Error ID to String
const char* get_error_name(int err) {
    int abs_err = abs(err); // Convert -110 to 110
    switch (abs_err) {
        case 110: return "ETIMEDOUT";
        case 111: return "ECONNREFUSED";
        case 113: return "EHOSTUNREACH";
        case 101: return "ENETUNREACH";
        case 104: return "ECONNRESET";
        case 32:  return "EPIPE";
        case 125: return "ECANCELED (Client Timeout)";
        default:  return "UNKNOWN";
    }
}

void get_time_str(time_t raw_time, char *buffer, size_t size) {
    struct tm *tm_info;
    tm_info = localtime(&raw_time);
    strftime(buffer, size, "%H:%M:%S", tm_info);
}

// 2. Update ask_ai to accept the Error Name
void ask_ai(struct tcp_event_t *e, char *ip_str, int port, char *start_str, char *end_str, double duration_sec, const char *err_name) {
    char prompt[2048];
    char command[4096];

    // Added "Error Name" to the prompt so AI understands better
    snprintf(prompt, sizeof(prompt), 
        "Network Failure Analysis:\\n"
        "Time: %s to %s (Duration: %.2fs)\\n"
        "Process: %s (PID %d)\\n"
        "Target: %s:%d\\n"
        "Error: %d (%s)\\n"  // <--- Added Error Name here
        "Question: Explain this %s error and suggest a fix.",
        start_str, end_str, duration_sec,
        e->comm, e->pid, ip_str, port, e->ret, err_name, err_name);

    printf("\n[🤖 Sending to AI...] %s:%d\n", ip_str, port);

    snprintf(command, sizeof(command),
        "curl -s -X POST http://localhost:11434/api/generate -d '{"
        "\"model\": \"llama3.1:8b\"," 
        "\"prompt\": \"%s\","
        "\"stream\": false"
        "}' | jq -r .response", prompt);

    system(command);
    printf("\n---------------------------------------------------\n");
}

static int handle_event(void *ctx, void *data, size_t data_sz) {
    struct tcp_event_t *e = data;
    char ip_str[INET_ADDRSTRLEN];
    char start_str[32], end_str[32];
    
    // 1. Convert Data FIRST (so we can check the port)
    inet_ntop(AF_INET, &e->daddr, ip_str, sizeof(ip_str));
    int port = ntohs(e->dport);

    // 2. 🔇 SMART FILTERING
    // Ignore 'ollama' server itself
    if (strcmp(e->comm, "ollama") == 0) return 0;

    // Ignore 'curl' ONLY if it is talking to the AI (Port 11434)
    // This allows your test `curl` (to port 80) to pass through!
    if (strcmp(e->comm, "curl") == 0 && port == 11434) return 0;

    // Ignore background noise (Chrome, VS Code)
    if (strncmp(e->comm, "Chrome", 6) == 0) return 0;
    if (strcmp(e->comm, "code") == 0) return 0;
    if (strcmp(e->comm, "slack") == 0) return 0;

    // 3. Time Filter (Ignore short-lived cancellations < 1.0s)
    time_t now = time(NULL);
    double duration_sec = e->duration_ns / 1000000000.0;
    
    if (e->ret == -125 && duration_sec < 1.0) {
        return 0; 
    }

    time_t start_time = now - (time_t)duration_sec;
    get_time_str(start_time, start_str, sizeof(start_str));
    get_time_str(now, end_str, sizeof(end_str));

    const char *err_name = get_error_name(e->ret);

    printf("[❌ TCP FAIL] %s | %s -> %s | Target: %s:%d | Error: %d (%s) | Latency: %.2fs\n", 
           e->comm, start_str, end_str, ip_str, port, e->ret, err_name, duration_sec);
    
    ask_ai(e, ip_str, port, start_str, end_str, duration_sec, err_name);
    return 0;
}

int main() {
    struct main_bpf *skel;
    struct ring_buffer *rb = NULL;
    int err;

    skel = main_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    err = main_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF skeleton\n");
        return 1;
    }

    printf("🦈 TCP Sniffer Active! Try: curl -v http://10.255.255.1:80\n");

    rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), handle_event, NULL, NULL);
    if (!rb) return 1;

    while (1) {
        ring_buffer__poll(rb, 100);
    }

    return 0;
}