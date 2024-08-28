#include <windows.h>
#include <iostream>
#include "code_errors.h"

int IsServiceRunning(const std::wstring& serviceName, DWORD& serviceStatus) {
    // Открываем менеджер управления службами
    SC_HANDLE hSCManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCManager) return SERV_CONTR_ERR;
    

    // Открываем службу
    SC_HANDLE hService = OpenService(hSCManager, serviceName.c_str(), SERVICE_QUERY_STATUS);
    if (!hService) {
        CloseServiceHandle(hSCManager);
        DWORD codeError = GetLastError();
        if (codeError == ERROR_SERVICE_DOES_NOT_EXIST) 
            return LTRD_NOT_EXIST;
        else
            return FAIL_OPEN_SERV;
    }

    // Запрашиваем статус службы
    SERVICE_STATUS_PROCESS ssp = {};
    DWORD dwBytesNeeded;
    BOOL bQuery = QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &dwBytesNeeded);
    if (!bQuery) {
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCManager);
        return FAIL_GET_QUERRY_STAT;
    }

    serviceStatus = ssp.dwCurrentState;

    // Закрываем дескрипторы
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);

    return 1;
}

void outStatus(const std::wstring& serviceName, DWORD& serviceStatus) {
    std::wcout << L"Service " << serviceName << L" is ";
    switch (serviceStatus) {
    case SERVICE_STOPPED:
        std::wcout << L"stopped";
        break;
    case SERVICE_START_PENDING:
        std::wcout << L"starting";
        break;
    case SERVICE_STOP_PENDING:
        std::wcout << L"stopping";
        break;
    case SERVICE_RUNNING:
        std::wcout << L"running";
        break;
    case SERVICE_CONTINUE_PENDING:
        std::wcout << L"resuming";
        break;
    case SERVICE_PAUSE_PENDING:
        std::wcout << L"pausing";
        break;
    case SERVICE_PAUSED:
        std::wcout << L"paused";
        break;
    default:
        std::wcout << L"in an unknown state";
        break;
    }
    std::wcout << std::endl;
}