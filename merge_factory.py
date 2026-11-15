
Import("env")

import subprocess
import sys
from pathlib import Path


FLASH_SIZE = "4MB"
FLASH_SIZE_BYTES = 4 * 1024 * 1024


def get_package_dir(platform, package_names):
  """Find an installed PlatformIO package."""

  for package_name in package_names:
    try:
      package_dir = platform.get_package_dir(package_name)

      if package_dir and Path(package_dir).is_dir():
        return Path(package_dir)

    except (KeyError, Exception):
      pass

  raise RuntimeError(
    "PlatformIO package not found: " + ", ".join(package_names)
  )


def find_esptool(platform):
  """Locate an esptool executable or Python entry point."""

  package_dir = get_package_dir(
    platform,
    ["tool-esptool", "tool-esptoolpy"]
  )

  candidates = [
    package_dir / "esptool.exe",
    package_dir / "esptool",
    package_dir / "esptool.py",
    package_dir / "esptool" / "__main__.py"
  ]

  for candidate in candidates:
    if candidate.is_file():
      if candidate.suffix.lower() == ".py" or candidate.name == "__main__.py":
        return [sys.executable, str(candidate)]

      return [str(candidate)]

  raise RuntimeError(
    "No esptool executable found in: " + str(package_dir)
  )


def find_boot_app0(platform):
  """Locate the Arduino ESP32 OTA initialization image."""

  framework_dir = get_package_dir(
    platform,
    ["framework-arduinoespressif32"]
  )

  expected_path = (
    framework_dir
    / "tools"
    / "partitions"
    / "boot_app0.bin"
  )

  if expected_path.is_file():
    return expected_path

  matches = list(framework_dir.rglob("boot_app0.bin"))

  if matches:
    return matches[0]

  raise RuntimeError(
    "boot_app0.bin not found in: " + str(framework_dir)
  )


def run_merge(esptool, factory, files):
  """Merge firmware images using the installed esptool version."""

  commands = [
    {
      "name": "merge-bin",
      "options": [
        "--flash-mode", "dio",
        "--flash-freq", "40m",
        "--flash-size", FLASH_SIZE
      ]
    },
    {
      "name": "merge_bin",
      "options": [
        "--flash_mode", "dio",
        "--flash_freq", "40m",
        "--flash_size", FLASH_SIZE
      ]
    }
  ]

  errors = []

  for command_info in commands:
    command = [
      *esptool,
      "--chip", "esp32",
      command_info["name"],
      "-o", str(factory),
      *command_info["options"],
      "--pad-to-size", FLASH_SIZE
    ]

    for address, image_path in files:
      command.extend([address, str(image_path)])

    print("Trying esptool command:", command_info["name"])

    result = subprocess.run(
      command,
      capture_output=True,
      text=True
    )

    if result.returncode == 0:
      if result.stdout:
        print(result.stdout)

      return

    errors.append(
      command_info["name"] + ":\n" +
      result.stdout + "\n" +
      result.stderr
    )

  raise RuntimeError(
    "Unable to merge firmware images:\n\n" +
    "\n\n".join(errors)
  )


def merge_factory(source, target, env):
  """Create a complete factory image after firmware compilation."""

  build_dir = Path(env.subst("$BUILD_DIR"))
  platform = env.PioPlatform()

  esptool = find_esptool(platform)
  boot_app0 = find_boot_app0(platform)

  bootloader = build_dir / "bootloader.bin"
  partitions = build_dir / "partitions.bin"
  firmware = build_dir / "firmware.bin"
  factory = build_dir / "firmware-merged.bin"

  required_files = [
    boot_app0,
    bootloader,
    partitions,
    firmware
  ]

  for file_path in required_files:
    if not file_path.is_file():
      raise RuntimeError(
        "Required file not found: " + str(file_path)
      )

  print("")
  print("========================================")
  print(" Creating complete factory firmware")
  print("========================================")

  print("esptool:", " ".join(esptool))
  print("bootloader:", bootloader)
  print("partitions:", partitions)
  print("boot_app0:", boot_app0)
  print("firmware:", firmware)

  files = [
    ("0x1000", bootloader),
    ("0x8000", partitions),
    ("0xE000", boot_app0),
    ("0x10000", firmware)
  ]

  run_merge(esptool, factory, files)

  if not factory.is_file():
    raise RuntimeError("Factory image was not created")

  actual_size = factory.stat().st_size

  if actual_size != FLASH_SIZE_BYTES:
    raise RuntimeError(
      "Invalid factory image size: " + str(actual_size)
    )

  print("")
  print("Factory firmware successfully created:")
  print(factory)
  print("Size:", actual_size, "bytes")
  print("")


env.AddPostAction(
  "$BUILD_DIR/${PROGNAME}.bin",
  merge_factory
)
