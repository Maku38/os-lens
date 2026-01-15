#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <bpf/libbpf.h>
#include "main.skel.h"

// ---------------------------------------------------
// 1. CONFIGURATION (ENV VARS)
// ---------------------------------------------------

char *CONF_OPENAI_KEY = NULL;
char *CONF_GEMINI_KEY = NULL;
char *CONF_MODEL = "llama3.1:8b";
char CONF_FULL_URL[512]; 

void load_config() {
    CONF_OPENAI_KEY = getenv("KOPS_OPENAI_KEY");
    CONF_GEMINI_KEY = getenv("KOPS_GEMINI_KEY");
    
    char *env_model = getenv("KOPS_MODEL");
    char *env_url = getenv("KOPS_URL");

    // ---------------------------------------------------------
    // 🛑 THE FIX: Respect the Environment Variable First!
    // ---------------------------------------------------------
    if (env_model) CONF_MODEL = env_model;

    // 1. Gemini Mode
    if (CONF_GEMINI_KEY) {
        // If the model is still the Docker default (Llama), switch to Gemini default
        if (strcmp(CONF_MODEL, "llama3.1:8b") == 0) {
            CONF_MODEL = "gemini-1.5-flash";
        }
        
        snprintf(CONF_FULL_URL, sizeof(CONF_FULL_URL), 
            "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s", 
            CONF_MODEL, CONF_GEMINI_KEY);
    } 
    // 2. OpenAI Mode
    else if (CONF_OPENAI_KEY) {
        if (strcmp(CONF_MODEL, "llama3.1:8b") == 0) {
            CONF_MODEL = "gpt-4o-mini";
        }
        snprintf(CONF_FULL_URL, sizeof(CONF_FULL_URL), "https://api.openai.com/v1/chat/completions");
    }
    // 3. Ollama (Default)
    else {
        if (env_url) {
            strncpy(CONF_FULL_URL, env_url, sizeof(CONF_FULL_URL));
        } else {
            snprintf(CONF_FULL_URL, sizeof(CONF_FULL_URL), "http://host.docker.internal:11434/api/generate");
        }
    }
}

// ---------------------------------------------------
// 2. GLOBALS & SIGNALS
// ---------------------------------------------------

volatile sig_atomic_t app_state = 0;

void sig_handler(int sig) {
    if (sig == SIGINT) {
        if (app_state == 0) {
            app_state = 1; 
            printf("\n[⚠️  Ctrl+C Detected] Skipping current analysis...\n");
        } else {
            app_state = 2; 
            printf("\n[🛑 Shutting Down] Cleaning up...\n");
        }
    }
}

// ---------------------------------------------------
// 3. DATA STRUCTURES
// ---------------------------------------------------

struct tcp_event_t {
    unsigned int pid;
    int ret;
    unsigned int daddr_v4;
    unsigned char daddr_v6[16];
    unsigned short family;
    unsigned short dport;
    char comm[16];
    unsigned long long duration_ns;
};

typedef struct ai_job {
    struct ai_job *next;
    struct tcp_event_t event;
    time_t timestamp;
} ai_job_t;

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  queue_cond  = PTHREAD_COND_INITIALIZER;
ai_job_t *queue_head = NULL;
ai_job_t *queue_tail = NULL;

// ---------------------------------------------------
// 4. LIBCURL HELPERS
// ---------------------------------------------------

