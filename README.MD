## Daily Check-In Device

v3.0 - The only change is to the schematic, where 1K series resistors are
inserted into the GPIO connections between the Arduino Pro Mini and the
Lolin D1 Mini.  This may not be necessary, but it complies with standard
practice where the two MCUs operate at different voltages.

V2.0 - In the sketch for the Pro Mini, most of the strings have been moved
into PROGMEM, but there were no other changes.  The schematic has also been
modified to add a 100nF decoupling capacitor at the 5V pin of the D1 Mini.

This device allows a senior who lives alone to check in one to three times
a day by pressing a button.  No phone calls made or answered.  Just press
a button.  But if not checked in, a push notification (Pushover app) is
sent to the relative or other person set up to receive such notices.  This
is not to be used for emergency medical alerts, only to confirm at least
once a day that the solitary senior is still functioning.  A detailed
description is found in the PDF file.
