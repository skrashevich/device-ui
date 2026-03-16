FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY server.py .
COPY config.yaml .

EXPOSE 8080

VOLUME ["/app/sessions"]

ENV PYTHONUNBUFFERED=1

CMD ["python", "server.py"]
