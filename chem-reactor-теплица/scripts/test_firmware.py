"""Native regression checks; needs g++ (or --cxx) and Python 3."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import shutil


def compile_test(source, out, cxx="g++", extra=()):
    subprocess.run([cxx, "-std=c++11", "-Wall", "-Wextra", *extra,
                    str(source), "-o", str(out)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--sensor-repo", type=Path, default=root.parents[1] / "arduino-sensor")
    parser.add_argument("--actuator-repo", type=Path, default=root.parents[1] / "arduino-actuator")
    parser.add_argument("--bridge-out", type=Path, help="export the C++ bridge for HTTP integration tests")
    args = parser.parse_args()
    sensor = args.sensor_repo.resolve() / "chem-reactor-теплица/arduino-sensors"
    actuator = args.actuator_repo.resolve() / "chem-reactor-теплица/arduino-actuator"
    for folder in (sensor, actuator):
        if not folder.is_dir():
            parser.error(f"Missing Arduino project: {folder}; set --sensor-repo / --actuator-repo")
    protocol = (root / "firmware/common/HardwareProtocol.h").read_bytes()
    for folder in (sensor, actuator):
        assert (folder / "src/common/HardwareProtocol.h").read_bytes() == protocol
    extra = tuple(f'-D{name}="{path.as_posix()}"' for name, path in (
        ("CHEM_ACTUATOR_API_HEADER", actuator / "src/ActuatorApi.h"),
        ("CHEM_SENSOR_API_HEADER", sensor / "src/SensorApi.h"),
        ("CHEM_MQ2_HEADER", sensor / "MQ2.h")))
    with tempfile.TemporaryDirectory(prefix="chem-firmware-") as directory:
        outputs = Path(directory) / "outputs.exe"
        compile_test(root / "tests/lmp8_outputs_test.cpp", outputs, args.cxx, ("-DCHEM_OUTPUTS",))
        subprocess.run([str(outputs)], check=True)
        doors = Path(directory) / "doors.exe"
        compile_test(root / "tests/door_automation_test.cpp", doors, args.cxx, ("-DCHEM_DOORS",))
        subprocess.run([str(doors)], check=True)
        binary = Path(directory) / "firmware-chain.exe"
        compile_test(root / "tests/firmware_chain.cpp", binary, args.cxx, extra)
        subprocess.run([str(binary)], check=True)
        if args.bridge_out:
            target = args.bridge_out.resolve()
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(binary, target)
            print("Exported HTTP integration bridge:", target)


if __name__ == "__main__":
    main()
