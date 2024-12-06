from flask import Flask, request, jsonify
from ultralytics import YOLO
from PIL import Image
import urllib.request
import uuid

app = Flask(__name__)

# Load my YOLO model
model = YOLO('model_inputs/yolo11x-cls.pt')  # You can change this to any YOLOv11 model variant

# Function to load and preprocess the image
def load_local_image(image_path):
    image = Image.open(image_path)
    return image

# Function to load and preprocess the image
def load_remote_image(image_url):
    filename = str(uuid.uuid4()) + ".jpg"
    urllib.request.urlretrieve(image_url, "captures/" + filename)
    return load_local_image("captures/" + filename)



@app.route('/infer', methods=['POST'])
def infer():
    data = request.get_json()
    ip_address = data.get('ip_address')

    if not ip_address:
        return jsonify({'error': 'ip_address parameter is required'}), 400

    image = load_remote_image("http://"+ip_address+"/capture")

    results = model.predict(image)

    for result in results:
        probs = result.probs  # Access the probabilities of each class
        top_class = probs.top1  # Get the index of the top predicted class
        top_label = model.names[top_class]  # Get the label of the top predicted class
        confidence = probs.top1conf  # Get the confidence score of the top predicted class

        print(f"Predicted Label: {top_label}, Confidence: {confidence:.2f}") 

    return jsonify({'top_label': top_label, 'confidence': float(confidence)})

if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=8081) # Deploy config, consider preloading model here



