#ifndef BLOCK_TYPE_SUPPORTS_NGSPICE_API_H
#define BLOCK_TYPE_SUPPORTS_NGSPICE_API_H

struct NgspiceVecValue {
    char* name;
    double real;
    double imaginary;
    int isScale;
    int isComplex;
};

struct NgspiceVecValuesAll {
    int vectorCount;
    int vectorIndex;
    NgspiceVecValue** vectors;
};

using NgspiceSendChar = int (*)(char*, int, void*);
using NgspiceSendStatus = int (*)(char*, int, void*);
using NgspiceControlledExit = int (*)(int, int, int, int, void*);
using NgspiceSendData = int (*)(NgspiceVecValuesAll*, int, int, void*);
using NgspiceSendInitData = int (*)(void*, int, void*);
using NgspiceBackgroundThreadRunning = int (*)(int, int, void*);
using NgspiceGetSourceData = int (*)(double*, double, char*, int, void*);
using NgspiceGetSyncData = int (*)(double, double*, double, int, int, int, void*);

extern "C" {
int ngSpice_Init(
    NgspiceSendChar,
    NgspiceSendStatus,
    NgspiceControlledExit,
    NgspiceSendData,
    NgspiceSendInitData,
    NgspiceBackgroundThreadRunning,
    void*);
int ngSpice_Init_Sync(
    NgspiceGetSourceData,
    NgspiceGetSourceData,
    NgspiceGetSyncData,
    int*,
    void*);
int ngSpice_Reset(void);
int ngSpice_Command(char*);
int ngSpice_SetBkpt(double);
}

#endif
