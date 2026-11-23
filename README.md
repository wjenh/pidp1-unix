# Pidp1-unix
A port of the original PDP-7 Unix v0 to the pidp1-mods ecosystem
This port is based on the work done by DoctorWkt/pdp7-unix also in github.
It requires a current pidp1-mods install.

## Using it
Check out this repository to wherever you want to put it.
It expects to find /opt/pidp1-mods already installed, see wjenh/pidp1-mods.

It also includes a prebuilt Unix filesystem and boot program so you
don't actually need to build anything.

Just:\
Copy unix.drum to /opt/pidp1-mods/pdp23drum or use the *pdp1central* program to manage it.\
Start the pidp1 emulator.\
Load boot.rim as usual.\
Telnet to <host> port 2030.\
At the login prompt, use ken, password ken, or dmr, password dmr.

Read the README.md in the install!
There is a lot here, including a version of B that generates executable PDP-1 code,
as well as all the commands and games the original PDP-7 system had.

Enjoy!
Bill
