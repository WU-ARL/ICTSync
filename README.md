ICTSync: Information Centric Transport Synchronization Library for NDN
========================================================

If you are new to the NDN community of software generally, read the
[Contributor's Guide](https://github.com/named-data/NFD/blob/master/CONTRIBUTING.md).

The ICTSync library implements the [ICTSync protocol](ACM ICN ICT paper: https://dl.acm.org/doi/pdf/10.1145/3267955.3267963 ?). 
<NDNComm 2019 Presentation>
<description>
Overview of ICTSync:
    - Replaces ChronoSync digest with list of tuples in sync name
        - Tuple: Participant id and latest sequence number published
        - Example ICT-Sync name: /ndn/sync/syncId/[528:1][912:15][435:32]

    - Participant receiving sync interest:
        - Compares its state to state in sync name
        - Can immediately tell if there is a difference 
        - Can immediately tell what the difference is.
        - Responsible for requesting data in the associated data namespace.

ICTSync is an open source project licensed under LGPL 3.0 (see `COPYING.md` for more
detail).  We highly welcome all contributions to the ICTSync code base, provided that
they can be licensed under LGPL 3.0+ or other compatible license.

Feedback
--------

Please submit any bugs or issues to the **ICTSync** issue tracker:

* https://redmine.named-data.net/projects/ictsync (coming soon)

Installation instructions
-------------------------

### Prerequisites

Required:

* [ndn-cxx and its dependencies](https://named-data.net/doc/ndn-cxx/)

### Build

To build ICTSync from the source:

    ./waf configure
    ./waf
    sudo ./waf install


