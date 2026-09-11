import argparse
import os
from pathlib import Path
import subprocess


def build(sdk, output):
    sdk = sdk.resolve()
    output = output.resolve()
    source = Path(__file__).resolve().parent
    environment = dict(os.environ, PATH=str(sdk / "bin") + os.pathsep + os.environ.get("PATH", ""))
    output.mkdir(parents=True, exist_ok=True)
    for name in ("keysvc", "restart"):
        elf = output / (name + ".elf")
        zehn = output / (name + ".zehn")
        tns = output / (name + ".tns")
        subprocess.run([str(sdk / "bin/nspire-gcc"), "-Wall", "-W", "-marm", "-Os",
                        str(source / (name + ".c")), "-o", str(elf)], env=environment, check=True)
        subprocess.run([str(sdk / "bin/genzehn"), "--input", str(elf), "--output", str(zehn),
                        "--name", name, "--uses-lcd-blit", "true"], env=environment, check=True)
        subprocess.run([str(sdk / "bin/make-prg"), str(zehn), str(tns)], env=environment, check=True)
        print(tns)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Build the native OS key service and restart utility.")
    parser.add_argument("--sdk", type=Path,
                        default=Path(__file__).resolve().parents[2] / "ndl-src/ndl-sdk")
    parser.add_argument("--output", type=Path, required=True)
    options = parser.parse_args()
    build(options.sdk, options.output)
