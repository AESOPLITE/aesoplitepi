/* ===========================================================================
* aesoplitepi
* Service for data raspberry pi to connect to AESOPLite DAQ Board via USB-UART
* 
* Auhor:
* Brian Lucas
* 
* Versions:
* 0.0.x Initial simple version that runs manually and records to 1 file
* 0.1.x Created default data file permissions change
* 0.2.x Retries rather than exits after unsuccessful open
*/
#define MAJOR_VERSION 0 //Changes on major revisions, new tasks and inputs
#define MINOR_VERSION 2 //Changes on minor revisions
#define PATCH_VERSION 4 //Changes on most new compilations while developing
#define TIMEOUTS_BEFORE_REOPEN 10 //Number of timeouts before closing and reopen


#include <errno.h>
#include <fcntl.h> 
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int set_default_attribs(int fd)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0) {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        return -1;
    }

    cfsetospeed(&tty, (speed_t)B19200);
    cfsetispeed(&tty, (speed_t)B19200);

    tty.c_cflag |= (CS8 | CLOCAL | CREAD);    // 8bit, ignore modem ctrl
    tty.c_cflag &= ~ (CSIZE | CSTOPB | PARENB | CRTSCTS); //stop bit, no parity, no flowctrl

    // setup for raw non-canonical
    tty.c_iflag &= ~(BRKINT | PARMRK | IGNBRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    
    tty.c_cc[VMIN] = 34; // 1 AL Frame
    tty.c_cc[VTIME] = 51; // 5.1 sec is greater than default AL housekeeping ratw

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}




int main()
{
//    char *portName = "/dev/serial/by-id/usb-Cypress_Semiconductor_USBUART_740302080D143374=if00"; //HWv3DAQ
    char *portName = "/dev/serial/by-id/usb-Cypress_Semiconductor_USBUART_0300021216132494-if00"; //test HWv2DAQ
    char *filename = "datatemp.dat";
    int fdUsb; 
    FILE* fpData;
    bool isOpenDAQ = false;
    bool isOpenDataFile = false;
    uint numReadTO = 0;

    do
    {

        fdUsb = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
        if (fdUsb < 0)
        {
            printf("Error opening %s: %s\n", portName, strerror(errno));
            sleep(1); //sleep for 1 sec to limit the retry rate
        }
        else
        {
            isOpenDAQ = true;
            set_default_attribs(fdUsb);
            numReadTO = 0;
        }

        while (isOpenDAQ)
        {

            if (false == isOpenDataFile)
            {
            fpData = fopen(filename, "wb");
                if (!fpData)
                {
                    printf("Error opening %s: %s\n", filename, strerror(errno));
                }
                else
                {
                    isOpenDataFile = true;
                    printf("Opened %s: %d\n", filename, fpData);
                }
            }
            while (isOpenDataFile && isOpenDAQ)
            {
                unsigned char buf[102];
                int rdLen, wrLen;

                rdLen = read(fdUsb, buf, sizeof(buf) - 1);
                if (rdLen > 0)
                {
                    unsigned char *h = buf;
                    printf("Read %d:", rdLen);
                    for (int i = 0; i < rdLen; i++)
                    {
                        printf(" 0x%X", buf[i]);
                    }
                    printf("\n");
                    wrLen = fwrite(buf, 1, rdLen, fpData);
                    if (wrLen < rdLen)
                    {
                        printf("Error from write: %d of %d bytes written\n", wrLen, rdLen);
                    }
                }
                else if (rdLen < 0)
                {
                    printf("Error from read: %d: %s\n", rdLen, strerror(errno));
                }
                else
                {
                    printf("Timeout from read\n");
                    numReadTO++;
                    if (TIMEOUTS_BEFORE_REOPEN <= numReadTO)
                    {
                        isOpenDAQ = false;
                    }
                }
            }
        }
        close(fdUsb);
    } while (1);
}
