ONNX Runtime C API headers, unmodified, from Microsoft's ONNX Runtime 1.24 (https://github.com/microsoft/onnxruntime), MIT (LICENSE.txt).

Used by vr/video3d ([cinema] video_3d). Only the headers are here: the plugin loads onnxruntime.dll and DirectML.dll at run time
from d2rloader\d2r_vr_libs (the installer carries them, with the depth network); nothing links against them, so a missing or older
runtime leaves the videos flat and never stops the plugin loading.
