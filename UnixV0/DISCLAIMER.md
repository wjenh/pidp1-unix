## Provenance & Licensing

This project is an original PDP-1 assembly implementation modeled on the behavior and system design of
the original Bell Labs Unix (PDP-7 Unix / "V0"). 

* **Architectural Reimplementation:** Rather than a direct line-by-line port,
the kernel and utilities have been re-engineered for the PDP-1 architecture,
most notably allocating dedicated 4K memory segments per user rather than relying on PDP-7 disk swapping.

* **No Original Bell Labs Source Code:** This repository contains no original Bell Labs assembly listings, scans,
or verbatim code.

Historical PDP-7 source artifacts can be found at the [Computer History Museum](https://computerhistory.org) site
and the TUHS PDP-7 Unix Restoration Project.

* **License:** Informed by the open-source restoration work in `DoctorWkt/pdp7-unix`,
all PDP-1 code in this repository is licensed under the **GNU General Public License v3.0 (GPLv3)**.
See the `LICENSE` file for full details.

