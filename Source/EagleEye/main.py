import ultralytics

model = ultralytics.YOLO("yolo26x.pt")

model.export(
    format="onnx",
    opset=17,
    imgsz=640,
    dynamic=False,
    simplify=True,
    nms=False,
)
