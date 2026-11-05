#include <windows.h>

static unsigned int append_uint(char *buffer, unsigned int value)
{
    char reversed[10];
    unsigned int count = 0, i;

    do
    {
        reversed[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);

    for (i = 0; i < count; ++i) buffer[i] = reversed[count - i - 1];
    return count;
}

void __stdcall mainCRTStartup(void)
{
    static const char prefix[] = "D3D9WEB_GETACP=";
    char output[64];
    DWORD written;
    unsigned int i, length = 0;

    for (i = 0; i < sizeof(prefix) - 1; ++i) output[length++] = prefix[i];
    length += append_uint(output + length, GetACP());
    output[length++] = '\r';
    output[length++] = '\n';
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), output, length, &written, NULL);
    ExitProcess(0);
}
