import ultralytics

model = ultralytics.YOLO("yolo26x.pt")

model.export(format="onnx")