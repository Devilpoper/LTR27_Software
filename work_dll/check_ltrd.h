#pragma once
int IsServiceRunning(const std::wstring& serviceName, DWORD& serviceStatus);
void outStatus(const std::wstring& serviceName, DWORD& serviceStatus);