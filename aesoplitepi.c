/* ===========================================================================
* aesoplitepi
* Service for data raspberry pi to connect to AESOPLite DAQ Board via USB-UART
* record the data to file and send it to GSE computers via UDP
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
* 0.7.x Added multiple UDP destinations
* 0.8.x Added broadcast address option
* 1.0.x Changes to stdout for running as a service
* 1.1.x Added file location parameter DATADIR
* 1.2.x Added Date & Time to filename
* 2.0.x Added functionality to open new files based on a minutes parameter
* 2.1.x Added file name formating parameter FORMATFILE
* 2.2.x Added file name timestamp formatting parameter FORMATTIME
* 3.0.x Added minimun UDP size parameter MINUDP
*/
#define MAJOR_VERSION 3 //Changes on major revisions, new tasks and inputs
#define MINOR_VERSION 0 //Changes on minor revisions
#define PATCH_VERSION 0 //Changes with fixes on the same feature set
#define TIMEOUTS_BEFORE_REOPEN 10 //Number of timeouts before closing and reopen
#define PARAM_MAX_LENGTH  255   //Max to read from each parameter file
#define PARAM_TOTAL  8   //Number of parameters in parameter files
#define DESTINATION_MAX_LENGTH  8   //Max number of UDP destinations
#define SOCKET_MIN_STRING_LENGTH  9   //Min char for a socket style x.x.x.x:p
#define IP_MAX_STRING_LENGTH  16   //Max char for a IPv4 style x.x.x.x
#define MAX_FILE_TIMES  2   //Max number of File open times to keep
#define AL_FRAME_SIZE  34   //Number of bytes in 1 AL Frame


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
#include <time.h>
#include <unistd.h>

enum ParamType {RUNNUMBER, USBPORT, DESTUDP, DATADIR, MINSNEWFILE, FORMATFILE, FORMATTIME, MINUDP};
typedef struct ParameterEntry {
    char * fileLoc;
    char fileBuf[PARAM_MAX_LENGTH];
} ParameterEntry;
typedef struct UDPEntry {
    int sockUDP;
    struct sockaddr_in sockGSE;
} UDPEntry;

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
    tty.c_cflag &= ~ (CSIZE | CSTOPB | PARENB); //stop bit, no parity, no flowctrl

    // setup for raw non-canonical
    tty.c_iflag &= ~(BRKINT | PARMRK | IGNBRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    
    tty.c_cc[VMIN] = AL_FRAME_SIZE; // 1 AL Frame
    tty.c_cc[VTIME] = 51; // 5.1 sec is greater than default AL housekeeping ratw

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}




