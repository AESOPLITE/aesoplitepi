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
* 0.3.x Simple fixed address udp
* 0.4.x Add run number parameter
* 0.5.x Increment run number parameter after data file open
* 0.6.x Added param for USB port
*/
#define MAJOR_VERSION 0 //Changes on major revisions, new tasks and inputs
#define MINOR_VERSION 6 //Changes on minor revisions
#define PATCH_VERSION 1 //Changes on most new compilations while developing
#define TIMEOUTS_BEFORE_REOPEN 10 //Number of timeouts before closing and reopen
#define PARAM_MAX_LENGTH  254   //Max to read from each parameter file
#define PARAM_TOTAL  2   //Number of parameters in file parameter file


#include <arpa/inet.h> 
#include <errno.h>
#include <fcntl.h> 
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h> 
#include <termios.h>
#include <unistd.h>

enum ParamType {RUNNUMBER, USBPORT};
typedef struct ParameterEntry {
    char * fileLoc;
    char fileBuf[PARAM_MAX_LENGTH];
} ParameterEntry;

int ReadCreateParamFile(ParameterEntry * entry)
{
    FILE * fpTmp;
    if ((fpTmp = fopen(entry->fileLoc, "r")))//try to open parameter file
    {
        fgets(entry->fileBuf, PARAM_MAX_LENGTH, fpTmp);
        fclose(fpTmp);
        return 1;
    }
    else
    {
        fpTmp = fopen(entry->fileLoc, "w"); //create the new parameter file
        fputs(entry->fileBuf, fpTmp);
        fclose(fpTmp);
        return 0;
    }

    return -1;
}

int SetDefaultAttribs(int fd)
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
    const char * paramFileLocation[PARAM_TOTAL] = {"RUNNUMBER.prm", "USBPORT.prm"};
    const char * paramFileDefault[PARAM_TOTAL] = {"0", "/dev/ACM0"};
    ParameterEntry params[PARAM_TOTAL];
    enum ParamType paramIndex;
//    char *portName = "/dev/serial/by-id/usb-Cypress_Semiconductor_USBUART_740302080D143374-if00"; /HWv3DAQ
    // char * portName = "/dev/serial/by-id/usb-Cypress_Semiconductor_USBUART_C5030215230A1F04-if00"; //HWv3DAQ flight
//    char *portName = "/dev/serial/by-id/usb-Cypress_Semiconductor_USBUART_0300021216132494-if00"; //test HWv2DAQ
    char portName[PARAM_MAX_LENGTH];
    // char * filename = "datatemp.dat";
    char filename[PARAM_MAX_LENGTH];
    FILE * fpData;
    int fdUsb; 
    int sockUDP; 
    struct sockaddr_in sockGSE;
    bool isOpenDAQ = false;
    bool isOpenDataFile = false;
    uint numReadTO = 0;
    uint runNum;

    for (paramIndex = 0; paramIndex < PARAM_TOTAL; paramIndex++)
    {
        params[paramIndex].fileLoc = paramFileLocation[paramIndex];
        strcpy(params[paramIndex].fileBuf, paramFileDefault[paramIndex]);

        ReadCreateParamFile(params + paramIndex);
    }
    sscanf(params[RUNNUMBER].fileBuf, "%u", &runNum);
    sscanf(params[USBPORT].fileBuf, "%s", &portName);
    sprintf(filename, "%05u.dat", runNum);
    do
    {
        if ((sockUDP = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
        { 
            printf("Error opening socket\n"); 
            exit(EXIT_FAILURE); 
        } 
        if (0 == (inet_pton(AF_INET, "192.168.1.34", &(sockGSE.sin_addr)))) 
        { 
            printf("Invalid IP\n"); 
            exit(EXIT_FAILURE); 
        } 
        sockGSE.sin_port = htons(2102);
        sockGSE.sin_family = AF_INET;

        fdUsb = open(portName, O_RDWR | O_NOCTTY | O_SYNC);
        if (fdUsb < 0)
        {
            printf("Error opening %s: %s\n", portName, strerror(errno));
            sleep(1); //sleep for 1 sec to limit the retry rate
        }
        else
        {
            isOpenDAQ = true;
            SetDefaultAttribs(fdUsb);
            numReadTO = 0;
        }


        while (isOpenDAQ)
        {

            if (false == isOpenDataFile)
            {
            fpData = fopen(filename, "wb");//TODO skip if file exists
                if (!fpData)
                {
                    printf("Error opening %s: %s\n", filename, strerror(errno));
                }
                else
                {
                    isOpenDataFile = true;
                    printf("Opened %s: %d\n", filename, fpData);
                    FILE * fpWriteRunNum = fopen(paramFileLocation[RUNNUMBER], "w"); // open to write new runnum TODO error handling
                    char numStr[6];
                    sprintf(numStr, "%05u", runNum + 1); //increment in file so next open is new number
                    fputs(numStr, fpWriteRunNum);
                    fclose(fpWriteRunNum);
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
                    sendto(sockUDP, (const char *)buf, rdLen, MSG_CONFIRM, (const struct sockaddr *) &sockGSE, sizeof(sockGSE)); 
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
