
#-------------------------------------------
# 1) Build the project and download the model
#-------------------------------------------
./scripts/build.ps1

#-------------------------------------------
# 2) Run the executable with the model
#-------------------------------------------
Set-Location -Path "./build/Release"
./AudioTranscriptionTool.exe "models/ggml-base.bin" "models/silero_vad.onnx" 
Set-Location -Path "../.."