int main()
{
    const char * paramFileLocation[PARAM_TOTAL] = {"RUNNUMBER.prm", "USBPORT.prm", "DESTUDP.prm", "DATADIR.prm", "MINSNEWFILE.prm", "FORMATFILE.prm", "FORMATTIME.prm", "MINUDP.prm"};
    const char * paramFileDefault[PARAM_TOTAL] = {"0", "/dev/ttyACM0", "127.0.0.1:2102,127.0.0.1:2101","./", "60", "%sAL%05u%s.dat", "_%Y-%m-%d_%H-%M", "340"};
    ParameterEntry params[PARAM_TOTAL];
    enum ParamType paramIndex;
    UDPEntry destUDP[DESTINATION_MAX_LENGTH];
    uint8_t nDestUDP = 0;
//    char *portName = "/dev/serial/by-id/usb-Cypress_Semiconductor_USBUART_740302080D143374-if00"; /HWv3DAQ
    // char * portName = "/dev/serial/by-id/usb-Cypress_Semiconductor_USBUART_C5030215230A1F04-if00"; //HWv3DAQ flight
//    char *portName = "/dev/serial/by-id/usb-Cypress_Semiconductor_USBUART_0300021216132494-if00"; //test HWv2DAQ
    char portName[PARAM_MAX_LENGTH];
    // char * filename = "datatemp.dat";
    char filename[PARAM_MAX_LENGTH];
    FILE * fpData;
    int fdUsb; 
    // int sockUDP; 
    // struct sockaddr_in sockGSE;
    bool isOpenDAQ = false;
    bool isOpenDataFile = false;
    uint16_t numReadTO = 0;
    unsigned int runNum;
    time_t fileTimes[MAX_FILE_TIMES];
    uint8_t iFileTimes = 0;
    unsigned int minutesNewFile;
    unsigned int minUDPsize;
    unsigned int bufSize;

    printf("AESOPLitePi v%d.%d.%d starting...\n", MAJOR_VERSION, MINOR_VERSION, PATCH_VERSION);
    for (paramIndex = 0; paramIndex < PARAM_TOTAL; paramIndex++)
    {
        params[paramIndex].fileLoc = paramFileLocation[paramIndex];
        strcpy(params[paramIndex].fileBuf, paramFileDefault[paramIndex]);

        ReadCreateParamFile(params + paramIndex);
    }
    sscanf(params[RUNNUMBER].fileBuf, "%u", &runNum);
    // sprintf(filename, "%s%05u.dat", params[DATADIR].fileBuf, runNum);
    sscanf(params[USBPORT].fileBuf, "%s", portName); //TODO replace portnme
    sscanf(params[MINSNEWFILE].fileBuf, "%u", &minutesNewFile);
    sscanf(params[MINUDP].fileBuf, "%u", &minUDPsize);
    if (minUDPsize < (2 * AL_FRAME_SIZE))
    {
        minUDPsize = 2 * AL_FRAME_SIZE;
        bufSize = 4 * AL_FRAME_SIZE;
        printf("Minmum UDP Packet size increased to %d\n", minUDPsize); 

    }
    else if (minUDPsize > (40 * AL_FRAME_SIZE)) //much larger than this will likely fragment
    {
        minUDPsize = 40 * AL_FRAME_SIZE;
        bufSize = 80 * AL_FRAME_SIZE;
        printf("Minmum UDP Packet size decreased to %d\n", minUDPsize); 

    }
    else
    {
        bufSize = 2 * minUDPsize;
    }
    if (SOCKET_MIN_STRING_LENGTH <= strlen(params[DESTUDP].fileBuf))
    {

        char * delim = ",";
        char * tok = strtok(params[DESTUDP].fileBuf, delim);
        bool continueTok = true;
        while (continueTok)
        {
            if(NULL != tok)
            {
                if (SOCKET_MIN_STRING_LENGTH >= strlen(params[DESTUDP].fileBuf))
                {
                    printf("Error opening socket: %s is too short\n", tok); 
                    continueTok = false;
                }
                else
                {
                    unsigned int destPort;
                    char destIP[IP_MAX_STRING_LENGTH];
                    // printf("tok: %s\n", tok); 
                    sscanf(tok, "%[^:]:%u", destIP, &destPort);
                    printf("Opening UDP Socket: %s : %d\n", destIP, destPort); 
                    if ((destUDP[nDestUDP].sockUDP = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
                    { 
                        printf("Error opening socket\n"); 
                        exit(EXIT_FAILURE); 
                    } 
                    if (NULL != strstr(tok, "255"))
                    {
                        int en = 1;
                        if(0 == setsockopt(destUDP[nDestUDP].sockUDP, SOL_SOCKET, SO_BROADCAST, &en, sizeof(en)))
                        {
                            printf("Enabled Broadcast for: %s\n", tok);
                        }
                        else 
                        {
                            printf("Failed to Enable Broadcast for: %s with error: %s\n", tok, strerror(errno));
                        }

                    }

                    if (0 == (inet_pton(AF_INET, destIP, &(destUDP[nDestUDP].sockGSE.sin_addr)))) 
                    { 
                        printf("Invalid IP\n"); 
                        exit(EXIT_FAILURE); 
                    } 
                    destUDP[nDestUDP].sockGSE.sin_port = htons(destPort);
                    destUDP[nDestUDP].sockGSE.sin_family = AF_INET;
                    nDestUDP++;
                    tok =strtok(NULL, delim);
                }
            }
            else
            {
                continueTok = false;
            }
        }
        
    }
    
    do
    {
        
        printf("Opening USB Port: %s\n", portName);

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
            unsigned char buf[bufSize];
            int rdLen, wrLen;
            unsigned int iBuf = 0;

            if (false == isOpenDataFile)
            {
                time(fileTimes + iFileTimes); //update file time
                struct tm* curFileTime = gmtime(fileTimes + iFileTimes);
                char formatTimeStr[PARAM_MAX_LENGTH];
                strftime(formatTimeStr, PARAM_MAX_LENGTH - 1, params[FORMATTIME].fileBuf, curFileTime);
                sprintf(filename, params[FORMATFILE].fileBuf, params[DATADIR].fileBuf, runNum, formatTimeStr);
                fpData = fopen(filename, "wb");//TODO skip if file exists
                if (!fpData)
                {
                    printf("Error opening %s: %s\n", filename, strerror(errno));
                }
                else
                {
                    isOpenDataFile = true;
                    iFileTimes = (iFileTimes + 1) % MAX_FILE_TIMES;
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
                
                if(iBuf >= sizeof(buf))
                {
                    for(uint8_t i=0; i < nDestUDP; i++)
                    {
                        int tempUDPSent;
                        tempUDPSent = sendto(destUDP[i].sockUDP, (const char *)buf, iBuf, MSG_CONFIRM, (const struct sockaddr *) &destUDP[i].sockGSE, sizeof(destUDP[i].sockGSE)); 
                        // printf("Sent %d bytes to UDP %d\n", tempUDPSent, i);
                    }
                    rdLen = 0;
                    iBuf = 0;
                }
                else
                {
                    int rdMax = (sizeof(buf) - iBuf);
                    if (rdMax > (5 * AL_FRAME_SIZE))
                    {
                        rdMax = 5 * AL_FRAME_SIZE; // 5 frame 
                    }
                    rdLen = read(fdUsb, (buf + iBuf), rdMax);
                    
                    if (rdLen > 0)
                    {
                        // unsigned char *h = buf;
                        // printf("Read %d:", rdLen);
                        // for (int i = 0; i < rdLen; i++)
                        // {
                        //     printf(" 0x%X", buf[i]);
                        // }
                        // printf("\n");
                        wrLen = fwrite((buf + iBuf), 1, rdLen, fpData);
                        if (wrLen < rdLen)
                        {
                            printf("Error from write: %d of %d bytes written\n", wrLen, rdLen);
                        }
                        iBuf += rdLen;
                        if ((iBuf) >= minUDPsize)
                        {
                            for (uint8_t i = 0; i < nDestUDP; i++)
                            {
                                int tempUDPSent;
                                tempUDPSent = sendto(destUDP[i].sockUDP, (const char *)buf, iBuf, MSG_CONFIRM, (const struct sockaddr *)&destUDP[i].sockGSE, sizeof(destUDP[i].sockGSE));
                                // printf("Sent %d bytes to UDP %d\n", tempUDPSent, i);
                                // printf("Index %d new read %d over %d sent to dest %d\n", iBuf,  rdLen, minUDPsize, i);
                            }
                            usleep(iBuf * 520); //DEBUG delay for UDP ordering with file input 
                            iBuf = 0;
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
                            numReadTO = 0;
                            isOpenDAQ = false;
                        }
                    }
                    time(fileTimes + iFileTimes); //update file time
                    double curFileSecs = difftime(fileTimes[iFileTimes], fileTimes[((MAX_FILE_TIMES - 1) + iFileTimes) % MAX_FILE_TIMES]);
                    if(((unsigned int)curFileSecs) >= (minutesNewFile * 60)) //check if time has elapsed for new file
                    {
                        runNum++;
                        fclose(fpData);
                        isOpenDataFile = false;
                    }
                }
            }
            if(iBuf > 0)
            {
                for(uint8_t i=0; i < nDestUDP; i++)
                {
                    int tempUDPSent;
                    tempUDPSent = sendto(destUDP[i].sockUDP, (const char *)buf, iBuf, MSG_CONFIRM, (const struct sockaddr *) &destUDP[i].sockGSE, sizeof(destUDP[i].sockGSE)); 
                    // printf("Sent %d leftover bytes to UDP %d\n", tempUDPSent, i);
                }

                usleep(iBuf * 520); //DEBUG delay for UDP ordering with file input 

                rdLen = 0;
                iBuf = 0;
            }
        }
        close(fdUsb);
    } while (1);
    return EXIT_FAILURE;
}
