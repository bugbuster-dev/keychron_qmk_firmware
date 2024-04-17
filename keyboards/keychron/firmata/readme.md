qmk firmata
===========

integration of "arduino firmata" src into "qmk_firmware".
arduino firmata src in firmata dir is from 2 repos:

arduino: https://github.com/arduino/ArduinoCore-avr/tree/master/cores/arduino
arduino firmata: https://github.com/firmata/arduino


qmk firmata c wrapper: Firmata_QMK.h/cpp
qmk sysex handler: <kb>/firmata_sysex_handler.c
qmk rgb host buffer handler: <kb>/firmata_rgb_matrix_user.c

firmata "com port"
------------------

- virtser
- raw hid

"qmk virtser" can be enabled and used for firmata if the board has enough usb endpoints.
see nuphy air96 v2.
on host the virtser port is shown as a serial com port and is used by pyfirmata2.

alternatively "qmk raw hid" (shared with via) can be used, see keychron q3 max.
on host a "rawhid serial port" is created and passed to pyfirmata2 to be used as 
a serial port.

note: using firmata, "console stream" is part of "firmata stream" and does not need separate endpoints.

integration
-----------

todo bb...

<kb>=your keyboard dir (example: keyboards/keychron/q3_max/)

copy firmata dir to <kb> or a common dir for keyboard(s) to support firmata (example: keyboards/keychron/)
copy ... .h files to <kb>
copy/extend rgb_matrix_user.inc in <kb>
copy firmata_rgb_matrix_user.c to <kb>
copy/strip/extend firmata_sysex_handler.c to <kb>

include firmata/firmata.mk in your mk file
modify firmata.mk if needed for:
- virtser or raw hid
- console on/off
- rgb matrix buffer set from host
