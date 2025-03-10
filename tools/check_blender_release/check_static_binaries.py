#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2023 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

__all__ = (
    "main",
)

import os
from pathlib import Path
import re
import subprocess
import unittest
import glob

from check_utils import (
    sliceCommandLineArguments,
    parseArguments,
)


ALLOWED_LIBS = [
    # Core C/C++ libraries:
    "ld-linux.so",
    "ld-linux-x86-64.so",
    "libc.so",
    "libm.so",
    "libstdc++.so",
    "libdl.so",
    "libpthread.so",
    "libgcc_s.so",
    "librt.so",
    "libutil.so",

    # Libraries which are part of default install:
    "libcrypt.so",
    "libuuid.so",

    # Bundled Python NCURSES dependencies.
    "libpanelw.so",
    "libncursesw.so",
    "libtinfo.so",

    # X11 libraries we don't link statically:
    "libdrm.so",
    "libX11.so",
    "libXext.so",
    "libXrender.so",
    "libXxf86vm.so",
    "libXi.so",
    "libXfixes.so",
    "libxkbcommon.so",

    # MaterialX X11 libs:
    "libICE.so",
    "libSM.so",
    "libXt.so",
    "libOpenGL.so",
    "libGLX.so",

    # Level Zero (Intel GPU Render)
    "libze_loader.so",

    # OpenGL libraries:
    "libGL.so",
    "libGLU.so",

    # Library the software-GL is linking against and distributes with it:
    'libglapi.so',
    'libxcb.so',
]

IGNORE_FILES = ("blender-launcher", "blender-softwaregl", )
IGNORE_EXTENSION = (".sh", ".py", )


# Library dependencies.

def getNeededLibrariesLDD(binary_filepath):
    """
    This function uses ldd to collect libraries which binary depends on.

    Not totally safe since ldd might actually execute the binary to get it's
    symbols and will also collect indirect dependencies which might not be
    desired.

    Has advantage of telling that some dependency library is not found.
    """
    ldd_command = ("ldd", str(binary_filepath))
    ldd_output = subprocess.check_output(ldd_command, stderr=subprocess.STDOUT)
    lines = ldd_output.decode().split("\n")
    libraries = []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        lib_name = line.split("=>")[0]
        lib_name = lib_name.split(" (")[0].strip()
        lib_file_name = os.path.basename(lib_name)
        libraries.append(lib_file_name)
    return libraries


def getNeededLibrariesOBJDUMP(binary_filepath):
    """
    This function uses objdump to get direct dependencies of a given binary.

    Totally safe, but will require manual check over libraries which are not
    found on the system.
    """
    objdump_command = ("objdump", "-p", str(binary_filepath))
    objdump_output = subprocess.check_output(objdump_command,
                                             stderr=subprocess.STDOUT)
    lines = objdump_output.decode().split("\n")
    libraries = []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        if not line.startswith("NEEDED"):
            continue
        lib_name = line[6:].strip()
        libraries.append(lib_name)
    return libraries


def getNeededLibraries(binary_filepath):
    """
    Get all libraries given binary depends on.
    """
    if False:
        return getNeededLibrariesLDD(binary_filepath)
    else:
        return getNeededLibrariesOBJDUMP(binary_filepath)


def stripLibraryABI(lib_name):
    """
    Strip ABI suffix from .so file

    Example; ``libexample.so.1.0`` => ``libexample.so``.
    """
    lib_name_no_abi = lib_name
    # TODO(sergey): Optimize this!
    while True:
        no_abi = re.sub(r"\.[0-9]+$", "", lib_name_no_abi)
        if lib_name_no_abi == no_abi:
            break
        lib_name_no_abi = no_abi
    return lib_name_no_abi


class UnitTesting(unittest.TestCase):
    def checkBinary(self, binary_filepath):
        """
        Check given binary file to be a proper static self-sufficient.
        """

        libraries = getNeededLibraries(binary_filepath)
        for lib_name in libraries:
            lib_name_no_abi = stripLibraryABI(lib_name)
            with self.subTest(msg=os.path.basename(binary_filepath) + ' check'):
                self.assertTrue(lib_name_no_abi in ALLOWED_LIBS,
                                "Error detected in {}: library used {}" . format(
                                    binary_filepath, lib_name))

    def checkDirectory(self, directory):
        """
        Recursively traverse directory and check every binary in.
        """

        for path in Path(directory).rglob("*"):
            # Ignore any checks on directory.
            if path.is_dir():
                continue
            # Ignore script files.
            if path.name in IGNORE_FILES:
                continue
            if path.suffix in IGNORE_EXTENSION:
                continue
            # Check any executable binary,
            if path.stat().st_mode & 0o111 != 0:
                self.checkBinary(path)
            # Check all dynamic libraries.
            elif path.suffix == ".so":
                self.checkBinary(path)

    def test_directoryIsStatic(self):
        # Parse arguments which are not handled by unit testing framework.
        args = parseArguments()
        # Do some sanity checks first.
        self.assertTrue(os.path.exists(args.directory),
                        "Given directory does not exist: {}" .
                        format(args.directory))
        self.assertTrue(os.path.isdir(args.directory),
                        "Given path is not a directory: {}" .
                        format(args.directory))
        # Add sanitizer libraries if needed.
        if args.is_sanitizer_build:
            ALLOWED_LIBS.extend([
                "libasan.so",
                "libubsan.so",
            ])
        # Add all libraries the we bundle to the allowed list
        ALLOWED_LIBS.extend(glob.glob("*.so", root_dir=args.directory + "/lib"))
        # Add OIDN libs that do not have an `.so` symbolic-link.
        for oidn_lib in glob.glob("libOpenImageDenoise_*.so*", root_dir=args.directory + "/lib"):
            ALLOWED_LIBS.append(stripLibraryABI(oidn_lib))
        # Add all bundled python libs
        for python_lib in glob.glob("[0-9].[0-9]/python/lib/**/*.so", root_dir=args.directory, recursive=True):
            ALLOWED_LIBS.append(os.path.basename(python_lib))

        # Perform actual test,
        self.checkDirectory(args.directory)


def main():
    # Slice command line arguments by '--'
    unittest_args, _parser_args = sliceCommandLineArguments()
    # Construct and run unit tests.
    unittest.main(argv=unittest_args)


if __name__ == "__main__":
    main()
