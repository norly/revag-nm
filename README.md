VAG / VW reverse-engineered Network Management implementation
==============================================================

This is a dump of my previous work in re-implementing the Network
Management routines used on the CAN bus in a VW Golf Mk 6, using
Linux SocketCAN.

The code was cobbled together quickly, but I'd rather have it out
in the open than lost in the sands of time.


How to use this
----------------

    make
    ./revag-nm can0 0x0b

This will start the fake NM node on SocketCAN interface can0 and
pretend to be node 0x0b. That is, it will send CAN frames with
ID 0x42b which is the same as used (presumably) by the instrument
cluster in a VW Golf Mk 6.


Sources
--------

The vendor specific CAN protocol has been reverse engineered entirely
from wire traces.


The terminology used for the state machine in the code is derived
from the public standard document for OSEK/VDX Network Management,
Version 2.5.3 as published on the OSEK/VDX website:

  http://portal.osek-vdx.org/files/pdf/specs/nm253.pdf

Note that no claim is made as to adherence to this specification.
The primary focus is on cloning the behaviour of a Golf Mk 6's ECUs,
and the specification merely provides terminology that will hopefully
help future readers understand the code.


Disclaimer
-----------

This code has only been used in a lab bench setup, driving an RCD 310
radio head unit, as well as a MDI/Media-In interface. It has NOT been
tested inside a real car, and the author(s) take NO responsibility
whatsoever for any damage, safety issues, or anything else, be it
in a lab setup or in an actual car.


Licence
--------

GNU GPL v2 only.

Please see the file COPYING for details.
