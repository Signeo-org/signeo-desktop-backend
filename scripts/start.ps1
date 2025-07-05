#-------------------------------------------
# 1) Build the project and download the model
#-------------------------------------------
./scripts/build.ps1

#-------------------------------------------
# 2) Run the executable with the model
#-------------------------------------------
Set-Location -Path "./build/Release"
./AudioTranscriptionTool.exe --model "models/ggml-base.bin"
Set-Location -Path "../.."
