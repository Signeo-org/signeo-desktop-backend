import torch
import sys
from pkg_resources import parse_version

print("="*50)
print("CUDA and cuDNN Information")
print("="*50)

# Check PyTorch and CUDA
print("\nPyTorch Version:", torch.__version__)
print("Python Version:", sys.version)
print(f"CUDA available: {torch.cuda.is_available()}")

if torch.cuda.is_available():
    print(f"\nCUDA Device Count: {torch.cuda.device_count()}")
    for i in range(torch.cuda.device_count()):
        print(f"\nDevice {i}: {torch.cuda.get_device_name(i)}")
        print(f"  - Compute Capability: {torch.cuda.get_device_capability(i)[0]}.{torch.cuda.get_device_capability(i)[1]}")
        print(f"  - Total Memory: {torch.cuda.get_device_properties(i).total_memory / 1e9:.2f} GB")
        print(f"  - CUDA Version: {torch.version.cuda}")
    
    # Check cuDNN
    if hasattr(torch.backends, 'cudnn'):
        cudnn_version = torch.backends.cudnn.version()
        print(f"\ncuDNN Version: {cudnn_version}")
        print(f"cuDNN enabled: {torch.backends.cudnn.enabled}")
        print(f"cuDNN benchmark: {torch.backends.cudnn.benchmark}")
    else:
        print("\ncuDNN not properly installed or detected with PyTorch")
else:
    print("\nCUDA is not available. Please check your installation.")
    print("Make sure you have compatible GPU drivers and CUDA toolkit installed.")

# Additional system info
print("\n" + "="*50)
print("System Information")
print("="*50)
import platform
print(f"OS: {platform.system()} {platform.release()}")
print(f"Processor: {platform.processor()}")
print(f"Machine: {platform.machine()}")
