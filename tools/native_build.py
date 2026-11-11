# PlatformIO compiles the engine as C++ and the Unity test framework as C.
Import("env")
env.Append(CXXFLAGS=["-fno-exceptions", "-fno-rtti"])
