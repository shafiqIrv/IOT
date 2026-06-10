FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1

WORKDIR /app

COPY ["cloud/sleep_detection_service/requirements.txt", "/app/requirements.txt"]
RUN pip install --no-cache-dir -r /app/requirements.txt

COPY ["cloud/sleep_detection_service/sleep_service", "/app/sleep_service"]
COPY ["edge ai/prediction/sleep detection/sleep_detection_binary_model.joblib", "/models/sleep_detection_binary_model.joblib"]
COPY ["edge ai/prediction/sleep detection/sleep_detection_feature_columns.joblib", "/models/sleep_detection_feature_columns.joblib"]
COPY ["edge ai/prediction/sleep detection/sleep_detection_model_metadata.json", "/models/sleep_detection_model_metadata.json"]

CMD ["python", "-m", "sleep_service.main"]
