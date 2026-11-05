#include <stdio.h>

__declspec(noinline) static unsigned int d3d9web_phase0_call(
    unsigned int arg1,
    unsigned int arg2) {
    unsigned int result;
    __asm {
        push arg2
        push arg1
        push 0
        _emit 0xCD
        _emit 0x9A
        add esp, 12
        mov result, eax
    }
    return result;
}

int main(void) {
    const unsigned int arg1 = 0xA1B2C3D4u;
    const unsigned int arg2 = 0x11223344u;
    const unsigned int result = d3d9web_phase0_call(arg1, arg2);

    printf("D3D9WEB_GUEST index=0 arg1=0x%08X arg2=0x%08X eax=0x%08X\n",
           arg1, arg2, result);
    return result == 0x1234u ? 0 : 1;
}
