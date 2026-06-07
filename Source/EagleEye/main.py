import ultralytics

model = ultralytics.YOLO("yolo26s.pt")

model.export(
    format="onnx",
    opset=17,
    imgsz=640,
    dynamic=False,
    simplify=True,
    nms=False,
)
