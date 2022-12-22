#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <stdlib.h>

#include <WinSock2.h>
#include <process.h>
#include <vector>
#include <string>

#include <Windows.h>

#include "jdbc/mysql_connection.h"
#include "jdbc/cppconn/driver.h"
#include "jdbc/cppconn/exception.h"
#include "jdbc/cppconn/prepared_statement.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment (lib, "mysqlcppconn.lib")

#define PORT 19934
#define IP_ADDRESS "172.16.2.84"
#define PACKET_SIZE 100

using namespace std;

const string server = "tcp://127.0.0.1:3306";
const string username = "root";
const string password = "1234";

vector<SOCKET> vSocketList;

CRITICAL_SECTION ServerCS;

sql::Driver* driver = nullptr;
sql::Connection* con = nullptr;
sql::Statement* stmt = nullptr;
sql::PreparedStatement* pstmt = nullptr;
sql::ResultSet* rs = nullptr;

std::string Utf8ToMultiByte(std::string utf8_str)
{
    std::string resultString; char* pszIn = new char[utf8_str.length() + 1];
    strncpy_s(pszIn, utf8_str.length() + 1, utf8_str.c_str(), utf8_str.length());
    int nLenOfUni = 0, nLenOfANSI = 0; wchar_t* uni_wchar = NULL;
    char* pszOut = NULL;
    // 1. utf8 Length
    if ((nLenOfUni = MultiByteToWideChar(CP_UTF8, 0, pszIn, (int)strlen(pszIn), NULL, 0)) <= 0)
        return nullptr;
    uni_wchar = new wchar_t[nLenOfUni + 1];
    memset(uni_wchar, 0x00, sizeof(wchar_t) * (nLenOfUni + 1));
    // 2. utf8 --> unicode
    nLenOfUni = MultiByteToWideChar(CP_UTF8, 0, pszIn, (int)strlen(pszIn), uni_wchar, nLenOfUni);
    // 3. ANSI(multibyte) Length
    if ((nLenOfANSI = WideCharToMultiByte(CP_ACP, 0, uni_wchar, nLenOfUni, NULL, 0, NULL, NULL)) <= 0)
    {
        delete[] uni_wchar; return 0;
    }
    pszOut = new char[nLenOfANSI + 1];
    memset(pszOut, 0x00, sizeof(char) * (nLenOfANSI + 1));
    // 4. unicode --> ANSI(multibyte)
    nLenOfANSI = WideCharToMultiByte(CP_ACP, 0, uni_wchar, nLenOfUni, pszOut, nLenOfANSI, NULL, NULL);
    pszOut[nLenOfANSI] = 0;
    resultString = pszOut;
    delete[] uni_wchar;
    delete[] pszOut;
    return resultString;
}

size_t UTF8StringByteCount(const std::string& str)
{
    size_t utf8_char_count = 0;
    for (int i = 0; i < str.length();)
    {
        // 4바이트 문자인지 확인
        // 0xF0 = 1111 0000
        if (0xF0 == (0xF0 & str[i]))
        {
            // 나머지 3바이트 확인
            // 0x80 = 1000 0000
            if (0x80 != (0x80 & str[i + 1]) || 0x80 != (0x80 & str[i + 2]) || 0x80 != (0x80 & str[i + 3]))
            {
                throw std::exception("not utf-8 encoded string");
            }
            i += 4;
            utf8_char_count += 4;
            continue;
        }
        // 3바이트 문자인지 확인
        // 0xE0 = 1110 0000
        else if (0xE0 == (0xE0 & str[i]))
        {
            // 나머지 2바이트 확인
            // 0x80 = 1000 0000
            if (0x80 != (0x80 & str[i + 1]) || 0x80 != (0x80 & str[i + 2]))
            {
                throw std::exception("not utf-8 encoded string");
            }
            i += 3;
            utf8_char_count += 3;
            continue;
        }
        // 2바이트 문자인지 확인
        // 0xC0 = 1100 0000
        else if (0xC0 == (0xC0 & str[i]))
        {
            // 나머지 1바이트 확인
            // 0x80 = 1000 0000
            if (0x80 != (0x80 & str[i + 1]))
            {
                throw std::exception("not utf-8 encoded string");
            }
            i += 2;
            utf8_char_count += 2;
            continue;
        }
        // 최상위 비트가 0인지 확인
        else if (0 == (str[i] >> 7))
        {
            i += 1;
            utf8_char_count += 1;
        }
        else
        {
            throw std::exception("not utf-8 encoded string");
        }
    }
    return utf8_char_count;
}

