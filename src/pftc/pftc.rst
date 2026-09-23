Portfolio File Transfer Configuration
======================================

``PFTC.COM`` is a standalone, non-resident client that pushes WiFi
configuration to the ESP/SmartCable bridge over the ``int 61h`` PUSH
transport (``AH=30h``, ``AL=0`` transmit / ``AL=1`` receive) - the
reverse direction from PFTD's host-driven pull protocol. The Portfolio
always initiates the exchange.

Status **implemented so far: iteration 0.0** - launch, open ports, send
HELLO with a retry loop, show a Connected/offline result, exit. No menu,
no configuration commands yet.

Opcode table (target design - see the design plan for full detail; only
HELLO is implemented as of this iteration)
-----------------------------------------

======  ===========  ============================  ===========
Opcode  Name         Payload                        Response
======  ===========  ============================  ===========
0x01    HELLO        (none)                         version block
0x02    GET_NETIFS   (none)                          interface list
0x03    GET_NETIF    1 byte: interface               netif block
0x04    SET_NETIF    netif block                     status
0x05    GET_IPCFG    1 byte: interface               ipcfg block
0x06    SET_IPCFG    ipcfg block                     status
0x07    GET_WIFISCAN 1 byte: interface               scan result block
======  ===========  ============================  ===========

Every response's first byte is a status (``0x20`` ok / ``0x10`` error)
and the second byte is an error code (``0x00`` on success; ``0x01``
unknown command, ``0x02`` malformed payload, ``0x03`` not connected,
``0x04`` interface not supported, ``0xFF`` internal error otherwise).

HELLO response (14 bytes) - implemented
----------------------------------------

.. code-block::

   offset 0     status: 0x20
   offset 1     error code: 0x00
   offset 2-5   magic "PFC1"
   offset 6-9   build id (4 bytes, binary LE)
   offset 10    version major
   offset 11    version minor
   offset 12    version patch
   offset 13    reserved (0x00)

Build and run
--------------

.. code-block:: console

   $ make pftc
   $ python3 tools/wiretest/smartcable_pftc.py

The client opens ports, shows "Connecting...", sends HELLO, and shows
either "Connected / PFTC vX.Y.Z" or "SmartCable: offline" depending on
whether a HELLO ack was received.
