#!/bin/bash
echo "🦈 Launching kOps-AI..."

# 1. Check Docker
if ! docker info > /dev/null 2>&1; then
    echo "❌ Error: Docker is not running."
    exit 1
fi

# 2. Auto-Build (The Magic Fix)
if [[ "$(docker images -q kops-ai 2> /dev/null)" == "" ]]; then
    echo "🏗️  First time setup: Building Docker Image..."
    echo "   (This takes 1-2 minutes)"
    docker build -t kops-ai .
fi

echo "----------------------------------------"
echo "Choose AI Mode:"
echo "1) Local LLM (Free, Private, requires 4GB download)"
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
      -e KOPS_MODEL="gpt-4o-mini" \
      kops-ai

elif [ "$mode" == "3" ]; then
    read -p "Enter Gemini API Key (AIza...): " apikey
    docker run -it --rm --privileged \
      -v /sys/kernel/debug:/sys/kernel/debug \
      --pid=host \
      --net=host \
      -e KOPS_GEMINI_KEY="$apikey" \
      -e KOPS_MODEL="gemini-1.5-flash-latest" \
      kops-ai

else
    # Local Mode
    docker run -it --rm --privileged \
      -v /sys/kernel/debug:/sys/kernel/debug \
      --pid=host \
      --net=host \
      --add-host=host.docker.internal:host-gateway \
      kops-ai
fi
