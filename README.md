# aesoplitepi
Service for data raspberry pi to connect to AESOPLite DAQ Board via USB-UART

Parameters for aesoplitpi are separated into single line “prm” files. This organization allows for low data, minimal interaction (suitable for satellite link) changes to parameters using scp. The files are listed below along with the default values that will populate a newly created file if none exists.

Filename: DATADIR.prm			Default Value: ./

Unix-style path to directory for storing recorded files. Combines with FORMATFILE.prm, FORMATTIME.prm, & RUNNUMBER.prm to form the recorded filename.

Filename: DESTUDP.prm			Default Value: 127.0.0.1:2102,127.0.0.1:2101
	List of up to 8 UDP destination address separated by commas. Addresses are in the form: IP address followed by colon followed by port number.

Filename: FORMATFILE.prm		Default Value: %sAL%05u%s.dat

	Format to combine parameters from DATADIR.prm, RUNNUMBER.prm, & FORMATTIME.prm to form the recorded filename. This string is passed directly into sprintf() and order of “conversion specifiers” special character sequence is important. 
Special character sequence “%s” must start the string to expand to the data directory 
Next special character sequence must be “%u” conversion specifier and can include length modifiers to format the run number.
Special character sequence “%s” must be somewhere after the run number to expand to the timestamp 
Special character sequences are described in man pages such as https://linux.die.net/man/3/sprintf

Filename: FORMATTIME.prm		Default Value: _%Y-%m-%d_%H-%M

	Format for timestamp at the end of the filename. This string is passed directly into strftime() to format the date and time. Combines with FORMATFILE.prm, DATADIR.prm, & RUNNUMBER.prm to form the recorded filename. “Conversion specifications” special character sequences are described in man pages such as https://linux.die.net/man/3/strftime

Filename: MINSNEWFILE.prm		Default Value: 60

Number of minutes to run before opening a new record file and incrementing the run number.

Filename: MINUDP.prm			Default Value: 340

	Minimum size in bytes for a UDP packet before sending.

Filename: RUNNUMBER.prm		Default Value: 0

	Run number to be included in filename. Combines with FORMATFILE.prm, DATADIR.prm, & FORMATTIME.prm to form the recorded filename. The file is periodically opened and closed during the run and the value is updated with the next run number to be used.

Filename: USBPORT.prm		Default Value: /dev/ttyACM0

	Unix-style path to the USB device connected to the AESOP-Lite DAQ board. Can use other paths to test functionality if no DAQ board is present
