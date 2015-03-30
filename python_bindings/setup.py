from setuptools import setup, Extension

scp_module = Extension(
    "rig_scp",
    sources=['rig_scp.c'],
    libraries=['rigscp']
)

setup(
    name="rig_scp",
    descripion="Fast implementation of the SpiNNaker Command Protocol (SCP)",
    version="0.1.0-beta",
    url="https://github.com/project-rig/rig-scp",
    author="Project Rig",
    license="GPL2",
    ext_modules=[scp_module]
)
