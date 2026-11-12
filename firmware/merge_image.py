"""Build the simulator's bootable image from PlatformIO's upload segments."""
from pathlib import Path

Import("env")

segments = list(env["FLASH_EXTRA_IMAGES"])
segments.append(("$ESP32_APP_OFFSET", "$BUILD_DIR/${PROGNAME}.bin"))
inputs = [env.subst(str(image)) for _, image in segments]
esptool = Path(env.PioPlatform().get_package_dir("tool-esptoolpy")) / "esptool.py"

# Each input is an SCons dependency. The offsets are part of the action
# signature, so a layout change also invalidates the previous merged image.
command = f'"$PYTHONEXE" "{esptool}" --chip esp32 merge_bin -o "$TARGET"'
for index, (offset, _) in enumerate(segments):
    command += f' {env.subst(str(offset))} "${{SOURCES[{index}]}}"'

image = env.Command(
    "$BUILD_DIR/firmware.merged.bin",
    inputs,
    env.VerboseAction(command, "Merging bootloader, partitions and application for Wokwi"),
)
env.Default(image)
