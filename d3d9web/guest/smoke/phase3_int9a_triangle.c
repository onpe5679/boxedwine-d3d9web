#include <stdio.h>

__declspec(noinline) static unsigned int d3d9web_call(
    unsigned int index,
    unsigned int arg1,
    unsigned int arg2) {
    unsigned int result;
    __asm {
        push arg2
        push arg1
        push index
        _emit 0xCD
        _emit 0x9A
        add esp, 12
        mov result, eax
    }
    return result;
}

int main(void) {
    const unsigned int result = d3d9web_call(1, 800, 600);
    printf("D3D9WEB_PHASE3_GUEST eax=0x%08X\n", result);
    return result == 0x3001u ? 0 : 1;
}