size_t UTF8StringLength(const std::string& str)
{
    size_t utf8_char_count = 0;
    for (int i = 0; i < str.length();)
    {
        // 4바이트 문자인지 확인
        // 0xF0 = 1111 0000
        if (0xF0 == (0xF0 & str[i]))
        {
            // 나머지 3바이트 확인
            // 0x80 = 1000 0000
            if (0x80 != (0x80 & str[i + 1]) || 0x80 != (0x80 & str[i + 2]) || 0x80 != (0x80 & str[i + 3]))
            {
                throw std::exception("not utf-8 encoded string");
            }
            i += 4;
            utf8_char_count++;
            continue;
        }
        // 3바이트 문자인지 확인
        // 0xE0 = 1110 0000
        else if (0xE0 == (0xE0 & str[i]))
        {
            // 나머지 2바이트 확인
            // 0x80 = 1000 0000
            if (0x80 != (0x80 & str[i + 1]) || 0x80 != (0x80 & str[i + 2]))
            {
                throw std::exception("not utf-8 encoded string");
            }
            i += 3;
            utf8_char_count++;
            continue;
        }
        // 2바이트 문자인지 확인
        // 0xC0 = 1100 0000
        else if (0xC0 == (0xC0 & str[i]))
        {
            // 나머지 1바이트 확인
            // 0x80 = 1000 0000
            if (0x80 != (0x80 & str[i + 1]))
            {
                throw std::exception("not utf-8 encoded string");
            }
            i += 2;
            utf8_char_count++;
            continue;
        }
        // 최상위 비트가 0인지 확인
        else if (0 == (str[i] >> 7))
        {
            i += 1;
            utf8_char_count++;
        }
        else
        {
            throw std::exception("not utf-8 encoded string");
        }
    }
    return utf8_char_count;
}

unsigned WINAPI WorkThread(void* Args)
{
    SOCKET CS = *(SOCKET*)Args;

    while (true)
    {
        char Buffer[PACKET_SIZE] = { 0, };
        int RecvBytes = recv(CS, Buffer, sizeof(Buffer), 0);

        if (RecvBytes > PACKET_SIZE)
        {
            continue;
        }

        if (RecvBytes <= 0)
        {
            cout << "클라이언트 연결 종료 : " << CS << '\n';
            closesocket(CS);
            EnterCriticalSection(&ServerCS);
            vSocketList.erase(find(vSocketList.begin(), vSocketList.end(), CS));
            LeaveCriticalSection(&ServerCS);
            break;
        }
        Buffer[PACKET_SIZE - 1] = '\0';

        std::string ChatBuffer = Buffer;

        //size_t szChatMsgLen = UTF8StringLength(ChatBuffer);
        //size_t szChatMsgByteLength = UTF8StringByteCount(ChatBuffer);
        //
        //if (szChatMsgLen > 20)
        //{
        //    ChatBuffer = ChatBuffer.substr(0, 20);
        //}

        pstmt = con->prepareStatement("INSERT INTO ChatTable(`CONTENTS`)VALUES(?)");
        pstmt->setString(1, ChatBuffer);
        pstmt->execute();

        cout << Utf8ToMultiByte(ChatBuffer) << '\n';
        cout << "RecvBytes : " << RecvBytes << '\n';

        EnterCriticalSection(&ServerCS);
        for (int i = 0; i < vSocketList.size(); i++)
        {
            int SendBytes = 0;
            int TotalSentBytes = 0;
            do
            {
                SendBytes = send(vSocketList[i], &Buffer[TotalSentBytes], sizeof(Buffer) - TotalSentBytes, 0);
                TotalSentBytes += SendBytes;
            } while (TotalSentBytes < sizeof(Buffer));

            if (SendBytes <= 0)
            {
                closesocket(CS);
                EnterCriticalSection(&ServerCS);
                vSocketList.erase(find(vSocketList.begin(), vSocketList.end(), CS));
                LeaveCriticalSection(&ServerCS);
                break;
            }

            LeaveCriticalSection(&ServerCS);
        }
    }
    return 0;
}

int main()
{
    driver = get_driver_instance();
    con = driver->connect(server, username, password);
    con->setSchema("UE4SERVER");

    cout << "[채팅 서버 활성화]" << '\n';

    InitializeCriticalSection(&ServerCS);

    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);
    SOCKET SS = socket(AF_INET, SOCK_STREAM, 0);

    SOCKADDR_IN SA = { 0, };
    SA.sin_family = AF_INET;
    SA.sin_addr.S_un.S_addr = inet_addr(IP_ADDRESS);
    SA.sin_port = htons(PORT);

    if (::bind(SS, (SOCKADDR*)&SA, sizeof(SA)) != 0) { exit(-3); };
    if (listen(SS, SOMAXCONN) == SOCKET_ERROR) { exit(-4); };

    cout << "클라이언트 연결을 기다리는 중입니다......." << '\n';

    while (true)
    {
        SOCKADDR_IN CA = { 0, };
        int sizeCA = sizeof(CA);
        SOCKET CS = accept(SS, (SOCKADDR*)&CA, &sizeCA);

        cout << "클라이언트 접속 : " << CS << '\n';

        EnterCriticalSection(&ServerCS);
        vSocketList.push_back(CS);
        LeaveCriticalSection(&ServerCS);

        HANDLE hThread = (HANDLE)_beginthreadex(0, 0, WorkThread, (void*)&CS, 0, 0);
    }
    closesocket(SS);

    WSACleanup();
}

