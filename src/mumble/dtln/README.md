DTLN model files used by the optional DTLN speech-cleanup backend.

Source:
- https://github.com/breizhn/DTLN

Baseline files:
- `model_1.onnx`
- `model_2.onnx`

Packaged variants:
- `baseline`
- `norm_500h`
- `norm_40h`

Each packaged variant contains the same two-file layout:
- `model_1.onnx`
- `model_2.onnx`

These models are copied next to the client at build/install time when `dtln=ON`:
- `dtln/baseline/`
- `dtln/norm_500h/`
- `dtln/norm_40h/`

The runtime selects the variant directory based on the configured speech-cleanup model id.
