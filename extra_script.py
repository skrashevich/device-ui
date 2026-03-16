# See https://docs.platformio.org/en/latest/manifests/library-json/fields/build/extrascript.html
Import("env")
import os
import shutil
import subprocess
import sys
from os.path import join, realpath

LIBRARY_DIR = realpath(Dir(".").abspath)
BERRY_REPO_URL = "https://github.com/berry-lang/berry.git"
BERRY_REV = "0d26d13d9d858ed37d60157dbee310561440f138"
BERRY_CACHE_DIR = realpath(join(LIBRARY_DIR, ".berry-cache"))
BERRY_DIR = realpath(join(BERRY_CACHE_DIR, "berry"))


def has_define(name):
    for item in env.get("CPPDEFINES", []):
        if item == name:
            return True
        if isinstance(item, tuple) and item[0] == name:
            return True
    return False


def berry_head(path):
    try:
        return subprocess.check_output(
            ["git", "-C", path, "rev-parse", "HEAD"], stderr=subprocess.DEVNULL, text=True
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def ensure_berry_checkout():
    os.makedirs(BERRY_CACHE_DIR, exist_ok=True)

    current = berry_head(BERRY_DIR)
    if current == BERRY_REV:
        return

    if os.path.isdir(BERRY_DIR):
        shutil.rmtree(BERRY_DIR)

    subprocess.check_call(["git", "clone", "--depth", "1", BERRY_REPO_URL, BERRY_DIR])
    if berry_head(BERRY_DIR) != BERRY_REV:
        subprocess.check_call(["git", "-C", BERRY_DIR, "fetch", "--depth", "1", "origin", BERRY_REV])
        subprocess.check_call(["git", "-C", BERRY_DIR, "checkout", "--detach", "FETCH_HEAD"])


def generate_berry_objects():
    coc = join(BERRY_DIR, "tools", "coc", "coc")
    generate_dir = join(BERRY_DIR, "generate")
    default_dir = join(BERRY_DIR, "default")
    src_dir = join(BERRY_DIR, "src")
    conf = join(default_dir, "berry_conf.h")

    os.makedirs(generate_dir, exist_ok=True)
    subprocess.check_call([sys.executable, coc, "-o", generate_dir, src_dir, default_dir, "-c", conf], cwd=BERRY_DIR)

# Base srcFilter. Cannot be set in library.json.
src_filter = [
    "+<resources>",
    "+<locale>",
    "+<source>"
]

for item in env.get("CPPDEFINES", []):
    # Add generated view directory to include path dependending on VIEW_* macro
    if isinstance(item,str) and item.startswith("VIEW_"):
        view = f"ui_{item[5:]}".lower()  # Ex value: "ui_320x240"
        env.Append(CPPPATH=[realpath(join(LIBRARY_DIR, "generated", view))])
        src_filter.append(f"+<generated/{view}>")
    # Add portduino directory to include path dependending on ARCH_PORTDUINO macro
    elif item == "ARCH_PORTDUINO":
        env.Append(CPPPATH=[realpath(join(LIBRARY_DIR, "portduino"))])
        src_filter.append("+<portduino>")

if has_define("HAS_SCRIPTING_BERRY"):
    ensure_berry_checkout()
    generate_berry_objects()
    env.Append(CPPPATH=[
        realpath(join(BERRY_DIR, "src")),
        realpath(join(BERRY_DIR, "default")),
        realpath(join(BERRY_DIR, "generate")),
    ])
    env.Append(CFLAGS=["-std=c99"])
    src_filter.extend([
        "+<.berry-cache/berry/src>",
        "+<.berry-cache/berry/default/be_port.c>",
        "+<.berry-cache/berry/default/be_modtab.c>",
    ])

# Only `Replace` is supported for SRC_FILTER, not `Append` or `Prepend`
env.Replace(SRC_FILTER=src_filter)

# Dump construction environment (for debug purposes)
# print("meshtastic-device-ui Library ENV:")
# print(env.Dump())
