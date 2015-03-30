Rig SCP Python Bindings
=======================

CPython bindings for the high performance Rig SCP library.


Installation
------------

Once the installation of Rig SCP itself is complete (see [how to install
here](https://github.com/project-rig/rig-scp#installation)) then the Python
bindings can be built and installed with:

    $ sudo python setup.py install

Or

    $ python setup.py develop


Tests
-----

The binding can be tested against all supported Python versions by using
[Tox](https://pypi.python.org/pypi/tox) which can be installed with:

    $ sudo pip install tox

**TO FINISH**

Tests may then be run with, where `${HOSTNAME}` is the hostname or address of a
booted SpiNNaker board that isn't running any other applications:

    $ tox -- ${HOSTNAME}


Documentation
-------------

**TODO**
