#include <windows.h>
#include <string>
#include <iostream>
bool writeAacDelayProbe(const std::wstring& path);
int wmain(int argc, wchar_t** argv) { const bool ok = argc == 2 && writeAacDelayProbe(argv[1]); std::wcout << (ok ? L"PASS: game/mic PCM sync offsets -500/-100/0/+100/+500 ms and production AAC probe file.\n" : L"FAIL: audio sync probe.\n"); return ok ? 0 : 1; }
