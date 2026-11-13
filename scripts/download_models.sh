#!/bin/bash
set -e
mkdir -p models
echo "Downloading YuNet Face Detection model..."
wget -q -O models/face_detection_yunet_2023mar.onnx https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx
echo "Model downloaded successfully."
