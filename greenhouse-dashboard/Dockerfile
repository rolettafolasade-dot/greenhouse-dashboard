FROM python:3.11-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

# Hugging Face Spaces requires port 7860
EXPOSE 7860

CMD ["panel", "serve", "dashboard.py", \
     "--address", "0.0.0.0", \
     "--port", "7860", \
     "--allow-websocket-origin", "*", \
     "--num-procs", "1"]