struct memory_struct {
    char *memory;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct memory_struct *mem = (struct memory_struct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) return 0;
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

// ---------------------------------------------------
// 5. AI WORKER
// ---------------------------------------------------

const char* get_error_name(int err) {
    int abs_err = abs(err);
    switch (abs_err) {
        case 110: return "ETIMEDOUT";
        case 111: return "ECONNREFUSED";
        case 113: return "EHOSTUNREACH";
        case 101: return "ENETUNREACH";
        case 104: return "ECONNRESET";
        case 32:  return "EPIPE";
        case 125: return "ECANCELED";
        default:  return "UNKNOWN";
    }
}

void perform_ai_analysis(ai_job_t *job) {
    if (app_state >= 2) return;

    char ip_str[INET6_ADDRSTRLEN];
    char start_str[32], end_str[32];
    
    if (job->event.family == 10) {
        inet_ntop(AF_INET6, job->event.daddr_v6, ip_str, sizeof(ip_str));
    } else {
        inet_ntop(AF_INET, &job->event.daddr_v4, ip_str, sizeof(ip_str));
    }
    int port = ntohs(job->event.dport);
    double duration_sec = job->event.duration_ns / 1000000000.0;
    
    time_t start_time = job->timestamp - (time_t)duration_sec;
    struct tm *tm_info = localtime(&start_time);
    strftime(start_str, sizeof(start_str), "%H:%M:%S", tm_info);
    
    tm_info = localtime(&job->timestamp);
    strftime(end_str, sizeof(end_str), "%H:%M:%S", tm_info);
    
    const char *err_name = get_error_name(job->event.ret);

    printf("[❌ TCP FAIL] %s | %s -> %s | Target: %s:%d | Error: %d (%s) | Latency: %.2fs\n", 
           job->event.comm, start_str, end_str, ip_str, port, job->event.ret, err_name, duration_sec);
    
    const char *provider = "Ollama";
    if (CONF_GEMINI_KEY) provider = "Gemini";
    else if (CONF_OPENAI_KEY) provider = "OpenAI";
    
    printf("[🤖 Sending to %s (%s)...]\n", provider, CONF_MODEL);

    CURL *curl;
    CURLcode res;
    struct memory_struct chunk = {0};
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl = curl_easy_init();
    if(curl) {
        struct json_object *jobj = json_object_new_object();
        
        char prompt[2048];
        snprintf(prompt, sizeof(prompt), 
            "Analyze Network Failure:\nProcess: %s (PID %d)\nTarget: %s:%d\nError: %s (%d)\nSuggest root cause and fix.",
            job->event.comm, job->event.pid, ip_str, port, err_name, job->event.ret);

        // --- GEMINI FORMAT ---
        if (CONF_GEMINI_KEY) {
            struct json_object *contents_arr = json_object_new_array();
            struct json_object *content_obj = json_object_new_object();
            struct json_object *parts_arr = json_object_new_array();
            struct json_object *part_obj = json_object_new_object();
            
            json_object_object_add(part_obj, "text", json_object_new_string(prompt));
            json_object_array_add(parts_arr, part_obj);
            json_object_object_add(content_obj, "parts", parts_arr);
            json_object_array_add(contents_arr, content_obj);
            json_object_object_add(jobj, "contents", contents_arr);
        }
        // --- OPENAI FORMAT ---
        else if (CONF_OPENAI_KEY) {
            json_object_object_add(jobj, "model", json_object_new_string(CONF_MODEL));
            struct json_object *jmessages = json_object_new_array();
            struct json_object *jmsg = json_object_new_object();
            json_object_object_add(jmsg, "role", json_object_new_string("user"));
            json_object_object_add(jmsg, "content", json_object_new_string(prompt));
            json_object_array_add(jmessages, jmsg);
            json_object_object_add(jobj, "messages", jmessages);
        } 
        // --- OLLAMA FORMAT ---
        else {
            json_object_object_add(jobj, "model", json_object_new_string(CONF_MODEL));
            json_object_object_add(jobj, "prompt", json_object_new_string(prompt));
            json_object_object_add(jobj, "stream", json_object_new_boolean(0));
        }

        const char *json_str = json_object_to_json_string(jobj);
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        if (CONF_OPENAI_KEY) {
            char auth_header[256];
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", CONF_OPENAI_KEY);
            headers = curl_slist_append(headers, auth_header);
        }

        curl_easy_setopt(curl, CURLOPT_URL, CONF_FULL_URL);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        res = curl_easy_perform(curl);

        if(res != CURLE_OK) {
            fprintf(stderr, "curl error: %s\n", curl_easy_strerror(res));
            fprintf(stderr, "DEBUG: Failed URL: %s\n", CONF_FULL_URL);
        } else {
            struct json_object *parsed_json = json_tokener_parse(chunk.memory);
            struct json_object *response_obj;
            struct json_object *arr_obj;
            
            // 1. Parse Gemini (candidates[0].content.parts[0].text)
            if (CONF_GEMINI_KEY && json_object_object_get_ex(parsed_json, "candidates", &arr_obj)) {
                 struct json_object *first_cand = json_object_array_get_idx(arr_obj, 0);
                 struct json_object *content_obj;
                 if (json_object_object_get_ex(first_cand, "content", &content_obj)) {
                    struct json_object *parts_arr;
                    if (json_object_object_get_ex(content_obj, "parts", &parts_arr)) {
                        struct json_object *part = json_object_array_get_idx(parts_arr, 0);
                        struct json_object *text_obj;
                        if (json_object_object_get_ex(part, "text", &text_obj)) {
                            printf("\n%s\n", json_object_get_string(text_obj));
                        }
                    }
                 }
            }
            // 2. Parse OpenAI (choices[0].message.content)
            else if (CONF_OPENAI_KEY && json_object_object_get_ex(parsed_json, "choices", &arr_obj)) {
                 struct json_object *first_choice = json_object_array_get_idx(arr_obj, 0);
                 struct json_object *message_obj;
                 if (json_object_object_get_ex(first_choice, "message", &message_obj)) {
                     struct json_object *content_obj;
                     if (json_object_object_get_ex(message_obj, "content", &content_obj)) {
                         printf("\n%s\n", json_object_get_string(content_obj));
                     }
                 }
            } 
            // 3. Parse Ollama (response)
            else if (json_object_object_get_ex(parsed_json, "response", &response_obj)) {
                printf("\n%s\n", json_object_get_string(response_obj));
            } else {
                 printf("\n[⚠️ Raw Response] %s\n", chunk.memory);
            }
            if (parsed_json) json_object_put(parsed_json);
        }

        json_object_put(jobj);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        free(chunk.memory);
    }
    printf("---------------------------------------------------\n");
}

void* ai_worker_thread(void *arg) {
    while (app_state < 2) {
        ai_job_t *job = NULL;
        pthread_mutex_lock(&queue_mutex);
        while (queue_head == NULL && app_state < 2) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            pthread_cond_timedwait(&queue_cond, &queue_mutex, &ts);
        }
        if (app_state >= 2) {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        job = queue_head;
        if (job) {
            queue_head = job->next;
            if (queue_head == NULL) queue_tail = NULL;
        }
        pthread_mutex_unlock(&queue_mutex);

        if (job) {
            perform_ai_analysis(job);
            free(job);
        }
    }
    return NULL;
}

static int handle_event(void *ctx, void *data, size_t data_sz) {
    struct tcp_event_t *e = data;
    if (e->pid == getpid()) return 0;
    if (strcmp(e->comm, "ollama") == 0) return 0;
    
    double duration_sec = e->duration_ns / 1000000000.0;
    if (e->ret == -125 && duration_sec < 1.0) return 0; 

    ai_job_t *job = malloc(sizeof(ai_job_t));
    if (!job) return 0;

    memcpy(&job->event, e, sizeof(struct tcp_event_t));
    job->timestamp = time(NULL);
    job->next = NULL;

    pthread_mutex_lock(&queue_mutex);
    if (queue_tail) {
        queue_tail->next = job;
        queue_tail = job;
    } else {
        queue_head = queue_tail = job;
    }
    pthread_cond_signal(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
    return 0;
}

int main() {
    load_config(); 

    struct main_bpf *skel;
    struct ring_buffer *rb = NULL;
    pthread_t worker_tid;

    signal(SIGINT, sig_handler);

    if (pthread_create(&worker_tid, NULL, ai_worker_thread, NULL) != 0) return 1;

    skel = main_bpf__open_and_load();
    if (!skel) return 1;

    if (main_bpf__attach(skel)) return 1;

    char *mode = "Local Ollama";
    if (CONF_GEMINI_KEY) mode = "Google Gemini";
    else if (CONF_OPENAI_KEY) mode = "OpenAI Cloud";

    printf("🦈 TCP Sniffer Active! [Target AI: %s]\n", mode);
    
    rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), handle_event, NULL, NULL);
    if (!rb) return 1;

    while (app_state < 2) {
        ring_buffer__poll(rb, 100);
    }

    ring_buffer__free(rb);
    main_bpf__destroy(skel);
    return 0;
}