# Navigate to the build directory
Set-Location -Path "./build"

# Generate the build files
cmake ..

# Compile the project
cmake --build .

# Back to the root directory
Set-Location -Path ".."

# Run the resulting executable
./build/Release/MyAudioProject.exe
 