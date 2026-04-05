Import("env")
import os
import re

# Read custom_src_dir from platformio.ini [env] section
# Usage: custom_src_dir = telegram-test
src_dir_name = env.GetProjectOption("custom_src_dir")
env["PROJECT_SRC_DIR"] = os.path.join(env.subst("$PROJECT_DIR"), src_dir_name)
env["BUILD_SRC_FILTER"] = ["+<*>", "-<.git/>", "-<.svn/>"]

def before_upload(source, target, env):
    upload_protocol = env.subst("$UPLOAD_PROTOCOL")
    if upload_protocol == "dfu":
        port = env.subst("$UPLOAD_PORT")
        if port:
            # Try to extract the MAC address from macOS /dev/cu.usbmodem<MAC><index> port name
            m = re.search(r"usbmodem([A-Fa-f0-9]{12})", port)
            if m:
                serial = m.group(1)
                print(f"\n[DFU Upload] Adding -S {serial} to dfu-util arguments to target specific device.\n")
                env.Prepend(UPLOADERFLAGS=["-S", serial])

env.AddPreAction("upload", before_upload)
