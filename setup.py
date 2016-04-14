import io
import re
from setuptools import setup, find_packages

with open("rig_c_scp/version.py", "r") as f:
    exec(f.read())

setup(
    name="rig_c_scp",
    version=__version__,
    packages=find_packages(),
    
    # Files required by CFFI wrapper
    package_data = {'rig_c_scp': ["rs.h", "rs.c",
                                  "rs__internal.h",
                                  "rs__cancel.c",
                                  "rs__process_queue.c",
                                  "rs__process_response.c",
                                  "rs__queue.c",
                                  "rs__scp.c",
                                  "rs__transport.c"]},

    # Metadata for PyPi
    url="https://github.com/project-rig/rig_c_scp",
    author="The Rig Authors",
    description="A C library (and CFFI Python Interface) for high throughput communication with SpiNNaker machines via Ethernet.",
    license="GPLv2",
    classifiers=[
        "Development Status :: 3 - Alpha",

        "Intended Audience :: Developers",
        "Intended Audience :: Science/Research",

        "License :: OSI Approved :: GNU General Public License v2 (GPLv2)",

        "Operating System :: POSIX :: Linux",
        "Operating System :: MacOS",

        "Programming Language :: Python :: 2",
        "Programming Language :: Python :: 2.7",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.4",
        "Programming Language :: Python :: 3.5",

        "Topic :: Software Development :: Libraries",
    ],
    keywords="spinnaker sdp scp cffi",

    # Build CFFI Interface
    cffi_modules=["rig_c_scp/cffi_compile.py:ffi"],
    setup_requires=["cffi>=1.0.0"],
    install_requires=["cffi>=1.0.0", "rig>=1.0.0,<2.0.0"],
)
