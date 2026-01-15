#!/bin/bash
set -e

# Configuration
OLLAMA_BASE="http://host.docker.internal:11434"
TARGET_MODEL=${KOPS_MODEL:-"llama3.1:8b"}

# 0. If API Key is present, SKIP LOCAL CHECKS
if [ ! -z "$KOPS_OPENAI_KEY" ] || [ ! -z "$KOPS_GEMINI_KEY" ]; then
    echo "🔑 Cloud API Key detected. Skipping local model check."
    echo "🚀 Starting kOps-AI (Cloud Mode)..."
    exec "$@"
    exit 0
fi

echo "🔍 Checking AI Environment..."

# 1. Check if we can reach Ollama
if ! curl -s --max-time 2 "${OLLAMA_BASE}/api/tags" > /dev/null; then
    echo "⚠️  Warning: Cannot reach Ollama at ${OLLAMA_BASE}"
    echo "    (Ensure OLLAMA_HOST=0.0.0.0)"
else
    # 2. Check if the Model exists
    if curl -s "${OLLAMA_BASE}/api/tags" | grep -q "\"${TARGET_MODEL}\""; then
        echo "✅ Model '${TARGET_MODEL}' found."
    else
        echo "📦 Model '${TARGET_MODEL}' missing. Downloading now..."
        echo "⏳ This usually takes 2-5 minutes (4.7 GB). Please wait..."
        
        # Trigger the pull and capture the HTTP code
        HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST "${OLLAMA_BASE}/api/pull" \
             -d "{\"name\": \"${TARGET_MODEL}\", \"stream\": false}")
             
        if [ "$HTTP_CODE" == "200" ]; then
            echo -e "\n✅ Download complete."
        else
            echo -e "\n❌ Download FAILED (HTTP $HTTP_CODE)."
            echo "   Please run 'ollama pull ${TARGET_MODEL}' manually on your host."
            echo "   Or select Option 2 (OpenAI) in run.sh."
            exit 1
        fi
    fi
fi

echo "🚀 Starting kOps-AI..."
exec "$@"