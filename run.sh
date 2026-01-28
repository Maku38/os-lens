#!/bin/bash
echo "🦈 Launching OS-Lens..."

# 1. Check Docker
if ! docker info > /dev/null 2>&1; then
    echo "❌ Error: Docker is not running."
    exit 1
fi

# 2. Check for Ollama (Only needed if running Local Mode later)
HAS_OLLAMA=0
if command -v ollama >/dev/null 2>&1; then
    HAS_OLLAMA=1
fi

# 3. Auto-Build (The Magic Fix)
if [[ "$(docker images -q kops-ai 2> /dev/null)" == "" ]]; then
    echo "🏗️  First time setup: Building Docker Image..."
    echo "   (This takes 1-2 minutes)"
    docker build -t kops-ai .
fi

echo "----------------------------------------"
echo "Choose AI Mode:"
echo "1) Local LLM (Free, Private, requires Ollama)"
echo "2) OpenAI API (Fast, requires Key)"
echo "3) Google Gemini (Fast, Free Tier available)"
echo "----------------------------------------"
read -p "Select [1/2/3]: " mode

if [ "$mode" == "2" ]; then
    read -p "Enter OpenAI Key (sk-...): " apikey
    docker run -it --rm --privileged \
      -v /sys/kernel/debug:/sys/kernel/debug \
      --pid=host \
      --net=host \
      -e KOPS_OPENAI_KEY="$apikey" \
      -e KOPS_MODEL="gpt-5.2" \
      kops-ai

elif [ "$mode" == "3" ]; then
    read -p "Enter Gemini API Key (AIza...): " apikey
    docker run -it --rm --privileged \
      -v /sys/kernel/debug:/sys/kernel/debug \
      --pid=host \
      --net=host \
      -e KOPS_GEMINI_KEY="$apikey" \
      -e KOPS_MODEL="gemini-2.5-flash" \
      kops-ai

else
    # --- LOCAL MODE LOGIC ---
    
    # A. Check if Ollama is actually reachable
    if ! curl -s http://127.0.0.1:11434/api/tags >/dev/null; then
        echo "⚠️  Cannot reach Ollama on 127.0.0.1:11434"
        echo "   Please run: 'ollama serve' in another terminal."
        exit 1
    fi

    # B. Auto-Download Model if missing
    # We check if the user has 'llama3.1:8b'. If not, we try to pull it.
    if [ $HAS_OLLAMA -eq 1 ]; then
        if ! ollama list | grep -q "llama3.1:8b"; then
            echo "📦 Model 'llama3.1:8b' missing. Downloading now..."
            echo "   (This is a 4.7GB download. Please wait.)"
            ollama pull llama3.1:8b
        fi
    fi

    echo "🚀 Starting in Local Mode..."
    
    # C. THE NETWORKING FIX
    # We pass KOPS_URL="http://127.0.0.1..." explicitly. 
    # Because we use --net=host, 127.0.0.1 refers to the HOST's localhost, where Ollama lives.
    docker run -it --rm --privileged \
      -v /sys/kernel/debug:/sys/kernel/debug \
      --pid=host \
      --net=host \
      -e KOPS_URL="http://127.0.0.1:11434/api/generate" \
      kops-ai
fi